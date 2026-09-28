// Generator Control - transfer and breaker eligibility evaluation.
#include "genctl/transfer.hpp"

#include <algorithm>
#include <map>

namespace genctl {

Result<SwitchRef> SwitchRef::parse(std::string_view text) {
  if (text.empty()) {
    return make_status(ErrorCode::InvalidIdentifier, ValidationStage::Format,
                       "switch reference must not be empty");
  }
  if (text.size() > kMaxLength) {
    return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Format,
                       "switch reference exceeds " + std::to_string(kMaxLength) + " characters");
  }
  for (const char c : text) {
    const unsigned char uc = static_cast<unsigned char>(c);
    const bool ok = (uc >= '0' && uc <= '9') || (uc >= 'A' && uc <= 'Z') ||
                    (uc >= 'a' && uc <= 'z') || c == '-' || c == '_' || c == '.' || c == ':';
    if (!ok) {
      return make_status(ErrorCode::InvalidIdentifier, ValidationStage::Format,
                         std::string("switch reference contains an unsupported character: '") + c +
                             "'");
    }
  }
  SwitchRef out;
  out.value_.assign(text);
  return out;
}

std::string_view to_string(TransferPreconditionKind kind) noexcept {
  switch (kind) {
    case TransferPreconditionKind::Unknown: return "unknown";
    case TransferPreconditionKind::FacilitySwitchAuthorityGranted:
      return "facility-switch-authority-granted";
    case TransferPreconditionKind::FacilitySwitchAuthorityFresh:
      return "facility-switch-authority-fresh";
    case TransferPreconditionKind::BreakerPositionObserved: return "breaker-position-observed";
    case TransferPreconditionKind::BreakerClosePermissive: return "breaker-close-permissive";
    case TransferPreconditionKind::ProtectionRelayPermissive: return "protection-relay-permissive";
    case TransferPreconditionKind::GeneratorSynchronized: return "generator-synchronized";
    case TransferPreconditionKind::GeneratorReadinessSatisfied: return "generator-readiness-satisfied";
    case TransferPreconditionKind::TransferPathIdentified: return "transfer-path-identified";
    case TransferPreconditionKind::TargetBusEnergized: return "target-bus-energized";
    case TransferPreconditionKind::SourceBusEnergized: return "source-bus-energized";
    case TransferPreconditionKind::NoConflictingTransferPending:
      return "no-conflicting-transfer-pending";
  }
  return "unknown";
}

Result<TransferPreconditionKind> parse_transfer_precondition_kind(std::string_view text) {
  static const TransferPreconditionKind kKinds[] = {
      TransferPreconditionKind::FacilitySwitchAuthorityGranted,
      TransferPreconditionKind::FacilitySwitchAuthorityFresh,
      TransferPreconditionKind::BreakerPositionObserved,
      TransferPreconditionKind::BreakerClosePermissive,
      TransferPreconditionKind::ProtectionRelayPermissive,
      TransferPreconditionKind::GeneratorSynchronized,
      TransferPreconditionKind::GeneratorReadinessSatisfied,
      TransferPreconditionKind::TransferPathIdentified,
      TransferPreconditionKind::TargetBusEnergized,
      TransferPreconditionKind::SourceBusEnergized,
      TransferPreconditionKind::NoConflictingTransferPending,
  };
  for (const TransferPreconditionKind kind : kKinds) {
    if (to_string(kind) == text) return kind;
  }
  return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                     std::string("unrecognised transfer precondition '") + std::string(text) + "'");
}

bool transfer_requires_external(TransferPreconditionKind kind) noexcept {
  switch (kind) {
    case TransferPreconditionKind::FacilitySwitchAuthorityGranted:
    case TransferPreconditionKind::FacilitySwitchAuthorityFresh:
    case TransferPreconditionKind::BreakerPositionObserved:
    case TransferPreconditionKind::BreakerClosePermissive:
    case TransferPreconditionKind::ProtectionRelayPermissive:
    case TransferPreconditionKind::TargetBusEnergized:
    case TransferPreconditionKind::SourceBusEnergized:
      return true;
    default:
      return false;
  }
}

