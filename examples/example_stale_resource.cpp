// Example: stale resource evidence refuses a start.
//
// A fuel reading that is still perfectly plausible is refused as soon as it falls
// outside its freshness bound, and missing evidence is never turned into zero.
#include "example_common.hpp"

int main() {
  using namespace example;
  const std::string store = scratch_directory("stale-resource");
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

  // The fuel reading ages past its freshness bound while the engine keeps running.
  const EpochMillis evidence_age = 4 * kMillisPerHour;
  clock.advance(evidence_age);

  const Result<ResourceAssessment> resources =
      engine->resource_assessment(id.value(), clock.now_millis());
  if (!resources.ok()) return 1;
  std::printf("fuel evidence is %s old; the runtime reports:\n",
              format_millis(evidence_age).c_str());
  std::printf("  sufficient: %s\n", resources.value().sufficient ? "yes" : "no");
  std::printf("  primary error: %s\n",
              std::string(to_string(resources.value().primary_error)).c_str());
  for (const auto& finding : resources.value().findings) {
    std::printf("  %-24s %-10s required=%s %s\n", std::string(to_string(finding.kind)).c_str(),
                std::string(to_string(finding.usability)).c_str(),
                finding.required ? "yes" : "no", finding.detail.c_str());
  }

  const OperationRequest request = make_request(*engine, id.value(), OperationKind::Start,
                                                "example-stale-0001", AuthorityClass::Normal);
  const Result<EvaluationReport> report = engine->evaluate(request);
  if (!report.ok()) return 1;
  std::printf("evaluation: %s\n", report.value().permitted ? "permitted" : "refused");
  std::printf("  primary error: %s [%s]\n",
              std::string(to_string(report.value().primary_error)).c_str(),
              std::string(to_string(report.value().primary_stage)).c_str());
  std::printf("  detail: %s\n", report.value().primary_detail.c_str());

  const Result<AttemptRecord> attempt = engine->execute(request, false);
  std::printf("start attempt: %s\n",
              attempt.ok() ? "accepted (unexpected)" : attempt.status().to_string().c_str());
  std::printf("commands issued to the device: %zu\n", adapter.issued_count());

  // Re-reading the whole installation restores permission; the runtime never assumes
  // that any of it stayed true while it was not looking.
  const Status refreshed = seed_evidence(*engine, id.value(), false, false, clock.now_millis());
  if (!refreshed.ok()) {
    std::printf("re-reading the installation failed: %s\n", refreshed.to_string().c_str());
    return 1;
  }
  const Result<ResourceAssessment> after =
      engine->resource_assessment(id.value(), clock.now_millis());
  if (!after.ok()) return 1;
  std::printf("after re-reading the installation: sufficient=%s\n",
              after.value().sufficient ? "yes" : "no");
  const Result<EvaluationReport> plan_after = engine->evaluate(
      make_request(*engine, id.value(), OperationKind::Start, "example-stale-0002",
                   AuthorityClass::Normal));
  if (!plan_after.ok()) return 1;
  std::printf("start is now: %s\n", plan_after.value().permitted ? "permitted" : "refused");

  (void)engine->close();
  (void)platform::remove_directory_tree(store);
  return 0;
}
