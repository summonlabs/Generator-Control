// Generator Control - control authority, epochs and fencing.
//
// Authority is granted externally and is never inferred. Two independent things
// must both be current for a mutation to be accepted:
//
//   * the *control epoch* - the external power control plane's grant epoch;
//   * the *incarnation* - which runtime instance holds the store's writer lease.
//
// A request states the controller generation and state revision it was planned
// against. If either has moved on, the request is refused as stale. It is never
// merged, retried or rounded forward.
//
// Emergency authority is explicit, bounded, attributable and auditable. It is not
// a bypass: safety interlocks and protection permissives remain mandatory, and only
// checks classified as Advisory may be waived.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "genctl/digest.hpp"
#include "genctl/ids.hpp"
#include "genctl/readiness.hpp"
#include "genctl/state.hpp"
#include "genctl/time.hpp"

namespace genctl {

struct AuthorityGrant {
  AuthorityClass cls{AuthorityClass::None};
  ControlEpoch epoch{};
  // Identity of the granting party (control plane, operator role, maintenance
  // authority). Free-form but bounded and never a credential.
  std::string granted_by{};
  EpochMillis granted_at{0};
  EpochMillis valid_until{0};
  // Mandatory for emergency grants: why the grant was issued.
  std::string reason{};
  // Operations the grant covers, in declaration order; empty means every operation
  // permitted by the class.
  std::vector<OperationKind> scope{};
  bool explicit_grant{false};
  Digest256 token_digest{};

  friend bool operator==(const AuthorityGrant&, const AuthorityGrant&) noexcept = default;
};

[[nodiscard]] Status validate_authority_grant(const AuthorityGrant& grant, AuthorityClass required,
                                              OperationKind operation, EpochMillis now);

// Emergency grants are bounded in duration and must carry a reason and an
// attributable granter.
struct EmergencyPolicy {
  Millis max_validity_millis{4 * kMillisPerHour};
  std::size_t min_reason_length{8};
  std::size_t max_reason_length{256};
  bool require_explicit_grant{true};
};

[[nodiscard]] const EmergencyPolicy& default_emergency_policy() noexcept;
[[nodiscard]] Status validate_emergency_grant(const AuthorityGrant& grant, const EmergencyPolicy& policy,
                                              EpochMillis now);

// Records every authority-backed decision that relaxed a default behaviour so the
// relaxation is auditable after the fact.
struct AuthorityAuditEntry {
  JournalSeq seq{};
  EpochMillis at{0};
  GeneratorId generator{};
  OperationKind operation{OperationKind::Unknown};
  AuthorityClass cls{AuthorityClass::None};
  ControlEpoch epoch{};
  std::string granted_by{};
  std::string reason{};
  std::vector<CheckKind> waived_checks{};
  Digest256 decision_digest{};

  friend bool operator==(const AuthorityAuditEntry&, const AuthorityAuditEntry&) noexcept = default;
};

// Ceiling on any authority epoch this runtime will accept. Guards against a
// corrupted or hostile store proposing an epoch far in the future, which would
// otherwise permanently fence every legitimate writer.
inline constexpr std::uint64_t kMaxAcceptedControlEpoch = 1'000'000'000ull;

}  // namespace genctl
