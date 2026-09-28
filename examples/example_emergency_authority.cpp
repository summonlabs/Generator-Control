// Example: emergency authority is explicit and never bypasses safety interlocks.
#include "example_common.hpp"

int main() {
  using namespace example;
  const std::string store = scratch_directory("emergency-authority");
  (void)platform::remove_directory_tree(store);
  if (!platform::ensure_directory(store).ok()) return 1;

  ManualClock clock{0};
  const Result<GeneratorId> id = GeneratorId::parse("gen-1");
  if (!id.ok()) return 1;
  SyntheticAdapter adapter(id.value(), clock, SyntheticAdapter::Options{});

  OpenOptions options{};
  options.store_directory = store;
  options.create_if_missing = true;
  Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(options, clock, &adapter);
  if (!opened.ok()) return 1;
  std::unique_ptr<GeneratorControlEngine> engine = std::move(opened.value());
  if (!engine->register_generator(id.value(), RegisterOptions{}, clock.now_millis()).ok()) return 1;
  if (!seed_evidence(*engine, id.value(), false, false).ok()) return 1;

  // An emergency grant that is not explicit, has no reason or has no bounded window
  // is refused before anything else happens.
  AuthorityGrant grant{};
  grant.cls = AuthorityClass::Emergency;
  grant.epoch = engine->controller().epoch;
  grant.granted_by = "site-control-plane";
  grant.granted_at = clock.now_millis();
  grant.valid_until = clock.now_millis() + 30 * kMillisPerMinute;
  grant.reason = "utility loss; site running on standby generation";
  grant.explicit_grant = false;
  std::printf("implicit emergency grant: %s\n",
              validate_emergency_grant(grant, default_emergency_policy(), clock.now_millis())
                  .to_string()
                  .c_str());
  grant.explicit_grant = true;
  grant.valid_until = clock.now_millis() + 24 * kMillisPerHour;
  std::printf("over-long emergency grant: %s\n",
              validate_emergency_grant(grant, default_emergency_policy(), clock.now_millis())
                  .to_string()
                  .c_str());
  grant.valid_until = clock.now_millis() + 30 * kMillisPerMinute;
  std::printf("bounded, explicit emergency grant: %s\n",
              validate_emergency_grant(grant, default_emergency_policy(), clock.now_millis())
                  .ok()
                  ? "accepted"
                  : "refused");

  // Emergency mode is entered explicitly.
  const Result<GeneratorState> before = engine->inspect(id.value());
  if (!before.ok()) return 1;
  ModeUpdate mode{};
  mode.generator = id.value();
  mode.controller = engine->controller();
  mode.revision = before.value().revision;
  const Result<IdempotencyKey> mode_key =
      IdempotencyKey::parse("example-emergency-mode-" + std::to_string(engine->commit_seq().value()));
  if (!mode_key.ok()) return 1;
  mode.idempotency_key = mode_key.value();
  mode.mode = OperatingMode::Emergency;
  mode.authority = grant;
  mode.requested_at = clock.now_millis();
  std::printf("entering emergency mode: %s\n",
              engine->update_mode(mode).ok() ? "ok" : "refused");

  const OperationRequest start = make_request(*engine, id.value(), OperationKind::EmergencyStart,
                                              "example-emergency-01", AuthorityClass::Emergency,
                                              grant.reason);
  const Result<AttemptRecord> started = engine->execute(start, false);
  std::printf("emergency start: %s\n",
              started.ok() ? "accepted by the device" : started.status().to_string().c_str());
  if (started.ok()) print_attempt(started.value());

  // The commanded effect must be proven by an observation before another command may
  // be issued against the same generator.
  if (started.ok()) {
    clock.advance(12 * kMillisPerSecond);
    const Result<AttemptRecord> resolved = engine->verify(
        VerifyOptions{started.value().id, engine->controller(), clock.now_millis()});
    std::printf("first emergency start resolved by observation: %s\n",
                resolved.ok() ? std::string(to_string(resolved.value().effect_state)).c_str()
                              : resolved.status().to_string().c_str());
  }

  // Stop the set again so that the next emergency start is evaluated from a state the
  // rule accepts, and so that the refusal below can only be the interlock.
  if (started.ok()) {
    const Result<AttemptRecord> emergency_stop = engine->execute(
        make_request(*engine, id.value(), OperationKind::EmergencyStop, "example-emergency-03",
                     AuthorityClass::Emergency, grant.reason),
        false);
    if (!emergency_stop.ok()) {
      std::printf("emergency stop refused: %s\n", emergency_stop.status().to_string().c_str());
      return 1;
    }
    clock.advance(12 * kMillisPerSecond);
    const Result<AttemptRecord> stopped = engine->verify(
        VerifyOptions{emergency_stop.value().id, engine->controller(), clock.now_millis()});
    if (!stopped.ok()) {
      std::printf("resolving the emergency stop failed: %s\n", stopped.status().to_string().c_str());
      return 1;
    }
  }

  // The emergency-stop interlock is engaged. Emergency authority relaxes advisory
  // checks only: the safety interlock still refuses the command.
  const Result<GeneratorState> current = engine->inspect(id.value());
  if (!current.ok()) return 1;
  CheckUpdate check{};
  check.generator = id.value();
  check.controller = engine->controller();
  check.revision = current.value().revision;
  const Result<IdempotencyKey> check_key =
      IdempotencyKey::parse("example-estop-" + std::to_string(engine->commit_seq().value()));
  if (!check_key.ok()) return 1;
  check.idempotency_key = check_key.value();
  check.record.kind = CheckKind::EmergencyStopNotEngaged;
  check.record.required = true;
  check.record.state = EvidenceState::Present;
  check.record.source = EvidenceSource::SyntheticAdapter;
  check.record.observed_at = clock.now_millis();
  check.record.max_age_millis = 60 * kMillisPerMinute;
  check.record.value = TypedValue{TypedValueKind::Boolean, 0};
  if (!engine->record_check(check).ok()) return 1;

  const Result<EvaluationReport> blocked = engine->evaluate(
      make_request(*engine, id.value(), OperationKind::EmergencyStart, "example-emergency-02",
                   AuthorityClass::Emergency, grant.reason));
  if (!blocked.ok()) return 1;
  std::printf("second emergency start with the interlock engaged: %s (%s)\n",
              blocked.value().permitted ? "permitted" : "refused",
              std::string(to_string(blocked.value().primary_error)).c_str());

  (void)engine->close();
  (void)platform::remove_directory_tree(store);
  return 0;
}
