// Generator Control - synchronization eligibility evaluation.
#include "genctl/sync.hpp"

#include <algorithm>
#include <map>

namespace genctl {

std::string_view to_string(SyncPreconditionKind kind) noexcept {
  switch (kind) {
    case SyncPreconditionKind::Unknown: return "unknown";
    case SyncPreconditionKind::BusEnergized: return "bus-energized";
    case SyncPreconditionKind::GeneratorExcited: return "generator-excited";
    case SyncPreconditionKind::VoltageMatch: return "voltage-match";
    case SyncPreconditionKind::FrequencyMatch: return "frequency-match";
    case SyncPreconditionKind::PhaseAngleMatch: return "phase-angle-match";
    case SyncPreconditionKind::PhaseSequenceMatch: return "phase-sequence-match";
    case SyncPreconditionKind::SlipFrequencyAcceptable: return "slip-frequency-acceptable";
    case SyncPreconditionKind::SyncWindowStable: return "sync-window-stable";
    case SyncPreconditionKind::SyncRelayPermissive: return "sync-relay-permissive";
    case SyncPreconditionKind::ProtectionRelayPermissive: return "protection-relay-permissive";
    case SyncPreconditionKind::GovernorAvrReady: return "governor-avr-ready";
    case SyncPreconditionKind::BreakerClosePermissive: return "breaker-close-permissive";
    case SyncPreconditionKind::AntiIslandingPermissive: return "anti-islanding-permissive";
  }
  return "unknown";
}

Result<SyncPreconditionKind> parse_sync_precondition_kind(std::string_view text) {
  static const SyncPreconditionKind kKinds[] = {
      SyncPreconditionKind::BusEnergized,
      SyncPreconditionKind::GeneratorExcited,
      SyncPreconditionKind::VoltageMatch,
      SyncPreconditionKind::FrequencyMatch,
      SyncPreconditionKind::PhaseAngleMatch,
      SyncPreconditionKind::PhaseSequenceMatch,
      SyncPreconditionKind::SlipFrequencyAcceptable,
      SyncPreconditionKind::SyncWindowStable,
      SyncPreconditionKind::SyncRelayPermissive,
      SyncPreconditionKind::ProtectionRelayPermissive,
      SyncPreconditionKind::GovernorAvrReady,
      SyncPreconditionKind::BreakerClosePermissive,
      SyncPreconditionKind::AntiIslandingPermissive,
  };
  for (const SyncPreconditionKind kind : kKinds) {
    if (to_string(kind) == text) return kind;
  }
  return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                     std::string("unrecognised synchronization precondition '") + std::string(text) +
                         "'");
}

bool sync_requires_external(SyncPreconditionKind kind) noexcept {
  switch (kind) {
    case SyncPreconditionKind::SyncRelayPermissive:
    case SyncPreconditionKind::ProtectionRelayPermissive:
    case SyncPreconditionKind::GovernorAvrReady:
    case SyncPreconditionKind::BreakerClosePermissive:
    case SyncPreconditionKind::AntiIslandingPermissive:
      return true;
    default:
      return false;
  }
}

std::vector<SyncPreconditionKind> default_sync_preconditions(const SyncPolicy& policy) {
  std::vector<SyncPreconditionKind> out = {
      SyncPreconditionKind::BusEnergized,
      SyncPreconditionKind::GeneratorExcited,
      SyncPreconditionKind::VoltageMatch,
      SyncPreconditionKind::FrequencyMatch,
      SyncPreconditionKind::PhaseAngleMatch,
      SyncPreconditionKind::PhaseSequenceMatch,
      SyncPreconditionKind::ProtectionRelayPermissive,
      SyncPreconditionKind::GovernorAvrReady,
      SyncPreconditionKind::BreakerClosePermissive,
  };
  if (policy.require_sync_relay) out.push_back(SyncPreconditionKind::SyncRelayPermissive);
  if (policy.utility_parallel) out.push_back(SyncPreconditionKind::AntiIslandingPermissive);
  return out;
}

