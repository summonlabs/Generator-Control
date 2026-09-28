// Example: acknowledgement without synchronization.
//
// The device accepts a synchronizing command, the external switch actor never
// closes the breaker, and the runtime refuses to describe the generator as
// synchronized. Only an external observation of the closed breaker may do that.
#include "example_common.hpp"

int main() {
  using namespace example;
  const std::string store = scratch_directory("ack-without-sync");
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
  if (!seed_evidence(*engine, id.value(), true, false).ok()) return 1;

  // Start and observe the set as running before synchronization is attempted.
  const Result<AttemptRecord> start = engine->execute(
      make_request(*engine, id.value(), OperationKind::Start, "example-presync-01",
                   AuthorityClass::Normal),
      false);
  if (!start.ok()) {
    std::printf("start refused: %s\n", start.status().to_string().c_str());
    return 1;
  }
  clock.advance(12 * kMillisPerSecond);
  ObserveRequest observe{};
  observe.generator = id.value();
  observe.controller = engine->controller();
  observe.attempt = start.value().id;
  observe.resolve_attempt = true;
  observe.requested_at = clock.now_millis();
  if (!engine->observe(observe).ok()) return 1;

  const Result<SynchronizationEligibility> eligibility =
      engine->sync_eligibility(id.value(), clock.now_millis());
  if (!eligibility.ok()) return 1;
  std::printf("synchronization eligibility: %s\n",
              std::string(to_string(eligibility.value().outcome)).c_str());
  for (const auto& finding : eligibility.value().findings) {
    std::printf("  %-30s %-13s %s\n", std::string(to_string(finding.kind)).c_str(),
                std::string(to_string(finding.outcome)).c_str(), finding.detail.c_str());
  }

  const Result<AttemptRecord> synchronize = engine->execute(
      make_request(*engine, id.value(), OperationKind::Synchronize, "example-sync-0001",
                   AuthorityClass::Normal),
      false);
  if (!synchronize.ok()) {
    std::printf("synchronize refused: %s\n", synchronize.status().to_string().c_str());
    return 1;
  }
  print_attempt(synchronize.value());
  const Result<GeneratorState> commanded = engine->inspect(id.value());
  if (!commanded.ok()) return 1;
  std::printf("after an accepted synchronizing command: synchronization=%s operating=%s\n",
              std::string(to_string(commanded.value().synchronization)).c_str(),
              std::string(to_string(commanded.value().operating)).c_str());

  clock.advance(60 * kMillisPerSecond);
  observe.attempt = synchronize.value().id;
  observe.resolve_attempt = true;
  observe.requested_at = clock.now_millis();
  const Result<ObserveOutcome> outcome = engine->observe(observe);
  if (!outcome.ok()) return 1;
  std::printf("observation after the settle window: effect=%s\n",
              std::string(to_string(outcome.value().attempt.effect_state)).c_str());
  const Result<GeneratorState> unchanged = engine->inspect(id.value());
  if (!unchanged.ok()) return 1;
  std::printf("the runtime still reports synchronization=%s\n",
              std::string(to_string(unchanged.value().synchronization)).c_str());

  // The external switch actor closes the breaker; the next observation establishes
  // the synchronized condition, and only that observation can.
  adapter.external_set_breaker(BreakerPosition::Closed);
  adapter.external_set_synchronized(true);
  observe.attempt = AttemptId{};
  observe.resolve_attempt = false;
  observe.requested_at = clock.now_millis();
  if (!engine->observe(observe).ok()) return 1;
  const Result<GeneratorState> synced = engine->inspect(id.value());
  if (!synced.ok()) return 1;
  std::printf("after the external breaker closed and was observed: synchronization=%s "
              "operating=%s\n",
              std::string(to_string(synced.value().synchronization)).c_str(),
              std::string(to_string(synced.value().operating)).c_str());

  (void)engine->close();
  (void)platform::remove_directory_tree(store);
  return 0;
}
