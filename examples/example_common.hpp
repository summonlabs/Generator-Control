// Shared scaffolding for the Generator Control examples. Examples exercise the
// public API exactly as a deployment would; none of them reaches into the runtime.
#pragma once

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "genctl/adapter.hpp"
#include "genctl/authority.hpp"
#include "genctl/engine.hpp"
#include "genctl/path_safety.hpp"
#include "genctl/platform.hpp"

namespace example {

using namespace genctl;

inline std::string scratch_directory(const std::string& label) {
  const Result<std::string> temp = platform::temporary_directory();
  const std::string base = temp.ok() ? temp.value() : std::string(".");
  return base + "\\genctl-examples\\" + label;
}

inline void print_attempt(const AttemptRecord& attempt) {
  std::printf("  attempt %llu  command=%-12s ack=%-9s effect=%-22s authority=%s\n",
              static_cast<unsigned long long>(attempt.id.value()),
              std::string(to_string(attempt.command_state)).c_str(),
              std::string(to_string(attempt.ack_status)).c_str(),
              std::string(to_string(attempt.effect_state)).c_str(),
              std::string(to_string(attempt.authority)).c_str());
}

inline void print_status(const char* what, const Status& status) {
  std::printf("  %-34s %s\n", what, status.ok() ? "ok" : status.to_string().c_str());
}

// Records the evidence a laboratory installation would read before it acts. Every
// record is labelled as coming from the synthetic adapter.
inline Status seed_evidence(GeneratorControlEngine& engine, const GeneratorId& id,
                            bool include_sync, bool include_transfer,
                            EpochMillis observed_at = 0, Millis max_age = 60 * kMillisPerMinute) {
  const Result<GeneratorState> inspected = engine.inspect(id);
  if (!inspected.ok()) return inspected.status();

  for (const CheckKind kind : inspected.value().required_checks) {
    const Result<GeneratorState> fresh = engine.inspect(id);
    if (!fresh.ok()) return fresh.status();
    CheckUpdate update{};
    update.generator = id;
    update.controller = engine.controller();
    update.revision = fresh.value().revision;
    const Result<IdempotencyKey> key = IdempotencyKey::parse(
        "example-check-" + std::to_string(engine.commit_seq().value()) + "-" +
        std::to_string(static_cast<unsigned>(kind)));
    if (!key.ok()) return key.status();
    update.idempotency_key = key.value();
    update.record.kind = kind;
    update.record.required = true;
    update.record.state = EvidenceState::Present;
    update.record.source = EvidenceSource::SyntheticAdapter;
    update.record.observed_at = observed_at;
    update.record.max_age_millis = max_age;
    update.record.value = TypedValue{TypedValueKind::Boolean, 1};
    const Status status = engine.record_check(update);
    if (!status.ok()) return status;
  }

  {
    const Result<GeneratorState> fresh = engine.inspect(id);
    if (!fresh.ok()) return fresh.status();
    ResourceUpdate update{};
    update.generator = id;
    update.controller = engine.controller();
    update.revision = fresh.value().revision;
    const Result<IdempotencyKey> key =
        IdempotencyKey::parse("example-resource-" + std::to_string(engine.commit_seq().value()));
    if (!key.ok()) return key.status();
    update.idempotency_key = key.value();
    const auto fill = [observed_at](auto& evidence, auto quantity) {
      evidence.state = EvidenceState::Present;
      evidence.value = quantity;
      evidence.source = EvidenceSource::SyntheticAdapter;
      evidence.observed_at = observed_at;
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
    const Status status = engine.record_resource(update);
    if (!status.ok()) return status;
  }

  if (include_sync) {
    const Result<GeneratorState> current = engine.inspect(id);
    if (!current.ok()) return current.status();
    for (const SyncPreconditionKind kind : default_sync_preconditions(current.value().sync_policy)) {
      const Result<GeneratorState> fresh = engine.inspect(id);
      if (!fresh.ok()) return fresh.status();
      SyncPreconditionUpdate update{};
      update.generator = id;
      update.controller = engine.controller();
      update.revision = fresh.value().revision;
      const Result<IdempotencyKey> key = IdempotencyKey::parse(
          "example-sync-" + std::to_string(engine.commit_seq().value()) + "-" +
          std::to_string(static_cast<unsigned>(kind)));
      if (!key.ok()) return key.status();
      update.idempotency_key = key.value();
      update.record.kind = kind;
      update.record.required = true;
      update.record.state = EvidenceState::Present;
      update.record.source = EvidenceSource::SyntheticAdapter;
      update.record.observed_at = observed_at;
      update.record.max_age_millis = max_age;
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
      const Status status = engine.record_sync_precondition(update);
      if (!status.ok()) return status;
    }
  }

  if (include_transfer) {
    {
      const Result<GeneratorState> fresh = engine.inspect(id);
      if (!fresh.ok()) return fresh.status();
      TransferPathUpdate update{};
      update.generator = id;
      update.controller = engine.controller();
      update.revision = fresh.value().revision;
      const Result<IdempotencyKey> key =
          IdempotencyKey::parse("example-path-" + std::to_string(engine.commit_seq().value()));
      if (!key.ok()) return key.status();
      update.idempotency_key = key.value();
      update.path.breaker = SwitchRef::parse("generator-primary-breaker").value();
      update.path.source_bus = SwitchRef::parse("utility-bus-a").value();
      update.path.target_bus = SwitchRef::parse("generator-bus-a").value();
      update.path.feeder = SwitchRef::parse("feeder-1").value();
      update.path.transfer_path = SwitchRef::parse("path-utility-to-generator").value();
      const Status status = engine.set_transfer_path(update);
      if (!status.ok()) return status;
    }
    {
      const Result<GeneratorState> fresh = engine.inspect(id);
      if (!fresh.ok()) return fresh.status();
      SwitchAuthorityUpdate update{};
      update.generator = id;
      update.controller = engine.controller();
      update.revision = fresh.value().revision;
      const Result<IdempotencyKey> key =
          IdempotencyKey::parse("example-token-" + std::to_string(engine.commit_seq().value()));
      if (!key.ok()) return key.status();
      update.idempotency_key = key.value();
      update.token.state = EvidenceState::Present;
      update.token.source = EvidenceSource::ExternalAuthority;
      update.token.lifetime = EvidenceLifetime::AttestedWithValidity;
      update.token.observed_at = observed_at;
      update.token.valid_until = observed_at + max_age;
      update.token.value.authority = SwitchRef::parse("example-switch-authority").value();
      update.token.value.subject_switch = SwitchRef::parse("generator-primary-breaker").value();
      update.token.value.subject_generator = id;
      update.token.value.subject_generation = fresh.value().generation;
      update.token.value.epoch = engine.controller().epoch;
      update.token.value.issued_at = observed_at;
      update.token.value.valid_until = observed_at + max_age;
      const Status status = engine.record_switch_authority(update);
      if (!status.ok()) return status;
    }
    for (const TransferPreconditionKind kind : default_transfer_preconditions()) {
      const Result<GeneratorState> fresh = engine.inspect(id);
      if (!fresh.ok()) return fresh.status();
      TransferPreconditionUpdate update{};
      update.generator = id;
      update.controller = engine.controller();
      update.revision = fresh.value().revision;
      const Result<IdempotencyKey> key = IdempotencyKey::parse(
          "example-transfer-" + std::to_string(engine.commit_seq().value()) + "-" +
          std::to_string(static_cast<unsigned>(kind)));
      if (!key.ok()) return key.status();
      update.idempotency_key = key.value();
      update.record.kind = kind;
      update.record.required = true;
      update.record.state = EvidenceState::Present;
      update.record.source = EvidenceSource::ExternalAuthority;
      update.record.observed_at = observed_at;
      update.record.max_age_millis = max_age;
      update.record.value = TypedValue{TypedValueKind::Boolean, 1};
      const Status status = engine.record_transfer_precondition(update);
      if (!status.ok()) return status;
    }
  }
  return Status::success();
}

inline OperationRequest make_request(GeneratorControlEngine& engine, const GeneratorId& id,
                                     OperationKind operation, const std::string& key,
                                     AuthorityClass cls,
                                     const std::string& reason = std::string()) {
  const Result<GeneratorState> current = engine.inspect(id);
  if (!current.ok()) return OperationRequest{};
  OperationRequest request{};
  request.generator = id;
  request.operation = operation;
  request.controller = engine.controller();
  request.generation = current.value().generation;
  request.revision = current.value().revision;
  request.authority.cls = cls;
  request.authority.epoch = engine.controller().epoch;
  request.authority.granted_by = "example-operator";
  request.authority.granted_at = 0;
  request.authority.reason = reason;
  request.authority.explicit_grant = true;
  if (cls == AuthorityClass::Emergency) request.authority.valid_until = 30 * kMillisPerMinute;
  const Result<IdempotencyKey> parsed = IdempotencyKey::parse(key);
  if (parsed.ok()) request.idempotency_key = parsed.value();
  request.requested_at = 0;
  return request;
}

}  // namespace example