bool transfer_requires_generator_sync(TransferPreconditionKind kind) noexcept {
  return kind == TransferPreconditionKind::GeneratorSynchronized;
}

std::vector<TransferPreconditionKind> default_transfer_preconditions() {
  return {
      TransferPreconditionKind::FacilitySwitchAuthorityGranted,
      TransferPreconditionKind::FacilitySwitchAuthorityFresh,
      TransferPreconditionKind::BreakerPositionObserved,
      TransferPreconditionKind::BreakerClosePermissive,
      TransferPreconditionKind::ProtectionRelayPermissive,
      TransferPreconditionKind::GeneratorSynchronized,
      TransferPreconditionKind::GeneratorReadinessSatisfied,
      TransferPreconditionKind::TransferPathIdentified,
      TransferPreconditionKind::TargetBusEnergized,
      TransferPreconditionKind::SourceBusEnergized,
      TransferPreconditionKind::NoConflictingTransferPending,
  };
}

namespace {

Digest256 digest_findings(const std::vector<TransferFinding>& findings) {
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

bool path_complete(const TransferPath& path, std::string* detail) {
  if (path.breaker.empty()) {
    *detail = "no breaker reference is configured for this generator";
    return false;
  }
  if (path.source_bus.empty() || path.target_bus.empty()) {
    *detail = "the transfer path is missing a source or target bus reference";
    return false;
  }
  if (path.transfer_path.empty()) {
    *detail = "no transfer path reference is configured";
    return false;
  }
  return true;
}

}  // namespace

Status evaluate_transfer(const GeneratorId& generator, StateRevision revision,
                         const ControllerGeneration& controller,
                         const GeneratorGeneration& generation, const TransferPath& path,
                         const std::vector<TransferPreconditionRecord>& records,
                         const TransferPolicy& policy, EpochMillis now,
                         TransferEligibility* out) {
  if (out == nullptr) {
    return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                       "transfer eligibility output must not be null");
  }
  TransferEligibility result{};
  result.generator = generator;
  result.revision = revision;
  result.controller = controller;
  result.generation = generation;
  result.evaluated_at = now;

  std::vector<TransferPreconditionKind> required = default_transfer_preconditions();
  if (!policy.require_protection_permissive) {
    required.erase(std::remove(required.begin(), required.end(),
                               TransferPreconditionKind::ProtectionRelayPermissive),
                   required.end());
  }
  if (!policy.require_generator_synchronized) {
    required.erase(std::remove(required.begin(), required.end(),
                               TransferPreconditionKind::GeneratorSynchronized),
                   required.end());
  }

  std::map<TransferPreconditionKind, const TransferPreconditionRecord*> by_kind;
  for (const auto& record : records) {
    if (record.kind == TransferPreconditionKind::Unknown) continue;
    by_kind.emplace(record.kind, &record);
  }
  std::vector<TransferPreconditionKind> kinds = required;
  for (const auto& entry : by_kind) {
    if (std::find(kinds.begin(), kinds.end(), entry.first) == kinds.end()) {
      kinds.push_back(entry.first);
    }
  }
  std::sort(kinds.begin(), kinds.end());

