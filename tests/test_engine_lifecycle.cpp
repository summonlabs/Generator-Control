// Proof obligations: the end-to-end start lifecycle, command acknowledgement versus
// electrical effect, test-mode separation, emergency authority, idempotent replay,
// fencing and deterministic single-device ordering.
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "genctl/authority.hpp"
#include "genctl/engine.hpp"
#include "support.hpp"
#include "test_framework.hpp"

namespace {
using namespace genctl;
using namespace gctest;

// A lab whose engine has been opened and whose evidence has been seeded. The fixture
// keeps the engine alive for the whole test so that no reopening (and therefore no
// evidence demotion) happens in the middle of a scenario.
struct OpenLab {
  explicit OpenLab(LabConfig config = LabConfig{}) : lab(config) {
    Result<std::unique_ptr<GeneratorControlEngine>> opened =
        GeneratorControlEngine::open(lab.open_options(), lab.clock(), &lab.adapter());
    if (!opened.ok()) fail("engine open failed: " + opened.status().message());
    engine = std::move(opened.value());
    const Result<GeneratorState> registered =
        engine->register_generator(lab.generator(), RegisterOptions{}, lab.now());
    if (!registered.ok()) fail("registration failed: " + registered.status().message());
    if (config.seed_checks) seed_checks();
    if (config.seed_resources) seed_resources();
    if (config.seed_sync) seed_sync();
    if (config.seed_transfer) seed_transfer();
  }

  void seed_checks() {
    const Result<GeneratorState> current = engine->inspect(lab.generator());
    if (!current.ok()) fail("inspect failed");
    for (const CheckKind kind : current.value().required_checks) {
      CheckUpdate update{};
      update.generator = lab.generator();
      update.controller = engine->controller();
      const Result<GeneratorState> fresh = engine->inspect(lab.generator());
      if (!fresh.ok()) fail("inspect failed");
      update.revision = fresh.value().revision;
      const Result<IdempotencyKey> key = IdempotencyKey::parse(
          "lab-check-" + std::to_string(engine->commit_seq().value()) + "-" +
          std::to_string(static_cast<unsigned>(kind)));
      if (!key.ok()) fail("key invalid");
      update.idempotency_key = key.value();
      update.record.kind = kind;
      update.record.required = true;
      update.record.state = EvidenceState::Present;
      update.record.source = EvidenceSource::SyntheticAdapter;
      update.record.observed_at = lab.now();
      update.record.max_age_millis = 3600 * kMillisPerSecond;
      update.record.value = TypedValue{TypedValueKind::Boolean, 1};
      const Status status = engine->record_check(update);
      if (!status.ok()) fail("check seeding failed: " + status.message());
    }
  }

