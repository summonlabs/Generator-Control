// Generator Control - readiness and interlock evaluation.
#include "genctl/readiness.hpp"

#include <algorithm>
#include <map>

namespace genctl {

std::string_view to_string(CheckKind kind) noexcept {
  switch (kind) {
    case CheckKind::Unknown: return "unknown";
    case CheckKind::EmergencyStopNotEngaged: return "emergency-stop-not-engaged";
    case CheckKind::FireSuppressionClear: return "fire-suppression-clear";
    case CheckKind::GasLeakDetectionClear: return "gas-leak-detection-clear";
    case CheckKind::RoomVentilationRunning: return "room-ventilation-running";
    case CheckKind::PersonnelGuardsInPlace: return "personnel-guards-in-place";
    case CheckKind::OverspeedTripClear: return "overspeed-trip-clear";
    case CheckKind::OvercrankLockoutClear: return "overcrank-lockout-clear";
    case CheckKind::LowOilPressureTripClear: return "low-oil-pressure-trip-clear";
    case CheckKind::HighCoolantTemperatureTripClear: return "high-coolant-temperature-trip-clear";
    case CheckKind::BreakerFailureLockoutClear: return "breaker-failure-lockout-clear";
    case CheckKind::ExhaustPathClear: return "exhaust-path-clear";
    case CheckKind::ProtectionRelayPermissive: return "protection-relay-permissive";
    case CheckKind::UtilityAntiIslandingPermissive: return "utility-anti-islanding-permissive";
    case CheckKind::SyncRelayAvailable: return "sync-relay-available";
    case CheckKind::GovernorAvrReady: return "governor-avr-ready";
    case CheckKind::FuelValveAvailable: return "fuel-valve-available";
    case CheckKind::BatteryVoltageSufficient: return "battery-voltage-sufficient";
    case CheckKind::CoolantTemperatureInRange: return "coolant-temperature-in-range";
    case CheckKind::LubeOilPressureInRange: return "lube-oil-pressure-in-range";
    case CheckKind::CoolantLevelSufficient: return "coolant-level-sufficient";
    case CheckKind::StartingAirPressureSufficient: return "starting-air-pressure-sufficient";
    case CheckKind::CrankLimiterClear: return "crank-limiter-clear";
    case CheckKind::MaintenanceWindowClear: return "maintenance-window-clear";
    case CheckKind::EconomicReserveSatisfied: return "economic-reserve-satisfied";
    case CheckKind::LoadHeadroomAvailable: return "load-headroom-available";
    case CheckKind::FuelQualityAttested: return "fuel-quality-attested";
  }
  return "unknown";
}

Result<CheckKind> parse_check_kind(std::string_view text) {
  static const CheckKind kKinds[] = {
      CheckKind::EmergencyStopNotEngaged,
      CheckKind::FireSuppressionClear,
      CheckKind::GasLeakDetectionClear,
      CheckKind::RoomVentilationRunning,
      CheckKind::PersonnelGuardsInPlace,
      CheckKind::OverspeedTripClear,
      CheckKind::OvercrankLockoutClear,
      CheckKind::LowOilPressureTripClear,
      CheckKind::HighCoolantTemperatureTripClear,
      CheckKind::BreakerFailureLockoutClear,
      CheckKind::ExhaustPathClear,
      CheckKind::ProtectionRelayPermissive,
      CheckKind::UtilityAntiIslandingPermissive,
      CheckKind::SyncRelayAvailable,
      CheckKind::GovernorAvrReady,
      CheckKind::FuelValveAvailable,
      CheckKind::BatteryVoltageSufficient,
      CheckKind::CoolantTemperatureInRange,
      CheckKind::LubeOilPressureInRange,
      CheckKind::CoolantLevelSufficient,
      CheckKind::StartingAirPressureSufficient,
      CheckKind::CrankLimiterClear,
      CheckKind::MaintenanceWindowClear,
      CheckKind::EconomicReserveSatisfied,
      CheckKind::LoadHeadroomAvailable,
      CheckKind::FuelQualityAttested,
  };
  for (const CheckKind kind : kKinds) {
    if (to_string(kind) == text) return kind;
  }
  return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                     std::string("unrecognised readiness check '") + std::string(text) + "'");
}

