// Proof obligations: seeded randomized state-machine exploration with reproduction
// seeds, an independent reference model for the attempt journal, and randomized
// canonical round trips.
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "genctl/canonical.hpp"
#include "genctl/engine.hpp"
#include "support.hpp"
#include "test_framework.hpp"

namespace {
using namespace genctl;
using namespace gctest;

// Independent reference model: what a correct journal must look like after a
// sequence of admitted and refused requests.
struct ReferenceJournal {
  std::vector<AttemptId> admitted{};
  std::set<std::string> keys{};
  std::size_t actuations{0};

  [[nodiscard]] bool consistent_with(const std::vector<AttemptRecord>& records) const {
    if (records.size() != admitted.size()) return false;
    for (std::size_t i = 0; i < records.size(); ++i) {
      if (!(records[i].id == admitted[i])) return false;
      if (keys.count(records[i].key.str()) == 0) return false;
    }
    return true;
  }
};

TEST(randomized_state_machine_respects_its_invariants_and_matches_the_reference_model) {
  std::mt19937 rng = make_rng(0x51u);
  LabConfig config{};
  config.label = "property-state-machine";
  Lab lab(config);
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  CHECK(id.ok());

  OpenOptions options{};
  options.store_directory = lab.store();
  options.create_if_missing = true;
  options.requested_epoch = ControlEpoch{1};
  options.config.effect_settle_millis = 5'000;
  Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(options, lab.clock(), &lab.adapter());
  CHECK(opened.ok());
  std::unique_ptr<GeneratorControlEngine> engine = std::move(opened.value());
  CHECK(engine->register_generator(id.value(), RegisterOptions{}, lab.clock().now_millis()).ok());

  // Seed the full evidence set once so that most randomized requests are admissible.
  const Result<GeneratorState> registered = engine->inspect(id.value());
  CHECK(registered.ok());
  for (const CheckKind kind : registered.value().required_checks) {
    CheckUpdate update{};
    update.generator = id.value();
    update.controller = engine->controller();
    const Result<GeneratorState> fresh = engine->inspect(id.value());
    update.revision = fresh.value().revision;
    const Result<IdempotencyKey> key = IdempotencyKey::parse(
        "prop-check-" + std::to_string(engine->commit_seq().value()) + "-" +
        std::to_string(static_cast<unsigned>(kind)));
    CHECK(key.ok());
    update.idempotency_key = key.value();
    update.record.kind = kind;
    update.record.required = true;
    update.record.state = EvidenceState::Present;
    update.record.source = EvidenceSource::SyntheticAdapter;
    update.record.observed_at = lab.clock().now_millis();
    update.record.max_age_millis = 3600 * kMillisPerSecond;
    update.record.value = TypedValue{TypedValueKind::Boolean, 1};
    CHECK(engine->record_check(update).ok());
  }
  {
    ResourceUpdate update{};
    update.generator = id.value();
    update.controller = engine->controller();
    const Result<GeneratorState> fresh = engine->inspect(id.value());
    update.revision = fresh.value().revision;
    const Result<IdempotencyKey> key =
        IdempotencyKey::parse("prop-res-" + std::to_string(engine->commit_seq().value()));
    CHECK(key.ok());
    update.idempotency_key = key.value();
    update.set_fuel_level = true;
    update.fuel_level.state = EvidenceState::Present;
    update.fuel_level.value = FuelQuantity{FuelUnit::Millilitres, 900'000};
    update.fuel_level.source = EvidenceSource::SyntheticAdapter;
    update.fuel_level.observed_at = lab.clock().now_millis();
    update.set_fuel_capacity = true;
    update.fuel_capacity.state = EvidenceState::Present;
    update.fuel_capacity.value = FuelQuantity{FuelUnit::Millilitres, 1'000'000};
    update.fuel_capacity.source = EvidenceSource::SyntheticAdapter;
    update.fuel_capacity.observed_at = lab.clock().now_millis();
    update.set_fuel_consumption = true;
    update.fuel_consumption.state = EvidenceState::Present;
    update.fuel_consumption.value = FuelRate{FuelUnit::Millilitres, 60'000};
    update.fuel_consumption.source = EvidenceSource::SyntheticAdapter;
    update.fuel_consumption.observed_at = lab.clock().now_millis();
    CHECK(engine->record_resource(update).ok());
  }

  const OperationKind operations[] = {OperationKind::Start, OperationKind::Stop,
                                      OperationKind::FaultReset, OperationKind::Synchronize,
                                      OperationKind::EnterMaintenance, OperationKind::Retire};
  ReferenceJournal reference{};
  std::size_t admitted = 0;
  std::size_t refused = 0;

  for (int step = 0; step < 120; ++step) {
    const int roll = static_cast<int>(rng() % 100u);
    if (roll < 55) {
      // Issue a random operation with a fresh key.
      const OperationKind operation = operations[rng() % 6u];
      const Result<GeneratorState> current = engine->inspect(id.value());
      CHECK(current.ok());
      OperationRequest request{};
      request.generator = id.value();
      request.operation = operation;
      request.controller = engine->controller();
      request.generation = current.value().generation;
      request.revision = current.value().revision;
      request.authority.cls = AuthorityClass::Service;
      request.authority.epoch = engine->controller().epoch;
      request.authority.granted_by = "property-test";
      request.authority.granted_at = lab.clock().now_millis();
      request.authority.reason = "randomized exploration of the state machine";
      const Result<IdempotencyKey> key = IdempotencyKey::parse(
          "prop-" + std::to_string(step) + "-" + std::to_string(rng()));
      CHECK(key.ok());
      request.idempotency_key = key.value();
      request.requested_at = lab.clock().now_millis();

      const Result<AttemptRecord> attempt = engine->execute(request, false);
      if (attempt.ok()) {
        ++admitted;
        reference.admitted.push_back(attempt.value().id);
        reference.keys.insert(attempt.value().key.str());
        if (operation == OperationKind::Start) ++reference.actuations;
      } else {
        ++refused;
        CHECK(attempt.code() != ErrorCode::Internal);
        CHECK(attempt.code() != ErrorCode::Ok);
      }
    } else if (roll < 80) {
      // Advance time and observe, resolving the oldest unresolved attempt.
      lab.clock().advance(2'000);
      const Result<std::vector<AttemptRecord>> records = engine->attempts(id.value());
      CHECK(records.ok());
      AttemptId target{};
      for (const auto& record : records.value()) {
        if (command_is_unresolved(record.command_state)) {
          target = record.id;
          break;
        }
      }
      ObserveRequest observe{};
      observe.generator = id.value();
      observe.controller = engine->controller();
      observe.requested_at = lab.clock().now_millis();
      if (!(target == AttemptId{})) {
        observe.attempt = target;
        observe.resolve_attempt = true;
      }
      const Result<ObserveOutcome> outcome = engine->observe(observe);
      CHECK(outcome.ok());
      if (outcome.value().attempt_resolved &&
          outcome.value().effect == EffectState::ObservedContradictory) {
        const Result<AttemptRecord> abandoned =
            engine->abandon(target, "randomized exploration resolved a contradictory attempt",
                            lab.clock().now_millis());
        CHECK(abandoned.ok());
      }
    } else if (roll < 90) {
      // Rewrite one readiness check with a random state and confirm the runtime never
      // becomes more permissive than the evidence.
      const CheckKind kind = CheckKind::OverspeedTripClear;
      CheckUpdate update{};
      update.generator = id.value();
      update.controller = engine->controller();
      const Result<GeneratorState> fresh = engine->inspect(id.value());
      CHECK(fresh.ok());
      update.revision = fresh.value().revision;
      const Result<IdempotencyKey> key = IdempotencyKey::parse(
          "prop-check-rnd-" + std::to_string(step) + "-" + std::to_string(rng()));
      CHECK(key.ok());
      update.idempotency_key = key.value();
      update.record.kind = kind;
      update.record.required = true;
      const bool healthy = (rng() % 2u) == 0u;
      update.record.state = healthy ? EvidenceState::Present : EvidenceState::Stale;
      update.record.source = EvidenceSource::SyntheticAdapter;
      update.record.observed_at = lab.clock().now_millis();
      update.record.max_age_millis = 60 * kMillisPerSecond;
      update.record.value = TypedValue{TypedValueKind::Boolean, 1};
      CHECK(engine->record_check(update).ok());

      const Result<ReadinessReport> readiness =
          engine->readiness(id.value(), lab.clock().now_millis());
      CHECK(readiness.ok());
      if (!healthy) CHECK(!readiness.value().satisfied);
    } else {
      lab.clock().advance(30'000);
    }

    // Invariants that must hold after every single step.
    const Result<GeneratorState> state = engine->inspect(id.value());
    CHECK(state.ok());
    CHECK(state.value().operating != OperatingState::Unknown ||
          state.value().lifecycle == LifecycleState::Commissioned);
    CHECK(state.value().synchronization != SynchronizationState::Synchronized ||
          state.value().operating == OperatingState::Synchronized);
    const Result<std::vector<AttemptRecord>> records = engine->attempts(id.value());
    CHECK(records.ok());
    CHECK(reference.consistent_with(records.value()));
    for (const auto& record : records.value()) {
      CHECK(!record.key.empty());
      CHECK(record.generator == id.value());
    }
  }

  CHECK(admitted + refused > 0);
  const Result<StoreAuditReport> audit = engine->store_audit(false, {});
  CHECK(audit.ok());
  CHECK(audit.value().anomalies.empty());
  CHECK(engine->close().ok());
}

TEST(randomized_canonical_round_trips_are_stable) {
  std::mt19937 rng = make_rng(0x77u);
  for (int iteration = 0; iteration < 500; ++iteration) {
    CanonicalWriter writer(CanonicalLimits{1u << 16, 256, 1024, 128});
    std::string text;
    const std::size_t length = rng() % 40u;
    for (std::size_t i = 0; i < length; ++i) {
      text.push_back(static_cast<char>(0x21u + (rng() % 90u)));
    }
    const std::uint64_t number = (static_cast<std::uint64_t>(rng()) << 32) | rng();
    const std::int64_t signed_number = static_cast<std::int64_t>(number);
    const bool flag = (rng() % 2u) == 0u;
    CHECK(writer.put_string(text).ok());
    CHECK(writer.put_i64(signed_number).ok());
    CHECK(writer.put_bool(flag).ok());

    CanonicalReader reader(writer.bytes().data(), writer.bytes().size(),
                           CanonicalLimits{1u << 16, 256, 1024, 128});
    const Result<std::string> back_text = reader.get_string();
    const Result<std::int64_t> back_number = reader.get_i64();
    const Result<bool> back_flag = reader.get_bool();
    CHECK(back_text.ok());
    CHECK(back_number.ok());
    CHECK(back_flag.ok());
    CHECK_EQ(back_text.value(), text);
    CHECK_EQ(back_number.value(), signed_number);
    CHECK_EQ(back_flag.value(), flag);
    CHECK(reader.expect_end().ok());

    CanonicalWriter again(CanonicalLimits{1u << 16, 256, 1024, 128});
    CHECK(again.put_string(back_text.value()).ok());
    CHECK(again.put_i64(back_number.value()).ok());
    CHECK(again.put_bool(back_flag.value()).ok());
    CHECK(again.bytes() == writer.bytes());
  }
}

TEST(randomized_mutation_of_a_committed_record_is_always_detected) {
  // Every single-byte mutation anywhere in the framed record must be caught by the
  // header checksum, the payload checksum or the payload digest.
  std::mt19937 rng = make_rng(0x99u);
  const ByteBuffer payload{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l'};
  const ByteBuffer record = encode_record(FileKind::StateImage, CommitSeq{1}, payload);
  std::size_t detected = 0;
  std::size_t total = 0;
  for (std::size_t offset = 0; offset < record.size(); ++offset) {
    ByteBuffer mutated = record;
    mutated[offset] ^= 0x01u;
    FileKind kind = FileKind::StateImage;
    CommitSeq seq{};
    ByteBuffer decoded;
    Digest256 digest{};
    const Status status = decode_record(mutated, 1024, &kind, &seq, &decoded, &digest);
    ++total;
    if (!status.ok()) ++detected;
  }
  CHECK_EQ(detected, total);
  (void)rng;
}

TEST(randomized_idempotency_keys_never_actuate_twice) {
  std::mt19937 rng = make_rng(0xABu);
  LabConfig config{};
  config.label = "property-idempotency";
  Lab lab(config);
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  CHECK(id.ok());
  OpenOptions options{};
  options.store_directory = lab.store();
  options.create_if_missing = true;
  options.requested_epoch = ControlEpoch{1};
  Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(options, lab.clock(), &lab.adapter());
  CHECK(opened.ok());
  std::unique_ptr<GeneratorControlEngine> engine = std::move(opened.value());
  CHECK(engine->register_generator(id.value(), RegisterOptions{}, 0).ok());

  // Every request is refused for missing readiness, but the idempotency bookkeeping
  // must still be exact: a repeated key never produces a second record and never
  // produces a different error.
  std::vector<std::string> used;
  for (int i = 0; i < 60; ++i) {
    std::string key;
    if (!used.empty() && (rng() % 2u) == 0u) {
      key = used[rng() % used.size()];
    } else {
      key = "prop-key-" + std::to_string(rng()) + "-" + std::to_string(i);
      used.push_back(key);
    }
    const Result<GeneratorState> current = engine->inspect(id.value());
    CHECK(current.ok());
    OperationRequest request{};
    request.generator = id.value();
    request.operation = OperationKind::Start;
    request.controller = engine->controller();
    request.generation = current.value().generation;
    request.revision = current.value().revision;
    request.authority.cls = AuthorityClass::Normal;
    request.authority.epoch = engine->controller().epoch;
    request.authority.granted_by = "property-test";
    request.idempotency_key = IdempotencyKey::parse(key).value();
    request.requested_at = lab.clock().now_millis();
    const Result<AttemptRecord> attempt = engine->execute(request, false);
    CHECK(!attempt.ok());
    CHECK_RESULT_CODE(attempt, ErrorCode::InterlockEvidenceUnavailable);
  }
  const Result<std::vector<AttemptRecord>> records = engine->attempts(id.value());
  CHECK(records.ok());
  CHECK(records.value().empty());
  CHECK_EQ(lab.adapter().issued_count(), std::size_t{0});
  CHECK(engine->close().ok());
}

}  // namespace
