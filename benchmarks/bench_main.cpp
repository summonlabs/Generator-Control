// Generator Control - completed-operation benchmark.
//
// Every measurement times a *completed* operation: the timer starts before the work
// and stops after the operation has actually finished, including every mandatory
// step. For a durable mutation that means validation, canonical encoding, the staged
// write, the flush to durable storage, the read-back verification, the atomic
// publish, the head commit and the fence advance. Nothing is measured at submission
// time and nothing is deferred to a background thread, because this runtime has no
// background threads.
//
// Labelling:
//   * REAL        - the measurement exercises real code and real durability on this
//                   host: file writes, FlushFileBuffers, atomic renames, hashing.
//   * SYNTHETIC   - the device behind the adapter is a deterministic simulator, so
//                   the electrical behaviour is simulated even though the control
//                   path and the persistence are real.
//   * UNSUPPORTED - the operation cannot be measured here.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "genctl/adapter.hpp"
#include "genctl/engine.hpp"
#include "genctl/model.hpp"
#include "genctl/platform.hpp"
#include "genctl/version.hpp"

namespace {

using namespace genctl;

std::string bench_directory() {
  const Result<std::string> temp = platform::temporary_directory();
  const std::string base = temp.ok() ? temp.value() : std::string(".");
  return base + "\\genctl-bench";
}

struct Measurement {
  std::string name;
  std::string label;
  std::string notes;
  std::size_t iterations{0};
  double total_millis{0.0};
  double best_millis{0.0};
  double worst_millis{0.0};

