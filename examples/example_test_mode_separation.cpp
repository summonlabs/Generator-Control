// Example: test-mode separation.
//
// A test run under test authority can never be reinterpreted as a production run,
// and test authority never authorizes a production operation.
#include "example_common.hpp"

int main() {
  using namespace example;
  const std::string store = scratch_directory("test-separation");
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

  // A production start is refused while the generator is in normal mode but under
  // test authority, because the operation requires normal authority.
  const OperationRequest wrong = make_request(*engine, id.value(), OperationKind::Start,
                                              "example-wrong-0001", AuthorityClass::Test);
  const Result<EvaluationReport> wrong_report = engine->evaluate(wrong);
  if (!wrong_report.ok()) return 1;
  std::printf("production start under test authority: %s (%s)\n",
              wrong_report.value().permitted ? "permitted" : "refused",
              std::string(to_string(wrong_report.value().primary_error)).c_str());

  // Entering test mode requires test authority and is refused while the set runs.
  const Result<GeneratorState> before = engine->inspect(id.value());
  if (!before.ok()) return 1;
  ModeUpdate mode{};
  mode.generator = id.value();
  mode.controller = engine->controller();
  mode.revision = before.value().revision;
  const Result<IdempotencyKey> mode_key =
      IdempotencyKey::parse("example-test-mode-" + std::to_string(engine->commit_seq().value()));
  if (!mode_key.ok()) return 1;
  mode.idempotency_key = mode_key.value();
  mode.mode = OperatingMode::Test;
  mode.authority.cls = AuthorityClass::Test;
  mode.authority.epoch = engine->controller().epoch;
  mode.authority.granted_by = "example-maintenance-authority";
  mode.authority.granted_at = clock.now_millis();
  mode.requested_at = clock.now_millis();
  const Status entered = engine->update_mode(mode);
  std::printf("entering test mode: %s\n",
              entered.ok() ? "ok" : entered.to_string().c_str());

  const OperationRequest test_start = make_request(
      *engine, id.value(), OperationKind::TestStart, "example-test-0001", AuthorityClass::Test);
  const Result<AttemptRecord> started = engine->execute(test_start, false);
  if (!started.ok()) {
    std::printf("test start refused: %s\n", started.status().to_string().c_str());
    return 1;
  }
  print_attempt(started.value());
  const Result<GeneratorState> in_test = engine->inspect(id.value());
  if (!in_test.ok()) return 1;
  std::printf("operating mode is now '%s'; test-mode commands carry no transfer authority\n",
              std::string(to_string(in_test.value().mode)).c_str());

  clock.advance(12 * kMillisPerSecond);
  ObserveRequest observe{};
  observe.generator = id.value();
  observe.controller = engine->controller();
  observe.attempt = started.value().id;
  observe.resolve_attempt = true;
  observe.requested_at = clock.now_millis();
  const Result<ObserveOutcome> outcome = engine->observe(observe);
  if (!outcome.ok()) return 1;
  std::printf("test run effect after observation: %s\n",
              std::string(to_string(outcome.value().attempt.effect_state)).c_str());

  // A transfer request under test authority is refused by the mode gate while the
  // set is still in a state the transfer rule accepts.
  const Result<EvaluationReport> transfer_report = engine->evaluate(make_request(
      *engine, id.value(), OperationKind::TransferToGenerator, "example-transfer-01",
      AuthorityClass::Test));
  if (!transfer_report.ok()) return 1;
  std::printf("transfer under test authority: %s (%s)\n",
              transfer_report.value().permitted ? "permitted" : "refused",
              std::string(to_string(transfer_report.value().primary_error)).c_str());

  // Return the set to a stopped condition, still in test mode, so that the refusal
  // below is the mode gate rather than the operating state.
  const Result<AttemptRecord> test_stop = engine->execute(
      make_request(*engine, id.value(), OperationKind::TestStop, "example-test-0002",
                   AuthorityClass::Test),
      false);
  if (!test_stop.ok()) {
    std::printf("test stop refused: %s\n", test_stop.status().to_string().c_str());
    return 1;
  }
  clock.advance(12 * kMillisPerSecond);
  observe.attempt = test_stop.value().id;
  observe.resolve_attempt = true;
  observe.requested_at = clock.now_millis();
  if (!engine->observe(observe).ok()) return 1;
  const Result<GeneratorState> stopped = engine->inspect(id.value());
  if (!stopped.ok()) return 1;
  std::printf("test run complete: operating=%s mode=%s\n",
              std::string(to_string(stopped.value().operating)).c_str(),
              std::string(to_string(stopped.value().mode)).c_str());

  const OperationRequest production =
      make_request(*engine, id.value(), OperationKind::Start, "example-prod-0001",
                   AuthorityClass::Normal);
  const Result<EvaluationReport> production_report = engine->evaluate(production);
  if (!production_report.ok()) return 1;
  std::printf("production start while in test mode: %s (%s)\n",
              production_report.value().permitted ? "permitted" : "refused",
              std::string(to_string(production_report.value().primary_error)).c_str());

  (void)engine->close();
  (void)platform::remove_directory_tree(store);
  return 0;
}
