// Generator Control - lifecycle, operating states and the transition rule table.
//
// The rule table in this header is the single source of truth for the documented
// state machine: it declares, for every operation, the states it may start from,
// the provisional state a command may set, the authority class it needs and which
// evidence classes must be satisfied before it can be authorized.
//
// Two properties of the table are load bearing and are asserted by tests:
//   * no operation's provisional state is Synchronized - a command can never make
//     a generator synchronized, only an observation can;
//   * no operation is allowed from a retired generator, and only explicitly listed
//     recovery/service operations are allowed from maintenance, isolated or
//     faulted states.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

#include "genctl/ids.hpp"
#include "genctl/result.hpp"

namespace genctl {

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------
// Administrative envelope: what the installation allows us to do at all.
enum class LifecycleState : std::uint8_t {
  Unknown = 0,       // never commissioned or not described
  Commissioned = 1,  // in service; operations are gated by the rule table
  Maintenance = 2,   // fail closed except for listed service transitions
  Isolated = 3,      // deliberately separated from the bus; fail closed
  Retired = 4,       // permanently out of service; nothing is allowed
};

// Physical/operating condition of the generating set.
enum class OperatingState : std::uint8_t {
  Unknown = 0,            // not established; never treated as stopped
  Stopped = 1,
  Starting = 2,
  WarmUp = 3,
  ReadyUnsynchronized = 4,  // running at rated speed/voltage, breaker open
  Synchronized = 5,         // paralleled with the bus; observation only
  Cooldown = 6,
  Degraded = 7,             // running with a reduced capability
  Faulted = 8,              // tripped/locked out; requires an explicit reset
};

enum class OperatingMode : std::uint8_t {
  Unknown = 0,
  Normal = 1,
  Test = 2,
  Emergency = 3,
  Service = 4,
};

enum class SynchronizationState : std::uint8_t {
  Unknown = 0,
  NotSynchronized = 1,
  Synchronizing = 2,   // a synchronizing command is outstanding
  Synchronized = 3,    // proven by observation
  Failed = 4,
};

// Breaker position is always an external observation; this runtime never owns it.
enum class BreakerPosition : std::uint8_t {
  Unknown = 0,
  Open = 1,
  Closed = 2,
  Between = 3,  // neither open nor closed; a transitional or faulted position
  Faulted = 4,
};

// Class of authority under which an operation is attempted.
enum class AuthorityClass : std::uint8_t {
  None = 0,
  Normal = 1,
  Test = 2,
  Service = 3,
  Emergency = 4,
};

[[nodiscard]] std::string_view to_string(LifecycleState state) noexcept;
[[nodiscard]] std::string_view to_string(OperatingState state) noexcept;
[[nodiscard]] std::string_view to_string(OperatingMode mode) noexcept;
[[nodiscard]] std::string_view to_string(SynchronizationState state) noexcept;
[[nodiscard]] std::string_view to_string(BreakerPosition position) noexcept;
[[nodiscard]] std::string_view to_string(AuthorityClass cls) noexcept;

[[nodiscard]] Result<LifecycleState> parse_lifecycle_state(std::string_view text);
[[nodiscard]] Result<OperatingState> parse_operating_state(std::string_view text);
[[nodiscard]] Result<OperatingMode> parse_operating_mode(std::string_view text);
[[nodiscard]] Result<AuthorityClass> parse_authority_class(std::string_view text);

// Outcome of an eligibility evaluation. Indeterminate is a first-class result:
// it means the question cannot be answered from current evidence, which is not the
// same as "no" and is never treated as "yes".
enum class EligibilityOutcome : std::uint8_t {
  Unknown = 0,
  Eligible = 1,
  Ineligible = 2,
  Indeterminate = 3,
};

[[nodiscard]] std::string_view to_string(EligibilityOutcome outcome) noexcept;
[[nodiscard]] bool is_positive(EligibilityOutcome outcome) noexcept;

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------
enum class OperationKind : std::uint8_t {
  Unknown = 0,
  Start = 1,
  Stop = 2,
  TestStart = 3,
  TestStop = 4,
  EmergencyStart = 5,
  EmergencyStop = 6,
  FaultReset = 7,
  EnterMaintenance = 8,
  ExitMaintenance = 9,
  Isolate = 10,
  ReturnToService = 11,
  Retire = 12,
  Synchronize = 13,
  Desynchronize = 14,
  TransferToGenerator = 15,
  TransferToUtility = 16,
};

[[nodiscard]] std::string_view to_string(OperationKind kind) noexcept;
[[nodiscard]] Result<OperationKind> parse_operation_kind(std::string_view text);
// True for operations whose effect must be proven by an observation before the
// generator may be described as being in the target condition.
[[nodiscard]] bool requires_observed_effect(OperationKind kind) noexcept;
// True for operations that require the facility switch authority token (never
// owned by this runtime).
[[nodiscard]] bool requires_transfer_authority(OperationKind kind) noexcept;

constexpr std::uint32_t state_bit(OperatingState state) noexcept {
  return 1u << static_cast<std::uint32_t>(state);
}

[[nodiscard]] bool is_running_state(OperatingState state) noexcept;
[[nodiscard]] bool is_commandable_state(OperatingState state) noexcept;
[[nodiscard]] bool is_fault_state(OperatingState state) noexcept;
// True when the operating state may only ever be established by observation.
[[nodiscard]] bool is_observation_only_state(OperatingState state) noexcept;

// ---------------------------------------------------------------------------
// Transition rules
// ---------------------------------------------------------------------------
struct TransitionRule {
  OperationKind operation{OperationKind::Unknown};
  // Mode the generator must already be in. OperatingMode::Unknown means "any mode":
  // stopping is never gated on the current mode.
  OperatingMode required_mode{OperatingMode::Normal};
  // Mode established by a successful operation. OperatingMode::Unknown means the
  // mode is unchanged.
  OperatingMode mode_after{OperatingMode::Normal};
  AuthorityClass required_authority{AuthorityClass::Normal};
  // Stop-type operations are accepted under any held authority class: refusing to
  // stop is never the fail-safe direction.
  bool accept_any_authority{false};
  // Bit mask over OperatingState values: the states this operation may start from.
  std::uint32_t from_mask{0};
  // State a command may provisionally set. Observation is still required to prove
  // the physical effect; the provisional state is recorded as commanded.
  OperatingState provisional{OperatingState::Unknown};
  bool changes_lifecycle{false};
  LifecycleState lifecycle_target{LifecycleState::Unknown};
  bool require_readiness{false};
  bool require_resource{false};
  bool require_synchronization{false};
  bool require_transfer{false};
  // Interlocks are required for every actuating operation, in every mode,
  // including emergency operations.
  bool require_interlocks{true};
  // Whether a specifically labelled advisory (non-safety) check may be waived.
  bool advisory_waivable{false};
  bool require_observation{false};
  [[nodiscard]] bool allows_from(OperatingState state) const noexcept {
    return (from_mask & state_bit(state)) != 0u;
  }
};

// Returns the rule for an operation, or nullptr for OperationKind::Unknown.
[[nodiscard]] const TransitionRule* rule_for(OperationKind kind) noexcept;
// The complete table in declaration order (deterministic; used by tests and docs).
[[nodiscard]] const TransitionRule* transition_table(std::size_t* count) noexcept;

// The authority class an operating mode requires.
[[nodiscard]] AuthorityClass authority_class_for_mode(OperatingMode mode) noexcept;

// Authority matching. Test authority is deliberately disjoint from production
// authority in both directions: holding test authority never authorizes a
// production transfer, and holding normal authority never authorizes a test run.
[[nodiscard]] bool authority_permits(AuthorityClass held, AuthorityClass required) noexcept;

// ---------------------------------------------------------------------------
// Evaluation of the state gates (pure functions, no state mutation)
// ---------------------------------------------------------------------------
struct StateGateResult {
  bool allowed{false};
  OperatingState provisional{OperatingState::Unknown};
  ErrorCode code{ErrorCode::Ok};
  std::string detail{};

  [[nodiscard]] bool ok() const noexcept { return allowed; }
};

// Checks lifecycle + operating state + mode against the rule table and returns the
// earliest failure in rule order. This is the Transition stage of the documented
// validation precedence; fencing and identity are checked before it.
[[nodiscard]] StateGateResult gate_transition(const TransitionRule& rule, LifecycleState lifecycle,
                                              OperatingState operating, OperatingMode mode,
                                              ErrorCode lifecycle_error);

[[nodiscard]] ErrorCode lifecycle_error_for(LifecycleState state) noexcept;

}  // namespace genctl
