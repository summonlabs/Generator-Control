// Example: a complete synthetic start lifecycle inside one controller incarnation.
//
// Shows the separation the runtime is built on: an accepted command is not a
// running engine, and only an observation proves the electrical effect.
#include "example_common.hpp"

int main() {
  using namespace example;
  const std::string store = scratch_directory("start-lifecycle");
  (void)platform::remove_directory_tree(store);
  if (!platform::ensure_directory(store).ok()) {
    std::printf("cannot create the example store\n");
    return 1;
  }

  ManualClock clock{0};
  const Result<GeneratorId> id = GeneratorId::parse("gen-1");
  if (!id.ok()) return 1;
  SyntheticAdapter::Options adapter_options{};
  adapter_options.journal_path = store + "\\device.jrnl";
  SyntheticAdapter adapter(id.value(), clock, adapter_options);

  OpenOptions options{};
  options.store_directory = store;
  options.create_if_missing = true;
  Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(options, clock, &adapter);
  if (!opened.ok()) {
    std::printf("open failed: %s\n", opened.status().to_string().c_str());
    return 1;
  }
  std::unique_ptr<GeneratorControlEngine> engine = std::move(opened.value());

  RegisterOptions registration{};
  registration.hardware_generation = HardwareGeneration{3};
  registration.requirement.required_runtime_seconds = 1800;
  registration.requirement.reserve = FuelQuantity{FuelUnit::Millilitres, 200'000};
  const Result<GeneratorState> registered =
      engine->register_generator(id.value(), registration, clock.now_millis());
  if (!registered.ok()) {
    std::printf("registration failed: %s\n", registered.status().to_string().c_str());
    return 1;
  }
  std::printf("generator %s registered (hardware generation %u)\n", id.value().str().c_str(),
              registration.hardware_generation.value());

  const Status seeded = seed_evidence(*engine, id.value(), false, false);
  if (!seeded.ok()) {
    std::printf("evidence seeding failed: %s\n", seeded.to_string().c_str());
    return 1;
  }
  std::printf("evidence recorded from the synthetic installation (SYNTHETIC)\n");

  const OperationRequest request = make_request(*engine, id.value(), OperationKind::Start,
                                                "example-start-0001", AuthorityClass::Normal);
  const Result<EvaluationReport> planned = engine->evaluate(request);
  if (!planned.ok()) return 1;
  std::printf("planning: %s (readiness %s, resources %s)\n",
              planned.value().permitted ? "permitted" : "refused",
              planned.value().readiness.satisfied ? "satisfied" : "not satisfied",
              planned.value().resources.sufficient ? "sufficient" : "not sufficient");

  const Result<AttemptRecord> attempt = engine->execute(request, false);
  if (!attempt.ok()) {
    std::printf("start refused: %s\n", attempt.status().to_string().c_str());
    return 1;
  }
  std::printf("command issued to the device:\n");
  print_attempt(attempt.value());
  print_status("acknowledgement", Status::success());
  std::printf("  state after the command is still provisional: the runtime claims no effect\n");

  clock.advance(12 * kMillisPerSecond);
  ObserveRequest observe{};
  observe.generator = id.value();
  observe.controller = engine->controller();
  observe.attempt = attempt.value().id;
  observe.resolve_attempt = true;
  observe.requested_at = clock.now_millis();
  const Result<ObserveOutcome> outcome = engine->observe(observe);
  if (!outcome.ok()) {
    std::printf("observation failed: %s\n", outcome.status().to_string().c_str());
    return 1;
  }
  const Result<GeneratorState> final_state = engine->inspect(id.value());
  if (!final_state.ok()) return 1;
  std::printf("after observation: operating=%s synchronization=%s effect=%s\n",
              std::string(to_string(final_state.value().operating)).c_str(),
              std::string(to_string(final_state.value().synchronization)).c_str(),
              std::string(to_string(outcome.value().attempt.effect_state)).c_str());
  std::printf("state revision %llu, commit sequence %llu\n",
              static_cast<unsigned long long>(final_state.value().revision.value()),
              static_cast<unsigned long long>(engine->commit_seq().value()));

  const Status closed = engine->close();
  const Status removed = platform::remove_directory_tree(store);
  std::printf("cleanup: close %s, store removed %s\n", closed.ok() ? "ok" : "failed",
              removed.ok() ? "yes" : "no");
  return 0;
}