  [[nodiscard]] double operations_per_second() const {
    return total_millis <= 0.0 ? 0.0 : (static_cast<double>(iterations) * 1000.0) / total_millis;
  }
  [[nodiscard]] double mean_micros() const {
    return iterations == 0 ? 0.0 : (total_millis * 1000.0) / static_cast<double>(iterations);
  }
};

void print(const Measurement& m) {
  std::printf("%-26s %-11s n=%-6zu total=%9.2f ms  mean=%8.2f us  best=%8.2f us  worst=%8.2f us  "
              "%10.1f ops/s\n",
              m.name.c_str(), m.label.c_str(), m.iterations, m.total_millis, m.mean_micros(),
              m.best_millis * 1000.0, m.worst_millis * 1000.0, m.operations_per_second());
  if (!m.notes.empty()) std::printf("%-26s   %s\n", "", m.notes.c_str());
}

template <typename Setup, typename Work>
Measurement measure(const std::string& name, const std::string& label, const std::string& notes,
                    std::size_t iterations, Setup setup, Work work) {
  Measurement measurement{};
  measurement.name = name;
  measurement.label = label;
  measurement.notes = notes;
  measurement.iterations = iterations;
  measurement.best_millis = 1e18;
  for (std::size_t i = 0; i < iterations; ++i) {
    setup();
    const auto start = std::chrono::steady_clock::now();
    work();
    const auto stop = std::chrono::steady_clock::now();
    const double millis = std::chrono::duration<double, std::milli>(stop - start).count();
    measurement.total_millis += millis;
    measurement.best_millis = std::min(measurement.best_millis, millis);
    measurement.worst_millis = std::max(measurement.worst_millis, millis);
  }
  return measurement;
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const std::string store = bench_directory();
  (void)platform::remove_directory_tree(store);
  if (!platform::ensure_directory(store).ok()) {
    std::printf("cannot create the benchmark store\n");
    return 1;
  }

  std::printf("%s\n", version_banner().c_str());
  std::printf("benchmark host: %s\n",
#if defined(_MSC_VER)
              ("MSVC " + std::to_string(_MSC_VER)).c_str()
#else
              "non-MSVC toolchain"
#endif
  );
  std::printf("store: %s\n", store.c_str());
  std::printf("workload: one generator, %zu required readiness checks, complete publication per "
              "durable operation\n\n",
              default_required_checks().size());

  // The clock starts at the Unix epoch so that the benchmark state is reproducible.
  ManualClock clock{0};
  const Result<GeneratorId> id = GeneratorId::parse("bench-gen-1");
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
  if (!engine->register_generator(id.value(), RegisterOptions{}, 0).ok()) {
    std::printf("registration failed\n");
    return 1;
  }

  // Bind the full evidence set once so that planning and actuation are admissible.
  {
    const Result<GeneratorState> registered = engine->inspect(id.value());
    if (!registered.ok()) return 1;
    for (const CheckKind kind : registered.value().required_checks) {
      const Result<GeneratorState> fresh = engine->inspect(id.value());
      if (!fresh.ok()) return 1;
      CheckUpdate update{};
      update.generator = id.value();
      update.controller = engine->controller();
      update.revision = fresh.value().revision;
      const Result<IdempotencyKey> key = IdempotencyKey::parse(
          "bench-bind-" + std::to_string(engine->commit_seq().value()) + "-" +
          std::to_string(static_cast<unsigned>(kind)));
      if (!key.ok()) return 1;
      update.idempotency_key = key.value();
      update.record.kind = kind;
      update.record.required = true;
      update.record.state = EvidenceState::Present;
      update.record.source = EvidenceSource::SyntheticAdapter;
      update.record.observed_at = 0;
      update.record.max_age_millis = 240 * kMillisPerHour;
      update.record.value = TypedValue{TypedValueKind::Boolean, 1};
      if (!engine->record_check(update).ok()) return 1;
    }
    ResourceUpdate update{};
    const Result<GeneratorState> fresh = engine->inspect(id.value());
    if (!fresh.ok()) return 1;
    update.generator = id.value();
    update.controller = engine->controller();
    update.revision = fresh.value().revision;
    const Result<IdempotencyKey> key =
        IdempotencyKey::parse("bench-bind-resource-" + std::to_string(engine->commit_seq().value()));
    if (!key.ok()) return 1;
    update.idempotency_key = key.value();
    const auto fill = [](auto& evidence, auto quantity) {
      evidence.state = EvidenceState::Present;
      evidence.value = quantity;
      evidence.source = EvidenceSource::SyntheticAdapter;
      evidence.observed_at = 0;
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
    if (!engine->record_resource(update).ok()) return 1;
  }

  const auto make_request = [&](const std::string& key, OperationKind operation) {
    const Result<GeneratorState> current = engine->inspect(id.value());
    OperationRequest request{};
    if (!current.ok()) return request;
    request.generator = id.value();
    request.operation = operation;
    request.controller = engine->controller();
    request.generation = current.value().generation;
    request.revision = current.value().revision;
    request.authority.cls = AuthorityClass::Normal;
    request.authority.epoch = engine->controller().epoch;
    request.authority.granted_by = "benchmark";
    const Result<IdempotencyKey> parsed = IdempotencyKey::parse(key);
    if (parsed.ok()) request.idempotency_key = parsed.value();
    request.requested_at = 0;
    return request;
  };

  const auto refresh_resources = [&]() {
    // A controller re-reads its consumables between commands. Scenario setup, never
    // part of a measured region.
    const Result<GeneratorState> fresh = engine->inspect(id.value());
    if (!fresh.ok()) return;
    ResourceUpdate update{};
    update.generator = id.value();
    update.controller = engine->controller();
    update.revision = fresh.value().revision;
    const Result<IdempotencyKey> key =
        IdempotencyKey::parse("bench-refresh-" + std::to_string(engine->commit_seq().value()));
    if (!key.ok()) return;
    update.idempotency_key = key.value();
    const EpochMillis at = clock.now_millis();
    const auto fill = [at](auto& evidence, auto quantity) {
      evidence.state = EvidenceState::Present;
      evidence.value = quantity;
      evidence.source = EvidenceSource::SyntheticAdapter;
      evidence.observed_at = at;
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
    (void)engine->record_resource(update);
  };

  std::vector<Measurement> measurements;

  measurements.push_back(measure(
      "operation_planning", "REAL",
      "engine.evaluate: format, identity, fencing, lifecycle, transition, mode, authority, "
      "interlock, readiness, resource, synchronization and transfer stages",
      20000, []() {},
      [&]() {
        const Result<EvaluationReport> report = engine->evaluate(make_request("bench-plan-000001", OperationKind::Start));
        if (!report.ok()) std::printf("planning failed: %s\n", report.status().message().c_str());
      }));

  measurements.push_back(measure(
      "readiness_evaluation", "REAL",
      "readiness report over the full required check set, including the binding digest",
      50000, []() {},
      [&]() {
        const Result<ReadinessReport> report = engine->readiness(id.value(), 0);
        if (!report.ok()) std::printf("readiness failed\n");
      }));

  measurements.push_back(measure(
      "durable_mutation", "REAL",
      "record_check end to end: validate, canonical encode, staged write, FlushFileBuffers, "
      "read-back verify, atomic publish, head commit, fence advance, residue retirement",
      300, []() {},
      [&]() {
        const Result<GeneratorState> fresh = engine->inspect(id.value());
        if (!fresh.ok()) return;
        CheckUpdate update{};
        update.generator = id.value();
        update.controller = engine->controller();
        update.revision = fresh.value().revision;
        const Result<IdempotencyKey> key = IdempotencyKey::parse(
            "bench-mutate-" + std::to_string(engine->commit_seq().value()));
        if (!key.ok()) return;
        update.idempotency_key = key.value();
        update.record.kind = CheckKind::CoolantLevelSufficient;
        update.record.required = true;
        update.record.state = EvidenceState::Present;
        update.record.source = EvidenceSource::SyntheticAdapter;
        update.record.observed_at = 0;
        update.record.max_age_millis = 24 * kMillisPerHour;
        update.record.value = TypedValue{TypedValueKind::Boolean, 1};
        if (!engine->record_check(update).ok()) {
          std::printf("durable mutation failed\n");
        }
      }));

  // One actuation completes the operation; resolving the attempt afterwards is
  // scenario setup for the next iteration, not part of the measured region.
  measurements.push_back(measure(
      "actuation_attempt", "SYNTHETIC",
      "execute a start: planning, durable write-ahead command record, device issue, durable "
      "acknowledgement commit. The device is a deterministic simulator; the control path, the "
      "durability and the adapter boundary are real",
      200,
      [&]() {
        // Scenario reset, deliberately outside the measured region: resolve every
        // outstanding effect by observation, then return the set to a stopped
        // condition so the next start is admissible.
        const auto resolve_outstanding = [&]() {
          for (int guard = 0; guard < 4; ++guard) {
            const Result<std::vector<AttemptRecord>> attempts = engine->attempts(id.value());
            if (!attempts.ok()) return;
            const AttemptRecord* pending = nullptr;
            for (const auto& record : attempts.value()) {
              const bool open_command = command_is_unresolved(record.command_state);
              const bool open_effect =
                  record.command_state == CommandState::Acknowledged &&
                  (record.effect_state == EffectState::NotObserved ||
                   record.effect_state == EffectState::Unknown);
              if (open_command || open_effect) {
                pending = &record;
                break;
              }
            }
            if (pending == nullptr) return;
            clock.advance(60 * kMillisPerSecond);
            ObserveRequest observe{};
            observe.generator = id.value();
            observe.controller = engine->controller();
            observe.attempt = pending->id;
            observe.resolve_attempt = true;
            observe.requested_at = clock.now_millis();
            if (!engine->observe(observe).ok()) return;
            const Result<AttemptRecord> resolved = engine->attempt(pending->id);
            if (resolved.ok() &&
                resolved.value().effect_state == EffectState::ObservedContradictory) {
              (void)engine->abandon(pending->id, "benchmark scenario reset", clock.now_millis());
            }
          }
        };
        resolve_outstanding();

        // Refresh the consumable readings exactly as a controller would between
        // commands. Scenario setup, not part of the measured operation.
        {
          const Result<GeneratorState> fresh = engine->inspect(id.value());
          if (fresh.ok()) {
            ResourceUpdate update{};
            update.generator = id.value();
            update.controller = engine->controller();
            update.revision = fresh.value().revision;
            const Result<IdempotencyKey> key = IdempotencyKey::parse(
                "bench-refresh-" + std::to_string(engine->commit_seq().value()));
            if (key.ok()) {
              update.idempotency_key = key.value();
              const EpochMillis at = clock.now_millis();
              const auto fill = [at](auto& evidence, auto quantity) {
                evidence.state = EvidenceState::Present;
                evidence.value = quantity;
                evidence.source = EvidenceSource::SyntheticAdapter;
                evidence.observed_at = at;
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
              (void)engine->record_resource(update);
            }
          }
        }

        const Result<GeneratorState> state = engine->inspect(id.value());
        if (state.ok() && is_running_state(state.value().operating)) {
          const Result<AttemptRecord> stopped = engine->execute(
              make_request("bench-reset-" + std::to_string(engine->commit_seq().value()),
                           OperationKind::Stop),
              false);
          if (stopped.ok()) {
            clock.advance(60 * kMillisPerSecond);
            ObserveRequest observe{};
            observe.generator = id.value();
            observe.controller = engine->controller();
            observe.attempt = stopped.value().id;
            observe.resolve_attempt = true;
            observe.requested_at = clock.now_millis();
            (void)engine->observe(observe);
          }
        }
      },
      [&]() {
        const Result<AttemptRecord> attempt = engine->execute(
            make_request("bench-act-" + std::to_string(engine->commit_seq().value()),
                         OperationKind::Start),
            false);
        if (!attempt.ok()) {
          std::printf("actuation refused: %s\n", attempt.status().message().c_str());
        }
      }));

  const std::string known_key = "bench-replay-000001";
  {
    // Resolve whatever the actuation benchmark left outstanding and return the set to
    // a stopped condition, then establish one accepted attempt whose key can be
    // retried.
    for (int guard = 0; guard < 4; ++guard) {
      const Result<std::vector<AttemptRecord>> attempts = engine->attempts(id.value());
      if (!attempts.ok()) break;
      const AttemptRecord* pending = nullptr;
      for (const auto& record : attempts.value()) {
        const bool open_command = command_is_unresolved(record.command_state);
        const bool open_effect = record.command_state == CommandState::Acknowledged &&
                                 (record.effect_state == EffectState::NotObserved ||
                                  record.effect_state == EffectState::Unknown);
        if (open_command || open_effect) {
          pending = &record;
          break;
        }
      }
      if (pending == nullptr) break;
      clock.advance(60 * kMillisPerSecond);
      ObserveRequest observe{};
      observe.generator = id.value();
      observe.controller = engine->controller();
      observe.attempt = pending->id;
      observe.resolve_attempt = true;
      observe.requested_at = clock.now_millis();
      if (!engine->observe(observe).ok()) break;
      const Result<AttemptRecord> resolved = engine->attempt(pending->id);
      if (resolved.ok() && resolved.value().effect_state == EffectState::ObservedContradictory) {
        (void)engine->abandon(pending->id, "benchmark scenario reset", clock.now_millis());
      }
    }
    const Result<GeneratorState> state = engine->inspect(id.value());
    if (state.ok() && is_running_state(state.value().operating)) {
      const Result<AttemptRecord> stopped = engine->execute(
          make_request("bench-reset-final-" + std::to_string(engine->commit_seq().value()),
                       OperationKind::Stop),
          false);
      if (stopped.ok()) {
        clock.advance(60 * kMillisPerSecond);
        ObserveRequest observe{};
        observe.generator = id.value();
        observe.controller = engine->controller();
        observe.attempt = stopped.value().id;
        observe.resolve_attempt = true;
        observe.requested_at = clock.now_millis();
        (void)engine->observe(observe);
      }
    }
    // Refresh the consumable readings so that the replay measurement is not measuring
    // a refusal.
    refresh_resources();
    const Result<AttemptRecord> first =
        engine->execute(make_request(known_key, OperationKind::Start), false);
    if (!first.ok()) {
      std::printf("replay setup failed: %s\n", first.status().message().c_str());
      return 1;
    }
  }
  measurements.push_back(measure(
      "idempotent_replay", "REAL",
      "retry of an accepted attempt: key lookup before any staleness check, no actuation, no "
      "publication",
      50000, []() {},
      [&]() {
        const Result<AttemptRecord> replayed = engine->execute(make_request(known_key, OperationKind::Start), false);
        if (!replayed.ok() || !replayed.value().replayed) {
          std::printf("replay failed\n");
        }
      }));

  measurements.push_back(measure(
      "resource_assessment", "REAL",
      "fuel and consumable sufficiency: per-field usability, checked runtime arithmetic and the "
      "resource binding digest",
      50000, []() {},
      [&]() {
        const Result<ResourceAssessment> report = engine->resource_assessment(id.value(), 0);
        if (!report.ok()) std::printf("resource assessment failed\n");
      }));

  measurements.push_back(measure(
      "store_audit", "REAL",
      "full store audit: head, fence, lease, every generation re-read, integrity and digest "
      "verification",
      200, []() {},
      [&]() {
        const Result<StoreAuditReport> report = engine->store_audit(false, {});
        if (!report.ok()) std::printf("audit failed\n");
      }));

  std::printf("completed operations (timer covers the whole operation):\n");
  for (const auto& measurement : measurements) print(measurement);

  // Reopen benchmark: the store is closed and reopened, and the committed image is
  // adopted and verified.
  {
    Measurement reopen{};
    reopen.name = "store_reopen_recovery";
    reopen.label = "REAL";
    reopen.notes = "close plus reopen: acquire the writer lease, read the head marker, read and "
                   "verify the committed generation, demote volatile evidence, retire residue";
    reopen.best_millis = 1e18;
    const std::size_t iterations = 30;
    reopen.iterations = iterations;
    for (std::size_t i = 0; i < iterations; ++i) {
      (void)engine->close();
      engine.reset();
      const auto start = std::chrono::steady_clock::now();
      Result<std::unique_ptr<GeneratorControlEngine>> again =
          GeneratorControlEngine::open(options, clock, &adapter);
      const auto stop = std::chrono::steady_clock::now();
      if (!again.ok()) {
        std::printf("reopen failed: %s\n", again.status().message().c_str());
        return 1;
      }
      engine = std::move(again.value());
      const double millis = std::chrono::duration<double, std::milli>(stop - start).count();
      reopen.total_millis += millis;
      reopen.best_millis = std::min(reopen.best_millis, millis);
      reopen.worst_millis = std::max(reopen.worst_millis, millis);
    }
    print(reopen);
    measurements.push_back(reopen);
  }

  const Result<StoreAuditReport> final_audit = engine->store_audit(false, {});
  if (!final_audit.ok()) {
    std::printf("final audit failed: %s\n", final_audit.status().message().c_str());
    return 1;
  }
  std::printf("\nverified state before cleanup: %s\n", final_audit.value().summary.c_str());
  std::printf("  generations valid: %zu of %zu, anomalies: %zu\n",
              final_audit.value().generations_valid, final_audit.value().generations_scanned,
              final_audit.value().anomalies.size());
  const Result<std::vector<AttemptRecord>> attempts = engine->attempts(id.value());
  if (attempts.ok()) {
    std::printf("  attempt records: %zu\n", attempts.value().size());
  }

  const Status closed = engine->close();
  const Status removed = platform::remove_directory_tree(store);
  std::printf("cleanup: close %s, benchmark store removed %s\n", closed.ok() ? "ok" : "failed",
              removed.ok() ? "yes" : "no");
  const Result<bool> residue = platform::path_exists(store);
  std::printf("cleanup verified: store directory exists = %s\n",
              (residue.ok() && residue.value()) ? "yes (residue)" : "no");
  return 0;
}