CheckClass check_class(CheckKind kind) noexcept {
  switch (kind) {
    case CheckKind::EmergencyStopNotEngaged:
    case CheckKind::FireSuppressionClear:
    case CheckKind::GasLeakDetectionClear:
    case CheckKind::RoomVentilationRunning:
    case CheckKind::PersonnelGuardsInPlace:
    case CheckKind::OverspeedTripClear:
    case CheckKind::OvercrankLockoutClear:
    case CheckKind::LowOilPressureTripClear:
    case CheckKind::HighCoolantTemperatureTripClear:
    case CheckKind::BreakerFailureLockoutClear:
    case CheckKind::ExhaustPathClear:
      return CheckClass::Safety;
    case CheckKind::ProtectionRelayPermissive:
    case CheckKind::UtilityAntiIslandingPermissive:
    case CheckKind::SyncRelayAvailable:
    case CheckKind::GovernorAvrReady:
    case CheckKind::FuelValveAvailable:
      return CheckClass::Protection;
    case CheckKind::BatteryVoltageSufficient:
    case CheckKind::CoolantTemperatureInRange:
    case CheckKind::LubeOilPressureInRange:
    case CheckKind::CoolantLevelSufficient:
    case CheckKind::StartingAirPressureSufficient:
    case CheckKind::CrankLimiterClear:
      return CheckClass::Readiness;
    case CheckKind::MaintenanceWindowClear:
    case CheckKind::EconomicReserveSatisfied:
    case CheckKind::LoadHeadroomAvailable:
    case CheckKind::FuelQualityAttested:
    case CheckKind::Unknown:
      return CheckClass::Advisory;
  }
  return CheckClass::Advisory;
}

bool is_waivable(CheckKind kind) noexcept { return check_class(kind) == CheckClass::Advisory; }

std::vector<CheckKind> default_required_checks() {
  return {
      CheckKind::EmergencyStopNotEngaged,
      CheckKind::FireSuppressionClear,
      CheckKind::GasLeakDetectionClear,
      CheckKind::RoomVentilationRunning,
      CheckKind::PersonnelGuardsInPlace,
      CheckKind::OverspeedTripClear,
      CheckKind::OvercrankLockoutClear,
      CheckKind::LowOilPressureTripClear,
      CheckKind::HighCoolantTemperatureTripClear,
      CheckKind::BreakerFailureLockoutClear,
      CheckKind::ExhaustPathClear,
      CheckKind::ProtectionRelayPermissive,
      CheckKind::GovernorAvrReady,
      CheckKind::FuelValveAvailable,
      CheckKind::BatteryVoltageSufficient,
      CheckKind::CoolantTemperatureInRange,
      CheckKind::LubeOilPressureInRange,
      CheckKind::CoolantLevelSufficient,
      CheckKind::CrankLimiterClear,
      CheckKind::MaintenanceWindowClear,
  };
}

std::vector<CheckKind> default_parallel_checks() {
  std::vector<CheckKind> out = default_required_checks();
  out.push_back(CheckKind::SyncRelayAvailable);
  out.push_back(CheckKind::UtilityAntiIslandingPermissive);
  return out;
}