namespace {

bool value_in_window(const PreconditionRecord& record, const SyncPolicy& policy,
                     std::string* detail) {
  switch (record.kind) {
    case SyncPreconditionKind::VoltageMatch: {
      if (record.value.kind != TypedValueKind::Millivolts) {
        *detail = "voltage precondition must carry a millivolt value, found '" +
                  std::string(to_string(record.value.kind)) + "'";
        return false;
      }
      const Voltage value{record.value.amount};
      if (!policy.voltage_difference.contains(value)) {
        *detail = "voltage difference " + format_voltage(value) + " is outside the window [" +
                  format_voltage(policy.voltage_difference.minimum) + ", " +
                  format_voltage(policy.voltage_difference.maximum) + "]";
        return false;
      }
      *detail = "voltage difference " + format_voltage(value) + " is inside the window";
      return true;
    }
    case SyncPreconditionKind::FrequencyMatch: {
      if (record.value.kind != TypedValueKind::Millihertz) {
        *detail = "frequency precondition must carry a millihertz value, found '" +
                  std::string(to_string(record.value.kind)) + "'";
        return false;
      }
      const Frequency value{record.value.amount};
      if (!policy.frequency_difference.contains(value)) {
        *detail = "frequency difference " + format_frequency(value) + " is outside the window [" +
                  format_frequency(policy.frequency_difference.minimum) + ", " +
                  format_frequency(policy.frequency_difference.maximum) + "]";
        return false;
      }
      *detail = "frequency difference " + format_frequency(value) + " is inside the window";
      return true;
    }
    case SyncPreconditionKind::PhaseAngleMatch: {
      if (record.value.kind != TypedValueKind::Millidegrees) {
        *detail = "phase angle precondition must carry a millidegree value, found '" +
                  std::string(to_string(record.value.kind)) + "'";
        return false;
      }
      const PhaseAngle value{record.value.amount};
      if (!policy.phase_angle_difference.contains(value)) {
        *detail = "phase angle difference " + format_phase_angle(value) + " is outside the window [" +
                  format_phase_angle(policy.phase_angle_difference.minimum) + ", " +
                  format_phase_angle(policy.phase_angle_difference.maximum) + "]";
        return false;
      }
      *detail = "phase angle difference " + format_phase_angle(value) + " is inside the window";
      return true;
    }
    default:
      *detail = "precondition carries no windowed numeric value";
      return true;
  }
}

Digest256 digest_findings(const std::vector<SyncFinding>& findings) {
  CanonicalWriter writer(CanonicalLimits{1u << 20, 512, 1u << 16, 1024});
  for (const auto& finding : findings) {
    if (!writer.put_u16(static_cast<std::uint16_t>(finding.kind)).ok()) break;
    if (!writer.put_u8(finding.required ? 1u : 0u).ok()) break;
    if (!writer.put_u8(finding.satisfied ? 1u : 0u).ok()) break;
    if (!writer.put_u8(static_cast<std::uint8_t>(finding.outcome)).ok()) break;
    if (!writer.put_u8(static_cast<std::uint8_t>(finding.source)).ok()) break;
    if (!writer.put_u16(static_cast<std::uint16_t>(finding.code)).ok()) break;
    if (!writer.put_string(finding.detail).ok()) break;
  }
  return sha256(writer.bytes());
}

}  // namespace

