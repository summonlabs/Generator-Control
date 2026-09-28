// Generator Control - generatorctl administration and inspection CLI.
//
// The CLI is a thin shell over the library: it parses arguments, opens a store,
// drives the engine and prints deterministic text. It contains no control policy of
// its own - every decision, every refusal and every durable effect comes from the
// library, which is what makes the CLI evidence about the library rather than about
// itself.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "genctl/adapter.hpp"
#include "genctl/authority.hpp"
#include "genctl/engine.hpp"
#include "genctl/path_safety.hpp"
#include "genctl/platform.hpp"
#include "genctl/version.hpp"

namespace {

using namespace genctl;

struct Options {
  std::map<std::string, std::string> values;
  std::vector<std::string> flags;

  [[nodiscard]] bool has(const std::string& name) const {
    return values.count(name) != 0 ||
           std::find(flags.begin(), flags.end(), name) != flags.end();
  }
  [[nodiscard]] std::string get(const std::string& name, const std::string& fallback = {}) const {
    const auto it = values.find(name);
    return it == values.end() ? fallback : it->second;
  }
  [[nodiscard]] EpochMillis millis(const std::string& name, EpochMillis fallback) const {
    const auto it = values.find(name);
    if (it == values.end()) return fallback;
    return static_cast<EpochMillis>(std::strtoll(it->second.c_str(), nullptr, 10));
  }
  [[nodiscard]] std::int64_t number(const std::string& name, std::int64_t fallback) const {
    const auto it = values.find(name);
    if (it == values.end()) return fallback;
    return static_cast<std::int64_t>(std::strtoll(it->second.c_str(), nullptr, 10));
  }
};

void print_usage() {
  std::printf(
      "generatorctl - %s\n"
      "\n"
      "usage: generatorctl <command> [options]\n"
      "\n"
      "Store and identity options\n"
      "  --store <dir>            store directory (required for every command except version)\n"
      "  --generator <id>         generator identity\n"
      "  --epoch <n>              control epoch this invocation holds\n"
      "  --now <millis>           override the evaluation instant (UTC milliseconds)\n"
      "  --at <millis>            instant attached to recorded evidence\n"
      "  --revision <n|auto>      state revision the request is planned against (default auto)\n"
      "  --key <idempotency>      idempotency key for an actuating request\n"
      "  --authority <class>      normal | test | service | emergency\n"
      "  --authority-by <name>    who granted the authority\n"
      "  --reason <text>          authority reason (mandatory for emergency)\n"
      "  --authority-until <ms>   authority validity bound (mandatory for emergency)\n"
      "  --note <text>            free-form annotation stored with the attempt\n"
      "  --crash-at <point>       deterministic crash injection point (validation tooling)\n"
      "\n"
      "Administration\n"
      "  init                     create a store\n"
      "  register                 register a generator\n"
      "  check                    record a readiness check or interlock\n"
      "  resource                 record fuel/resource evidence\n"
      "  sync-precondition        record a synchronization precondition\n"
      "  transfer-precondition    record a transfer precondition\n"
      "  switch-authority         record the external facility switch authority token\n"
      "  transfer-path            record the external breaker/bus/path references\n"
      "  authority                record an authority grant\n"
      "  mode                     change the operating mode\n"
      "  lifecycle                change the administrative lifecycle\n"
      "\n"
      "Inspection\n"
      "  inspect                  show the generator's identity, state and generations\n"
      "  readiness                evaluate readiness and interlocks\n"
      "  resource-report          evaluate resource sufficiency\n"
      "  synchronization          evaluate synchronization eligibility\n"
      "  transfer                 evaluate transfer eligibility\n"
      "  evaluate                 plan an operation without actuating anything\n"
      "  attempts                 list attempt records\n"
      "  history                  list the state transition history\n"
      "  key-window               show or acknowledge the idempotency window\n"
      "  store-audit              verify the store, its fence and every generation\n"
      "\n"
      "Actuation (through the synthetic adapter)\n"
      "  act                      issue an operation (--operation <name>)\n"
      "  observe                  read the device and refresh the observed state\n"
      "  verify                   observe and resolve an attempt's effect\n"
      "  abandon                  abandon an unresolved attempt after a contradictory observation\n"
      "  revalidate               re-check every evidence binding against the current instant\n"
      "\n"
      "Diagnostics\n"
      "  self-test                verify the local digest implementations\n"
      "  version                  print the product and store format version\n",
      version_string().c_str());
}

int fail(const Status& status) {
  std::fprintf(stderr, "error: %s\n", status.to_string().c_str());
  return status.exit_code();
}

std::string now_string(EpochMillis millis) { return format_epoch_millis(millis); }

void print_state(const GeneratorState& state) {
  std::printf("generator: %s\n", state.id.str().c_str());
  std::printf("  hardware-generation: %u\n", state.generation.hardware.value());
  std::printf("  binding-epoch: %llu\n", static_cast<unsigned long long>(state.generation.binding.value()));
  std::printf("  commissioned: %s\n", state.commissioned ? "yes" : "no");
  std::printf("  lifecycle: %s\n", std::string(to_string(state.lifecycle)).c_str());
  std::printf("  operating: %s\n", std::string(to_string(state.operating)).c_str());
  std::printf("  mode: %s\n", std::string(to_string(state.mode)).c_str());
  std::printf("  synchronization: %s\n", std::string(to_string(state.synchronization)).c_str());
  std::printf("  breaker: %s\n", std::string(to_string(state.breaker)).c_str());
  std::printf("  state-revision: %llu\n", static_cast<unsigned long long>(state.revision.value()));
  std::printf("  last-observed: %s\n",
              state.last_observed_at == 0 ? "never" : now_string(state.last_observed_at).c_str());
  std::printf("  history-entries: %zu\n", state.history.size());
}

void print_readiness(const ReadinessReport& report) {
  std::printf("readiness: %s\n", report.satisfied ? "satisfied" : "not-satisfied");
  std::printf("  evaluated-at: %s\n", now_string(report.evaluated_at).c_str());
  std::printf("  interlock-evidence-complete: %s\n",
              report.interlock_evidence_complete ? "yes" : "no");
  std::printf("  synthetic-evidence: %s\n", report.synthetic_evidence_used ? "yes" : "no");
  std::printf("  advisory-waived: %s\n", report.advisory_waived ? "yes" : "no");
  std::printf("  binding-digest: %s\n", report.binding_digest.short_hex().c_str());
  if (!report.satisfied) {
    std::printf("  primary-error: %s\n", std::string(to_string(report.primary_error)).c_str());
  }
  for (const auto& finding : report.findings) {
    std::printf("  [%s] %-38s %-9s %-14s %s\n", finding.required ? "required" : "optional",
                std::string(to_string(finding.kind)).c_str(),
                std::string(to_string(finding.usability)).c_str(),
                std::string(to_string(finding.source)).c_str(),
                finding.waived ? "waived" : finding.detail.c_str());
  }
}

void print_resources(const ResourceAssessment& assessment) {
  std::printf("resources: %s\n", assessment.sufficient ? "sufficient" : "not-sufficient");
  std::printf("  runtime: %s\n",
              assessment.runtime.known ? format_millis(assessment.runtime.seconds * 1000).c_str()
                                       : "unknown");
  std::printf("  runtime-detail: %s\n", assessment.runtime.detail.c_str());
  std::printf("  synthetic-evidence: %s\n", assessment.synthetic_evidence_used ? "yes" : "no");
  std::printf("  binding-digest: %s\n", assessment.binding_digest.short_hex().c_str());
  if (!assessment.sufficient) {
    std::printf("  primary-error: %s\n", std::string(to_string(assessment.primary_error)).c_str());
  }
  for (const auto& finding : assessment.findings) {
    std::printf("  [%s] %-26s %-14s %s\n", finding.required ? "required" : "optional",
                std::string(to_string(finding.kind)).c_str(),
                std::string(to_string(finding.usability)).c_str(), finding.detail.c_str());
  }
}

void print_sync(const SynchronizationEligibility& eligibility) {
  std::printf("synchronization: %s\n", std::string(to_string(eligibility.outcome)).c_str());
  std::printf("  synthetic-evidence: %s\n", eligibility.synthetic_evidence_used ? "yes" : "no");
  std::printf("  binding-digest: %s\n", eligibility.binding_digest.short_hex().c_str());
  if (!eligibility.eligible()) {
    std::printf("  primary-error: %s\n", std::string(to_string(eligibility.primary_error)).c_str());
  }
  for (const auto& finding : eligibility.findings) {
    std::printf("  [%s] %-32s %-14s %s\n", finding.required ? "required" : "optional",
                std::string(to_string(finding.kind)).c_str(),
                std::string(to_string(finding.outcome)).c_str(), finding.detail.c_str());
  }
}

void print_transfer(const TransferEligibility& eligibility) {
  std::printf("transfer: %s\n", std::string(to_string(eligibility.outcome)).c_str());
  std::printf("  synthetic-evidence: %s\n", eligibility.synthetic_evidence_used ? "yes" : "no");
  std::printf("  binding-digest: %s\n", eligibility.binding_digest.short_hex().c_str());
  if (!eligibility.eligible()) {
    std::printf("  primary-error: %s\n", std::string(to_string(eligibility.primary_error)).c_str());
  }
  for (const auto& finding : eligibility.findings) {
    std::printf("  [%s] %-36s %-14s %s\n", finding.required ? "required" : "optional",
                std::string(to_string(finding.kind)).c_str(),
                std::string(to_string(finding.outcome)).c_str(), finding.detail.c_str());
  }
}

void print_attempt(const AttemptRecord& attempt) {
  std::printf("attempt: %llu\n", static_cast<unsigned long long>(attempt.id.value()));
  std::printf("  generator: %s\n", attempt.generator.str().c_str());
  std::printf("  operation: %s\n", std::string(to_string(attempt.operation)).c_str());
  std::printf("  planned-against: %s revision %llu\n", to_string(attempt.planned_against.controller).c_str(),
              static_cast<unsigned long long>(attempt.planned_against.revision.value()));
  std::printf("  idempotency-key: %s\n", attempt.key.str().c_str());
  std::printf("  fingerprint: %llu\n", static_cast<unsigned long long>(attempt.fingerprint.value()));
  std::printf("  authority: %s epoch %llu\n", std::string(to_string(attempt.authority)).c_str(),
              static_cast<unsigned long long>(attempt.authority_epoch.value()));
  std::printf("  command-state: %s\n", std::string(to_string(attempt.command_state)).c_str());
  std::printf("  acknowledgement: %s\n", std::string(to_string(attempt.ack_status)).c_str());
  std::printf("  command-id: %llu\n", static_cast<unsigned long long>(attempt.command_id.value()));
  std::printf("  effect-state: %s\n", std::string(to_string(attempt.effect_state)).c_str());
  std::printf("  observed-state: %s\n", std::string(to_string(attempt.observed_state)).c_str());
  std::printf("  observed-synchronization: %s\n",
              std::string(to_string(attempt.observed_synchronization)).c_str());
  std::printf("  observed-breaker: %s\n",
              std::string(to_string(attempt.observed_breaker)).c_str());
  std::printf("  effect-source: %s\n", std::string(to_string(attempt.effect_source)).c_str());
  std::printf("  effect-digest: %s\n", attempt.effect_digest.short_hex().c_str());
  std::printf("  replayed: %s\n", attempt.replayed ? "yes" : "no");
  if (!attempt.ack_detail.empty()) std::printf("  ack-detail: %s\n", attempt.ack_detail.c_str());
  if (!attempt.effect_detail.empty()) {
    std::printf("  effect-detail: %s\n", attempt.effect_detail.c_str());
  }
}

AuthorityGrant build_authority(const Options& options, ControlEpoch epoch, EpochMillis now) {
  AuthorityGrant grant{};
  const std::string cls = options.get("--authority");
  if (cls.empty()) return grant;
  const Result<AuthorityClass> parsed = parse_authority_class(cls);
  if (!parsed.ok()) return grant;
  grant.cls = parsed.value();
  grant.epoch = epoch;
  grant.granted_by = options.get("--authority-by", "operator");
  grant.granted_at = now;
  grant.valid_until = options.millis("--authority-until", 0);
  grant.reason = options.get("--reason");
  grant.explicit_grant = options.has("--authority-explicit");
  return grant;
}

EvidenceLifetime parse_lifetime(const std::string& text) {
  if (text == "attested") return EvidenceLifetime::AttestedWithValidity;
  if (text == "static") return EvidenceLifetime::StaticConfiguration;
  return EvidenceLifetime::VolatileObservation;
}

TypedValue build_value(const Options& options) {
  TypedValue value{};
  const std::string kind = options.get("--value-kind");
  if (kind.empty()) return value;
  static const std::map<std::string, TypedValueKind> kKinds = {
      {"none", TypedValueKind::None},
      {"boolean", TypedValueKind::Boolean},
      {"millivolts", TypedValueKind::Millivolts},
      {"millihertz", TypedValueKind::Millihertz},
      {"millidegrees", TypedValueKind::Millidegrees},
      {"milliamps", TypedValueKind::MilliAmps},
      {"watts", TypedValueKind::Watts},
      {"vars", TypedValueKind::Vars},
      {"millidegrees-celsius", TypedValueKind::MilliDegreesCelsius},
      {"pascals", TypedValueKind::Pascals},
      {"basis-points", TypedValueKind::BasisPoints},
      {"millilitres", TypedValueKind::Millilitres},
      {"grams", TypedValueKind::Grams},
      {"litres", TypedValueKind::Litres},
      {"seconds", TypedValueKind::Seconds},
  };
  const auto it = kKinds.find(kind);
  if (it == kKinds.end()) return value;
  value.kind = it->second;
  value.amount = options.number("--value", 0);
  return value;
}

Result<EvidenceState> parse_state(const std::string& text) {
  static const std::map<std::string, EvidenceState> kStates = {
      {"present", EvidenceState::Present},   {"missing", EvidenceState::Missing},
      {"stale", EvidenceState::Stale},       {"unsupported", EvidenceState::Unsupported},
      {"contradictory", EvidenceState::Contradictory},
      {"denied", EvidenceState::Denied},     {"unknown", EvidenceState::Unknown},
  };
  const auto it = kStates.find(text);
  if (it == kStates.end()) {
    return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                       "unrecognised evidence state '" + text + "'");
  }
  return it->second;
}