namespace {

struct CheckAssessment {
  bool satisfied{false};
  EvidenceUsability usability{EvidenceUsability::Unknown};
  ErrorCode code{ErrorCode::EvidenceMissing};
  std::string detail{};
};

CheckAssessment assess_check(const CheckRecord* record, const CheckKind kind, EpochMillis now,
                             const EvidencePolicy& policy) {
  CheckAssessment out{};
  const CheckClass cls = check_class(kind);
  const bool interlock = cls == CheckClass::Safety || cls == CheckClass::Protection;

  if (record == nullptr) {
    out.usability = EvidenceUsability::Missing;
    out.code = interlock ? ErrorCode::InterlockEvidenceUnavailable : ErrorCode::EvidenceMissing;
    out.detail = "no record has been submitted for this check";
    return out;
  }

  switch (record->state) {
    case EvidenceState::Unknown:
      out.usability = EvidenceUsability::Unknown;
      out.code = interlock ? ErrorCode::InterlockEvidenceUnavailable : ErrorCode::EvidenceUnknown;
      out.detail = record->detail.empty() ? "check state is unknown" : record->detail;
      return out;
    case EvidenceState::Missing:
      out.usability = EvidenceUsability::Missing;
      out.code = interlock ? ErrorCode::InterlockEvidenceUnavailable : ErrorCode::EvidenceMissing;
      out.detail = record->detail.empty() ? "check has no evidence" : record->detail;
      return out;
    case EvidenceState::Stale:
      out.usability = EvidenceUsability::Stale;
      out.code = interlock ? ErrorCode::InterlockEvidenceUnavailable : ErrorCode::EvidenceStale;
      out.detail = record->detail.empty() ? "check evidence is stale" : record->detail;
      return out;
    case EvidenceState::Unsupported:
      out.usability = EvidenceUsability::Unsupported;
      out.code = interlock ? ErrorCode::InterlockEvidenceUnavailable : ErrorCode::EvidenceUnsupported;
      out.detail = record->detail.empty() ? "check cannot be reported by this installation"
                                          : record->detail;
      return out;
    case EvidenceState::Contradictory:
      out.usability = EvidenceUsability::Contradictory;
      out.code = ErrorCode::EvidenceContradictory;
      out.detail = record->detail.empty() ? "check evidence is contradictory" : record->detail;
      return out;
    case EvidenceState::Denied:
      out.usability = EvidenceUsability::Denied;
      out.code = interlock ? ErrorCode::InterlockEngaged : ErrorCode::EvidenceDenied;
      out.detail = record->detail.empty() ? "an authority denied this check" : record->detail;
      return out;
    case EvidenceState::Present:
      break;
  }

  if (!policy.allow_synthetic && record->source == EvidenceSource::SyntheticAdapter) {
    out.usability = EvidenceUsability::ProvenanceRejected;
    out.code = ErrorCode::EvidenceProvenanceRejected;
    out.detail = "synthetic evidence is refused by policy for this check";
    return out;
  }
  if (!policy.allow_attestation && record->source == EvidenceSource::OperatorAttestation) {
    out.usability = EvidenceUsability::ProvenanceRejected;
    out.code = ErrorCode::EvidenceProvenanceRejected;
    out.detail = "operator attestation is refused by policy for this check";
    return out;
  }
  if (record->source == EvidenceSource::CommandAcknowledgement) {
    out.usability = EvidenceUsability::ProvenanceRejected;
    out.code = ErrorCode::ObservationNotAuthoritative;
    out.detail = "a command acknowledgement cannot satisfy a readiness check";
    return out;
  }
  if (policy.require_external_authority && record->source != EvidenceSource::ExternalAuthority) {
    out.usability = EvidenceUsability::ProvenanceRejected;
    out.code = ErrorCode::EvidenceProvenanceRejected;
    out.detail = "this check requires an external authority";
    return out;
  }

  if (record->lifetime == EvidenceLifetime::AttestedWithValidity && record->valid_until != 0 &&
      now > record->valid_until) {
    out.usability = EvidenceUsability::Stale;
    out.code = interlock ? ErrorCode::InterlockEvidenceUnavailable : ErrorCode::EvidenceStale;
    out.detail = "attested check expired at " + format_epoch_millis(record->valid_until);
    return out;
  }

  const FreshnessAssessment freshness = assess_freshness(
      record->observed_at, now, FreshnessPolicy::within(record->max_age_millis));
  switch (freshness.verdict) {
    case FreshnessVerdict::Fresh:
    case FreshnessVerdict::Unbounded:
      break;
    case FreshnessVerdict::Stale:
      out.usability = EvidenceUsability::Stale;
      out.code = interlock ? ErrorCode::InterlockEvidenceUnavailable : ErrorCode::EvidenceStale;
      out.detail = freshness.detail;
      return out;
    case FreshnessVerdict::FutureDated:
      out.usability = EvidenceUsability::Contradictory;
      out.code = ErrorCode::EvidenceFutureDated;
      out.detail = freshness.detail;
      return out;
    case FreshnessVerdict::Unknown:
      out.usability = EvidenceUsability::Unknown;
      out.code = interlock ? ErrorCode::InterlockEvidenceUnavailable : ErrorCode::EvidenceUnknown;
      out.detail = "check freshness could not be established";
      return out;
  }

  // Interlocks carry an explicit clearance value; a record that reports "engaged"
  // is a hard stop even though its own state is Present.
  if (interlock && record->value.kind == TypedValueKind::Boolean && record->value.amount == 0) {
    out.usability = EvidenceUsability::Usable;
    out.satisfied = false;
    out.code = ErrorCode::InterlockEngaged;
    out.detail = record->detail.empty() ? "interlock reports an engaged/tripped condition"
                                        : record->detail;
    return out;
  }

  out.usability = EvidenceUsability::Usable;
  out.satisfied = true;
  out.code = ErrorCode::Ok;
  out.detail = record->detail.empty() ? "satisfied" : record->detail;
  return out;
}

Digest256 digest_findings(const std::vector<CheckFinding>& findings) {
  CanonicalWriter writer(CanonicalLimits{1u << 20, 512, 1u << 16, 1024});
  for (const auto& finding : findings) {
    if (!writer.put_u16(static_cast<std::uint16_t>(finding.kind)).ok()) break;
    if (!writer.put_u8(finding.required ? 1u : 0u).ok()) break;
    if (!writer.put_u8(finding.satisfied ? 1u : 0u).ok()) break;
    if (!writer.put_u8(finding.waived ? 1u : 0u).ok()) break;
    if (!writer.put_u8(static_cast<std::uint8_t>(finding.usability)).ok()) break;
    if (!writer.put_u8(static_cast<std::uint8_t>(finding.source)).ok()) break;
    if (!writer.put_u16(static_cast<std::uint16_t>(finding.code)).ok()) break;
    if (!writer.put_string(finding.detail).ok()) break;
  }
  return sha256(writer.bytes());
}

}  // namespace

