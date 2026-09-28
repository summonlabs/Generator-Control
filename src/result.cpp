// Generator Control - error model implementation.
#include "genctl/result.hpp"

#include "genctl/version.hpp"

namespace genctl {

std::string version_string() {
  return std::to_string(kVersionMajor) + "." + std::to_string(kVersionMinor) + "." +
         std::to_string(kVersionPatch);
}

std::string version_banner() {
  return "Generator Control " + version_string() + " (store format " +
         std::to_string(kStoreFormatVersion) + ")";
}

std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::Ok: return "Ok";
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::InvalidIdentifier: return "InvalidIdentifier";
    case ErrorCode::LengthLimitExceeded: return "LengthLimitExceeded";
    case ErrorCode::MalformedEncoding: return "MalformedEncoding";
    case ErrorCode::ValueOutOfRange: return "ValueOutOfRange";
    case ErrorCode::ArithmeticOverflow: return "ArithmeticOverflow";
    case ErrorCode::DivisionByZero: return "DivisionByZero";
    case ErrorCode::UnsupportedUnit: return "UnsupportedUnit";
    case ErrorCode::UnsupportedField: return "UnsupportedField";
    case ErrorCode::UnknownGenerator: return "UnknownGenerator";
    case ErrorCode::DuplicateGenerator: return "DuplicateGenerator";
    case ErrorCode::GeneratorLimitExceeded: return "GeneratorLimitExceeded";
    case ErrorCode::GeneratorNotBound: return "GeneratorNotBound";
    case ErrorCode::DuplicateIdentity: return "DuplicateIdentity";
    case ErrorCode::IdempotencyKeyMissing: return "IdempotencyKeyMissing";
    case ErrorCode::IdempotencyKeyConflict: return "IdempotencyKeyConflict";
    case ErrorCode::IdempotencyWindowExpired: return "IdempotencyWindowExpired";
    case ErrorCode::StaleControlEpoch: return "StaleControlEpoch";
    case ErrorCode::StaleIncarnation: return "StaleIncarnation";
    case ErrorCode::StaleStateRevision: return "StaleStateRevision";
    case ErrorCode::StaleGeneratorGeneration: return "StaleGeneratorGeneration";
    case ErrorCode::StaleEvidenceBinding: return "StaleEvidenceBinding";
    case ErrorCode::FencedWriter: return "FencedWriter";
    case ErrorCode::StaleAttempt: return "StaleAttempt";
    case ErrorCode::LifecycleClosed: return "LifecycleClosed";
    case ErrorCode::IllegalTransition: return "IllegalTransition";
    case ErrorCode::OperatingStateUnknown: return "OperatingStateUnknown";
    case ErrorCode::AttemptUnresolved: return "AttemptUnresolved";
    case ErrorCode::FaultResetRequired: return "FaultResetRequired";
    case ErrorCode::MaintenanceActive: return "MaintenanceActive";
    case ErrorCode::GeneratorIsolated: return "GeneratorIsolated";
    case ErrorCode::GeneratorRetired: return "GeneratorRetired";
    case ErrorCode::ModeNotPermitted: return "ModeNotPermitted";
    case ErrorCode::AuthorityMissing: return "AuthorityMissing";
    case ErrorCode::AuthorityExpired: return "AuthorityExpired";
    case ErrorCode::AuthorityClassMismatch: return "AuthorityClassMismatch";
    case ErrorCode::EmergencyAuthorityRequired: return "EmergencyAuthorityRequired";
    case ErrorCode::EmergencyAuthorityNotExplicit: return "EmergencyAuthorityNotExplicit";
    case ErrorCode::TestAuthorityRequired: return "TestAuthorityRequired";
    case ErrorCode::ServiceAuthorityRequired: return "ServiceAuthorityRequired";
    case ErrorCode::TestOperationNotPermitted: return "TestOperationNotPermitted";
    case ErrorCode::EvidenceMissing: return "EvidenceMissing";
    case ErrorCode::EvidenceStale: return "EvidenceStale";
    case ErrorCode::EvidenceUnknown: return "EvidenceUnknown";
    case ErrorCode::EvidenceUnsupported: return "EvidenceUnsupported";
    case ErrorCode::EvidenceContradictory: return "EvidenceContradictory";
    case ErrorCode::EvidenceDenied: return "EvidenceDenied";
    case ErrorCode::EvidenceFutureDated: return "EvidenceFutureDated";
    case ErrorCode::EvidenceProvenanceRejected: return "EvidenceProvenanceRejected";
    case ErrorCode::ReadinessNotSatisfied: return "ReadinessNotSatisfied";
    case ErrorCode::InterlockEngaged: return "InterlockEngaged";
    case ErrorCode::InterlockEvidenceUnavailable: return "InterlockEvidenceUnavailable";
    case ErrorCode::ResourceEvidenceMissing: return "ResourceEvidenceMissing";
    case ErrorCode::ResourceEvidenceStale: return "ResourceEvidenceStale";
    case ErrorCode::ResourceInsufficient: return "ResourceInsufficient";
    case ErrorCode::ResourceUnitMismatch: return "ResourceUnitMismatch";
    case ErrorCode::ResourceRuntimeUnknown: return "ResourceRuntimeUnknown";
    case ErrorCode::ResourceCapacityUnknown: return "ResourceCapacityUnknown";
    case ErrorCode::SynchronizationNotEligible: return "SynchronizationNotEligible";
    case ErrorCode::SynchronizationEvidenceMissing: return "SynchronizationEvidenceMissing";
    case ErrorCode::SynchronizationEvidenceStale: return "SynchronizationEvidenceStale";
    case ErrorCode::SynchronizationPreconditionFailed: return "SynchronizationPreconditionFailed";
    case ErrorCode::SynchronizationAuthorityMissing: return "SynchronizationAuthorityMissing";
    case ErrorCode::TransferNotEligible: return "TransferNotEligible";
    case ErrorCode::TransferAuthorityMissing: return "TransferAuthorityMissing";
    case ErrorCode::TransferAuthorityStale: return "TransferAuthorityStale";
    case ErrorCode::TransferPreconditionFailed: return "TransferPreconditionFailed";
    case ErrorCode::TransferTopologyReferenceInvalid: return "TransferTopologyReferenceInvalid";
    case ErrorCode::AdapterUnavailable: return "AdapterUnavailable";
    case ErrorCode::AdapterRejected: return "AdapterRejected";
    case ErrorCode::AdapterUnsupported: return "AdapterUnsupported";
    case ErrorCode::AdapterBusy: return "AdapterBusy";
    case ErrorCode::AdapterProtocolError: return "AdapterProtocolError";
    case ErrorCode::AttemptNotFound: return "AttemptNotFound";
    case ErrorCode::AttemptAlreadyResolved: return "AttemptAlreadyResolved";
    case ErrorCode::EffectNotObserved: return "EffectNotObserved";
    case ErrorCode::EffectContradictory: return "EffectContradictory";
    case ErrorCode::EffectVerificationFailed: return "EffectVerificationFailed";
    case ErrorCode::CommandNotAcknowledged: return "CommandNotAcknowledged";
    case ErrorCode::ObservationNotAuthoritative: return "ObservationNotAuthoritative";
    case ErrorCode::StoreNotFound: return "StoreNotFound";
    case ErrorCode::StoreOpenFailed: return "StoreOpenFailed";
    case ErrorCode::StoreClosed: return "StoreClosed";
    case ErrorCode::StoreLocked: return "StoreLocked";
    case ErrorCode::StoreReadOnly: return "StoreReadOnly";
    case ErrorCode::StoreCorrupt: return "StoreCorrupt";
    case ErrorCode::StoreTruncated: return "StoreTruncated";
    case ErrorCode::StoreOversize: return "StoreOversize";
    case ErrorCode::StoreVersionUnsupported: return "StoreVersionUnsupported";
    case ErrorCode::StoreIntegrityFailure: return "StoreIntegrityFailure";
    case ErrorCode::StoreRollbackDetected: return "StoreRollbackDetected";
    case ErrorCode::StorePublicationFailed: return "StorePublicationFailed";
    case ErrorCode::StoreFlushFailed: return "StoreFlushFailed";
    case ErrorCode::StoreGenerationMissing: return "StoreGenerationMissing";
    case ErrorCode::StoreRecoveryRefused: return "StoreRecoveryRefused";
    case ErrorCode::PathInvalid: return "PathInvalid";
    case ErrorCode::PathTraversal: return "PathTraversal";
    case ErrorCode::PathDeviceName: return "PathDeviceName";
    case ErrorCode::PathReparsePoint: return "PathReparsePoint";
    case ErrorCode::PathNotAbsolute: return "PathNotAbsolute";
    case ErrorCode::PathTooLong: return "PathTooLong";
    case ErrorCode::PathEncodingInvalid: return "PathEncodingInvalid";
    case ErrorCode::PathNotDirectory: return "PathNotDirectory";
    case ErrorCode::PathIsDirectory: return "PathIsDirectory";
    case ErrorCode::NotImplemented: return "NotImplemented";
    case ErrorCode::Internal: return "Internal";
    case ErrorCode::Cancelled: return "Cancelled";
  }
  return "UnknownErrorCode";
}

