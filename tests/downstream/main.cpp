// Independent downstream consumer.
//
// Registers a generator, binds readiness and resource evidence, drives a complete
// synthetic start through the installed library, verifies the effect by observation
// and proves that a retry of the accepted attempt replays instead of actuating
// again. It fails loudly if any of that stops working through the installed package.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <genctl/adapter.hpp>
#include <genctl/authority.hpp>
#include <genctl/engine.hpp>
#include <genctl/platform.hpp>
#include <genctl/version.hpp>

namespace {

int fail(const char* what, const genctl::Status& status) {
  std::printf("downstream FAILURE: %s: %s\n", what, status.to_string().c_str());
  return 1;
}

}  // namespace

int main() {
  using namespace genctl;
  std::printf("downstream consumer built against %s\n", version_banner().c_str());

  if (!digest_self_test().ok()) {
    std::printf("downstream FAILURE: digest self test\n");
    return 1;
  }

  const Result<std::string> temp = platform::temporary_directory();
  if (!temp.ok()) return fail("temporary directory", temp.status());
  const std::string store = temp.value() + "\\genctl-downstream-store";
  (void)platform::remove_directory_tree(store);
  if (!platform::ensure_directory(store).ok()) {
    std::printf("downstream FAILURE: cannot create the store directory\n");
    return 1;
  }

  ManualClock clock{0};
  const Result<GeneratorId> id = GeneratorId::parse("downstream-gen-1");
  if (!id.ok()) return fail("generator identity", id.status());

  SyntheticAdapter::Options adapter_options{};
  adapter_options.journal_path = store + "\\device.jrnl";
  SyntheticAdapter adapter(id.value(), clock, adapter_options);

  OpenOptions options{};
  options.store_directory = store;
  options.create_if_missing = true;
  Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(options, clock, &adapter);
  if (!opened.ok()) return fail("engine open", opened.status());
  std::unique_ptr<GeneratorControlEngine> engine = std::move(opened.value());

  const Result<GeneratorState> registered =
      engine->register_generator(id.value(), RegisterOptions{}, clock.now_millis());
  if (!registered.ok()) return fail("register", registered.status());

  // Bind every required check and the consumables.
  const Result<GeneratorState> initial = engine->inspect(id.value());
  if (!initial.ok()) return fail("inspect", initial.status());
  for (const CheckKind kind : initial.value().required_checks) {
    const Result<GeneratorState> fresh = engine->inspect(id.value());
    if (!fresh.ok()) return fail("inspect", fresh.status());
    CheckUpdate update{};
    update.generator = id.value();
    update.controller = engine->controller();
    update.revision = fresh.value().revision;
    const Result<IdempotencyKey> key = IdempotencyKey::parse(
        "downstream-check-" + std::to_string(engine->commit_seq().value()) + "-" +
        std::to_string(static_cast<unsigned>(kind)));
    if (!key.ok()) return fail("idempotency key", key.status());
    update.idempotency_key = key.value();
    update.record.kind = kind;
    update.record.required = true;
    update.record.state = EvidenceState::Present;
    update.record.source = EvidenceSource::SyntheticAdapter;
    update.record.observed_at = 0;
    update.record.max_age_millis = 60 * kMillisPerMinute;
    update.record.value = TypedValue{TypedValueKind::Boolean, 1};
    const Status status = engine->record_check(update);
    if (!status.ok()) return fail("record check", status);
  }
  {
    const Result<GeneratorState> fresh = engine->inspect(id.value());
    if (!fresh.ok()) return fail("inspect", fresh.status());
    ResourceUpdate update{};
    update.generator = id.value();
    update.controller = engine->controller();
    update.revision = fresh.value().revision;
    const Result<IdempotencyKey> key =
        IdempotencyKey::parse("downstream-resource-" + std::to_string(engine->commit_seq().value()));
    if (!key.ok()) return fail("idempotency key", key.status());
    update.idempotency_key = key.value();
    update.set_fuel_level = true;
    update.fuel_level.state = EvidenceState::Present;
    update.fuel_level.value = FuelQuantity{FuelUnit::Millilitres, 900'000};
    update.fuel_level.source = EvidenceSource::SyntheticAdapter;
    update.fuel_level.observed_at = 0;
    update.set_fuel_consumption = true;
    update.fuel_consumption.state = EvidenceState::Present;
    update.fuel_consumption.value = FuelRate{FuelUnit::Millilitres, 60'000};
    update.fuel_consumption.source = EvidenceSource::SyntheticAdapter;
    update.fuel_consumption.observed_at = 0;
    update.set_lube_oil_pressure = true;
    update.lube_oil_pressure.state = EvidenceState::Present;
    update.lube_oil_pressure.value = Pressure{410'000};
    update.lube_oil_pressure.source = EvidenceSource::SyntheticAdapter;
    update.lube_oil_pressure.observed_at = 0;
    update.set_coolant_temperature = true;
    update.coolant_temperature.state = EvidenceState::Present;
    update.coolant_temperature.value = Temperature{78'000};
    update.coolant_temperature.source = EvidenceSource::SyntheticAdapter;
    update.coolant_temperature.observed_at = 0;
    update.set_coolant_level = true;
    update.coolant_level.state = EvidenceState::Present;
    update.coolant_level.value = Percent{9'200};
    update.coolant_level.source = EvidenceSource::SyntheticAdapter;
    update.coolant_level.observed_at = 0;
    update.set_battery_voltage = true;
    update.battery_voltage.state = EvidenceState::Present;
    update.battery_voltage.value = Voltage{25'600};
    update.battery_voltage.source = EvidenceSource::SyntheticAdapter;
    update.battery_voltage.observed_at = 0;
    const Status status = engine->record_resource(update);
    if (!status.ok()) return fail("record resource", status);
  }

  const auto make_start = [&](const std::string& key) {
    const Result<GeneratorState> current = engine->inspect(id.value());
    OperationRequest request{};
    if (!current.ok()) return request;
    request.generator = id.value();
    request.operation = OperationKind::Start;
    request.controller = engine->controller();
    request.generation = current.value().generation;
    request.revision = current.value().revision;
    request.authority.cls = AuthorityClass::Normal;
    request.authority.epoch = engine->controller().epoch;
    request.authority.granted_by = "downstream-consumer";
    const Result<IdempotencyKey> parsed = IdempotencyKey::parse(key);
    if (parsed.ok()) request.idempotency_key = parsed.value();
    request.requested_at = clock.now_millis();
    return request;
  };

  const Result<AttemptRecord> attempt = engine->execute(make_start("downstream-start-1"), false);
  if (!attempt.ok()) return fail("execute start", attempt.status());
  if (attempt.value().command_state != CommandState::Acknowledged) {
    std::printf("downstream FAILURE: the device did not acknowledge the start\n");
    return 1;
  }

  clock.advance(12 * kMillisPerSecond);
  ObserveRequest observe{};
  observe.generator = id.value();
  observe.controller = engine->controller();
  observe.attempt = attempt.value().id;
  observe.resolve_attempt = true;
  observe.requested_at = clock.now_millis();
  const Result<ObserveOutcome> outcome = engine->observe(observe);
  if (!outcome.ok()) return fail("observe", outcome.status());
  if (outcome.value().attempt.effect_state != EffectState::Verified) {
    std::printf("downstream FAILURE: the observed effect was not verified\n");
    return 1;
  }

  const Result<AttemptRecord> replayed = engine->execute(make_start("downstream-start-1"), false);
  if (!replayed.ok()) return fail("replay", replayed.status());
  if (!replayed.value().replayed || !(replayed.value().id == attempt.value().id)) {
    std::printf("downstream FAILURE: the retry did not replay the accepted attempt\n");
    return 1;
  }
  if (adapter.accepted_count(OperationKind::Start) != 1) {
    std::printf("downstream FAILURE: the device was actuated more than once\n");
    return 1;
  }

  const Result<StoreAuditReport> audit = engine->store_audit(false, {});
  if (!audit.ok()) return fail("store audit", audit.status());
  if (!audit.value().anomalies.empty()) {
    std::printf("downstream FAILURE: the store reported anomalies\n");
    return 1;
  }

  std::printf("downstream: start acknowledged, effect verified by observation, retry replayed, "
              "device actuations = %zu, store %s\n",
              adapter.accepted_count(OperationKind::Start), audit.value().summary.c_str());

  (void)engine->close();
  (void)platform::remove_directory_tree(store);
  std::printf("downstream: OK\n");
  return 0;
}