Status evaluate_readiness(const GeneratorId& generator, StateRevision revision,
                          const ControllerGeneration& controller,
                          const GeneratorGeneration& generation,
                          const std::vector<CheckKind>& required,
                          const std::vector<CheckRecord>& records, EpochMillis now,
                          const EvidencePolicy& policy, bool waive_advisory,
                          ReadinessReport* out) {
  if (out == nullptr) {
    return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                       "readiness report output must not be null");
  }
  ReadinessReport report{};
  report.generator = generator;
  report.revision = revision;
  report.controller = controller;
  report.generation = generation;
  report.evaluated_at = now;

  std::map<CheckKind, const CheckRecord*> by_kind;
  for (const auto& record : records) {
    if (record.kind == CheckKind::Unknown) continue;
    by_kind.emplace(record.kind, &record);
  }

  std::vector<CheckKind> kinds = required;
  for (const auto& entry : by_kind) {
    if (std::find(kinds.begin(), kinds.end(), entry.first) == kinds.end()) {
      kinds.push_back(entry.first);
    }
  }
  std::sort(kinds.begin(), kinds.end());

  for (const CheckKind kind : kinds) {
    const bool is_required = std::find(required.begin(), required.end(), kind) != required.end();
    const auto entry = by_kind.find(kind);
    const CheckRecord* record = entry == by_kind.end() ? nullptr : entry->second;

    CheckFinding finding{};
    finding.kind = kind;
    finding.cls = check_class(kind);
    finding.required = is_required;
    const CheckAssessment assessment = assess_check(record, kind, now, policy);
    finding.satisfied = assessment.satisfied;
    finding.usability = assessment.usability;
    finding.code = assessment.code;
    finding.source = record == nullptr ? EvidenceSource::Unknown : record->source;
    finding.detail = assessment.detail;
    if (!finding.satisfied && finding.required && waive_advisory && is_waivable(kind)) {
      finding.waived = true;
    }
    if (record != nullptr && record->source == EvidenceSource::SyntheticAdapter) {
      report.synthetic_evidence_used = true;
    }
    report.findings.push_back(std::move(finding));
  }

  report.interlock_evidence_complete = true;
  for (const auto& finding : report.findings) {
    if (!finding.required) continue;
    if (finding.cls != CheckClass::Safety && finding.cls != CheckClass::Protection) continue;
    if (finding.usability == EvidenceUsability::Missing ||
        finding.usability == EvidenceUsability::Unknown ||
        finding.usability == EvidenceUsability::Unsupported ||
        finding.usability == EvidenceUsability::ProvenanceRejected) {
      report.interlock_evidence_complete = false;
      break;
    }
  }

  report.primary_error = ErrorCode::Ok;
  for (const auto& finding : report.findings) {
    if (!finding.required || finding.satisfied || finding.waived) continue;
    report.primary_error = finding.code == ErrorCode::Ok ? ErrorCode::ReadinessNotSatisfied
                                                         : finding.code;
    break;
  }
  report.satisfied = report.primary_error == ErrorCode::Ok;
  report.stage = report.satisfied ? ValidationStage::None : ValidationStage::Interlock;
  if (report.satisfied) {
    report.stage = ValidationStage::None;
  } else {
    // Safety and protection failures belong to the interlock stage; machine
    // readiness and advisory failures belong to the readiness stage.
    bool interlock_failure = false;
    for (const auto& finding : report.findings) {
      if (!finding.required || finding.satisfied || finding.waived) continue;
      interlock_failure = finding.cls == CheckClass::Safety || finding.cls == CheckClass::Protection;
      break;
    }
    report.stage = interlock_failure ? ValidationStage::Interlock : ValidationStage::Readiness;
  }
  report.advisory_waived = false;
  for (const auto& finding : report.findings) {
    if (finding.waived) {
      report.advisory_waived = true;
      break;
    }
  }
  report.binding_digest = digest_findings(report.findings);

  *out = std::move(report);
  return Status::success();
}

}  // namespace genctl