std::string_view to_string(ErrorCategory category) noexcept {
  switch (category) {
    case ErrorCategory::Ok: return "ok";
    case ErrorCategory::InvalidRequest: return "invalid-request";
    case ErrorCategory::Identity: return "identity";
    case ErrorCategory::Idempotency: return "idempotency";
    case ErrorCategory::StaleAuthority: return "stale-authority";
    case ErrorCategory::Lifecycle: return "lifecycle";
    case ErrorCategory::Mode: return "mode";
    case ErrorCategory::Evidence: return "evidence";
    case ErrorCategory::Resource: return "resource";
    case ErrorCategory::Synchronization: return "synchronization";
    case ErrorCategory::Transfer: return "transfer";
    case ErrorCategory::Interlock: return "interlock";
    case ErrorCategory::Adapter: return "adapter";
    case ErrorCategory::Persistence: return "persistence";
    case ErrorCategory::Internal: return "internal";
  }
  return "internal";
}

std::string_view to_string(ValidationStage stage) noexcept {
  switch (stage) {
    case ValidationStage::None: return "none";
    case ValidationStage::Format: return "format";
    case ValidationStage::Identity: return "identity";
    case ValidationStage::IdempotencyReplay: return "idempotency-replay";
    case ValidationStage::IdempotencyConflict: return "idempotency-conflict";
    case ValidationStage::Fencing: return "fencing";
    case ValidationStage::Lifecycle: return "lifecycle";
    case ValidationStage::Transition: return "transition";
    case ValidationStage::Mode: return "mode";
    case ValidationStage::Authority: return "authority";
    case ValidationStage::Interlock: return "interlock";
    case ValidationStage::Readiness: return "readiness";
    case ValidationStage::Resource: return "resource";
    case ValidationStage::Synchronization: return "synchronization";
    case ValidationStage::Transfer: return "transfer";
    case ValidationStage::Reservation: return "reservation";
    case ValidationStage::Actuation: return "actuation";
    case ValidationStage::Observation: return "observation";
    case ValidationStage::Verification: return "verification";
    case ValidationStage::Persistence: return "persistence";
    case ValidationStage::Internal: return "internal";
  }
  return "internal";
}

