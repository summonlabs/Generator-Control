// Generator Control - authority grants, emergency authority and audit.
#include "genctl/authority.hpp"

#include "genctl/readiness.hpp"

namespace genctl {

const EmergencyPolicy& default_emergency_policy() noexcept {
  static const EmergencyPolicy policy{};
  return policy;
}

Status validate_authority_grant(const AuthorityGrant& grant, AuthorityClass required,
                                OperationKind operation, EpochMillis now) {
  if (required == AuthorityClass::None) return Status::success();
  if (grant.cls == AuthorityClass::None) {
    return make_status(ErrorCode::AuthorityMissing, ValidationStage::Authority,
                       std::string("operation '") + std::string(to_string(operation)) +
                           "' requires " + std::string(to_string(required)) +
                           " authority and none was supplied");
  }
  if (grant.cls != required) {
    return make_status(ErrorCode::AuthorityClassMismatch, ValidationStage::Authority,
                       std::string("operation '") + std::string(to_string(operation)) +
                           "' requires " + std::string(to_string(required)) +
                           " authority but the grant is " + std::string(to_string(grant.cls)));
  }
  if (!grant.scope.empty()) {
    bool in_scope = false;
    for (const OperationKind scoped : grant.scope) {
      if (scoped == operation) {
        in_scope = true;
        break;
      }
    }
    if (!in_scope) {
      return make_status(ErrorCode::AuthorityClassMismatch, ValidationStage::Authority,
                         std::string("the ") + std::string(to_string(grant.cls)) +
                             " grant does not cover operation '" +
                             std::string(to_string(operation)) + "'");
    }
  }
  if (grant.valid_until != 0 && now > grant.valid_until) {
    return make_status(ErrorCode::AuthorityExpired, ValidationStage::Authority,
                       "authority grant expired at " + format_epoch_millis(grant.valid_until));
  }
  if (grant.granted_by.empty()) {
    return make_status(ErrorCode::AuthorityMissing, ValidationStage::Authority,
                       "authority grant has no attributable granter");
  }
  if (grant.cls == AuthorityClass::Emergency) {
    return validate_emergency_grant(grant, default_emergency_policy(), now);
  }
  return Status::success();
}

Status validate_emergency_grant(const AuthorityGrant& grant, const EmergencyPolicy& policy,
                                EpochMillis now) {
  if (grant.cls != AuthorityClass::Emergency) {
    return make_status(ErrorCode::EmergencyAuthorityRequired, ValidationStage::Authority,
                       "an emergency grant must carry the emergency authority class");
  }
  if (policy.require_explicit_grant && !grant.explicit_grant) {
    return make_status(ErrorCode::EmergencyAuthorityNotExplicit, ValidationStage::Authority,
                       "emergency authority must be granted explicitly; it is never inferred from "
                       "an alarm, a fault or an operating state");
  }
  if (grant.reason.size() < policy.min_reason_length) {
    return make_status(ErrorCode::EmergencyAuthorityNotExplicit, ValidationStage::Authority,
                       "emergency authority requires a reason of at least " +
                           std::to_string(policy.min_reason_length) + " characters");
  }
  if (grant.reason.size() > policy.max_reason_length) {
    return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Authority,
                       "emergency reason exceeds " + std::to_string(policy.max_reason_length) +
                           " characters");
  }
  if (grant.valid_until == 0) {
    return make_status(ErrorCode::EmergencyAuthorityNotExplicit, ValidationStage::Authority,
                       "emergency authority must carry a bounded validity window");
  }
  if (grant.valid_until <= grant.granted_at) {
    return make_status(ErrorCode::InvalidArgument, ValidationStage::Authority,
                       "emergency authority validity window is empty or inverted");
  }
  const Result<Millis> window = sub_millis(grant.valid_until, grant.granted_at);
  if (!window.ok()) {
    return make_status(ErrorCode::ArithmeticOverflow, ValidationStage::Authority,
                       "emergency authority window overflows");
  }
  if (window.value() > policy.max_validity_millis) {
    return make_status(ErrorCode::ValueOutOfRange, ValidationStage::Authority,
                       "emergency authority window of " + format_millis(window.value()) +
                           " exceeds the maximum of " +
                           format_millis(policy.max_validity_millis));
  }
  if (now > grant.valid_until) {
    return make_status(ErrorCode::AuthorityExpired, ValidationStage::Authority,
                       "emergency authority expired at " + format_epoch_millis(grant.valid_until));
  }
  if (grant.granted_by.empty()) {
    return make_status(ErrorCode::AuthorityMissing, ValidationStage::Authority,
                       "emergency authority has no attributable granter");
  }
  return Status::success();
}

}  // namespace genctl
