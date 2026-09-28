// Generator Control - state image digest and structural validation.
#include "genctl/model.hpp"

#include <algorithm>
#include <set>

#include "codecs.hpp"

namespace genctl {

const ModelLimits& default_model_limits() noexcept {
  static const ModelLimits limits{};
  return limits;
}

Digest256 digest_state_image(const StateImage& image) {
  CanonicalWriter writer(CanonicalLimits{16u * 1024u * 1024u, 4096, 1u << 20, 8192});
  const Status status = encode_value(writer, image);
  if (!status.ok()) {
    // A state image that cannot be canonically encoded is a programming error, not
    // a data error: return the zero digest so the caller fails closed.
    return Digest256::zero();
  }
  return sha256(writer.bytes());
}

Status validate_state_image(const StateImage& image) {
  const ModelLimits& limits = default_model_limits();
  if (image.format_version != kStoreFormatVersion) {
    return make_status(ErrorCode::StoreVersionUnsupported, ValidationStage::Persistence,
                       "state image declares format " + std::to_string(image.format_version));
  }
  if (image.generators.size() > limits.max_generators) {
    return make_status(ErrorCode::GeneratorLimitExceeded, ValidationStage::Persistence,
                       "state image holds " + std::to_string(image.generators.size()) +
                           " generators, above the limit of " +
                           std::to_string(limits.max_generators));
  }
  if (image.attempts.size() > limits.max_attempts + 1) {
    return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Persistence,
                       "state image holds " + std::to_string(image.attempts.size()) +
                           " attempts, above the limit of " +
                           std::to_string(limits.max_attempts + 1));
  }
  if (image.replay.size() > limits.max_attempts * 8u + 16u) {
    return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Persistence,
                       "state image holds an oversized replay window");
  }

  AttemptId previous_attempt{};
  std::set<IdempotencyKey> keys;
  for (const auto& entry : image.replay) {
    if (entry.key.empty()) {
      return make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence,
                         "replay entry carries an empty idempotency key");
    }
    if (!keys.insert(entry.key).second) {
      return make_status(ErrorCode::DuplicateIdentity, ValidationStage::Persistence,
                         "replay window contains a duplicate idempotency key");
    }
  }
  for (const auto& attempt : image.attempts) {
    if (!previous_attempt.is_unset() && attempt.id <= previous_attempt) {
      return make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence,
                         "attempt journal is not in ascending identifier order");
    }
    previous_attempt = attempt.id;
    if (attempt.generator.empty()) {
      return make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence,
                         "attempt record has an empty generator identity");
    }
  }
  if (!previous_attempt.is_unset() && image.next_attempt_id <= previous_attempt) {
    return make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence,
                       "next attempt identifier does not follow the journal");
  }

  for (const auto& entry : image.generators) {
    const GeneratorState& state = entry.second;
    if (state.id != entry.first) {
      return make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence,
                         "generator map key does not match the stored identity");
    }
    if (state.history.size() > limits.max_history) {
      return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Persistence,
                         "generator '" + state.id.str() + "' holds an oversized history");
    }
    if (state.audit.size() > limits.max_audit_entries) {
      return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Persistence,
                         "generator '" + state.id.str() + "' holds an oversized authority audit");
    }
    if (state.checks.size() > limits.max_checks) {
      return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Persistence,
                         "generator '" + state.id.str() + "' holds an oversized check binding");
    }
    if (state.sync_preconditions.size() > limits.max_preconditions ||
        state.transfer_preconditions.size() > limits.max_preconditions) {
      return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Persistence,
                         "generator '" + state.id.str() +
                             "' holds an oversized precondition binding");
    }
    JournalSeq previous_seq{};
    for (const auto& history : state.history) {
      if (!previous_seq.is_unset() && history.seq <= previous_seq) {
        return make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence,
                           "history of generator '" + state.id.str() +
                               "' is not in ascending sequence order");
      }
      previous_seq = history.seq;
    }
    std::set<CheckKind> kinds;
    for (const CheckKind kind : state.required_checks) {
      if (!kinds.insert(kind).second) {
        return make_status(ErrorCode::DuplicateIdentity, ValidationStage::Persistence,
                           "required check set of generator '" + state.id.str() +
                               "' contains a duplicate");
      }
    }
  }
  return Status::success();
}

}  // namespace genctl