ErrorCategory category_of(ErrorCode code) noexcept {
  const auto value = static_cast<std::uint16_t>(code);
  if (value == 0) return ErrorCategory::Ok;
  if (value >= 100 && value < 200) return ErrorCategory::InvalidRequest;
  if (value >= 200 && value < 300) return ErrorCategory::Identity;
  if (value >= 300 && value < 400) return ErrorCategory::Idempotency;
  if (value >= 400 && value < 500) return ErrorCategory::StaleAuthority;
  if (value >= 500 && value < 600) return ErrorCategory::Lifecycle;
  if (value >= 600 && value < 700) return ErrorCategory::Mode;
  if (value >= 700 && value < 800) {
    if (code == ErrorCode::InterlockEngaged || code == ErrorCode::InterlockEvidenceUnavailable) {
      return ErrorCategory::Interlock;
    }
    return ErrorCategory::Evidence;
  }
  if (value >= 800 && value < 900) return ErrorCategory::Resource;
  if (value >= 900 && value < 1000) return ErrorCategory::Synchronization;
  if (value >= 1000 && value < 1100) return ErrorCategory::Transfer;
  if (value >= 1100 && value < 1200) return ErrorCategory::Adapter;
  if (value >= 1200 && value < 1300) return ErrorCategory::Persistence;
  return ErrorCategory::Internal;
}

