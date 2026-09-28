// Proof obligations: cross-process writer exclusion through a real operating system
// primitive, release on abrupt process death, incarnation handoff, stale writer
// fencing and a still-valid store after termination.
#include <memory>
#include <string>

#include "genctl/platform.hpp"
#include "support.hpp"
#include "test_framework.hpp"

namespace {
using namespace genctl;
using namespace genctl::platform;
using namespace gctest;

std::string label_for(const std::string& name) { return "mp-" + name; }

TEST(a_second_writer_is_refused_while_the_first_holds_the_lease) {
  const Result<std::string> executable = platform::current_executable_path();
  CHECK(executable.ok());

  LabConfig config{};
  config.label = label_for("writer-exclusion");
  Lab lab(config);

  const std::string ready = lab.store() + "\\child.ready";
  const Result<ChildProcess> child = platform::spawn_process(
      executable.value(),
      {"--scenario", "lock-and-hold", lab.store(), ready, "1500"},
      lab.store() + "\\child-hold.txt");
  CHECK(child.ok());
  CHECK(wait_for_file(ready));

  // The parent is a real, independent process and must be refused.
  {
    const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
    CHECK(id.ok());
    SyntheticAdapter adapter(id.value(), lab.clock(), SyntheticAdapter::Options{});
    OpenOptions options{};
    options.store_directory = lab.store();
    options.requested_epoch = ControlEpoch{1};
    const Result<std::unique_ptr<GeneratorControlEngine>> blocked =
        GeneratorControlEngine::open(options, lab.clock(), &adapter);
    CHECK_RESULT_CODE(blocked, ErrorCode::StoreLocked);
  }

  ChildProcess handle = child.value();
  const Result<int> exit_code = wait_process(handle);
  CHECK(exit_code.ok());
  CHECK_EQ(exit_code.value(), 0);
  CHECK(close_process(handle).ok());

  // With the child gone the store opens normally again.
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  CHECK(id.ok());
  SyntheticAdapter adapter(id.value(), lab.clock(), SyntheticAdapter::Options{});
  OpenOptions options{};
  options.store_directory = lab.store();
  options.requested_epoch = ControlEpoch{1};
  const Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(options, lab.clock(), &adapter);
  CHECK(opened.ok());
  CHECK(opened.value()->close().ok());
}

TEST(abrupt_process_death_releases_the_writer_lease) {
  const Result<std::string> executable = platform::current_executable_path();
  CHECK(executable.ok());

  LabConfig config{};
  config.label = label_for("death-release");
  Lab lab(config);

  const std::string ready = lab.store() + "\\child.ready";
  const Result<ChildProcess> child = platform::spawn_process(
      executable.value(), {"--scenario", "lock-and-die", lab.store(), ready},
      lab.store() + "\\child-die.txt");
  CHECK(child.ok());
  CHECK(wait_for_file(ready));

  ChildProcess handle = child.value();
  const Result<int> code = wait_process(handle);
  CHECK(code.ok());
  CHECK_EQ(code.value(), 91);
  CHECK(close_process(handle).ok());

  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  CHECK(id.ok());
  SyntheticAdapter adapter(id.value(), lab.clock(), SyntheticAdapter::Options{});
  OpenOptions options{};
  options.store_directory = lab.store();
  options.requested_epoch = ControlEpoch{1};
  const Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(options, lab.clock(), &adapter);
  CHECK(opened.ok());
  CHECK(opened.value()->commit_seq().value() > 0);
  CHECK(opened.value()->reopen_report().incarnation.value() >= 2);
  CHECK(opened.value()->close().ok());
}

TEST(incarnation_handoff_is_monotonic_across_processes) {
  const Result<std::string> executable = platform::current_executable_path();
  CHECK(executable.ok());

  LabConfig config{};
  config.label = label_for("handoff");
  Lab lab(config);

  const std::string output = lab.store() + "\\child-hold.txt";
  for (int i = 0; i < 3; ++i) {
    const std::string ready = lab.store() + "\\child.ready." + std::to_string(i);
    const Result<int> code = platform::run_process(
        executable.value(), {"--scenario", "lock-and-hold", lab.store(), ready, "0"}, output);
    CHECK(code.ok());
    CHECK_EQ(code.value(), 0);
    CHECK(wait_for_file(ready));
  }

  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  CHECK(id.ok());
  SyntheticAdapter adapter(id.value(), lab.clock(), SyntheticAdapter::Options{});
  OpenOptions options{};
  options.store_directory = lab.store();
  options.requested_epoch = ControlEpoch{1};
  const Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(options, lab.clock(), &adapter);
  CHECK(opened.ok());
  CHECK(opened.value()->reopen_report().incarnation.value() > 3);
  CHECK(opened.value()->close().ok());
}

TEST(a_stale_writer_cannot_re_enter_after_its_lease_was_taken_over) {
  const Result<std::string> executable = platform::current_executable_path();
  CHECK(executable.ok());

  LabConfig config{};
  config.label = label_for("stale-writer");
  Lab lab(config);

  // A child takes the lease and exits cleanly: the lease persists with its
  // incarnation.
  const std::string ready = lab.store() + "\\child.ready";
  const Result<int> code = platform::run_process(
      executable.value(), {"--scenario", "lock-and-hold", lab.store(), ready, "0"},
      lab.store() + "\\child-hold.txt");
  CHECK(code.ok());
  CHECK_EQ(code.value(), 0);
  CHECK(wait_for_file(ready));

  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  CHECK(id.ok());
  SyntheticAdapter adapter(id.value(), lab.clock(), SyntheticAdapter::Options{});
  OpenOptions options{};
  options.store_directory = lab.store();
  options.requested_epoch = ControlEpoch{1};
  const Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(options, lab.clock(), &adapter);
  CHECK(opened.ok());
  const IncarnationId first = opened.value()->reopen_report().incarnation;
  CHECK(opened.value()->close().ok());

  // A second handover must strictly increase the incarnation, so anything planned
  // against the first is fenced.
  const Result<std::unique_ptr<GeneratorControlEngine>> second =
      GeneratorControlEngine::open(options, lab.clock(), &adapter);
  CHECK(second.ok());
  CHECK(second.value()->reopen_report().incarnation > first);
  CHECK(second.value()->close().ok());
}

TEST(a_control_epoch_below_the_stored_epoch_is_refused) {
  LabConfig config{};
  config.label = label_for("epoch-fencing");
  Lab lab(config);
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  CHECK(id.ok());
  SyntheticAdapter adapter(id.value(), lab.clock(), SyntheticAdapter::Options{});

  OpenOptions options{};
  options.store_directory = lab.store();
  options.create_if_missing = true;
  options.requested_epoch = ControlEpoch{7};
  const Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(options, lab.clock(), &adapter);
  CHECK(opened.ok());
  CHECK_EQ(opened.value()->controller().epoch.value(), 7u);
  CHECK(opened.value()->close().ok());

  options.requested_epoch = ControlEpoch{3};
  const Result<std::unique_ptr<GeneratorControlEngine>> stale =
      GeneratorControlEngine::open(options, lab.clock(), &adapter);
  CHECK_RESULT_CODE(stale, ErrorCode::StaleControlEpoch);

  options.requested_epoch = ControlEpoch{9};
  const Result<std::unique_ptr<GeneratorControlEngine>> advanced =
      GeneratorControlEngine::open(options, lab.clock(), &adapter);
  CHECK(advanced.ok());
  CHECK_EQ(advanced.value()->controller().epoch.value(), 9u);
  CHECK(advanced.value()->close().ok());
}

TEST(an_absurd_control_epoch_is_refused_rather_than_fencing_every_writer) {
  LabConfig config{};
  config.label = label_for("epoch-ceiling");
  Lab lab(config);
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  CHECK(id.ok());
  SyntheticAdapter adapter(id.value(), lab.clock(), SyntheticAdapter::Options{});
  OpenOptions options{};
  options.store_directory = lab.store();
  options.create_if_missing = true;
  options.requested_epoch = ControlEpoch{kMaxAcceptedControlEpoch + 1};
  const Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(options, lab.clock(), &adapter);
  CHECK_RESULT_CODE(opened, ErrorCode::ValueOutOfRange);
}

}  // namespace
