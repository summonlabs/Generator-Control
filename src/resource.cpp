// Generator Control - resource and fuel sufficiency assessment.
#include "genctl/resource.hpp"

#include <algorithm>

namespace genctl {

std::string_view to_string(ResourceKind kind) noexcept {
  switch (kind) {
    case ResourceKind::Unknown: return "unknown";
    case ResourceKind::FuelLevel: return "fuel-level";
    case ResourceKind::FuelCapacity: return "fuel-capacity";
    case ResourceKind::FuelConsumptionRate: return "fuel-consumption-rate";
    case ResourceKind::FuelTemperature: return "fuel-temperature";
    case ResourceKind::LubeOilPressure: return "lube-oil-pressure";
    case ResourceKind::LubeOilTemperature: return "lube-oil-temperature";
    case ResourceKind::CoolantTemperature: return "coolant-temperature";
    case ResourceKind::CoolantLevel: return "coolant-level";
    case ResourceKind::StartingBatteryVoltage: return "starting-battery-voltage";
    case ResourceKind::CompressedAirPressure: return "compressed-air-pressure";
  }
  return "unknown";
}

namespace {

Digest256 digest_findings(const std::vector<ResourceFinding>& findings) {
  CanonicalWriter writer(CanonicalLimits{1u << 20, 512, 1u << 16, 1024});
  for (const auto& finding : findings) {
    if (!writer.put_u8(static_cast<std::uint8_t>(finding.kind)).ok()) break;
    if (!writer.put_u8(static_cast<std::uint8_t>(finding.usability)).ok()) break;
    if (!writer.put_u8(static_cast<std::uint8_t>(finding.source)).ok()) break;
    if (!writer.put_u16(static_cast<std::uint16_t>(finding.code)).ok()) break;
    if (!writer.put_u8(finding.required ? 1u : 0u).ok()) break;
    if (!writer.put_string(finding.detail).ok()) break;
  }
  return sha256(writer.bytes());
}

ResourceFinding make_finding(ResourceKind kind, bool required, const EvidenceAssessment& assessment,
                             EvidenceSource source) {
  ResourceFinding finding{};
  finding.kind = kind;
  finding.required = required;
  finding.usability = assessment.usability;
  finding.code = assessment.code;
  finding.source = source;
  finding.detail = assessment.detail;
  return finding;
}

// A resource finding that is unusable maps to a resource-category error code so
// that the primary error of an evaluation stays inside the resource stage.
ErrorCode resource_code(ErrorCode code) {
  switch (code) {
    case ErrorCode::Ok: return ErrorCode::Ok;
    case ErrorCode::EvidenceStale:
    case ErrorCode::EvidenceFutureDated: return ErrorCode::ResourceEvidenceStale;
    case ErrorCode::EvidenceMissing:
    case ErrorCode::EvidenceUnknown:
    case ErrorCode::EvidenceUnsupported:
    case ErrorCode::EvidenceContradictory:
    case ErrorCode::EvidenceDenied:
    case ErrorCode::EvidenceProvenanceRejected:
    case ErrorCode::ObservationNotAuthoritative: return ErrorCode::ResourceEvidenceMissing;
    default: return code;
  }
}

}  // namespace

Status assess_resources(const GeneratorId& generator, StateRevision revision,
                        const ControllerGeneration& controller, const ResourceSnapshot& snapshot,
                        const ResourceRequirement& requirement, EpochMillis now,
                        const EvidencePolicy& policy, bool waive_advisory,
                        ResourceAssessment* out) {
  if (out == nullptr) {
    return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                       "resource assessment output must not be null");
  }
  (void)waive_advisory;  // fuel sufficiency is never waivable; kept for symmetry

  ResourceAssessment assessment{};
  assessment.generator = generator;
  assessment.revision = revision;
  assessment.controller = controller;
  assessment.evaluated_at = now;

  const EvidencePolicy fuel_policy = policy;

  struct Entry {
    ResourceKind kind;
    bool required;
    EvidenceAssessment value;
    EvidenceSource source;
  };
  std::vector<Entry> entries;

  {
    EvidencePolicy level_policy = fuel_policy;
    level_policy.freshness = FreshnessPolicy::within(requirement.fuel_max_age_millis);
    const auto level = assess_evidence(snapshot.fuel_level, now, level_policy);
    entries.push_back({ResourceKind::FuelLevel, true, level, snapshot.fuel_level.source});
    if (snapshot.fuel_level.source == EvidenceSource::SyntheticAdapter) {
      assessment.synthetic_evidence_used = true;
    }
  }
  {
    EvidencePolicy capacity_policy = fuel_policy;
    capacity_policy.freshness = FreshnessPolicy::within(requirement.fuel_max_age_millis);
    const auto capacity = assess_evidence(snapshot.fuel_capacity, now, capacity_policy);
    // Capacity is only required when a percentage-based level must be interpreted.
    const bool required =
        snapshot.fuel_level.has_value() &&
        snapshot.fuel_level.value.unit == FuelUnit::PercentOfCapacity;
    entries.push_back({ResourceKind::FuelCapacity, required, capacity, snapshot.fuel_capacity.source});
  }
  {
    EvidencePolicy rate_policy = fuel_policy;
    rate_policy.freshness = FreshnessPolicy::within(requirement.fuel_max_age_millis);
    const auto rate = assess_evidence(snapshot.fuel_consumption, now, rate_policy);
    const bool required = requirement.required_runtime_seconds >= 0;
    entries.push_back({ResourceKind::FuelConsumptionRate, required, rate,
                       snapshot.fuel_consumption.source});
  }