Status evaluate_synchronization(const GeneratorId& generator, StateRevision revision,
                                const ControllerGeneration& controller,
                                const std::vector<PreconditionRecord>& records,
                                const SyncPolicy& policy, EpochMillis now,
                                SynchronizationEligibility* out) {
  if (out == nullptr) {
    return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                       "synchronization eligibility output must not be null");
  }
  SynchronizationEligibility result{};
  result.generator = generator;
  result.revision = revision;
  result.controller = controller;
  result.evaluated_at = now;

  const std::vector<SyncPreconditionKind> required = default_sync_preconditions(policy);
  std::map<SyncPreconditionKind, const PreconditionRecord*> by_kind;
  for (const auto& record : records) {
    if (record.kind == SyncPreconditionKind::Unknown) continue;
    by_kind.emplace(record.kind, &record);
  }

  std::vector<SyncPreconditionKind> kinds = required;
  for (const auto& entry : by_kind) {
    if (std::find(kinds.begin(), kinds.end(), entry.first) == kinds.end()) {
      kinds.push_back(entry.first);
    }
  }
  std::sort(kinds.begin(), kinds.end());

  for (const SyncPreconditionKind kind : kinds) {
    SyncFinding finding{};
    finding.kind = kind;
    finding.required = std::find(required.begin(), required.end(), kind) != required.end();

    const auto entry = by_kind.find(kind);
    const PreconditionRecord* record = entry == by_kind.end() ? nullptr : entry->second;
    if (record == nullptr) {
      finding.outcome = EligibilityOutcome::Indeterminate;
      finding.code = ErrorCode::SynchronizationEvidenceMissing;
      finding.detail = "no evidence has been submitted for this precondition";
      result.synthetic_evidence_used = result.synthetic_evidence_used || false;
      result.findings.push_back(std::move(finding));
      continue;
    }
    finding.source = record->source;
    if (record->source == EvidenceSource::SyntheticAdapter) result.synthetic_evidence_used = true;

    if (record->source == EvidenceSource::CommandAcknowledgement) {
      finding.outcome = EligibilityOutcome::Indeterminate;
      finding.code = ErrorCode::ObservationNotAuthoritative;
      finding.detail = "a command acknowledgement cannot satisfy a synchronization precondition";
      result.findings.push_back(std::move(finding));
      continue;
    }
    if (sync_requires_external(kind) && record->source == EvidenceSource::SyntheticAdapter) {
      // Synthetic installations are allowed but the source is reported verbatim so
      // the whole result can be labelled SYNTHETIC.
      finding.detail = "permissive reported by a synthetic installation";
    }

    switch (record->state) {
      case EvidenceState::Unknown:
      case EvidenceState::Missing:
      case EvidenceState::Unsupported:
        finding.outcome = EligibilityOutcome::Indeterminate;
        finding.code = ErrorCode::SynchronizationEvidenceMissing;
        finding.detail = record->detail.empty() ? "precondition evidence is unavailable"
                                                : record->detail;
        result.findings.push_back(std::move(finding));
        continue;
      case EvidenceState::Stale:
        finding.outcome = EligibilityOutcome::Indeterminate;
        finding.code = ErrorCode::SynchronizationEvidenceStale;
        finding.detail = record->detail.empty() ? "precondition evidence is stale" : record->detail;
        result.findings.push_back(std::move(finding));
        continue;
      case EvidenceState::Contradictory:
        finding.outcome = EligibilityOutcome::Ineligible;
        finding.code = ErrorCode::SynchronizationPreconditionFailed;
        finding.detail = record->detail.empty() ? "precondition evidence is contradictory"
                                                : record->detail;
        result.findings.push_back(std::move(finding));
        continue;
      case EvidenceState::Denied:
        finding.outcome = EligibilityOutcome::Ineligible;
        finding.code = ErrorCode::SynchronizationPreconditionFailed;
        finding.detail = record->detail.empty() ? "an authority denied this precondition"
                                                : record->detail;
        result.findings.push_back(std::move(finding));
        continue;
      case EvidenceState::Present:
        break;
    }

    const FreshnessAssessment freshness = assess_freshness(
        record->observed_at, now, FreshnessPolicy::within(record->max_age_millis));
    if (freshness.verdict == FreshnessVerdict::Stale) {
      finding.outcome = EligibilityOutcome::Indeterminate;
      finding.code = ErrorCode::SynchronizationEvidenceStale;
      finding.detail = freshness.detail;
      result.findings.push_back(std::move(finding));
      continue;
    }
    if (freshness.verdict == FreshnessVerdict::FutureDated) {
      finding.outcome = EligibilityOutcome::Ineligible;
      finding.code = ErrorCode::SynchronizationPreconditionFailed;
      finding.detail = freshness.detail;
      result.findings.push_back(std::move(finding));
      continue;
    }

    if (record->value.kind == TypedValueKind::Boolean) {
      if (record->value.amount == 0) {
        finding.outcome = EligibilityOutcome::Ineligible;
        finding.code = ErrorCode::SynchronizationPreconditionFailed;
        finding.detail = record->detail.empty() ? "precondition is not satisfied" : record->detail;
      } else {
        finding.outcome = EligibilityOutcome::Eligible;
        finding.satisfied = true;
        finding.code = ErrorCode::Ok;
        finding.detail = record->detail.empty() ? "precondition satisfied" : record->detail;
      }
      result.findings.push_back(std::move(finding));
      continue;
    }

    std::string window_detail;
    if (value_in_window(*record, policy, &window_detail)) {
      finding.outcome = EligibilityOutcome::Eligible;
      finding.satisfied = true;
      finding.code = ErrorCode::Ok;
      finding.detail = window_detail;
    } else {
      const bool wrong_unit = record->value.kind == TypedValueKind::None ||
                              window_detail.find("must carry") != std::string::npos;
      finding.outcome =
          wrong_unit ? EligibilityOutcome::Indeterminate : EligibilityOutcome::Ineligible;
      finding.code = wrong_unit ? ErrorCode::SynchronizationEvidenceMissing
                                : ErrorCode::SynchronizationPreconditionFailed;
      finding.detail = window_detail;
    }
    result.findings.push_back(std::move(finding));
  }

  result.outcome = EligibilityOutcome::Eligible;
  result.primary_error = ErrorCode::Ok;
  bool indeterminate = false;
  ErrorCode indeterminate_code = ErrorCode::Ok;
  for (const auto& finding : result.findings) {
    if (!finding.required) continue;
    if (finding.outcome == EligibilityOutcome::Ineligible) {
      result.outcome = EligibilityOutcome::Ineligible;
      result.primary_error = finding.code;
      break;
    }
    if (finding.outcome != EligibilityOutcome::Eligible && !indeterminate) {
      indeterminate = true;
      indeterminate_code = finding.code;
    }
  }
  if (result.outcome != EligibilityOutcome::Ineligible && indeterminate) {
    result.outcome = EligibilityOutcome::Indeterminate;
    result.primary_error = indeterminate_code == ErrorCode::Ok
                               ? ErrorCode::SynchronizationEvidenceMissing
                               : indeterminate_code;
  }
  if (result.outcome == EligibilityOutcome::Eligible) result.primary_error = ErrorCode::Ok;
  result.stage = result.outcome == EligibilityOutcome::Eligible ? ValidationStage::None
                                                                : ValidationStage::Synchronization;
  result.binding_digest = digest_findings(result.findings);
  *out = std::move(result);
  return Status::success();
}

}  // namespace genctl
