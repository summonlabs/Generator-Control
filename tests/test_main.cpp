// Generator Control - test runner and child-process scenarios.
//
// The same executable is both the test runner and the child process used by the
// multiprocess, crash and reopen proofs. A child scenario prints its result to
// standard output (redirected to a file by the parent) and exits with a status the
// parent asserts on. No scenario relies on a timeout, on a pipe or on interactive
// input, and a child that must die abruptly calls the non-interactive
// TerminateProcess path directly.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "genctl/adapter.hpp"
#include "genctl/engine.hpp"
#include "genctl/platform.hpp"
#include "support.hpp"
#include "test_framework.hpp"

namespace {

using namespace genctl;

SyntheticAdapter::Options adapter_options_for(const std::string& journal) {
  SyntheticAdapter::Options options{};
  options.journal_path = journal;
  options.initial_breaker = BreakerPosition::Open;
  return options;
}

OpenOptions open_options_for(const std::string& store, const std::string& crash_point,
                             bool create) {
  OpenOptions options{};
  options.store_directory = store;
  options.create_if_missing = create;
  std::uint64_t epoch = 0;
  const Result<CrashPoint> point = parse_crash_point(crash_point);
  if (point.ok()) options.config.crash_point = point.value();
  options.requested_epoch = ControlEpoch{epoch};
  return options;
}

int scenario_lock_and_hold(int argc, char** argv) {
  // --scenario lock-and-hold <store> <ready-file> <millis>
  if (argc < 6) return 2;
  const std::string store = argv[3];
  const std::string ready = argv[4];
  const long hold = std::strtol(argv[5], nullptr, 10);
  ManualClock clock;
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  if (!id.ok()) return 2;
  SyntheticAdapter adapter(id.value(), clock, adapter_options_for(""));
  OpenOptions options{};
  options.store_directory = store;
  options.create_if_missing = true;
  const Result<std::unique_ptr<GeneratorControlEngine>> engine =
      GeneratorControlEngine::open(options, clock, &adapter);
  if (!engine.ok()) {
    std::printf("open-failed %s\n", engine.status().to_string().c_str());
    return 5;
  }
  const ByteBuffer marker{'r', 'e', 'a', 'd', 'y'};
  const Status written = platform::durable_write_file(ready, marker, false);
  if (!written.ok()) return 5;
  std::this_thread::sleep_for(std::chrono::milliseconds(hold));
  (void)engine.value()->close();
  std::printf("held\n");
  return 0;
}

int scenario_lock_and_die(int argc, char** argv) {
  // --scenario lock-and-die <store> <ready-file>
  if (argc < 5) return 2;
  const std::string store = argv[3];
  const std::string ready = argv[4];
  ManualClock clock;
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  if (!id.ok()) return 2;
  SyntheticAdapter adapter(id.value(), clock, adapter_options_for(""));
  OpenOptions options{};
  options.store_directory = store;
  options.create_if_missing = true;
  const Result<std::unique_ptr<GeneratorControlEngine>> engine =
      GeneratorControlEngine::open(options, clock, &adapter);
  if (!engine.ok()) {
    std::printf("open-failed %s\n", engine.status().to_string().c_str());
    return 5;
  }
  const ByteBuffer marker{'h', 'e', 'l', 'd'};
  const Status written = platform::durable_write_file(ready, marker, false);
  if (!written.ok()) return 5;
  // Abrupt, non-interactive death while holding the writer lease. The operating
  // system must release the file lock.
  platform::terminate_process_now(91);
  return 91;
}

int scenario_start(int argc, char** argv) {
  // --scenario start <store> <device-journal> <key> <crash-point>
  if (argc < 6) return 2;
  const std::string store = argv[3];
  const std::string journal = argv[4];
  const std::string key = argv[5];
  const std::string crash_point = argc > 6 ? argv[6] : "none";
  ManualClock clock;
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  if (!id.ok()) return 2;
  SyntheticAdapter adapter(id.value(), clock, adapter_options_for(journal));
  const Result<std::size_t> replayed = adapter.replay_journal();
  if (!replayed.ok()) return 5;
  const Result<std::unique_ptr<GeneratorControlEngine>> engine =
      GeneratorControlEngine::open(open_options_for(store, crash_point, false), clock, &adapter);
  if (!engine.ok()) {
    std::printf("open-failed %s\n", engine.status().to_string().c_str());
    return 5;
  }
  GeneratorControlEngine& control = *engine.value();
  const Result<GeneratorState> current = control.inspect(id.value());
  if (!current.ok()) return 5;
  for (const CheckKind kind : current.value().required_checks) {
    CheckUpdate update{};
    update.generator = id.value();
    update.controller = control.controller();
    update.revision = current.value().revision;
    const Result<GeneratorState> fresh = control.inspect(id.value());
    if (!fresh.ok()) return 5;
    update.revision = fresh.value().revision;
    const Result<IdempotencyKey> parsed = IdempotencyKey::parse(
        "child-check-" + std::to_string(control.commit_seq().value()) + "-" +
        std::to_string(static_cast<unsigned>(kind)));
    if (!parsed.ok()) return 5;
    update.idempotency_key = parsed.value();
    update.record.kind = kind;
    update.record.required = true;
    update.record.state = EvidenceState::Present;
    update.record.source = EvidenceSource::SyntheticAdapter;
    update.record.observed_at = clock.now_millis();
    update.record.max_age_millis = 60 * kMillisPerSecond;
    update.record.value = TypedValue{TypedValueKind::Boolean, 1};
    const Status status = control.record_check(update);
    if (!status.ok()) {
      std::printf("check-failed %s\n", status.to_string().c_str());
      return 5;
    }
  }
  {
    ResourceUpdate update{};
    update.generator = id.value();
    update.controller = control.controller();
    const Result<GeneratorState> fresh = control.inspect(id.value());
    if (!fresh.ok()) return 5;
    update.revision = fresh.value().revision;
    const Result<IdempotencyKey> parsed =
        IdempotencyKey::parse("child-resource-" + std::to_string(control.commit_seq().value()));
    if (!parsed.ok()) return 5;
    update.idempotency_key = parsed.value();
    update.set_fuel_level = true;
    update.fuel_level.state = EvidenceState::Present;
    update.fuel_level.value = FuelQuantity{FuelUnit::Millilitres, 900'000};
    update.fuel_level.source = EvidenceSource::SyntheticAdapter;
    update.fuel_level.observed_at = clock.now_millis();
    update.set_fuel_capacity = true;
    update.fuel_capacity.state = EvidenceState::Present;
    update.fuel_capacity.value = FuelQuantity{FuelUnit::Millilitres, 1'000'000};
    update.fuel_capacity.source = EvidenceSource::SyntheticAdapter;
    update.fuel_capacity.observed_at = clock.now_millis();
    update.set_fuel_consumption = true;
    update.fuel_consumption.state = EvidenceState::Present;
    update.fuel_consumption.value = FuelRate{FuelUnit::Millilitres, 60'000};
    update.fuel_consumption.source = EvidenceSource::SyntheticAdapter;
    update.fuel_consumption.observed_at = clock.now_millis();
    update.set_lube_oil_pressure = true;
    update.lube_oil_pressure.state = EvidenceState::Present;
    update.lube_oil_pressure.value = Pressure{410'000};
    update.lube_oil_pressure.source = EvidenceSource::SyntheticAdapter;
    update.lube_oil_pressure.observed_at = clock.now_millis();
    update.set_coolant_temperature = true;
    update.coolant_temperature.state = EvidenceState::Present;
    update.coolant_temperature.value = Temperature{78'000};
    update.coolant_temperature.source = EvidenceSource::SyntheticAdapter;
    update.coolant_temperature.observed_at = clock.now_millis();
    update.set_coolant_level = true;
    update.coolant_level.state = EvidenceState::Present;
    update.coolant_level.value = Percent{9'200};
    update.coolant_level.source = EvidenceSource::SyntheticAdapter;
    update.coolant_level.observed_at = clock.now_millis();
    update.set_battery_voltage = true;
    update.battery_voltage.state = EvidenceState::Present;
    update.battery_voltage.value = Voltage{25'600};
    update.battery_voltage.source = EvidenceSource::SyntheticAdapter;
    update.battery_voltage.observed_at = clock.now_millis();
    const Status status = control.record_resource(update);
    if (!status.ok()) {
      std::printf("resource-failed %s\n", status.to_string().c_str());
      return 5;
    }
  }

  const Result<GeneratorState> fresh = control.inspect(id.value());
  if (!fresh.ok()) return 5;
  OperationRequest request{};
  request.generator = id.value();
  request.operation = OperationKind::Start;
  request.controller = control.controller();
  request.generation = fresh.value().generation;
  request.revision = fresh.value().revision;
  request.authority.cls = AuthorityClass::Normal;
  request.authority.epoch = control.controller().epoch;
  request.authority.granted_by = "child-process";
  request.authority.granted_at = clock.now_millis();
  const Result<IdempotencyKey> parsed = IdempotencyKey::parse(key);
  if (!parsed.ok()) return 2;
  request.idempotency_key = parsed.value();
  request.requested_at = clock.now_millis();
  const Result<AttemptRecord> attempt = control.execute(request, false);
  if (!attempt.ok()) {
    std::printf("execute-refused %s\n", attempt.status().to_string().c_str());
    return 3;
  }
  std::printf("executed attempt=%llu command=%s ack=%s\n",
              static_cast<unsigned long long>(attempt.value().id.value()),
              std::string(to_string(attempt.value().command_state)).c_str(),
              std::string(to_string(attempt.value().ack_status)).c_str());
  (void)control.close();
  return 0;
}

int scenario_actuations(int argc, char** argv) {
  // --scenario actuations <device-journal> <operation>
  if (argc < 5) return 2;
  const std::string journal = argv[3];
  const std::string operation = argv[4];
  ManualClock clock;
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  if (!id.ok()) return 2;
  SyntheticAdapter adapter(id.value(), clock, adapter_options_for(journal));
  const Result<std::size_t> replayed = adapter.replay_journal();
  if (!replayed.ok()) return 5;
  const Result<OperationKind> kind = parse_operation_kind(operation);
  if (!kind.ok()) return 2;
  std::printf("accepted=%zu issued=%zu\n", adapter.accepted_count(kind.value()),
              adapter.issued_count());
  return 0;
}

}  // namespace

namespace gctest {

int run_scenario(int argc, char** argv) {
  const std::string name = argv[2];
  if (name == "lock-and-hold") return scenario_lock_and_hold(argc, argv);
  if (name == "lock-and-die") return scenario_lock_and_die(argc, argv);
  if (name == "start") return scenario_start(argc, argv);
  if (name == "actuations") return scenario_actuations(argc, argv);
  std::fprintf(stderr, "unknown scenario '%s'\n", name.c_str());
  return 2;
}

int run_all(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("Generator Control test runner. fixed seed = 0x%08X\n", test_seed());
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int passed = 0;
  int failed = 0;
  for (const auto& test : registry()) {
    if (filter != nullptr && test.name.find(filter) == std::string::npos) continue;
    std::printf("[run ] %s\n", test.name.c_str());
    try {
      test.fn();
      ++passed;
      std::printf("[ ok ] %s\n", test.name.c_str());
    } catch (const std::exception& error) {
      ++failed;
      std::printf("[FAIL] %s : %s\n", test.name.c_str(), error.what());
    } catch (...) {
      ++failed;
      std::printf("[FAIL] %s : unknown exception\n", test.name.c_str());
    }
  }
  std::printf("passed=%d failed=%d total=%zu\n", passed, failed, registry().size());
  return failed == 0 ? 0 : 1;
}

}  // namespace gctest

int main(int argc, char** argv) {
  if (argc >= 3 && std::string(argv[1]) == "--scenario") return gctest::run_scenario(argc, argv);
  return gctest::run_all(argc, argv);
}