  void seed_resources() {
    ResourceUpdate update{};
    update.generator = lab.generator();
    update.controller = engine->controller();
    const Result<GeneratorState> fresh = engine->inspect(lab.generator());
    if (!fresh.ok()) fail("inspect failed");
    update.revision = fresh.value().revision;
    const Result<IdempotencyKey> key =
        IdempotencyKey::parse("lab-res-" + std::to_string(engine->commit_seq().value()));
    if (!key.ok()) fail("key invalid");
    update.idempotency_key = key.value();
    const auto fill = [&](auto& evidence, auto quantity) {
      evidence.state = EvidenceState::Present;
      evidence.value = quantity;
      evidence.source = EvidenceSource::SyntheticAdapter;
      evidence.observed_at = lab.now();
    };
    update.set_fuel_level = true;
    fill(update.fuel_level, FuelQuantity{FuelUnit::Millilitres, 900'000});
    update.set_fuel_capacity = true;
    fill(update.fuel_capacity, FuelQuantity{FuelUnit::Millilitres, 1'000'000});
    update.set_fuel_consumption = true;
    fill(update.fuel_consumption, FuelRate{FuelUnit::Millilitres, 60'000});
    update.set_lube_oil_pressure = true;
    fill(update.lube_oil_pressure, Pressure{410'000});
    update.set_coolant_temperature = true;
    fill(update.coolant_temperature, Temperature{78'000});
    update.set_coolant_level = true;
    fill(update.coolant_level, Percent{9'200});
    update.set_battery_voltage = true;
    fill(update.battery_voltage, Voltage{25'600});
    const Status status = engine->record_resource(update);
    if (!status.ok()) fail("resource seeding failed: " + status.message());
  }

  void seed_sync() {
    const Result<GeneratorState> current = engine->inspect(lab.generator());
    if (!current.ok()) fail("inspect failed");
    for (const SyncPreconditionKind kind : default_sync_preconditions(current.value().sync_policy)) {
      SyncPreconditionUpdate update{};
      update.generator = lab.generator();
      update.controller = engine->controller();
      const Result<GeneratorState> fresh = engine->inspect(lab.generator());
      if (!fresh.ok()) fail("inspect failed");
      update.revision = fresh.value().revision;
      const Result<IdempotencyKey> key = IdempotencyKey::parse(
          "lab-sync-" + std::to_string(engine->commit_seq().value()) + "-" +
          std::to_string(static_cast<unsigned>(kind)));
      if (!key.ok()) fail("key invalid");
      update.idempotency_key = key.value();
      update.record.kind = kind;
      update.record.required = true;
      update.record.state = EvidenceState::Present;
      update.record.source = EvidenceSource::SyntheticAdapter;
      update.record.observed_at = lab.now();
      update.record.max_age_millis = 3600 * kMillisPerSecond;
      switch (kind) {
        case SyncPreconditionKind::VoltageMatch:
          update.record.value = TypedValue{TypedValueKind::Millivolts, 1200};
          break;
        case SyncPreconditionKind::FrequencyMatch:
          update.record.value = TypedValue{TypedValueKind::Millihertz, 40};
          break;
        case SyncPreconditionKind::PhaseAngleMatch:
          update.record.value = TypedValue{TypedValueKind::Millidegrees, 1500};
          break;
        default:
          update.record.value = TypedValue{TypedValueKind::Boolean, 1};
          break;
      }
      const Status status = engine->record_sync_precondition(update);
      if (!status.ok()) fail("sync seeding failed: " + status.message());
    }
  }

  void seed_transfer() {
    TransferPathUpdate path_update{};
    path_update.generator = lab.generator();
    path_update.controller = engine->controller();
    const Result<GeneratorState> fresh = engine->inspect(lab.generator());
    if (!fresh.ok()) fail("inspect failed");
    path_update.revision = fresh.value().revision;
    const Result<IdempotencyKey> key =
        IdempotencyKey::parse("lab-path-" + std::to_string(engine->commit_seq().value()));
    if (!key.ok()) fail("key invalid");
    path_update.idempotency_key = key.value();
    path_update.path.breaker = SwitchRef::parse("breaker-1").value();
    path_update.path.source_bus = SwitchRef::parse("bus-utility").value();
    path_update.path.target_bus = SwitchRef::parse("bus-generator").value();
    path_update.path.feeder = SwitchRef::parse("feeder-1").value();
    path_update.path.transfer_path = SwitchRef::parse("path-1").value();
    if (!engine->set_transfer_path(path_update).ok()) fail("path recording failed");

    SwitchAuthorityUpdate token{};
    token.generator = lab.generator();
    token.controller = engine->controller();
    const Result<GeneratorState> fresh2 = engine->inspect(lab.generator());
    if (!fresh2.ok()) fail("inspect failed");
    token.revision = fresh2.value().revision;
    const Result<IdempotencyKey> token_key =
        IdempotencyKey::parse("lab-token-" + std::to_string(engine->commit_seq().value()));
    if (!token_key.ok()) fail("key invalid");
    token.idempotency_key = token_key.value();
    token.token.state = EvidenceState::Present;
    token.token.source = EvidenceSource::ExternalAuthority;
    token.token.lifetime = EvidenceLifetime::AttestedWithValidity;
    token.token.observed_at = lab.now();
    token.token.valid_until = lab.now() + 3600 * kMillisPerSecond;
    token.token.value.authority = SwitchRef::parse("switch-authority").value();
    token.token.value.subject_switch = SwitchRef::parse("breaker-1").value();
    token.token.value.subject_generator = lab.generator();
    token.token.value.subject_generation = fresh2.value().generation;
    token.token.value.epoch = engine->controller().epoch;
    token.token.value.issued_at = lab.now();
    token.token.value.valid_until = lab.now() + 3600 * kMillisPerSecond;
    if (!engine->record_switch_authority(token).ok()) fail("token recording failed");

    for (const TransferPreconditionKind kind : default_transfer_preconditions()) {
      TransferPreconditionUpdate update{};
      update.generator = lab.generator();
      update.controller = engine->controller();
      const Result<GeneratorState> fresh3 = engine->inspect(lab.generator());
      if (!fresh3.ok()) fail("inspect failed");
      update.revision = fresh3.value().revision;
      const Result<IdempotencyKey> item_key = IdempotencyKey::parse(
          "lab-transfer-" + std::to_string(engine->commit_seq().value()) + "-" +
          std::to_string(static_cast<unsigned>(kind)));
      if (!item_key.ok()) fail("key invalid");
      update.idempotency_key = item_key.value();
      update.record.kind = kind;
      update.record.required = true;
      update.record.state = EvidenceState::Present;
      update.record.source = EvidenceSource::ExternalAuthority;
      update.record.observed_at = lab.now();
      update.record.max_age_millis = 3600 * kMillisPerSecond;
      update.record.value = TypedValue{TypedValueKind::Boolean, 1};
      const Status status = engine->record_transfer_precondition(update);
      if (!status.ok()) fail("transfer seeding failed: " + status.message());
    }
  }

  OperationRequest request(OperationKind operation, const std::string& key,
                           AuthorityClass cls = AuthorityClass::Normal,
                           const std::string& reason = {}) {
    const Result<GeneratorState> current = engine->inspect(lab.generator());
    if (!current.ok()) fail("inspect failed");
    OperationRequest built{};
    built.generator = lab.generator();
    built.operation = operation;
    built.controller = engine->controller();
    built.generation = current.value().generation;
    built.revision = current.value().revision;
    built.authority.cls = cls;
    built.authority.epoch = engine->controller().epoch;
    built.authority.granted_by = "test-harness";
    built.authority.granted_at = lab.now();
    built.authority.reason = reason;
    built.authority.explicit_grant = true;
    if (cls == AuthorityClass::Emergency) {
      built.authority.valid_until = lab.now() + 30 * kMillisPerMinute;
    }
    const Result<IdempotencyKey> parsed = IdempotencyKey::parse(key);
    if (!parsed.ok()) fail("key invalid");
    built.idempotency_key = parsed.value();
    built.requested_at = lab.now();
    return built;
  }

  void set_mode(OperatingMode mode, AuthorityClass cls) {
    ModeUpdate update{};
    update.generator = lab.generator();
    update.controller = engine->controller();
    const Result<GeneratorState> current = engine->inspect(lab.generator());
    if (!current.ok()) fail("inspect failed");
    update.revision = current.value().revision;
    const Result<IdempotencyKey> key = IdempotencyKey::parse(
        "mode-" + std::to_string(static_cast<unsigned>(mode)) + "-" +
        std::to_string(engine->commit_seq().value()));
    if (!key.ok()) fail("key invalid");
    update.idempotency_key = key.value();
    update.mode = mode;
    update.authority.cls = cls;
    update.authority.epoch = engine->controller().epoch;
    update.authority.granted_by = "test-harness";
    update.authority.granted_at = lab.now();
    update.authority.explicit_grant = true;
    update.authority.reason = "test mode change";
    if (cls == AuthorityClass::Emergency) {
      update.authority.valid_until = lab.now() + 30 * kMillisPerMinute;
    }
    update.requested_at = lab.now();
    const Status status = engine->update_mode(update);
    if (!status.ok()) fail("mode change failed: " + status.message());
  }

  void advance(Millis delta) { lab.clock().advance(delta); }

  Lab lab;
  std::unique_ptr<GeneratorControlEngine> engine;
};

TEST(successful_synthetic_start_lifecycle) {
  LabConfig config{};
  config.label = "start-lifecycle";
  OpenLab fixture(config);

  const Result<EvaluationReport> planned =
      fixture.engine->evaluate(fixture.request(OperationKind::Start, "eval-start-0001"));
  CHECK(planned.ok());
  CHECK(planned.value().permitted);
  CHECK(planned.value().readiness.satisfied);
  CHECK(planned.value().resources.sufficient);
  CHECK(!planned.value().readiness.findings.empty());

  const Result<AttemptRecord> attempt =
      fixture.engine->execute(fixture.request(OperationKind::Start, "start-00000001"), false);
  CHECK(attempt.ok());
  CHECK_EQ(attempt.value().command_state, CommandState::Acknowledged);
  CHECK_EQ(attempt.value().ack_status, AdapterAckStatus::Accepted);
  CHECK_EQ(attempt.value().effect_state, EffectState::NotObserved);
  CHECK(!attempt.value().effect_verified());

  // The commanded effect is provisional and provably not yet verified.
  const Result<GeneratorState> commanded = fixture.engine->inspect(fixture.lab.generator());
  CHECK(commanded.ok());
  CHECK_EQ(commanded.value().operating, OperatingState::Starting);
  CHECK(commanded.value().synchronization != SynchronizationState::Synchronized);

  // Time passes, the device runs, and only an observation may verify the effect.
  fixture.advance(11 * kMillisPerSecond);
  ObserveRequest observe{};
  observe.generator = fixture.lab.generator();
  observe.controller = fixture.engine->controller();
  observe.attempt = attempt.value().id;
  observe.resolve_attempt = true;
  observe.requested_at = fixture.lab.now();
  const Result<ObserveOutcome> outcome = fixture.engine->observe(observe);
  CHECK(outcome.ok());
  CHECK(outcome.value().attempt_resolved);
  CHECK_EQ(outcome.value().attempt.effect_state, EffectState::Verified);
  CHECK_EQ(outcome.value().attempt.effect_source, EvidenceSource::SyntheticAdapter);
  CHECK(outcome.value().observation.running);
  CHECK_EQ(outcome.value().observation.reported_state, OperatingState::ReadyUnsynchronized);

  const Result<GeneratorState> state = fixture.engine->inspect(fixture.lab.generator());
  CHECK(state.ok());
  CHECK_EQ(state.value().operating, OperatingState::ReadyUnsynchronized);
  CHECK(state.value().synchronization != SynchronizationState::Synchronized);
}

TEST(acknowledgement_without_effect_is_not_a_synchronized_state) {
  LabConfig config{};
  config.label = "ack-no-sync";
  config.seed_sync = true;
  OpenLab fixture(config);

  // The set is started and observed as running before synchronization is attempted.
  const Result<AttemptRecord> start =
      fixture.engine->execute(fixture.request(OperationKind::Start, "presync-start-01"), false);
  CHECK(start.ok());
  fixture.advance(20 * kMillisPerSecond);
  ObserveRequest start_observe{};
  start_observe.generator = fixture.lab.generator();
  start_observe.controller = fixture.engine->controller();
  start_observe.attempt = start.value().id;
  start_observe.resolve_attempt = true;
  start_observe.requested_at = fixture.lab.now();
  const Result<ObserveOutcome> started = fixture.engine->observe(start_observe);
  CHECK(started.ok());
  CHECK_EQ(started.value().attempt.effect_state, EffectState::Verified);
  CHECK_EQ(started.value().observation.reported_state, OperatingState::ReadyUnsynchronized);

  // The device acknowledges a synchronization command but the external switch actor
  // never closes the breaker: the runtime must not claim synchronization.
  const Result<AttemptRecord> attempt = fixture.engine->execute(
      fixture.request(OperationKind::Synchronize, "sync-000000001"), false);
  CHECK(attempt.ok());
  CHECK_EQ(attempt.value().command_state, CommandState::Acknowledged);
  CHECK_EQ(attempt.value().effect_state, EffectState::NotObserved);

  const Result<GeneratorState> commanded = fixture.engine->inspect(fixture.lab.generator());
  CHECK(commanded.ok());
  CHECK(commanded.value().synchronization == SynchronizationState::Synchronizing);
  CHECK(commanded.value().synchronization != SynchronizationState::Synchronized);
  CHECK(commanded.value().operating != OperatingState::Synchronized);

  fixture.advance(60 * kMillisPerSecond);
  ObserveRequest observe{};
  observe.generator = fixture.lab.generator();
  observe.controller = fixture.engine->controller();
  observe.attempt = attempt.value().id;
  observe.resolve_attempt = true;
  observe.requested_at = fixture.lab.now();
  const Result<ObserveOutcome> outcome = fixture.engine->observe(observe);
  CHECK(outcome.ok());
  CHECK_EQ(outcome.value().attempt.effect_state, EffectState::ObservedContradictory);

  const Result<GeneratorState> after = fixture.engine->inspect(fixture.lab.generator());
  CHECK(after.ok());
  CHECK(after.value().synchronization != SynchronizationState::Synchronized);
  CHECK(after.value().operating != OperatingState::Synchronized);

  // Only an external observation of the closed breaker and a synchronized device may
  // establish the synchronized condition.
  fixture.lab.adapter().external_set_breaker(BreakerPosition::Closed);
  fixture.lab.adapter().external_set_synchronized(true);
  observe.attempt = AttemptId{};
  observe.resolve_attempt = false;
  observe.requested_at = fixture.lab.now();
  const Result<ObserveOutcome> synced = fixture.engine->observe(observe);
  CHECK(synced.ok());
  const Result<GeneratorState> final_state = fixture.engine->inspect(fixture.lab.generator());
  CHECK(final_state.ok());
  CHECK_EQ(final_state.value().synchronization, SynchronizationState::Synchronized);
  CHECK_EQ(final_state.value().operating, OperatingState::Synchronized);
}

TEST(ack_without_effect_fault_is_recorded_and_abandonable_only_by_observation) {
  LabConfig config{};
  config.label = "ack-fault";
  OpenLab fixture(config);
  SyntheticFaults faults{};
  faults.ack_without_effect = true;
  fixture.lab.adapter().set_faults(faults);

  const Result<AttemptRecord> attempt =
      fixture.engine->execute(fixture.request(OperationKind::Start, "start-fault-0001"), false);
  CHECK(attempt.ok());
  CHECK_EQ(attempt.value().ack_status, AdapterAckStatus::Accepted);

  // Abandoning before an observation proves the absence of the effect is refused.
  const Result<AttemptRecord> premature =
      fixture.engine->abandon(attempt.value().id, "operator impatience", fixture.lab.now());
  CHECK_RESULT_CODE(premature, ErrorCode::EffectNotObserved);

  // A second actuation while the first attempt is unresolved is refused.
  const Result<AttemptRecord> second =
      fixture.engine->execute(fixture.request(OperationKind::Start, "start-fault-0002"), false);
  CHECK_RESULT_CODE(second, ErrorCode::AttemptUnresolved);

  fixture.advance(60 * kMillisPerSecond);
  ObserveRequest observe{};
  observe.generator = fixture.lab.generator();
  observe.controller = fixture.engine->controller();
  observe.attempt = attempt.value().id;
  observe.resolve_attempt = true;
  observe.requested_at = fixture.lab.now();
  const Result<ObserveOutcome> outcome = fixture.engine->observe(observe);
  CHECK(outcome.ok());
  CHECK_EQ(outcome.value().attempt.effect_state, EffectState::ObservedContradictory);

  const Result<AttemptRecord> abandoned =
      fixture.engine->abandon(attempt.value().id, "device never started", fixture.lab.now());
  CHECK(abandoned.ok());
  CHECK_EQ(abandoned.value().command_state, CommandState::Abandoned);

  // With the attempt resolved, a new attempt is admitted.
  const Result<AttemptRecord> third =
      fixture.engine->execute(fixture.request(OperationKind::Start, "start-fault-0003"), false);
  CHECK(third.ok());
  CHECK(third.value().id != attempt.value().id);
}

TEST(test_operation_cannot_become_production_transfer_authority) {
  LabConfig config{};
  config.label = "test-separation";
  config.seed_sync = true;
  OpenLab fixture(config);

  // A test operation is refused while the generator is in normal mode.
  const Result<AttemptRecord> test_in_normal_mode = fixture.engine->execute(
      fixture.request(OperationKind::TestStart, "test-in-normal-01", AuthorityClass::Test), false);
  CHECK_RESULT_CODE(test_in_normal_mode, ErrorCode::TestOperationNotPermitted);

  // Entering test mode requires test authority; normal authority cannot do it.
  ModeUpdate wrong_update{};
  wrong_update.generator = fixture.lab.generator();
  wrong_update.controller = fixture.engine->controller();
  const Result<GeneratorState> before = fixture.engine->inspect(fixture.lab.generator());
  CHECK(before.ok());
  wrong_update.revision = before.value().revision;
  const Result<IdempotencyKey> wrong_key = IdempotencyKey::parse("test-mode-wrong-1");
  CHECK(wrong_key.ok());
  wrong_update.idempotency_key = wrong_key.value();
  wrong_update.mode = OperatingMode::Test;
  wrong_update.authority.cls = AuthorityClass::Normal;
  wrong_update.authority.epoch = fixture.engine->controller().epoch;
  wrong_update.authority.granted_by = "test-harness";
  wrong_update.authority.granted_at = fixture.lab.now();
  wrong_update.requested_at = fixture.lab.now();
  CHECK_CODE(fixture.engine->update_mode(wrong_update), ErrorCode::AuthorityClassMismatch);

  fixture.set_mode(OperatingMode::Test, AuthorityClass::Test);

  // Test authority drives the test run; a production start in test mode is refused.
  const Result<AttemptRecord> test_start = fixture.engine->execute(
      fixture.request(OperationKind::TestStart, "test-start-00001", AuthorityClass::Test), false);
  CHECK(test_start.ok());
  const Result<GeneratorState> in_test = fixture.engine->inspect(fixture.lab.generator());
  CHECK(in_test.ok());
  CHECK_EQ(in_test.value().mode, OperatingMode::Test);

  const Result<AttemptRecord> production_in_test_mode = fixture.engine->execute(
      fixture.request(OperationKind::Start, "production-in-test"), false);
  CHECK_RESULT_CODE(production_in_test_mode, ErrorCode::AttemptUnresolved);

  // Resolve the test run, then confirm the production operation is still refused.
  fixture.advance(20 * kMillisPerSecond);
  ObserveRequest observe{};
  observe.generator = fixture.lab.generator();
  observe.controller = fixture.engine->controller();
  observe.attempt = test_start.value().id;
  observe.resolve_attempt = true;
  observe.requested_at = fixture.lab.now();
  CHECK(fixture.engine->observe(observe).ok());

  // A transfer under test authority is refused by the mode gate, not by the switch
  // authority: test mode never confers production transfer authority.
  const Result<AttemptRecord> transfer_in_test = fixture.engine->execute(
      fixture.request(OperationKind::TransferToGenerator, "transfer-in-test-1",
                      AuthorityClass::Test),
      false);
  CHECK_RESULT_CODE(transfer_in_test, ErrorCode::TestOperationNotPermitted);

  // Return the set to a stopped condition, still in test mode, and confirm that a
  // production start is refused for the mode reason rather than merely for the
  // operating state.
  const Result<AttemptRecord> test_stop = fixture.engine->execute(
      fixture.request(OperationKind::TestStop, "test-stop-000001", AuthorityClass::Test), false);
  CHECK(test_stop.ok());
  fixture.advance(10 * kMillisPerSecond);
  ObserveRequest stop_observe{};
  stop_observe.generator = fixture.lab.generator();
  stop_observe.controller = fixture.engine->controller();
  stop_observe.attempt = test_stop.value().id;
  stop_observe.resolve_attempt = true;
  stop_observe.requested_at = fixture.lab.now();
  CHECK(fixture.engine->observe(stop_observe).ok());

  const Result<AttemptRecord> production_refused = fixture.engine->execute(
      fixture.request(OperationKind::Start, "production-in-test-2"), false);
  CHECK_RESULT_CODE(production_refused, ErrorCode::TestOperationNotPermitted);
}

TEST(maintenance_isolated_and_retired_refuse_actuation) {
  LabConfig config{};
  config.label = "lifecycle-closed";
  OpenLab fixture(config);

  auto lifecycle_to = [&](LifecycleState target, const std::string& key) {
    LifecycleUpdate update{};
    update.generator = fixture.lab.generator();
    update.controller = fixture.engine->controller();
    const Result<GeneratorState> current = fixture.engine->inspect(fixture.lab.generator());
    if (!current.ok()) fail("inspect failed");
    update.revision = current.value().revision;
    const Result<IdempotencyKey> parsed = IdempotencyKey::parse(key);
    if (!parsed.ok()) fail("key invalid");
    update.idempotency_key = parsed.value();
    update.lifecycle = target;
    update.authority.cls = AuthorityClass::Service;
    update.authority.epoch = fixture.engine->controller().epoch;
    update.authority.granted_by = "test-harness";
    update.authority.granted_at = fixture.lab.now();
    update.requested_at = fixture.lab.now();
    return fixture.engine->update_lifecycle(update);
  };

  CHECK(lifecycle_to(LifecycleState::Maintenance, "to-maintenance-1").ok());
  const Result<AttemptRecord> in_maintenance =
      fixture.engine->execute(fixture.request(OperationKind::Start, "start-in-maint-1"), false);
  CHECK_RESULT_CODE(in_maintenance, ErrorCode::MaintenanceActive);

  CHECK(lifecycle_to(LifecycleState::Isolated, "to-isolated-001").ok());
  const Result<AttemptRecord> in_isolated =
      fixture.engine->execute(fixture.request(OperationKind::Start, "start-in-isol-01"), false);
  CHECK_RESULT_CODE(in_isolated, ErrorCode::GeneratorIsolated);

  CHECK(lifecycle_to(LifecycleState::Retired, "to-retired-0001").ok());
  const Result<AttemptRecord> in_retired =
      fixture.engine->execute(fixture.request(OperationKind::Start, "start-in-ret-001"), false);
  CHECK_RESULT_CODE(in_retired, ErrorCode::GeneratorRetired);
  const Status back = lifecycle_to(LifecycleState::Commissioned, "to-commissioned");
  CHECK_CODE(back, ErrorCode::GeneratorRetired);
}

TEST(emergency_authority_is_explicit_and_does_not_bypass_interlocks) {
  LabConfig config{};
  config.label = "emergency";
  OpenLab fixture(config);

  // An emergency grant without an explicit flag, reason or validity is refused.
  AuthorityGrant grant{};
  grant.cls = AuthorityClass::Emergency;
  grant.epoch = fixture.engine->controller().epoch;
  grant.granted_by = "control-plane";
  grant.granted_at = fixture.lab.now();
  grant.valid_until = fixture.lab.now() + 60'000;
  grant.reason = "utility loss, site on standby";
  grant.explicit_grant = false;
  CHECK_CODE(validate_emergency_grant(grant, default_emergency_policy(), fixture.lab.now()),
             ErrorCode::EmergencyAuthorityNotExplicit);

  grant.explicit_grant = true;
  grant.reason = "short";
  CHECK_CODE(validate_emergency_grant(grant, default_emergency_policy(), fixture.lab.now()),
             ErrorCode::EmergencyAuthorityNotExplicit);

  grant.reason = "utility loss, site on standby";
  grant.valid_until = fixture.lab.now() + 48 * kMillisPerHour;
  CHECK_CODE(validate_emergency_grant(grant, default_emergency_policy(), fixture.lab.now()),
             ErrorCode::ValueOutOfRange);

  grant.valid_until = fixture.lab.now() + 30 * kMillisPerMinute;
  CHECK(validate_emergency_grant(grant, default_emergency_policy(), fixture.lab.now()).ok());

  // Emergency mode plus emergency authority starts the set.
  fixture.set_mode(OperatingMode::Emergency, AuthorityClass::Emergency);
  const Result<AttemptRecord> started = fixture.engine->execute(
      fixture.request(OperationKind::EmergencyStart, "emergency-start-1", AuthorityClass::Emergency,
                      "utility loss, site on standby"),
      false);
  CHECK(started.ok());
  CHECK_EQ(started.value().command_state, CommandState::Acknowledged);
  CHECK_EQ(started.value().authority, AuthorityClass::Emergency);

  // The effect is proven by an observation, not by the acknowledgement.
  fixture.advance(20 * kMillisPerSecond);
  ObserveRequest start_observe{};
  start_observe.generator = fixture.lab.generator();
  start_observe.controller = fixture.engine->controller();
  start_observe.attempt = started.value().id;
  start_observe.resolve_attempt = true;
  start_observe.requested_at = fixture.lab.now();
  const Result<ObserveOutcome> observed = fixture.engine->observe(start_observe);
  CHECK(observed.ok());
  CHECK_EQ(observed.value().attempt.effect_state, EffectState::Verified);

  // A safety interlock is still mandatory: the same command is refused once the
  // emergency-stop interlock is engaged.
  const Result<AttemptRecord> stop =
      fixture.engine->execute(fixture.request(OperationKind::Stop, "emergency-stop--1"), false);
  CHECK(stop.ok());
  fixture.advance(10 * kMillisPerSecond);
  const Result<AttemptRecord> stop_resolve = fixture.engine->verify(
      VerifyOptions{stop.value().id, fixture.engine->controller(), fixture.lab.now()});
  CHECK(stop_resolve.ok());

  CheckUpdate update{};
  update.generator = fixture.lab.generator();
  update.controller = fixture.engine->controller();
  const Result<GeneratorState> current = fixture.engine->inspect(fixture.lab.generator());
  CHECK(current.ok());
  update.revision = current.value().revision;
  const Result<IdempotencyKey> key = IdempotencyKey::parse(
      "estop-" + std::to_string(fixture.engine->commit_seq().value()));
  CHECK(key.ok());
  update.idempotency_key = key.value();
  update.record.kind = CheckKind::EmergencyStopNotEngaged;
  update.record.required = true;
  update.record.state = EvidenceState::Present;
  update.record.source = EvidenceSource::SyntheticAdapter;
  update.record.observed_at = fixture.lab.now();
  update.record.max_age_millis = 3600 * kMillisPerSecond;
  update.record.value = TypedValue{TypedValueKind::Boolean, 0};
  CHECK(update.record.value.amount == 0);
  const Status recorded = fixture.engine->record_check(update);
  CHECK(recorded.ok());

  const Result<AttemptRecord> blocked = fixture.engine->execute(
      fixture.request(OperationKind::EmergencyStart, "emergency-start-2", AuthorityClass::Emergency,
                      "utility loss, site on standby"),
      false);
  CHECK_RESULT_CODE(blocked, ErrorCode::InterlockEngaged);
}

TEST(an_accepted_attempt_replays_before_a_staleness_check) {
  LabConfig config{};
  config.label = "idempotent-replay";
  OpenLab fixture(config);

  OperationRequest first = fixture.request(OperationKind::Start, "start-replay-001");
  const Result<AttemptRecord> accepted = fixture.engine->execute(first, false);
  CHECK(accepted.ok());
  CHECK(!accepted.value().replayed);

  // The world moves on: evidence is recorded, the revision advances, and the caller
  // retries with the revision it originally planned against.
  fixture.advance(1000);
  ResourceUpdate update{};
  update.generator = fixture.lab.generator();
  update.controller = fixture.engine->controller();
  const Result<GeneratorState> current = fixture.engine->inspect(fixture.lab.generator());
  CHECK(current.ok());
  update.revision = current.value().revision;
  const Result<IdempotencyKey> key =
      IdempotencyKey::parse("bump-key-" + std::to_string(fixture.engine->commit_seq().value()));
  CHECK(key.ok());
  update.idempotency_key = key.value();
  update.set_fuel_level = true;
  update.fuel_level.state = EvidenceState::Present;
  update.fuel_level.value = FuelQuantity{FuelUnit::Millilitres, 800'000};
  update.fuel_level.source = EvidenceSource::SyntheticAdapter;
  update.fuel_level.observed_at = fixture.lab.now();
  CHECK(fixture.engine->record_resource(update).ok());

  const Result<AttemptRecord> replayed = fixture.engine->execute(first, false);
  CHECK(replayed.ok());
  CHECK(replayed.value().replayed);
  CHECK_EQ(replayed.value().id.value(), accepted.value().id.value());

  // The same key with a different operation is a conflict, not a replay.
  OperationRequest conflicting = fixture.request(OperationKind::Stop, "start-replay-001");
  CHECK_RESULT_CODE(fixture.engine->execute(conflicting, false), ErrorCode::IdempotencyKeyConflict);

  // Exactly one command was handed to the device.
  CHECK_EQ(fixture.lab.adapter().accepted_count(OperationKind::Start), std::size_t{1});
}

TEST(stale_authority_is_refused_at_every_generation) {
  LabConfig config{};
  config.label = "fencing";
  OpenLab fixture(config);

  OperationRequest request = fixture.request(OperationKind::Start, "start-fenced-001");

  OperationRequest stale_epoch = request;
  stale_epoch.controller.epoch = ControlEpoch{request.controller.epoch.value() + 1};
  CHECK_RESULT_CODE(fixture.engine->execute(stale_epoch, false), ErrorCode::StaleControlEpoch);

  OperationRequest stale_incarnation = request;
  stale_incarnation.controller.incarnation =
      IncarnationId{request.controller.incarnation.value() + 1};
  CHECK_RESULT_CODE(fixture.engine->execute(stale_incarnation, false),
                    ErrorCode::StaleIncarnation);

  OperationRequest stale_revision = request;
  stale_revision.revision = StateRevision{request.revision.value() + 5};
  CHECK_RESULT_CODE(fixture.engine->execute(stale_revision, false), ErrorCode::StaleStateRevision);

  OperationRequest stale_generation = request;
  stale_generation.generation.hardware = HardwareGeneration{99};
  CHECK_RESULT_CODE(fixture.engine->execute(stale_generation, false),
                    ErrorCode::StaleGeneratorGeneration);

  // Nothing was actuated by any of the refused requests.
  CHECK_EQ(fixture.lab.adapter().issued_count(), std::size_t{0});
}

TEST(validation_precedence_produces_one_deterministic_primary_error) {
  LabConfig config{};
  config.label = "precedence";
  OpenLab fixture(config);

  // This request violates the fencing stage and the transition stage at once. The
  // earlier stage must always win.
  OperationRequest request = fixture.request(OperationKind::Synchronize, "precedence-0001");
  request.revision = StateRevision{request.revision.value() + 3};
  for (int i = 0; i < 5; ++i) {
    const Result<AttemptRecord> attempt = fixture.engine->execute(request, false);
    CHECK_RESULT_CODE(attempt, ErrorCode::StaleStateRevision);
  }

  const Result<EvaluationReport> report = fixture.engine->evaluate(request);
  CHECK(report.ok());
  CHECK(!report.value().permitted);
  CHECK_EQ(report.value().primary_error, ErrorCode::StaleStateRevision);
  CHECK(report.value().primary_stage == ValidationStage::Fencing);

  // A request that violates the format stage reports format, not fencing.
  OperationRequest bad_key = request;
  bad_key.idempotency_key = IdempotencyKey{};
  const Result<EvaluationReport> format_report = fixture.engine->evaluate(bad_key);
  CHECK(format_report.ok());
  CHECK_EQ(format_report.value().primary_error, ErrorCode::IdempotencyKeyMissing);
  CHECK(format_report.value().primary_stage == ValidationStage::Format);
}

TEST(revalidate_marks_expired_evidence_and_recomputes_reports) {
  LabConfig config{};
  config.label = "revalidate";
  OpenLab fixture(config);

  const Result<ReadinessReport> before = fixture.engine->readiness(fixture.lab.generator(),
                                                                  fixture.lab.now());
  CHECK(before.ok());
  CHECK(before.value().satisfied);

  fixture.advance(2 * kMillisPerHour);
  const Result<RevalidateReport> report =
      fixture.engine->revalidate(fixture.lab.generator(), fixture.lab.now());
  CHECK(report.ok());
  CHECK(report.value().demoted_evidence_records > 0);
  CHECK(!report.value().readiness.satisfied);
  CHECK(!report.value().resources.sufficient);
}

TEST(concurrent_attempts_preserve_single_device_ordering) {
  LabConfig config{};
  config.label = "concurrency";
  OpenLab fixture(config);

  constexpr int kThreads = 8;
  std::vector<std::thread> threads;
  std::vector<ErrorCode> codes(kThreads, ErrorCode::Internal);
  std::vector<AttemptId> ids(kThreads);
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&, i]() {
      OperationRequest request = fixture.request(
          OperationKind::Start, "concurrent-" + std::to_string(1000 + i));
      const Result<AttemptRecord> attempt = fixture.engine->execute(request, false);
      if (attempt.ok()) {
        codes[static_cast<std::size_t>(i)] = ErrorCode::Ok;
        ids[static_cast<std::size_t>(i)] = attempt.value().id;
      } else {
        codes[static_cast<std::size_t>(i)] = attempt.code();
      }
    });
  }
  for (auto& thread : threads) thread.join();

  int accepted = 0;
  int refused = 0;
  for (const ErrorCode code : codes) {
    if (code == ErrorCode::Ok) {
      ++accepted;
    } else {
      ++refused;
      CHECK(code == ErrorCode::AttemptUnresolved || code == ErrorCode::StaleStateRevision);
    }
  }
  CHECK_EQ(accepted + refused, kThreads);
  CHECK_EQ(accepted, 1);
  CHECK_EQ(fixture.lab.adapter().accepted_count(OperationKind::Start), std::size_t{1});

  // Exactly one attempt record exists and it is the accepted one.
  const Result<std::vector<AttemptRecord>> attempts =
      fixture.engine->attempts(fixture.lab.generator());
  CHECK(attempts.ok());
  CHECK_EQ(attempts.value().size(), std::size_t{1});
}

TEST(queries_run_while_a_writer_exists_and_never_block_it) {
  LabConfig config{};
  config.label = "reader-writer";
  OpenLab fixture(config);

  // Readers take an immutable snapshot; the assertion is that both sides actually ran.
  std::atomic<int> reads{0};
  std::atomic<bool> stop{false};
  std::atomic<bool> reader_failed{false};
  std::thread reader([&]() {
    // An exception must never escape a thread function, so failures are recorded and
    // asserted on the main thread.
    while (!stop.load()) {
      const Result<GeneratorState> state = fixture.engine->inspect(fixture.lab.generator());
      if (!state.ok()) {
        reader_failed.store(true);
        return;
      }
      const Result<ReadinessReport> readiness =
          fixture.engine->readiness(fixture.lab.generator(), fixture.lab.now());
      if (!readiness.ok()) {
        reader_failed.store(true);
        return;
      }
      reads.fetch_add(1);
    }
  });

  for (int i = 0; i < 40; ++i) {

    ResourceUpdate update{};
    update.generator = fixture.lab.generator();
    update.controller = fixture.engine->controller();
    const Result<GeneratorState> current = fixture.engine->inspect(fixture.lab.generator());
    CHECK(current.ok());
    update.revision = current.value().revision;
    const Result<IdempotencyKey> key = IdempotencyKey::parse(
        "rw-key-" + std::to_string(i) + "-" + std::to_string(fixture.engine->commit_seq().value()));
    CHECK(key.ok());
    update.idempotency_key = key.value();
    update.set_fuel_level = true;
    update.fuel_level.state = EvidenceState::Present;
    update.fuel_level.value = FuelQuantity{FuelUnit::Millilitres, 900'000 - i};
    update.fuel_level.source = EvidenceSource::SyntheticAdapter;
    update.fuel_level.observed_at = fixture.lab.now();
    CHECK(fixture.engine->record_resource(update).ok());
  }
  stop.store(true);
  reader.join();
  CHECK(!reader_failed.load());
  CHECK(reads.load() > 0);
}

}  // namespace
