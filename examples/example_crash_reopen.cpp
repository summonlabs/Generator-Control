// Example: a killed process leaves no duplicate start behind.
//
// The example re-executes itself as a child process, kills it between the durable
// command-attempt commit and the acknowledgement commit, then reopens the store and
// proves that the device actuated exactly once and that a retry replays the recorded
// attempt instead of issuing a second command.
#include "example_common.hpp"

#include <cstring>
#include <string>

namespace {

int run_child(const std::string& store) {
  using namespace example;
  ManualClock clock{0};
  const Result<GeneratorId> id = GeneratorId::parse("gen-1");
  if (!id.ok()) return 1;
  SyntheticAdapter::Options adapter_options{};
  adapter_options.journal_path = store + "\\device.jrnl";
  SyntheticAdapter adapter(id.value(), clock, adapter_options);
  if (!adapter.replay_journal().ok()) return 1;

  OpenOptions options{};
  options.store_directory = store;
  options.requested_epoch = ControlEpoch{0};
  options.config.crash_point = CrashPoint::AfterActuationBeforeAckCommit;
  Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(options, clock, &adapter);
  if (!opened.ok()) return 1;
  std::unique_ptr<GeneratorControlEngine> engine = std::move(opened.value());
  if (!seed_evidence(*engine, id.value(), false, false).ok()) return 1;
  const Result<AttemptRecord> attempt = engine->execute(
      make_request(*engine, id.value(), OperationKind::Start, "example-crash-0001",
                   AuthorityClass::Normal),
      false);
  if (!attempt.ok()) {
    std::printf("child: start refused: %s\n", attempt.status().to_string().c_str());
    return 1;
  }
  std::printf("child: this line must never be printed\n");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace example;
  const std::string store = scratch_directory("crash-reopen");
  if (argc > 2 && std::string(argv[1]) == "--child") return run_child(store);

  (void)platform::remove_directory_tree(store);
  if (!platform::ensure_directory(store).ok()) return 1;

  ManualClock clock{0};
  const Result<GeneratorId> id = GeneratorId::parse("gen-1");
  if (!id.ok()) return 1;
  {
    SyntheticAdapter adapter(id.value(), clock, SyntheticAdapter::Options{});
    OpenOptions options{};
    options.store_directory = store;
    options.create_if_missing = true;
    Result<std::unique_ptr<GeneratorControlEngine>> opened =
        GeneratorControlEngine::open(options, clock, &adapter);
    if (!opened.ok()) return 1;
    std::unique_ptr<GeneratorControlEngine> engine = std::move(opened.value());
    if (!engine->register_generator(id.value(), RegisterOptions{}, 0).ok()) return 1;
    (void)engine->close();
  }

  const Result<std::string> executable = platform::current_executable_path();
  if (!executable.ok()) return 1;
  const Result<int> exit_code =
      platform::run_process(executable.value(), {"--child", store}, store + "\\child.txt");
  if (!exit_code.ok()) return 1;
  std::printf("child process terminated with exit code %d (process death, not a clean exit)\n",
              exit_code.value());

  SyntheticAdapter::Options adapter_options{};
  adapter_options.journal_path = store + "\\device.jrnl";
  SyntheticAdapter adapter(id.value(), clock, adapter_options);
  const Result<std::size_t> replayed = adapter.replay_journal();
  if (!replayed.ok()) return 1;
  std::printf("device actuations recovered from the independent journal: %zu accepted start(s)\n",
              adapter.accepted_count(OperationKind::Start));

  OpenOptions reopen{};
  reopen.store_directory = store;
  Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(reopen, clock, &adapter);
  if (!opened.ok()) {
    std::printf("reopen failed: %s\n", opened.status().to_string().c_str());
    return 1;
  }
  std::unique_ptr<GeneratorControlEngine> engine = std::move(opened.value());
  std::printf("reopened at commit %llu, incarnation %llu, %zu unresolved attempt(s)\n",
              static_cast<unsigned long long>(engine->commit_seq().value()),
              static_cast<unsigned long long>(engine->reopen_report().incarnation.value()),
              engine->reopen_report().unresolved_attempts);

  const Result<std::vector<AttemptRecord>> attempts = engine->attempts(id.value());
  if (!attempts.ok()) return 1;
  for (const auto& record : attempts.value()) {
    print_attempt(record);
  }

  // A retry with the same idempotency key must replay the recorded attempt. The
  // request is rebuilt from the current state, exactly as a retrying client would.
  const Result<GeneratorState> current = engine->inspect(id.value());
  if (!current.ok()) return 1;
  OperationRequest retry{};
  retry.generator = id.value();
  retry.operation = OperationKind::Start;
  retry.controller = engine->controller();
  retry.generation = current.value().generation;
  retry.revision = current.value().revision;
  retry.authority.cls = AuthorityClass::Normal;
  retry.authority.epoch = engine->controller().epoch;
  retry.authority.granted_by = "example-operator";
  retry.idempotency_key = IdempotencyKey::parse("example-crash-0001").value();
  retry.requested_at = clock.now_millis();
  const Result<AttemptRecord> replayed_attempt = engine->execute(retry, false);
  std::printf("retry with the same key: %s\n",
              replayed_attempt.ok()
                  ? (replayed_attempt.value().replayed ? "replayed the recorded attempt"
                                                       : "issued a new command (unexpected)")
                  : replayed_attempt.status().to_string().c_str());
  std::printf("device actuations after the retry: %zu accepted start(s)\n",
              adapter.accepted_count(OperationKind::Start));

  (void)engine->close();
  (void)platform::remove_directory_tree(store);
  return 0;
}