// Records the evidence a laboratory installation would read from the device before
// it actuates anything. Every record is explicitly labelled as coming from the
// synthetic adapter so that no report can present it as measured hardware data.
Status seed_lab_evidence(GeneratorControlEngine* engine, const GeneratorId& generator,
                         EpochMillis now, bool include_sync, bool include_transfer) {
  const Result<GeneratorState> inspected = engine->inspect(generator);
  if (!inspected.ok()) return inspected.status();
  const GeneratorState& state = inspected.value();

  auto record_check = [&](CheckKind kind, bool boolean_value, const char* detail) -> Status {
    CheckUpdate update{};
    update.generator = generator;
    update.controller = engine->controller();
    { const Result<GeneratorState> fresh = engine->inspect(generator); if (!fresh.ok()) return fresh.status(); update.revision = fresh.value().revision; }
    const Result<IdempotencyKey> key =
        IdempotencyKey::parse("lab-check-" + std::to_string(engine->commit_seq().value()) + "-" +
                              std::to_string(static_cast<unsigned>(kind)));
    if (!key.ok()) return key.status();
    update.idempotency_key = key.value();
    update.record.kind = kind;
    update.record.required = true;
    update.record.state = EvidenceState::Present;
    update.record.source = EvidenceSource::SyntheticAdapter;
    update.record.lifetime = EvidenceLifetime::VolatileObservation;
    update.record.observed_at = now;
    update.record.max_age_millis = 30 * kMillisPerMinute;
    update.record.value = TypedValue{TypedValueKind::Boolean, boolean_value ? 1 : 0};
    update.record.detail = detail;
    return engine->record_check(update);
  };

  for (const CheckKind kind : state.required_checks) {
    GENCTL_TRY(record_check(kind, true, "laboratory installation reading; SYNTHETIC"));
  }

  {
    ResourceUpdate update{};
    update.generator = generator;
    update.controller = engine->controller();
    { const Result<GeneratorState> fresh = engine->inspect(generator); if (!fresh.ok()) return fresh.status(); update.revision = fresh.value().revision; }
    const Result<IdempotencyKey> key =
        IdempotencyKey::parse("lab-resource-" + std::to_string(engine->commit_seq().value()));
    if (!key.ok()) return key.status();
    update.idempotency_key = key.value();
    const auto fill = [&](auto& evidence) {
      evidence.state = EvidenceState::Present;
      evidence.source = EvidenceSource::SyntheticAdapter;
      evidence.lifetime = EvidenceLifetime::VolatileObservation;
      evidence.observed_at = now;
    };
    update.set_fuel_level = true;
    update.fuel_level.value = FuelQuantity{FuelUnit::Millilitres, 900'000};
    fill(update.fuel_level);
    update.set_fuel_capacity = true;
    update.fuel_capacity.value = FuelQuantity{FuelUnit::Millilitres, 1'000'000};
    fill(update.fuel_capacity);
    update.set_fuel_consumption = true;
    update.fuel_consumption.value = FuelRate{FuelUnit::Millilitres, 60'000};
    fill(update.fuel_consumption);
    update.set_lube_oil_pressure = true;
    update.lube_oil_pressure.state = EvidenceState::Present;
    update.lube_oil_pressure.value = Pressure{410'000};
    update.lube_oil_pressure.source = EvidenceSource::SyntheticAdapter;
    update.lube_oil_pressure.observed_at = now;
    update.set_coolant_temperature = true;
    update.coolant_temperature.state = EvidenceState::Present;
    update.coolant_temperature.value = Temperature{78'000};
    update.coolant_temperature.source = EvidenceSource::SyntheticAdapter;
    update.coolant_temperature.observed_at = now;
    update.set_coolant_level = true;
    update.coolant_level.state = EvidenceState::Present;
    update.coolant_level.value = Percent{9'200};
    update.coolant_level.source = EvidenceSource::SyntheticAdapter;
    update.coolant_level.observed_at = now;
    update.set_battery_voltage = true;
    update.battery_voltage.state = EvidenceState::Present;
    update.battery_voltage.value = Voltage{25'600};
    update.battery_voltage.source = EvidenceSource::SyntheticAdapter;
    update.battery_voltage.observed_at = now;
    GENCTL_TRY(engine->record_resource(update));
  }

  if (include_sync) {
    const std::vector<SyncPreconditionKind> kinds = default_sync_preconditions(state.sync_policy);
    for (const SyncPreconditionKind kind : kinds) {
      SyncPreconditionUpdate update{};
      update.generator = generator;
      update.controller = engine->controller();
      { const Result<GeneratorState> fresh = engine->inspect(generator); if (!fresh.ok()) return fresh.status(); update.revision = fresh.value().revision; }
      const Result<IdempotencyKey> key = IdempotencyKey::parse(
          "lab-sync-" + std::to_string(engine->commit_seq().value()) + "-" +
          std::to_string(static_cast<unsigned>(kind)));
      if (!key.ok()) return key.status();
      update.idempotency_key = key.value();
      update.record.kind = kind;
      update.record.required = true;
      update.record.state = EvidenceState::Present;
      update.record.source = EvidenceSource::SyntheticAdapter;
      update.record.lifetime = EvidenceLifetime::VolatileObservation;
      update.record.observed_at = now;
      update.record.max_age_millis = 30 * kMillisPerMinute;
      switch (kind) {
        case SyncPreconditionKind::VoltageMatch:
          update.record.value = TypedValue{TypedValueKind::Millivolts, 1200};
          break;
        case SyncPreconditionKind::FrequencyMatch:
          update.record.value = TypedValue{TypedValueKind::Millihertz, 40};
          break;
        case SyncPreconditionKind::PhaseAngleMatch:
          update.record.value = TypedValue{TypedValueKind::Millidegrees, 1500};
          break;
        default:
          update.record.value = TypedValue{TypedValueKind::Boolean, 1};
          break;
      }
      update.record.detail = "laboratory installation reading; SYNTHETIC";
      GENCTL_TRY(engine->record_sync_precondition(update));
    }
  }

  if (include_transfer) {
    const Result<GeneratorState> refreshed = engine->inspect(generator);
    if (!refreshed.ok()) return refreshed.status();
    TransferPathUpdate path_update{};
    path_update.generator = generator;
    path_update.controller = engine->controller();
    { const Result<GeneratorState> fresh = engine->inspect(generator); if (!fresh.ok()) return fresh.status(); path_update.revision = fresh.value().revision; }
    const Result<IdempotencyKey> path_key =
        IdempotencyKey::parse("lab-path-" + std::to_string(engine->commit_seq().value()));
    if (!path_key.ok()) return path_key.status();
    path_update.idempotency_key = path_key.value();
    const Result<SwitchRef> breaker = SwitchRef::parse("generator-primary-breaker");
    const Result<SwitchRef> source = SwitchRef::parse("utility-bus-a");
    const Result<SwitchRef> target = SwitchRef::parse("generator-bus-a");
    const Result<SwitchRef> feeder = SwitchRef::parse("feeder-1");
    const Result<SwitchRef> path = SwitchRef::parse("path-utility-to-generator");
    if (!breaker.ok()) return breaker.status();
    if (!source.ok()) return source.status();
    if (!target.ok()) return target.status();
    if (!feeder.ok()) return feeder.status();
    if (!path.ok()) return path.status();
    path_update.path.breaker = breaker.value();
    path_update.path.source_bus = source.value();
    path_update.path.target_bus = target.value();
    path_update.path.feeder = feeder.value();
    path_update.path.transfer_path = path.value();
    GENCTL_TRY(engine->set_transfer_path(path_update));

    const Result<GeneratorState> refreshed2 = engine->inspect(generator);
    if (!refreshed2.ok()) return refreshed2.status();
    SwitchAuthorityUpdate token_update{};
    token_update.generator = generator;
    token_update.controller = engine->controller();
    { const Result<GeneratorState> fresh = engine->inspect(generator); if (!fresh.ok()) return fresh.status(); token_update.revision = fresh.value().revision; }
    const Result<IdempotencyKey> token_key =
        IdempotencyKey::parse("lab-token-" + std::to_string(engine->commit_seq().value()));
    if (!token_key.ok()) return token_key.status();
    token_update.idempotency_key = token_key.value();
    token_update.token.state = EvidenceState::Present;
    token_update.token.source = EvidenceSource::ExternalAuthority;
    token_update.token.lifetime = EvidenceLifetime::AttestedWithValidity;
    token_update.token.observed_at = now;
    token_update.token.valid_until = now + 30 * kMillisPerMinute;
    token_update.token.value.authority = SwitchRef::parse("lab-switch-authority").value();
    token_update.token.value.subject_switch = breaker.value();
    token_update.token.value.subject_generator = generator;
    token_update.token.value.subject_generation = refreshed2.value().generation;
    token_update.token.value.epoch = engine->controller().epoch;
    token_update.token.value.issued_at = now;
    token_update.token.value.valid_until = now + 30 * kMillisPerMinute;
    GENCTL_TRY(engine->record_switch_authority(token_update));

    for (const TransferPreconditionKind kind : default_transfer_preconditions()) {
      TransferPreconditionUpdate update{};
      update.generator = generator;
      update.controller = engine->controller();
      { const Result<GeneratorState> fresh = engine->inspect(generator); if (!fresh.ok()) return fresh.status(); update.revision = fresh.value().revision; }
      const Result<IdempotencyKey> key = IdempotencyKey::parse(
          "lab-transfer-" + std::to_string(engine->commit_seq().value()) + "-" +
          std::to_string(static_cast<unsigned>(kind)));
      if (!key.ok()) return key.status();
      update.idempotency_key = key.value();
      update.record.kind = kind;
      update.record.required = true;
      update.record.state = EvidenceState::Present;
      update.record.source = EvidenceSource::ExternalAuthority;
      update.record.lifetime = EvidenceLifetime::VolatileObservation;
      update.record.observed_at = now;
      update.record.max_age_millis = 30 * kMillisPerMinute;
      update.record.value = TypedValue{TypedValueKind::Boolean, 1};
      update.record.detail = "laboratory installation reading; SYNTHETIC";
      GENCTL_TRY(engine->record_transfer_precondition(update));
    }
  }
  return Status::success();
}

int run(int argc, char** argv) {
  if (argc < 2) {
    print_usage();
    return 2;
  }
  const std::string command = argv[1];
  if (command == "help" || command == "--help" || command == "-h") {
    print_usage();
    return 0;
  }
  if (command == "version") {
    std::printf("%s\n", version_banner().c_str());
    return 0;
  }
  if (command == "self-test") {
    const Status status = digest_self_test();
    if (!status.ok()) return fail(status);
    std::printf("self-test: ok (SHA-256 and CRC-32C match published vectors)\n");
    return 0;
  }

  Options options;
  for (int i = 2; i < argc; ++i) {
    const std::string token = argv[i];
    if (token.rfind("--", 0) != 0) {
      std::fprintf(stderr, "error: unexpected argument '%s'\n", token.c_str());
      return 2;
    }
    if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
      options.values[token] = argv[i + 1];
      ++i;
    } else {
      options.flags.push_back(token);
    }
  }

  const std::string store_directory = options.get("--store");
  if (store_directory.empty()) {
    std::fprintf(stderr, "error: --store <dir> is required\n");
    return 2;
  }

  SystemClock clock;
  const EpochMillis now = options.millis("--now", clock.now_millis());
  const EpochMillis evidence_at = options.millis("--at", now);

  // ---- synthetic adapter -------------------------------------------------
  SyntheticAdapter::Options adapter_options{};
  adapter_options.journal_path = options.get("--adapter-journal");
  if (adapter_options.journal_path.empty()) {
    // Laboratory device state lives beside the store, in its own directory, so that
    // the store audit never has to reason about adapter artefacts.
    adapter_options.journal_path = store_directory + "\\lab-device\\synthetic-device.jrnl";
  }
  adapter_options.profile.hardware_generation = HardwareGeneration{1};
  adapter_options.profile.crank_millis = options.number("--crank-ms", 4000);
  adapter_options.profile.warmup_millis = options.number("--warmup-ms", 6000);
  adapter_options.profile.cooldown_millis = options.number("--cooldown-ms", 3000);
  if (options.has("--fault-ack-without-effect")) adapter_options.faults.ack_without_effect = true;
  if (options.has("--fault-stuck-cranking")) adapter_options.faults.stuck_cranking = true;
  if (options.has("--fault-observation-unavailable")) {
    adapter_options.faults.observation_unavailable = true;
  }
  if (options.has("--fault-contradictory")) adapter_options.faults.contradictory_observation = true;
  if (options.has("--fault-reject")) adapter_options.faults.reject_next = true;
  if (options.has("--fault-busy")) adapter_options.faults.busy = true;

  const std::string generator_text = options.get("--generator");
  GeneratorId generator{};
  if (!generator_text.empty()) {
    const Result<GeneratorId> parsed = GeneratorId::parse(generator_text);
    if (!parsed.ok()) return fail(parsed.status());
    generator = parsed.value();
  }

  SyntheticAdapter adapter(generator, clock, adapter_options);

  OpenOptions open_options{};
  open_options.store_directory = store_directory;
  open_options.read_only = false;
  open_options.create_if_missing = command == "init";
  open_options.recover_scan = options.has("--recover-scan");
  open_options.accept_rollback = options.has("--accept-rollback");
  open_options.rollback_acceptance_note = options.get("--note");
  open_options.requested_epoch = ControlEpoch{
      static_cast<std::uint64_t>(options.number("--epoch", 0))};
  open_options.config.journal.idempotency_window =
      static_cast<std::size_t>(options.number("--idempotency-window", 1024));
  open_options.config.journal.max_attempts =
      static_cast<std::size_t>(options.number("--max-attempts", 256));
  open_options.config.effect_settle_millis = options.number("--settle-ms", 20000);
  {
    const Result<CrashPoint> point = parse_crash_point(options.get("--crash-at", "none"));
    if (!point.ok()) return fail(point.status());
    open_options.config.crash_point = point.value();
  }
  if (options.has("--allow-synthetic-refusal")) open_options.config.evidence.allow_synthetic = false;
  if (command == "store-audit" && options.has("--read-only")) open_options.read_only = true;

  Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(open_options, clock, &adapter);
  if (!opened.ok()) return fail(opened.status());
  std::unique_ptr<GeneratorControlEngine> engine = std::move(opened.value());

  const Result<std::vector<GeneratorId>> known = engine->list_generators();
  if (!known.ok()) return fail(known.status());

  auto require_generator = [&]() -> bool {
    if (!generator.empty()) return true;
    std::fprintf(stderr, "error: --generator <id> is required for '%s'\n", command.c_str());
    return false;
  };

  auto current_revision = [&](StateRevision* out) -> Status {
    const Result<GeneratorState> state = engine->inspect(generator);
    if (!state.ok()) return state.status();
    *out = state.value().revision;
    return Status::success();
  };

  // ---- commands ----------------------------------------------------------
  if (command == "init") {
    const Result<StoreAuditReport> report = engine->store_audit(false, {});
    if (!report.ok()) return fail(report.status());
    std::printf("store: %s\n", engine->store_directory().c_str());
    std::printf("  created: %s\n", report.value().head_present ? "no (already existed)" : "yes");
    std::printf("  commit-sequence: %llu\n",
                static_cast<unsigned long long>(report.value().head_seq.value()));
    std::printf("  writer-incarnation: %llu\n",
                static_cast<unsigned long long>(engine->reopen_report().incarnation.value()));
    std::printf("  control-epoch: %llu\n",
                static_cast<unsigned long long>(engine->reopen_report().epoch.value()));
    return 0;
  }

  if (command == "store-audit") {
    const Result<StoreAuditReport> report =
        engine->store_audit(options.has("--accept-rollback"), options.get("--note"));
    if (!report.ok()) return fail(report.status());
    const StoreAuditReport& value = report.value();
    std::printf("store-audit: %s\n", value.directory.c_str());
    std::printf("  summary: %s\n", value.summary.c_str());
    std::printf("  writer-lock-held: %s\n", value.writer_lock_held ? "yes" : "no");
    std::printf("  read-only: %s\n", value.read_only ? "yes" : "no");
    std::printf("  head: %llu %s\n", static_cast<unsigned long long>(value.head_seq.value()),
                value.head_digest.short_hex().c_str());
    std::printf("  fence: %llu\n", static_cast<unsigned long long>(value.fence_seq.value()));
    std::printf("  lease: epoch %llu incarnation %llu pid %u token %s\n",
                static_cast<unsigned long long>(value.lease.epoch.value()),
                static_cast<unsigned long long>(value.lease.incarnation.value()),
                value.lease.process_id, value.lease.process_token.c_str());
    std::printf("  generations: %zu valid of %zu scanned\n", value.generations_valid,
                value.generations_scanned);
    std::printf("  residue-retired: %zu\n", value.residue);
    for (const auto& generation : value.generations) {
      std::printf("  generation %s seq=%llu bytes=%llu valid=%s %s\n", generation.name.c_str(),
                  static_cast<unsigned long long>(generation.commit_seq.value()),
                  static_cast<unsigned long long>(generation.size_bytes),
                  generation.valid ? "yes" : "no", generation.detail.c_str());
    }
    for (const auto& anomaly : value.anomalies) {
      std::printf("  anomaly: %s\n", anomaly.c_str());
    }
    std::printf("reopen: incarnation %llu demoted %zu residue %zu attempts %zu unresolved %zu\n",
                static_cast<unsigned long long>(engine->reopen_report().incarnation.value()),
                engine->reopen_report().demoted_evidence_records,
                engine->reopen_report().residue_retired,
                engine->reopen_report().attempts_recovered,
                engine->reopen_report().unresolved_attempts);
    for (const auto& note : engine->reopen_report().notes) {
      std::printf("  note: %s\n", note.c_str());
    }
    return 0;
  }

  if (command == "key-window") {
    if (options.has("--accept")) {
      const Status status = engine->acknowledge_key_window(
          options.get("--authority-by", "operator"), options.get("--reason", "operator accepted "
          "the idempotency window horizon"), now);
      if (!status.ok()) return fail(status);
      std::printf("key-window: acknowledged at commit %llu\n",
                  static_cast<unsigned long long>(engine->commit_seq().value()));
      return 0;
    }
    const Result<KeyWindowAcknowledgement> acknowledgement =
        engine->key_window_acknowledgement();
    if (!acknowledgement.ok()) return fail(acknowledgement.status());
    const Result<std::size_t> evicted = engine->count_evicted_keys();
    if (!evicted.ok()) return fail(evicted.status());
    std::printf("key-window:\n");
    std::printf("  acknowledged-at-commit: %llu\n",
                static_cast<unsigned long long>(acknowledgement.value().acknowledged_at_seq.value()));
    std::printf("  acknowledged-by: %s\n", acknowledgement.value().acknowledged_by.c_str());
    std::printf("  evicted-keys: %zu\n", evicted.value());
    return 0;
  }

  if (command == "register") {
    if (!require_generator()) return 2;
    RegisterOptions register_options{};
    register_options.hardware_generation = HardwareGeneration{
        static_cast<std::uint32_t>(options.number("--hardware", 1))};
    register_options.commissioned = true;
    register_options.requirement.reserve = FuelQuantity{
        FuelUnit::Millilitres, options.number("--reserve-ml", 200000)};
    register_options.requirement.required_runtime_seconds = options.number("--required-runtime", 1800);
    const Result<GeneratorState> state = engine->register_generator(generator, register_options, now);
    if (!state.ok()) return fail(state.status());
    print_state(state.value());
    return 0;
  }

  if (command == "inspect") {
    if (!require_generator()) return 2;
    const Result<GeneratorState> state = engine->inspect(generator);
    if (!state.ok()) return fail(state.status());
    print_state(state.value());
    std::printf("  required-checks: %zu\n", state.value().required_checks.size());
    std::printf("  recorded-checks: %zu\n", state.value().checks.size());
    std::printf("  sync-preconditions: %zu\n", state.value().sync_preconditions.size());
    std::printf("  transfer-preconditions: %zu\n", state.value().transfer_preconditions.size());
    std::printf("  transfer-path-breaker: %s\n",
                state.value().transfer_path.breaker.empty()
                    ? "(unset)"
                    : state.value().transfer_path.breaker.str().c_str());
    std::printf("  switch-authority: %s\n",
                std::string(to_string(state.value().switch_authority.state)).c_str());
    std::printf("  commit-sequence: %llu\n",
                static_cast<unsigned long long>(engine->commit_seq().value()));
    std::printf("  controller: %s\n", to_string(engine->controller()).c_str());
    return 0;
  }

  if (command == "attempts") {
    if (!require_generator()) return 2;
    const Result<std::vector<AttemptRecord>> records = engine->attempts(generator);
    if (!records.ok()) return fail(records.status());
    std::printf("attempts: %zu\n", records.value().size());
    for (const auto& record : records.value()) {
      std::printf("  #%llu %-22s command=%-12s ack=%-11s effect=%-22s key=%s\n",
                  static_cast<unsigned long long>(record.id.value()),
                  std::string(to_string(record.operation)).c_str(),
                  std::string(to_string(record.command_state)).c_str(),
                  std::string(to_string(record.ack_status)).c_str(),
                  std::string(to_string(record.effect_state)).c_str(), record.key.str().c_str());
    }
    return 0;
  }

  if (command == "history") {
    if (!require_generator()) return 2;
    const Result<std::vector<HistoryEntry>> entries =
        engine->history(generator, static_cast<std::size_t>(options.number("--limit", 0)));
    if (!entries.ok()) return fail(entries.status());
    std::printf("history: %zu\n", entries.value().size());
    for (const auto& entry : entries.value()) {
      std::printf("  #%llu %s %-22s %s -> %s (lifecycle %s -> %s) mode=%s %s\n",
                  static_cast<unsigned long long>(entry.seq.value()),
                  now_string(entry.at).c_str(),
                  std::string(to_string(entry.operation)).c_str(),
                  std::string(to_string(entry.from_operating)).c_str(),
                  std::string(to_string(entry.to_operating)).c_str(),
                  std::string(to_string(entry.from_lifecycle)).c_str(),
                  std::string(to_string(entry.to_lifecycle)).c_str(),
                  std::string(to_string(entry.mode)).c_str(),
                  entry.provisional ? "(provisional)" : "");
    }
    const Result<std::vector<AuthorityAuditEntry>> audit = engine->authority_audit(generator);
    if (!audit.ok()) return fail(audit.status());
    if (!audit.value().empty()) {
      std::printf("authority-audit: %zu\n", audit.value().size());
      for (const auto& entry : audit.value()) {
        std::printf("  #%llu %s %-20s class=%-9s by=%s reason=%s waived=%zu\n",
                    static_cast<unsigned long long>(entry.seq.value()),
                    now_string(entry.at).c_str(),
                    std::string(to_string(entry.operation)).c_str(),
                    std::string(to_string(entry.cls)).c_str(), entry.granted_by.c_str(),
                    entry.reason.c_str(), entry.waived_checks.size());
      }
    }
    return 0;
  }

  if (command == "readiness") {
    if (!require_generator()) return 2;
    const Result<ReadinessReport> report = engine->readiness(generator, now);
    if (!report.ok()) return fail(report.status());
    print_readiness(report.value());
    return 0;
  }

  if (command == "resource-report") {
    if (!require_generator()) return 2;
    const Result<ResourceAssessment> assessment = engine->resource_assessment(generator, now);
    if (!assessment.ok()) return fail(assessment.status());
    print_resources(assessment.value());
    return 0;
  }

  if (command == "synchronization") {
    if (!require_generator()) return 2;
    const Result<SynchronizationEligibility> eligibility = engine->sync_eligibility(generator, now);
    if (!eligibility.ok()) return fail(eligibility.status());
    print_sync(eligibility.value());
    return 0;
  }

  if (command == "transfer") {
    if (!require_generator()) return 2;
    const Result<TransferEligibility> eligibility = engine->transfer_eligibility(generator, now);
    if (!eligibility.ok()) return fail(eligibility.status());
    print_transfer(eligibility.value());
    return 0;
  }

  if (command == "evaluate" || command == "act") {
    if (!require_generator()) return 2;
    const Result<OperationKind> operation = parse_operation_kind(options.get("--operation"));
    if (!operation.ok()) return fail(operation.status());
    if (options.has("--seed-synthetic-evidence")) {
      // Reads the laboratory installation and records the resulting evidence inside
      // this controller incarnation, exactly as a controller would between start-up
      // and its first command. Everything it records is labelled SYNTHETIC.
      const Status seeded = seed_lab_evidence(
          engine.get(), generator, now, requires_observed_effect(operation.value()) &&
                                     (operation.value() == OperationKind::Synchronize ||
                                      operation.value() == OperationKind::Desynchronize),
          requires_transfer_authority(operation.value()));
      if (!seeded.ok()) return fail(seeded);
      std::printf("evidence: seeded from the synthetic installation at %s (SYNTHETIC)\n",
                  now_string(now).c_str());
    }
    StateRevision revision{};
    if (options.get("--revision", "auto") == "auto") {
      const Status status = current_revision(&revision);
      if (!status.ok()) return fail(status);
    } else {
      revision = StateRevision{static_cast<std::uint64_t>(options.number("--revision", 0))};
    }
    const Result<GeneratorState> state = engine->inspect(generator);
    if (!state.ok()) return fail(state.status());
    OperationRequest request{};
    request.generator = generator;
    request.operation = operation.value();
    request.controller = engine->controller();
    request.generation = state.value().generation;
    request.revision = revision;
    request.authority = build_authority(options, engine->controller().epoch, now);
    std::string key = options.get("--key");
    if (key.empty()) {
      key = "auto-" + std::to_string(engine->commit_seq().value()) + "-" +
            std::string(to_string(operation.value()));
    }
    const Result<IdempotencyKey> parsed_key = IdempotencyKey::parse(key);
    if (!parsed_key.ok()) return fail(parsed_key.status());
    request.idempotency_key = parsed_key.value();
    request.requested_at = now;
    request.note = options.get("--note");

    if (command == "evaluate") {
      const Result<EvaluationReport> report = engine->evaluate(request);
      if (!report.ok()) return fail(report.status());
      const EvaluationReport& value = report.value();
      std::printf("evaluation: %s\n", value.permitted ? "permitted" : "refused");
      std::printf("  operation: %s\n", std::string(to_string(value.operation)).c_str());
      std::printf("  generator: %s\n", value.generator.str().c_str());
      std::printf("  revision: %llu\n",
                  static_cast<unsigned long long>(value.revision.value()));
      std::printf("  advisory-waived: %s\n", value.advisory_waived ? "yes" : "no");
      if (!value.permitted) {
        std::printf("  primary-error: %s [%s]\n",
                    std::string(to_string(value.primary_error)).c_str(),
                    std::string(to_string(value.primary_stage)).c_str());
        std::printf("  primary-detail: %s\n", value.primary_detail.c_str());
      }
      for (const auto& stage : value.stages) {
        std::printf("  stage %-16s %-28s %s\n", std::string(to_string(stage.stage)).c_str(),
                    std::string(to_string(stage.code)).c_str(), stage.detail.c_str());
      }
      return value.permitted ? 0 : exit_code(value.primary_error);
    }

    const Result<AttemptRecord> attempt = engine->execute(request, false);
    if (!attempt.ok()) return fail(attempt.status());
    print_attempt(attempt.value());
    return 0;
  }

  if (command == "observe" || command == "verify") {
    if (!require_generator()) return 2;
    ObserveRequest request{};
    request.generator = generator;
    request.controller = engine->controller();
    request.requested_at = now;
    if (command == "verify") {
      request.resolve_attempt = true;
      request.attempt = AttemptId{static_cast<std::uint64_t>(options.number("--attempt", 0))};
    }
    const Result<ObserveOutcome> outcome = engine->observe(request);
    if (!outcome.ok()) return fail(outcome.status());
    std::printf("observation: %s\n", generator.str().c_str());
    std::printf("  observed-at: %s\n", now_string(outcome.value().observation.observed_at).c_str());
    std::printf("  source: %s\n",
                std::string(to_string(outcome.value().observation.source)).c_str());
    std::printf("  reported-state: %s\n",
                std::string(to_string(outcome.value().observation.reported_state)).c_str());
    std::printf("  synchronization: %s\n",
                std::string(to_string(outcome.value().observation.synchronization)).c_str());
    std::printf("  breaker: %s\n",
                std::string(to_string(outcome.value().observation.breaker)).c_str());
    std::printf("  state-revision: %llu\n",
                static_cast<unsigned long long>(outcome.value().revision.value()));
    if (outcome.value().attempt_resolved) {
      print_attempt(outcome.value().attempt);
    }
    return 0;
  }

  if (command == "abandon") {
    const Result<AttemptRecord> attempt = engine->abandon(
        AttemptId{static_cast<std::uint64_t>(options.number("--attempt", 0))},
        options.get("--reason"), now);
    if (!attempt.ok()) return fail(attempt.status());
    print_attempt(attempt.value());
    return 0;
  }

  if (command == "revalidate") {
    if (!require_generator()) return 2;
    const Result<RevalidateReport> report = engine->revalidate(generator, now);
    if (!report.ok()) return fail(report.status());
    std::printf("revalidate: %s\n", generator.str().c_str());
    std::printf("  revision: %llu\n", static_cast<unsigned long long>(report.value().revision.value()));
    std::printf("  demoted-evidence-records: %zu\n", report.value().demoted_evidence_records);
    for (const auto& note : report.value().notes) std::printf("  note: %s\n", note.c_str());
    print_readiness(report.value().readiness);
    print_resources(report.value().resources);
    print_sync(report.value().synchronization);
    print_transfer(report.value().transfer);
    return 0;
  }

  if (command == "check") {
    if (!require_generator()) return 2;
    StateRevision revision{};
    { const Status revision_status = current_revision(&revision); if (!revision_status.ok()) return fail(revision_status); }
    const Result<CheckKind> kind = parse_check_kind(options.get("--kind"));
    if (!kind.ok()) return fail(kind.status());
    const Result<EvidenceState> state_value = parse_state(options.get("--evidence", "present"));
    if (!state_value.ok()) return fail(state_value.status());
    CheckUpdate update{};
    update.generator = generator;
    update.controller = engine->controller();
    update.revision = revision;
    std::string key = options.get("--key", "check-" + std::string(to_string(kind.value())));
    const Result<IdempotencyKey> parsed_key = IdempotencyKey::parse(key);
    if (!parsed_key.ok()) return fail(parsed_key.status());
    update.idempotency_key = parsed_key.value();
    update.record.kind = kind.value();
    update.record.required = true;
    update.record.state = state_value.value();
    update.record.source = parse_evidence_source(options.get("--source", "vendor-adapter"))
                               .value_or(EvidenceSource::VendorAdapter);
    update.record.lifetime = parse_lifetime(options.get("--lifetime", "volatile"));
    update.record.observed_at = evidence_at;
    update.record.valid_until = options.millis("--valid-until", 0);
    update.record.max_age_millis = options.number("--max-age", 60000);
    update.record.value = build_value(options);
    update.record.detail = options.get("--detail");
    const Status status = engine->record_check(update);
    if (!status.ok()) return fail(status);
    std::printf("check: %s recorded as %s at %s\n", std::string(to_string(kind.value())).c_str(),
                std::string(to_string(state_value.value())).c_str(),
                now_string(evidence_at).c_str());
    return 0;
  }

  if (command == "resource") {
    if (!require_generator()) return 2;
    StateRevision revision{};
    { const Status revision_status = current_revision(&revision); if (!revision_status.ok()) return fail(revision_status); }
    ResourceUpdate update{};
    update.generator = generator;
    update.controller = engine->controller();
    update.revision = revision;
    const Result<IdempotencyKey> parsed_key =
        IdempotencyKey::parse(options.get("--key", "resource-" +
                                              std::to_string(engine->commit_seq().value())));
    if (!parsed_key.ok()) return fail(parsed_key.status());
    update.idempotency_key = parsed_key.value();
    const Result<EvidenceSource> source = parse_evidence_source(
        options.get("--source", "vendor-adapter"));
    if (!source.ok()) return fail(source.status());
    const EvidenceSource evidence_source = source.value();
    const EvidenceLifetime lifetime = parse_lifetime(options.get("--lifetime", "volatile"));
    const std::string unit_text = options.get("--unit", "mL");
    const Result<FuelUnit> unit = parse_fuel_unit(unit_text);
    if (!unit.ok()) return fail(unit.status());
    if (options.has("--fuel-level")) {
      update.set_fuel_level = true;
      update.fuel_level.state = parse_state(options.get("--evidence", "present")).value_or(
          EvidenceState::Present);
      update.fuel_level.value = FuelQuantity{unit.value(), options.number("--fuel-level", 0)};
      update.fuel_level.source = evidence_source;
      update.fuel_level.lifetime = lifetime;
      update.fuel_level.observed_at = evidence_at;
      update.fuel_level.detail = options.get("--detail");
    }
    if (options.has("--fuel-capacity")) {
      update.set_fuel_capacity = true;
      update.fuel_capacity.state = EvidenceState::Present;
      update.fuel_capacity.value = FuelQuantity{unit.value(), options.number("--fuel-capacity", 0)};
      update.fuel_capacity.source = evidence_source;
      update.fuel_capacity.lifetime = lifetime;
      update.fuel_capacity.observed_at = evidence_at;
    }
    if (options.has("--fuel-rate")) {
      update.set_fuel_consumption = true;
      update.fuel_consumption.state = parse_state(options.get("--evidence", "present")).value_or(
          EvidenceState::Present);
      update.fuel_consumption.value = FuelRate{unit.value(), options.number("--fuel-rate", 0)};
      update.fuel_consumption.source = evidence_source;
      update.fuel_consumption.lifetime = lifetime;
      update.fuel_consumption.observed_at = evidence_at;
      update.fuel_consumption.detail = options.get("--detail");
    }
    if (options.has("--lube-oil-pressure")) {
      update.set_lube_oil_pressure = true;
      update.lube_oil_pressure.state = EvidenceState::Present;
      update.lube_oil_pressure.value = Pressure{options.number("--lube-oil-pressure", 0)};
      update.lube_oil_pressure.source = evidence_source;
      update.lube_oil_pressure.lifetime = lifetime;
      update.lube_oil_pressure.observed_at = evidence_at;
    }
    if (options.has("--coolant-temperature")) {
      update.set_coolant_temperature = true;
      update.coolant_temperature.state = EvidenceState::Present;
      update.coolant_temperature.value = Temperature{options.number("--coolant-temperature", 0)};
      update.coolant_temperature.source = evidence_source;
      update.coolant_temperature.lifetime = lifetime;
      update.coolant_temperature.observed_at = evidence_at;
    }
    if (options.has("--coolant-level")) {
      update.set_coolant_level = true;
      update.coolant_level.state = EvidenceState::Present;
      update.coolant_level.value = Percent{options.number("--coolant-level", 0)};
      update.coolant_level.source = evidence_source;
      update.coolant_level.lifetime = lifetime;
      update.coolant_level.observed_at = evidence_at;
    }
    if (options.has("--battery-voltage")) {
      update.set_battery_voltage = true;
      update.battery_voltage.state = EvidenceState::Present;
      update.battery_voltage.value = Voltage{options.number("--battery-voltage", 0)};
      update.battery_voltage.source = evidence_source;
      update.battery_voltage.lifetime = lifetime;
      update.battery_voltage.observed_at = evidence_at;
    }
    const Status status = engine->record_resource(update);
    if (!status.ok()) return fail(status);
    std::printf("resource: evidence recorded at %s\n", now_string(evidence_at).c_str());
    return 0;
  }

  if (command == "sync-precondition") {
    if (!require_generator()) return 2;
    StateRevision revision{};
    { const Status revision_status = current_revision(&revision); if (!revision_status.ok()) return fail(revision_status); }
    const Result<SyncPreconditionKind> kind =
        parse_sync_precondition_kind(options.get("--kind"));
    if (!kind.ok()) return fail(kind.status());
    SyncPreconditionUpdate update{};
    update.generator = generator;
    update.controller = engine->controller();
    update.revision = revision;
    const Result<IdempotencyKey> parsed_key = IdempotencyKey::parse(
        options.get("--key", "sync-" + std::string(to_string(kind.value()))));
    if (!parsed_key.ok()) return fail(parsed_key.status());
    update.idempotency_key = parsed_key.value();
    update.record.kind = kind.value();
    update.record.required = true;
    update.record.state = parse_state(options.get("--evidence", "present")).value_or(
        EvidenceState::Present);
    update.record.source = parse_evidence_source(options.get("--source", "vendor-adapter"))
                               .value_or(EvidenceSource::VendorAdapter);
    update.record.lifetime = parse_lifetime(options.get("--lifetime", "volatile"));
    update.record.observed_at = evidence_at;
    update.record.max_age_millis = options.number("--max-age", 5000);
    update.record.value = build_value(options);
    update.record.detail = options.get("--detail");
    const Status status = engine->record_sync_precondition(update);
    if (!status.ok()) return fail(status);
    std::printf("sync-precondition: %s recorded\n", std::string(to_string(kind.value())).c_str());
    return 0;
  }

  if (command == "transfer-precondition") {
    if (!require_generator()) return 2;
    StateRevision revision{};
    { const Status revision_status = current_revision(&revision); if (!revision_status.ok()) return fail(revision_status); }
    const Result<TransferPreconditionKind> kind =
        parse_transfer_precondition_kind(options.get("--kind"));
    if (!kind.ok()) return fail(kind.status());
    TransferPreconditionUpdate update{};
    update.generator = generator;
    update.controller = engine->controller();
    update.revision = revision;
    const Result<IdempotencyKey> parsed_key = IdempotencyKey::parse(
        options.get("--key", "transfer-" + std::string(to_string(kind.value()))));
    if (!parsed_key.ok()) return fail(parsed_key.status());
    update.idempotency_key = parsed_key.value();
    update.record.kind = kind.value();
    update.record.required = true;
    update.record.state = parse_state(options.get("--evidence", "present")).value_or(
        EvidenceState::Present);
    update.record.source = parse_evidence_source(options.get("--source", "external-authority"))
                               .value_or(EvidenceSource::ExternalAuthority);
    update.record.observed_at = evidence_at;
    update.record.max_age_millis = options.number("--max-age", 5000);
    update.record.value = build_value(options);
    update.record.detail = options.get("--detail");
    const Status status = engine->record_transfer_precondition(update);
    if (!status.ok()) return fail(status);
    std::printf("transfer-precondition: %s recorded\n",
                std::string(to_string(kind.value())).c_str());
    return 0;
  }

  if (command == "switch-authority") {
    if (!require_generator()) return 2;
    StateRevision revision{};
    { const Status revision_status = current_revision(&revision); if (!revision_status.ok()) return fail(revision_status); }
    SwitchAuthorityUpdate update{};
    update.generator = generator;
    update.controller = engine->controller();
    update.revision = revision;
    const Result<IdempotencyKey> parsed_key = IdempotencyKey::parse(
        options.get("--key", "switch-authority-" + std::to_string(engine->commit_seq().value())));
    if (!parsed_key.ok()) return fail(parsed_key.status());
    update.idempotency_key = parsed_key.value();
    update.token.state = parse_state(options.get("--evidence", "present")).value_or(
        EvidenceState::Present);
    update.token.source = EvidenceSource::ExternalAuthority;
    update.token.lifetime = EvidenceLifetime::AttestedWithValidity;
    update.token.observed_at = evidence_at;
    update.token.valid_until = options.millis("--valid-until", 0);
    const Result<SwitchRef> authority = SwitchRef::parse(options.get("--authority-name", "switch-authority"));
    if (!authority.ok()) return fail(authority.status());
    const Result<SwitchRef> subject = SwitchRef::parse(options.get("--switch", "breaker-1"));
    if (!subject.ok()) return fail(subject.status());
    update.token.value.authority = authority.value();
    update.token.value.subject_switch = subject.value();
    update.token.value.subject_generator = generator;
    const Result<GeneratorState> current = engine->inspect(generator);
    if (!current.ok()) return fail(current.status());
    update.token.value.subject_generation = current.value().generation;
    update.token.value.epoch = engine->controller().epoch;
    update.token.value.issued_at = evidence_at;
    update.token.value.valid_until = options.millis("--valid-until", 0);
    const Status status = engine->record_switch_authority(update);
    if (!status.ok()) return fail(status);
    std::printf("switch-authority: token recorded\n");
    return 0;
  }

  if (command == "transfer-path") {
    if (!require_generator()) return 2;
    StateRevision revision{};
    { const Status revision_status = current_revision(&revision); if (!revision_status.ok()) return fail(revision_status); }
    TransferPathUpdate update{};
    update.generator = generator;
    update.controller = engine->controller();
    update.revision = revision;
    const Result<IdempotencyKey> parsed_key = IdempotencyKey::parse(
        options.get("--key", "transfer-path-" + std::to_string(engine->commit_seq().value())));
    if (!parsed_key.ok()) return fail(parsed_key.status());
    update.idempotency_key = parsed_key.value();
    const std::pair<const char*, SwitchRef*> fields[] = {
        {"--breaker", &update.path.breaker},
        {"--source-bus", &update.path.source_bus},
        {"--target-bus", &update.path.target_bus},
        {"--feeder", &update.path.feeder},
        {"--path", &update.path.transfer_path},
    };
    for (const auto& field : fields) {
      const std::string text = options.get(field.first);
      if (text.empty()) continue;
      const Result<SwitchRef> parsed = SwitchRef::parse(text);
      if (!parsed.ok()) return fail(parsed.status());
      *field.second = parsed.value();
    }
    const Status status = engine->set_transfer_path(update);
    if (!status.ok()) return fail(status);
    std::printf("transfer-path: references recorded\n");
    return 0;
  }

  if (command == "authority") {
    if (!require_generator()) return 2;
    StateRevision revision{};
    { const Status revision_status = current_revision(&revision); if (!revision_status.ok()) return fail(revision_status); }
    AuthorityUpdate update{};
    update.generator = generator;
    update.controller = engine->controller();
    update.revision = revision;
    const Result<IdempotencyKey> parsed_key = IdempotencyKey::parse(
        options.get("--key", "authority-" + std::to_string(engine->commit_seq().value())));
    if (!parsed_key.ok()) return fail(parsed_key.status());
    update.idempotency_key = parsed_key.value();
    update.grant = build_authority(options, engine->controller().epoch, now);
    update.grant.granted_at = now;
    const Status status = engine->grant_authority(update);
    if (!status.ok()) return fail(status);
    std::printf("authority: %s grant recorded\n",
                std::string(to_string(update.grant.cls)).c_str());
    return 0;
  }

  if (command == "mode") {
    if (!require_generator()) return 2;
    StateRevision revision{};
    { const Status revision_status = current_revision(&revision); if (!revision_status.ok()) return fail(revision_status); }
    const Result<OperatingMode> mode = parse_operating_mode(options.get("--mode"));
    if (!mode.ok()) return fail(mode.status());
    ModeUpdate update{};
    update.generator = generator;
    update.controller = engine->controller();
    update.revision = revision;
    const Result<IdempotencyKey> parsed_key = IdempotencyKey::parse(
        options.get("--key", "mode-" + std::string(to_string(mode.value())) + "-" +
                                 std::to_string(engine->commit_seq().value())));
    if (!parsed_key.ok()) return fail(parsed_key.status());
    update.idempotency_key = parsed_key.value();
    update.mode = mode.value();
    update.authority = build_authority(options, engine->controller().epoch, now);
    update.requested_at = now;
    const Status status = engine->update_mode(update);
    if (!status.ok()) return fail(status);
    std::printf("mode: %s\n", std::string(to_string(mode.value())).c_str());
    return 0;
  }

  if (command == "lifecycle") {
    if (!require_generator()) return 2;
    StateRevision revision{};
    { const Status revision_status = current_revision(&revision); if (!revision_status.ok()) return fail(revision_status); }
    const Result<LifecycleState> lifecycle = parse_lifecycle_state(options.get("--lifecycle"));
    if (!lifecycle.ok()) return fail(lifecycle.status());
    LifecycleUpdate update{};
    update.generator = generator;
    update.controller = engine->controller();
    update.revision = revision;
    update.lifecycle = lifecycle.value();
    const Result<IdempotencyKey> parsed_key = IdempotencyKey::parse(
        options.get("--key", "lifecycle-" + std::string(to_string(lifecycle.value())) + "-" +
                                 std::to_string(engine->commit_seq().value())));
    if (!parsed_key.ok()) return fail(parsed_key.status());
    update.idempotency_key = parsed_key.value();
    update.authority = build_authority(options, engine->controller().epoch, now);
    update.reason = options.get("--note");
    update.requested_at = now;
    const Status status = engine->update_lifecycle(update);
    if (!status.ok()) return fail(status);
    std::printf("lifecycle: %s\n", std::string(to_string(lifecycle.value())).c_str());
    return 0;
  }

  std::fprintf(stderr, "error: unknown command '%s'\n", command.c_str());
  print_usage();
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "error: internal failure: %s\n", error.what());
    return 1;
  }
}
