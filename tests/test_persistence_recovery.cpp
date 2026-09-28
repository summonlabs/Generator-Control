// Proof obligations: durable round trip, canonical determinism, retention, rollback
// fencing, crash before/after the command-attempt commit, and recovery without
// duplicate external actuation.
#include <memory>
#include <string>
#include <vector>

#include "genctl/persistence.hpp"
#include "genctl/platform.hpp"
#include "support.hpp"
#include "test_framework.hpp"

namespace {
using namespace genctl;
using namespace gctest;

std::unique_ptr<GeneratorControlEngine> open_lab(Lab& lab, SyntheticAdapter& adapter,
                                                 bool recover_scan = false) {
  OpenOptions options{};
  options.store_directory = lab.store();
  options.create_if_missing = true;
  options.recover_scan = recover_scan;
  options.requested_epoch = ControlEpoch{static_cast<std::uint64_t>(1)};
  // Keep every generation so that a test can restore an older head marker and prove
  // the fence notices the rollback.
  options.config.generation_retention = 64;
  Result<std::unique_ptr<GeneratorControlEngine>> engine =
      GeneratorControlEngine::open(options, lab.clock(), &adapter);
  if (!engine.ok()) fail("engine open failed: " + engine.status().message());
  return std::move(engine.value());
}

void register_generator(GeneratorControlEngine& engine, const GeneratorId& id, EpochMillis now) {
  const Result<GeneratorState> state =
      engine.register_generator(id, RegisterOptions{}, now);
  if (!state.ok()) fail("registration failed: " + state.status().message());
}

TEST(a_store_round_trips_through_close_and_reopen) {
  LabConfig config{};
  config.label = "round-trip";
  Lab lab(config);
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  CHECK(id.ok());

  ManualClock clock;
  SyntheticAdapter adapter(id.value(), clock, SyntheticAdapter::Options{});
  {
    std::unique_ptr<GeneratorControlEngine> engine = open_lab(lab, adapter);
    register_generator(*engine, id.value(), 1'000);
    CHECK(engine->commit_seq().value() > 0);
    CHECK(engine->close().ok());
  }
  {
    std::unique_ptr<GeneratorControlEngine> engine = open_lab(lab, adapter);
    const Result<GeneratorState> state = engine->inspect(id.value());
    CHECK(state.ok());
    CHECK_EQ(state.value().operating, OperatingState::Stopped);
    CHECK_EQ(state.value().lifecycle, LifecycleState::Commissioned);
    CHECK_EQ(state.value().mode, OperatingMode::Normal);
    CHECK(engine->reopen_report().incarnation.value() > 1);
    CHECK(engine->close().ok());
  }
}

TEST(equivalent_state_produces_an_identical_commit_digest_across_reopen) {
  LabConfig config{};
  config.label = "canonical-round-trip";
  Lab lab(config);
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  CHECK(id.ok());
  ManualClock clock;
  SyntheticAdapter adapter(id.value(), clock, SyntheticAdapter::Options{});

  std::string first_digest;
  {
    std::unique_ptr<GeneratorControlEngine> engine = open_lab(lab, adapter);
    register_generator(*engine, id.value(), 5'000);
    Result<StoreAuditReport> audit = engine->store_audit(false, {});
    CHECK(audit.ok());
    first_digest = audit.value().head_digest.hex();
    CHECK(engine->close().ok());
  }

  // Reopening a store that holds no volatile evidence must not change a single byte
  // of the committed state. If it did, the same logical state would hash differently.
  {
    std::unique_ptr<GeneratorControlEngine> engine = open_lab(lab, adapter);
    CHECK_EQ(engine->reopen_report().demoted_evidence_records, std::size_t{0});
    Result<StoreAuditReport> audit = engine->store_audit(false, {});
    CHECK(audit.ok());
    CHECK_EQ(audit.value().head_digest.hex(), first_digest);
    CHECK(engine->close().ok());
  }
}

TEST(generation_retention_bounds_the_store_and_residue_is_retired) {
  LabConfig config{};
  config.label = "retention";
  Lab lab(config);
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  CHECK(id.ok());
  ManualClock clock;
  SyntheticAdapter adapter(id.value(), clock, SyntheticAdapter::Options{});
  std::unique_ptr<GeneratorControlEngine> engine = open_lab(lab, adapter);
  register_generator(*engine, id.value(), 1'000);

  // Drop an interrupted publication into the store and confirm it is retired.
  const ByteBuffer residue{'g', 'a', 'r', 'b', 'a', 'g', 'e'};
  CHECK(platform::durable_write_file(lab.store() + "\\generation-00000000000000000999.gcs.staging",
                                     residue, false)
            .ok());
  CHECK(platform::durable_write_file(lab.store() + "\\genctl.head.tmp", residue, false).ok());
  CHECK(engine->close().ok());

  engine = open_lab(lab, adapter);
  CHECK(engine->reopen_report().residue_retired >= 2);
  const Result<StoreAuditReport> audit = engine->store_audit(false, {});
  CHECK(audit.ok());
  CHECK(audit.value().generations_valid <= 5);
  CHECK(audit.value().anomalies.empty());
  CHECK(engine->close().ok());
}

TEST(rollback_to_an_older_valid_generation_is_detected_and_fenced) {
  LabConfig config{};
  config.label = "rollback";
  Lab lab(config);
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  CHECK(id.ok());
  ManualClock clock;
  SyntheticAdapter adapter(id.value(), clock, SyntheticAdapter::Options{});
  std::unique_ptr<GeneratorControlEngine> engine = open_lab(lab, adapter);
  register_generator(*engine, id.value(), 1'000);

  // Keep a copy of the head marker as it stands now.
  const std::string head_path = lab.store() + "\\genctl.head";
  const Result<ByteBuffer> saved_head = platform::read_file_bounded(head_path, 1u << 20);
  CHECK(saved_head.ok());

  // Move the store forward.
  for (int i = 0; i < 4; ++i) {
    ResourceUpdate update{};
    update.generator = id.value();
    update.controller = engine->controller();
    const Result<GeneratorState> current = engine->inspect(id.value());
    CHECK(current.ok());
    update.revision = current.value().revision;
    const Result<IdempotencyKey> key =
        IdempotencyKey::parse("rollback-" + std::to_string(i) + "-" +
                              std::to_string(engine->commit_seq().value()));
    CHECK(key.ok());
    update.idempotency_key = key.value();
    update.set_fuel_level = true;
    update.fuel_level.state = EvidenceState::Present;
    update.fuel_level.value = FuelQuantity{FuelUnit::Millilitres, 900'000 - i};
    update.fuel_level.source = EvidenceSource::SyntheticAdapter;
    update.fuel_level.observed_at = clock.now_millis();
    CHECK(engine->record_resource(update).ok());
  }
  CHECK(engine->close().ok());

  // Restore the old head marker: the fence must notice that the store went backwards.
  CHECK(platform::durable_write_file(head_path, saved_head.value(), false).ok());

  OpenOptions options{};
  options.store_directory = lab.store();
  options.requested_epoch = ControlEpoch{1};
  options.config.generation_retention = 64;
  const Result<std::unique_ptr<GeneratorControlEngine>> refused =
      GeneratorControlEngine::open(options, clock, &adapter);
  CHECK_RESULT_CODE(refused, ErrorCode::StoreRollbackDetected);

  // An explicit operator acceptance opens the store, and the acceptance is recorded.
  options.accept_rollback = true;
  options.rollback_acceptance_note = "restored from the 02:00 backup after a disk fault";
  const Result<std::unique_ptr<GeneratorControlEngine>> accepted =
      GeneratorControlEngine::open(options, clock, &adapter);
  CHECK(accepted.ok());
  CHECK(accepted.value()->close().ok());
}

TEST(a_missing_head_marker_requires_an_explicit_recovery_scan) {
  LabConfig config{};
  config.label = "recovery-scan";
  Lab lab(config);
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  CHECK(id.ok());
  ManualClock clock;
  SyntheticAdapter adapter(id.value(), clock, SyntheticAdapter::Options{});
  std::unique_ptr<GeneratorControlEngine> engine = open_lab(lab, adapter);
  register_generator(*engine, id.value(), 1'000);
  CHECK(engine->close().ok());
  CHECK(platform::remove_file(lab.store() + "\\genctl.head").ok());

  OpenOptions options{};
  options.store_directory = lab.store();
  options.requested_epoch = ControlEpoch{1};
  options.config.generation_retention = 64;
  const Result<std::unique_ptr<GeneratorControlEngine>> refused =
      GeneratorControlEngine::open(options, clock, &adapter);
  CHECK_RESULT_CODE(refused, ErrorCode::StoreCorrupt);

  options.recover_scan = true;
  const Result<std::unique_ptr<GeneratorControlEngine>> recovered =
      GeneratorControlEngine::open(options, clock, &adapter);
  CHECK(recovered.ok());
  const Result<GeneratorState> state = recovered.value()->inspect(id.value());
  CHECK(state.ok());
  CHECK(recovered.value()->close().ok());
}

TEST(recovery_never_makes_volatile_evidence_fresh) {
  LabConfig config{};
  config.label = "demotion";
  Lab lab(config);
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  CHECK(id.ok());
  ManualClock clock;
  SyntheticAdapter adapter(id.value(), clock, SyntheticAdapter::Options{});
  std::unique_ptr<GeneratorControlEngine> engine = open_lab(lab, adapter);
  register_generator(*engine, id.value(), 1'000);
  const Result<GeneratorState> registered = engine->inspect(id.value());
  CHECK(registered.ok());
  for (const CheckKind kind : registered.value().required_checks) {
    CheckUpdate update{};
    update.generator = id.value();
    update.controller = engine->controller();
    const Result<GeneratorState> fresh = engine->inspect(id.value());
    CHECK(fresh.ok());
    update.revision = fresh.value().revision;
    const Result<IdempotencyKey> key = IdempotencyKey::parse(
        "demote-" + std::to_string(engine->commit_seq().value()) + "-" +
        std::to_string(static_cast<unsigned>(kind)));
    CHECK(key.ok());
    update.idempotency_key = key.value();
    update.record.kind = kind;
    update.record.required = true;
    update.record.state = EvidenceState::Present;
    update.record.source = EvidenceSource::VendorAdapter;
    update.record.observed_at = clock.now_millis();
    update.record.max_age_millis = 24 * kMillisPerHour;
    update.record.value = TypedValue{TypedValueKind::Boolean, 1};
    CHECK(engine->record_check(update).ok());
  }
  const Result<ReadinessReport> before = engine->readiness(id.value(), clock.now_millis());
  CHECK(before.ok());
  CHECK(before.value().satisfied);
  CHECK(engine->close().ok());

  engine = open_lab(lab, adapter);
  CHECK(engine->reopen_report().demoted_evidence_records > 0);
  const Result<ReadinessReport> after = engine->readiness(id.value(), clock.now_millis());
  CHECK(after.ok());
  CHECK(!after.value().satisfied);
  // The evidence is still comfortably inside its freshness bound; it is refused
  // because a new incarnation never inherits a previous incarnation's observation.
  CHECK_EQ(after.value().primary_error, ErrorCode::InterlockEvidenceUnavailable);
  CHECK(engine->close().ok());
}

TEST(synchronized_state_is_not_carried_across_a_restart) {
  LabConfig config{};
  config.label = "sync-restart";
  config.seed_sync = true;
  Lab lab(config);
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  CHECK(id.ok());
  ManualClock clock;
  SyntheticAdapter::Options adapter_options{};
  adapter_options.journal_path = lab.journal();
  SyntheticAdapter adapter(id.value(), clock, adapter_options);
  std::unique_ptr<GeneratorControlEngine> engine = open_lab(lab, adapter);
  register_generator(*engine, id.value(), 1'000);
  const Result<GeneratorState> registered = engine->inspect(id.value());
  CHECK(registered.ok());
  for (const CheckKind kind : registered.value().required_checks) {
    CheckUpdate update{};
    update.generator = id.value();
    update.controller = engine->controller();
    const Result<GeneratorState> fresh = engine->inspect(id.value());
    CHECK(fresh.ok());
    update.revision = fresh.value().revision;
    const Result<IdempotencyKey> key = IdempotencyKey::parse(
        "sync-restart-" + std::to_string(engine->commit_seq().value()) + "-" +
        std::to_string(static_cast<unsigned>(kind)));
    CHECK(key.ok());
    update.idempotency_key = key.value();
    update.record.kind = kind;
    update.record.required = true;
    update.record.state = EvidenceState::Present;
    update.record.source = EvidenceSource::VendorAdapter;
    update.record.observed_at = clock.now_millis();
    update.record.max_age_millis = 24 * kMillisPerHour;
    update.record.value = TypedValue{TypedValueKind::Boolean, 1};
    CHECK(engine->record_check(update).ok());
  }
  (void)engine->close();
  engine.reset();

  // Reopen, start the set for real, parallel it, and confirm the stored state.
  engine = open_lab(lab, adapter);
  // The state must be held in a named object: iterating a vector that lives inside a
  // temporary would read freed storage.
  const Result<GeneratorState> reopened_state = engine->inspect(id.value());
  CHECK(reopened_state.ok());
  for (const CheckKind kind : reopened_state.value().required_checks) {
    CheckUpdate update{};
    update.generator = id.value();
    update.controller = engine->controller();
    const Result<GeneratorState> fresh = engine->inspect(id.value());
    CHECK(fresh.ok());
    update.revision = fresh.value().revision;
    const Result<IdempotencyKey> key = IdempotencyKey::parse(
        "sync-restart2-" + std::to_string(engine->commit_seq().value()) + "-" +
        std::to_string(static_cast<unsigned>(kind)));
    CHECK(key.ok());
    update.idempotency_key = key.value();
    update.record.kind = kind;
    update.record.required = true;
    update.record.state = EvidenceState::Present;
    update.record.source = EvidenceSource::VendorAdapter;
    update.record.observed_at = clock.now_millis();
    update.record.max_age_millis = 24 * kMillisPerHour;
    update.record.value = TypedValue{TypedValueKind::Boolean, 1};
    CHECK(engine->record_check(update).ok());
  }
  adapter.external_set_running(true);
  adapter.external_set_synchronized(true);
  adapter.external_set_breaker(BreakerPosition::Closed);
  ObserveRequest observe{};
  observe.generator = id.value();
  observe.controller = engine->controller();
  observe.requested_at = clock.now_millis();
  CHECK(engine->observe(observe).ok());
  const Result<GeneratorState> synchronized_state = engine->inspect(id.value());
  CHECK(synchronized_state.ok());
  CHECK_EQ(synchronized_state.value().synchronization, SynchronizationState::Synchronized);
  CHECK(engine->close().ok());
  engine.reset();

  engine = open_lab(lab, adapter);
  const Result<GeneratorState> reopened = engine->inspect(id.value());
  CHECK(reopened.ok());
  CHECK(reopened.value().synchronization != SynchronizationState::Synchronized);
  CHECK_EQ(reopened.value().synchronization, SynchronizationState::Unknown);
  CHECK(reopened.value().operating != OperatingState::Synchronized);
  CHECK(engine->close().ok());
}

// ---------------------------------------------------------------------------
// Crash injection: every durable stage is interrupted, then the store is reopened.
// ---------------------------------------------------------------------------
struct CrashExpectation {
  const char* point;
  std::size_t actuations;
  bool attempt_committed;
};

TEST(process_death_at_every_durable_stage_leaves_one_whole_state) {
  const CrashExpectation cases[] = {
      {"after-reserve-before-staging", 0, false},
      {"after-staging-before-readback", 0, false},
      {"after-readback-before-publish", 0, false},
      {"after-publish-before-head-commit", 0, false},
      {"after-head-commit-before-fence", 0, false},
      {"after-publish-before-actuation", 0, true},
      {"after-actuation-before-ack-commit", 1, true},
      {"after-ack-commit", 1, true},
  };
  const Result<std::string> executable = platform::current_executable_path();
  CHECK(executable.ok());

  for (const CrashExpectation& item : cases) {
    LabConfig config{};
    config.label = std::string("crash-") + item.point;
    Lab lab(config);
    const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
    CHECK(id.ok());

    // Establish the store and the generator in this process, then release the lock.
    ManualClock setup_clock;
    SyntheticAdapter::Options adapter_options{};
    adapter_options.journal_path = lab.journal();
    SyntheticAdapter setup_adapter(id.value(), setup_clock, adapter_options);
    std::unique_ptr<GeneratorControlEngine> engine = open_lab(lab, setup_adapter);
    register_generator(*engine, id.value(), 1'000);
    CHECK(engine->close().ok());
    engine.reset();

    const std::string output = lab.store() + "\\child-output.txt";
    const Result<int> code = platform::run_process(
        executable.value(),
        {"--scenario", "start", lab.store(), lab.journal(), "crash-key-000001", item.point},
        output);
    CHECK(code.ok());
    CHECK(code.value() != 0);

    // Reopen: exactly one whole verified state must be adopted.
    ManualClock clock;
    const Result<std::size_t> replayed = setup_adapter.replay_journal();
    CHECK(replayed.ok());
    engine = open_lab(lab, setup_adapter);
    CHECK(engine->commit_seq().value() > 0);
    const Result<GeneratorState> state = engine->inspect(id.value());
    CHECK(state.ok());
    CHECK_EQ(setup_adapter.accepted_count(OperationKind::Start), item.actuations);
    Result<StoreAuditReport> audit = engine->store_audit(false, {});
    CHECK(audit.ok());
    CHECK(audit.value().anomalies.empty());

    const Result<std::vector<AttemptRecord>> attempts = engine->attempts(id.value());
    CHECK(attempts.ok());
    if (item.attempt_committed) {
      CHECK_EQ(attempts.value().size(), std::size_t{1});
      // Re-issuing the same key must replay the recorded attempt, never actuate again.
      const Result<AttemptRecord> replayed_attempt = engine->execute(
          [&] {
            const Result<GeneratorState> current = engine->inspect(id.value());
            OperationRequest request{};
            request.generator = id.value();
            request.operation = OperationKind::Start;
            request.controller = engine->controller();
            request.generation = current.value().generation;
            request.revision = current.value().revision;
            request.authority.cls = AuthorityClass::Normal;
            request.authority.epoch = engine->controller().epoch;
            request.authority.granted_by = "parent-process";
            request.idempotency_key = IdempotencyKey::parse("crash-key-000001").value();
            request.requested_at = clock.now_millis();
            return request;
          }(),
          false);
      CHECK(replayed_attempt.ok());
      CHECK(replayed_attempt.value().replayed);
      CHECK_EQ(setup_adapter.accepted_count(OperationKind::Start), item.actuations);

      if (item.actuations == 0) {
        // The command never reached the device. The attempt is unresolved, so a new
        // key is refused until it is resolved by observation and abandoned.
        OperationRequest blocking{};
        const Result<GeneratorState> current = engine->inspect(id.value());
        blocking.generator = id.value();
        blocking.operation = OperationKind::Start;
        blocking.controller = engine->controller();
        blocking.generation = current.value().generation;
        blocking.revision = current.value().revision;
        blocking.authority.cls = AuthorityClass::Normal;
        blocking.authority.epoch = engine->controller().epoch;
        blocking.authority.granted_by = "parent-process";
        blocking.idempotency_key = IdempotencyKey::parse("crash-key-000002").value();
        blocking.requested_at = clock.now_millis();
        CHECK_RESULT_CODE(engine->execute(blocking, false), ErrorCode::AttemptUnresolved);
      } else {
        // The device did actuate. Observation resolves the attempt and confirms it.
        clock.advance(20 * kMillisPerSecond);
        ObserveRequest observe{};
        observe.generator = id.value();
        observe.controller = engine->controller();
        observe.attempt = attempts.value().front().id;
        observe.resolve_attempt = true;
        observe.requested_at = clock.now_millis();
        const Result<ObserveOutcome> outcome = engine->observe(observe);
        CHECK(outcome.ok());
        CHECK_EQ(outcome.value().attempt.effect_state, EffectState::Verified);
      }
    } else {
      CHECK(attempts.value().empty());
    }
    CHECK(engine->close().ok());
    engine.reset();
  }
}

}  // namespace