  auto append_temperature = [&](ResourceKind kind, bool required,
                                const Evidence<Temperature>& evidence) {
    EvidencePolicy p = policy;
    p.freshness = FreshnessPolicy::within(requirement.machinery_max_age_millis);
    entries.push_back({kind, required, assess_evidence(evidence, now, p), evidence.source});
    if (evidence.source == EvidenceSource::SyntheticAdapter) assessment.synthetic_evidence_used = true;
  };
  auto append_pressure = [&](ResourceKind kind, bool required, const Evidence<Pressure>& evidence) {
    EvidencePolicy p = policy;
    p.freshness = FreshnessPolicy::within(requirement.machinery_max_age_millis);
    entries.push_back({kind, required, assess_evidence(evidence, now, p), evidence.source});
    if (evidence.source == EvidenceSource::SyntheticAdapter) assessment.synthetic_evidence_used = true;
  };
  auto append_percent = [&](ResourceKind kind, bool required, const Evidence<Percent>& evidence) {
    EvidencePolicy p = policy;
    p.freshness = FreshnessPolicy::within(requirement.machinery_max_age_millis);
    entries.push_back({kind, required, assess_evidence(evidence, now, p), evidence.source});
    if (evidence.source == EvidenceSource::SyntheticAdapter) assessment.synthetic_evidence_used = true;
  };
  auto append_voltage = [&](ResourceKind kind, bool required, const Evidence<Voltage>& evidence) {
    EvidencePolicy p = policy;
    p.freshness = FreshnessPolicy::within(requirement.machinery_max_age_millis);
    entries.push_back({kind, required, assess_evidence(evidence, now, p), evidence.source});
    if (evidence.source == EvidenceSource::SyntheticAdapter) assessment.synthetic_evidence_used = true;
  };

  append_pressure(ResourceKind::LubeOilPressure, requirement.require_lube_oil_pressure,
                  snapshot.lube_oil_pressure);
  append_temperature(ResourceKind::CoolantTemperature, requirement.require_coolant_temperature,
                     snapshot.coolant_temperature);
  append_voltage(ResourceKind::StartingBatteryVoltage, requirement.require_battery_voltage,
                 snapshot.battery_voltage);
  append_pressure(ResourceKind::CompressedAirPressure, requirement.require_compressed_air,
                  snapshot.compressed_air_pressure);
  append_percent(ResourceKind::CoolantLevel, requirement.require_coolant_temperature,
                 snapshot.coolant_level);

  for (const auto& entry : entries) {
    ResourceFinding finding = make_finding(entry.kind, entry.required, entry.value, entry.source);
    finding.code = entry.value.usable() ? ErrorCode::Ok : resource_code(entry.value.code);
    if (entry.required && !entry.value.usable()) finding.code = resource_code(entry.value.code);
    assessment.findings.push_back(std::move(finding));
  }

  // Runtime sufficiency: unknown consumption, an unusable level or a unit mismatch
  // are all refusals, never an assumed adequate runtime.
  RuntimeBasis basis = RuntimeBasis::Unknown;
  if (snapshot.fuel_consumption.has_value() &&
      snapshot.fuel_consumption.source != EvidenceSource::OperatorAttestation) {
    basis = RuntimeBasis::MeasuredRate;
  } else if (snapshot.fuel_consumption.has_value()) {
    basis = RuntimeBasis::AttestedRate;
  }
  Result<RuntimeEstimate> runtime = estimate_runtime(snapshot.fuel_level.value,
                                                     requirement.reserve,
                                                     snapshot.fuel_consumption.value, basis);
  if (!runtime.ok()) {
    assessment.runtime.known = false;
    assessment.runtime.detail = runtime.status().message();
  } else {
    assessment.runtime = runtime.value();
  }

  ErrorCode primary = ErrorCode::Ok;
  for (const auto& finding : assessment.findings) {
    if (!finding.required || finding.code == ErrorCode::Ok) continue;
    primary = finding.code;
    break;
  }

  if (primary == ErrorCode::Ok) {
    const bool runtime_required = requirement.required_runtime_seconds >= 0;
    if (runtime_required) {
      if (!assessment.runtime.known) {
        primary = ErrorCode::ResourceRuntimeUnknown;
      } else if (assessment.runtime.seconds < requirement.required_runtime_seconds) {
        primary = ErrorCode::ResourceInsufficient;
        assessment.runtime.detail += "; " + std::to_string(assessment.runtime.seconds) +
                                     "s available, " +
                                     std::to_string(requirement.required_runtime_seconds) +
                                     "s required";
      }
    }
  }

  assessment.primary_error = primary;
  assessment.sufficient = primary == ErrorCode::Ok;
  assessment.stage = assessment.sufficient ? ValidationStage::None : ValidationStage::Resource;
  assessment.binding_digest = digest_findings(assessment.findings);
  *out = std::move(assessment);
  return Status::success();
}

}  // namespace genctl
