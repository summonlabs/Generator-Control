// Generator Control - attempt records, request fingerprinting and replay journal.
#include "genctl/attempt.hpp"

#include <algorithm>

namespace genctl {

std::string_view to_string(CommandState state) noexcept {
  switch (state) {
    case CommandState::Planned: return "planned";
    case CommandState::Authorized: return "authorized";
    case CommandState::Issued: return "issued";
    case CommandState::Acknowledged: return "acknowledged";
    case CommandState::Rejected: return "rejected";
    case CommandState::Unsupported: return "unsupported";
    case CommandState::Busy: return "busy";
    case CommandState::AdapterUnavailable: return "adapter-unavailable";
    case CommandState::Abandoned: return "abandoned";
  }
  return "planned";
}

bool command_is_terminal(CommandState state) noexcept {
  switch (state) {
    case CommandState::Planned:
    case CommandState::Authorized:
    case CommandState::Issued:
      return false;
    default:
      return true;
  }
}

bool command_is_unresolved(CommandState state) noexcept {
  return state == CommandState::Planned || state == CommandState::Authorized ||
         state == CommandState::Issued;
}

std::string_view to_string(EffectState state) noexcept {
  switch (state) {
    case EffectState::Unknown: return "unknown";
    case EffectState::NotObserved: return "not-observed";
    case EffectState::ObservedConsistent: return "observed-consistent";
    case EffectState::ObservedContradictory: return "observed-contradictory";
    case EffectState::Verified: return "verified";
    case EffectState::VerificationFailed: return "verification-failed";
  }
  return "unknown";
}

bool effect_is_conclusive(EffectState state) noexcept {
  return state == EffectState::Verified || state == EffectState::VerificationFailed ||
         state == EffectState::ObservedContradictory;
}

bool AttemptRecord::resolved() const noexcept {
  return !command_is_unresolved(command_state) &&
         effect_state != EffectState::Unknown && effect_state != EffectState::NotObserved;
}

Result<RequestFingerprint> fingerprint_request(const OperationRequest& request) {
  CanonicalWriter writer(CanonicalLimits{8192, 512, 1024, 64});
  GENCTL_TRY(writer.put_string(request.generator.str()));
  GENCTL_TRY(writer.put_u8(static_cast<std::uint8_t>(request.operation)));
  GENCTL_TRY(writer.put_u32(request.generation.hardware.value()));
  GENCTL_TRY(writer.put_u64(request.generation.binding.value()));
  // The state revision the caller happened to plan against is deliberately NOT part
  // of the fingerprint: a retry after a lost response may carry a revision that has
  // since moved on, and it must still resolve as the same attempt rather than as a
  // conflicting new request. Revision staleness is enforced separately, after the
  // replay lookup.
  GENCTL_TRY(writer.put_u8(static_cast<std::uint8_t>(request.authority.cls)));
  GENCTL_TRY(writer.put_u64(request.authority.epoch.value()));
  GENCTL_TRY(writer.put_string(request.idempotency_key.str()));
  if (request.idempotency_key.empty()) {
    return make_status(ErrorCode::IdempotencyKeyMissing, ValidationStage::Format,
                       "an actuating request must carry an idempotency key");
  }
  return RequestFingerprint{fnv1a64(writer.bytes())};
}

Result<AttemptRecord> AttemptJournal::append(AttemptRecord record) {
  if (record.key.empty()) {
    return make_status(ErrorCode::IdempotencyKeyMissing, ValidationStage::Format,
                       "attempt records require an idempotency key");
  }
  if (records_.size() >= policy_.max_attempts + 1) {
    return make_status(ErrorCode::GeneratorLimitExceeded, ValidationStage::Persistence,
                       "attempt journal is full");
  }
  for (const auto& existing : replay_) {
    if (existing.key == record.key) {
      return make_status(ErrorCode::IdempotencyKeyConflict, ValidationStage::IdempotencyConflict,
                         "idempotency key '" + record.key.str() + "' is already recorded");
    }
  }
  record.id = next_id_;
  record.seq = next_seq_;
  next_id_ = next_id_.next();
  next_seq_ = next_seq_.next();

  ReplayEntry entry{};
  entry.key = record.key;
  entry.attempt = record.id;
  entry.fingerprint = record.fingerprint;
  entry.operation = record.operation;
  entry.command_state = record.command_state;
  entry.effect_state = record.effect_state;
  entry.command_id = record.command_id;
  entry.recorded_at = record.created_at;

  records_.push_back(record);
  replay_.push_back(entry);
  enforce_retention();
  return record;
}

Status AttemptJournal::update(const AttemptRecord& record) {
  for (auto& existing : records_) {
    if (existing.id == record.id) {
      existing = record;
      for (auto& entry : replay_) {
        if (entry.attempt == record.id) {
          entry.command_state = record.command_state;
          entry.effect_state = record.effect_state;
          entry.command_id = record.command_id;
          return Status::success();
        }
      }
      return Status::success();
    }
  }
  return make_status(ErrorCode::AttemptNotFound, ValidationStage::Actuation,
                     "attempt " + std::to_string(record.id.value()) + " is not in the journal");
}

const AttemptRecord* AttemptJournal::find(AttemptId id) const noexcept {
  for (const auto& record : records_) {
    if (record.id == id) return &record;
  }
  return nullptr;
}

Result<ReplayEntry> AttemptJournal::lookup_key(const IdempotencyKey& key) const {
  for (const auto& entry : replay_) {
    if (entry.key == key) return entry;
  }
  return make_status(ErrorCode::AttemptNotFound, ValidationStage::IdempotencyReplay,
                     "idempotency key '" + key.str() + "' has not been seen inside the retained "
                     "replay window");
}

std::vector<AttemptRecord> AttemptJournal::for_generator(const GeneratorId& generator) const {
  std::vector<AttemptRecord> out;
  for (const auto& record : records_) {
    if (record.generator == generator) out.push_back(record);
  }
  return out;
}

const AttemptRecord* AttemptJournal::blocking_for(const GeneratorId& generator) const noexcept {
  for (const auto& record : records_) {
    if (record.generator != generator) continue;
    if (command_is_unresolved(record.command_state)) return &record;
    if (record.command_state != CommandState::Acknowledged) continue;
    if (!requires_observed_effect(record.operation)) continue;
    if (record.effect_state == EffectState::Unknown ||
        record.effect_state == EffectState::NotObserved) {
      return &record;
    }
  }
  return nullptr;
}

void AttemptJournal::enforce_retention() {
  while (records_.size() > policy_.max_attempts) {
    records_.erase(records_.begin());
    retained_floor_ = records_.empty() ? next_seq_ : AttemptJournal{}.next_seq();
    if (!records_.empty()) {
      retained_floor_ = JournalSeq{records_.front().seq.value() - 1};
    }
  }
  while (replay_.size() > policy_.idempotency_window) {
    replay_.erase(replay_.begin());
    ++evicted_keys_;
  }
}

void AttemptJournal::restore(AttemptId next_id, JournalSeq next_seq, JournalSeq retained_floor,
                             std::uint64_t evicted_keys, std::vector<AttemptRecord> records,
                             std::vector<ReplayEntry> replay) {
  next_id_ = next_id;
  next_seq_ = next_seq;
  retained_floor_ = retained_floor;
  evicted_keys_ = evicted_keys;
  records_ = std::move(records);
  replay_ = std::move(replay);
  std::sort(records_.begin(), records_.end(),
            [](const AttemptRecord& a, const AttemptRecord& b) { return a.id < b.id; });
  if (replay_.empty()) {
    (void)rebuild_replay();
  }
}

Status AttemptJournal::rebuild_replay() {
  std::vector<ReplayEntry> rebuilt;
  rebuilt.reserve(records_.size());
  for (const auto& record : records_) {
    ReplayEntry entry{};
    entry.key = record.key;
    entry.attempt = record.id;
    entry.fingerprint = record.fingerprint;
    entry.operation = record.operation;
    entry.command_state = record.command_state;
    entry.effect_state = record.effect_state;
    entry.command_id = record.command_id;
    entry.recorded_at = record.created_at;
    rebuilt.push_back(entry);
  }
  for (const auto& entry : replay_) {
    const bool present =
        std::any_of(rebuilt.begin(), rebuilt.end(),
                    [&](const ReplayEntry& candidate) { return candidate.key == entry.key; });
    if (!present) rebuilt.push_back(entry);
  }
  std::sort(rebuilt.begin(), rebuilt.end(), [](const ReplayEntry& a, const ReplayEntry& b) {
    return a.attempt < b.attempt;
  });
  for (std::size_t i = 1; i < rebuilt.size(); ++i) {
    if (rebuilt[i].key == rebuilt[i - 1].key) {
      return make_status(ErrorCode::DuplicateIdentity, ValidationStage::Persistence,
                         "replay journal contains duplicate idempotency keys");
    }
  }
  replay_ = std::move(rebuilt);
  return Status::success();
}

}  // namespace genctl