ValidationStage default_stage(ErrorCode code) noexcept {
  const auto value = static_cast<std::uint16_t>(code);
  if (value == 0) return ValidationStage::None;
  if (value >= 100 && value < 200) return ValidationStage::Format;
  if (value >= 200 && value < 300) return ValidationStage::Identity;
  if (code == ErrorCode::IdempotencyKeyConflict) return ValidationStage::IdempotencyConflict;
  if (value >= 300 && value < 400) return ValidationStage::IdempotencyReplay;
  if (value >= 400 && value < 500) return ValidationStage::Fencing;
  if (code == ErrorCode::MaintenanceActive || code == ErrorCode::GeneratorIsolated ||
      code == ErrorCode::GeneratorRetired || code == ErrorCode::LifecycleClosed) {
    return ValidationStage::Lifecycle;
  }
  if (value >= 500 && value < 600) return ValidationStage::Transition;
  if (value >= 600 && value < 700) return ValidationStage::Mode;
  if (value >= 700 && value < 800) {
    if (code == ErrorCode::InterlockEngaged || code == ErrorCode::InterlockEvidenceUnavailable) {
      return ValidationStage::Interlock;
    }
    return ValidationStage::Readiness;
  }
  if (value >= 800 && value < 900) return ValidationStage::Resource;
  if (value >= 900 && value < 1000) return ValidationStage::Synchronization;
  if (value >= 1000 && value < 1100) return ValidationStage::Transfer;
  if (value >= 1100 && value < 1200) return ValidationStage::Actuation;
  if (value >= 1200 && value < 1300) return ValidationStage::Persistence;
  return ValidationStage::Internal;
}

bool is_transient(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::AdapterBusy:
    case ErrorCode::AdapterUnavailable:
    case ErrorCode::EvidenceStale:
    case ErrorCode::EvidenceMissing:
    case ErrorCode::EvidenceUnknown:
    case ErrorCode::ResourceEvidenceStale:
    case ErrorCode::ResourceEvidenceMissing:
    case ErrorCode::SynchronizationEvidenceMissing:
    case ErrorCode::SynchronizationEvidenceStale:
    case ErrorCode::TransferAuthorityStale:
    case ErrorCode::AttemptUnresolved:
    case ErrorCode::StoreLocked:
      return true;
    default:
      return false;
  }
}

int exit_code(ErrorCode code) noexcept {
  switch (category_of(code)) {
    case ErrorCategory::Ok: return 0;
    case ErrorCategory::InvalidRequest: return 2;
    case ErrorCategory::Identity: return 2;
    case ErrorCategory::Idempotency: return 8;
    case ErrorCategory::StaleAuthority: return 5;
    case ErrorCategory::Lifecycle: return 3;
    case ErrorCategory::Mode: return 3;
    case ErrorCategory::Evidence: return 4;
    case ErrorCategory::Resource: return 4;
    case ErrorCategory::Synchronization: return 4;
    case ErrorCategory::Transfer: return 4;
    case ErrorCategory::Interlock: return 3;
    case ErrorCategory::Adapter: return 6;
    case ErrorCategory::Persistence: return 7;
    case ErrorCategory::Internal: return 1;
  }
  return 1;
}

std::string Status::to_string() const {
  if (ok()) return "Ok";
  std::string out(genctl::to_string(code_));
  out += " [";
  out += genctl::to_string(stage_);
  out += "]: ";
  out += message_;
  return out;
}

Status make_status(ErrorCode code, std::string message) {
  return Status{code, default_stage(code), std::move(message)};
}

Status make_status(ErrorCode code, ValidationStage stage, std::string message) {
  return Status{code, stage, std::move(message)};
}

}  // namespace genctl