  for (const TransferPreconditionKind kind : kinds) {
    TransferFinding finding{};
    finding.kind = kind;
    finding.required = std::find(required.begin(), required.end(), kind) != required.end();

    if (kind == TransferPreconditionKind::TransferPathIdentified) {
      std::string detail;
      if (path_complete(path, &detail)) {
        finding.satisfied = true;
        finding.outcome = EligibilityOutcome::Eligible;
        finding.detail = "breaker " + path.breaker.str() + " on path " + path.transfer_path.str();
      } else {
        finding.outcome = EligibilityOutcome::Ineligible;
        finding.code = ErrorCode::TransferTopologyReferenceInvalid;
        finding.detail = detail;
      }
      result.findings.push_back(std::move(finding));
      continue;
    }

    const auto entry = by_kind.find(kind);
    const TransferPreconditionRecord* record = entry == by_kind.end() ? nullptr : entry->second;
    if (record == nullptr) {
      finding.outcome = EligibilityOutcome::Indeterminate;
      finding.code = ErrorCode::TransferAuthorityMissing;
      finding.detail = "no evidence has been submitted for this precondition";
      result.findings.push_back(std::move(finding));
      continue;
    }
    finding.source = record->source;
    if (record->source == EvidenceSource::SyntheticAdapter) result.synthetic_evidence_used = true;

    if (record->source == EvidenceSource::CommandAcknowledgement) {
      finding.outcome = EligibilityOutcome::Indeterminate;
      finding.code = ErrorCode::ObservationNotAuthoritative;
      finding.detail = "a command acknowledgement cannot prove a transfer precondition";
      result.findings.push_back(std::move(finding));
      continue;
    }
    if (transfer_requires_external(kind) && record->source != EvidenceSource::ExternalAuthority &&
        record->source != EvidenceSource::SyntheticAdapter) {
      finding.outcome = EligibilityOutcome::Indeterminate;
      finding.code = ErrorCode::TransferAuthorityMissing;
      finding.detail = std::string("precondition requires an external authority, found '") +
                       std::string(to_string(record->source)) + "'";
      result.findings.push_back(std::move(finding));
      continue;
    }

    switch (record->state) {
      case EvidenceState::Unknown:
      case EvidenceState::Missing:
      case EvidenceState::Unsupported:
        finding.outcome = EligibilityOutcome::Indeterminate;
        finding.code = ErrorCode::TransferAuthorityMissing;
        finding.detail = record->detail.empty() ? "precondition evidence is unavailable"
                                                : record->detail;
        result.findings.push_back(std::move(finding));
        continue;
      case EvidenceState::Stale:
        finding.outcome = EligibilityOutcome::Indeterminate;
        finding.code = ErrorCode::TransferAuthorityStale;
        finding.detail = record->detail.empty() ? "precondition evidence is stale" : record->detail;
        result.findings.push_back(std::move(finding));
        continue;
      case EvidenceState::Contradictory:
      case EvidenceState::Denied:
        finding.outcome = EligibilityOutcome::Ineligible;
        finding.code = ErrorCode::TransferPreconditionFailed;
        finding.detail = record->detail.empty() ? "precondition is definitively not satisfied"
                                                : record->detail;
        result.findings.push_back(std::move(finding));
        continue;
      case EvidenceState::Present:
        break;
    }

    const Millis max_age = kind == TransferPreconditionKind::BreakerPositionObserved
                               ? policy.max_position_age_millis
                               : policy.max_token_age_millis;
    const FreshnessAssessment freshness =
        assess_freshness(record->observed_at, now, FreshnessPolicy::within(max_age));
    if (freshness.verdict == FreshnessVerdict::Stale) {
      finding.outcome = EligibilityOutcome::Indeterminate;
      finding.code = ErrorCode::TransferAuthorityStale;
      finding.detail = freshness.detail;
      result.findings.push_back(std::move(finding));
      continue;
    }
    if (freshness.verdict == FreshnessVerdict::FutureDated) {
      finding.outcome = EligibilityOutcome::Ineligible;
      finding.code = ErrorCode::TransferPreconditionFailed;
      finding.detail = freshness.detail;
      result.findings.push_back(std::move(finding));
      continue;
    }

    if (record->value.kind == TypedValueKind::Boolean && record->value.amount == 0) {
      finding.outcome = EligibilityOutcome::Ineligible;
      finding.code = ErrorCode::TransferPreconditionFailed;
      finding.detail = record->detail.empty() ? "precondition is not satisfied" : record->detail;
    } else {
      finding.satisfied = true;
      finding.outcome = EligibilityOutcome::Eligible;
      finding.detail = record->detail.empty() ? "precondition satisfied" : record->detail;
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
    result.primary_error =
        indeterminate_code == ErrorCode::Ok ? ErrorCode::TransferAuthorityMissing : indeterminate_code;
  }
  if (result.outcome == EligibilityOutcome::Eligible) result.primary_error = ErrorCode::Ok;
  result.stage = result.outcome == EligibilityOutcome::Eligible ? ValidationStage::None
                                                                : ValidationStage::Transfer;
  result.binding_digest = digest_findings(result.findings);
  *out = std::move(result);
  return Status::success();
}

}  // namespace genctl
