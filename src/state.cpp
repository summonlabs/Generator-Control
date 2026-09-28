// Generator Control - state machine, transition table and authority matching.
#include "genctl/state.hpp"

#include <array>

namespace genctl {
namespace {

constexpr std::uint32_t kMaskAny = 0x1FFu;  // every OperatingState, including Unknown

constexpr std::uint32_t mask_of(std::initializer_list<OperatingState> states) {
  std::uint32_t mask = 0;
  for (const OperatingState state : states) {
    mask |= state_bit(state);
  }
  return mask;
}

// Operations that change only the administrative lifecycle.
constexpr std::uint32_t kFaultResetMask = mask_of({OperatingState::Faulted});
constexpr std::uint32_t kRunningMask = mask_of({OperatingState::Starting, OperatingState::WarmUp,
                                                OperatingState::ReadyUnsynchronized,
                                                OperatingState::Synchronized,
                                                OperatingState::Degraded});
constexpr std::uint32_t kStopMask = mask_of({OperatingState::Starting, OperatingState::WarmUp,
                                             OperatingState::ReadyUnsynchronized,
                                             OperatingState::Synchronized,
                                             OperatingState::Degraded,
                                             OperatingState::Cooldown,
                                             OperatingState::Stopped});
constexpr std::uint32_t kStartMask = mask_of({OperatingState::Stopped, OperatingState::Cooldown,
                                              OperatingState::Degraded});
constexpr std::uint32_t kSyncMask = mask_of({OperatingState::ReadyUnsynchronized});
constexpr std::uint32_t kTransferMask = mask_of({OperatingState::ReadyUnsynchronized,
                                                 OperatingState::Synchronized});
constexpr std::uint32_t kServiceMask = mask_of({OperatingState::Unknown, OperatingState::Stopped,
                                                OperatingState::Cooldown, OperatingState::Degraded,
                                                OperatingState::Faulted});
constexpr std::uint32_t kMaintenanceEntryMask = mask_of(
    {OperatingState::Stopped, OperatingState::Cooldown, OperatingState::Degraded,
     OperatingState::ReadyUnsynchronized, OperatingState::Faulted});

constexpr TransitionRule kRules[] = {
    {OperationKind::Start, OperatingMode::Normal, OperatingMode::Normal, AuthorityClass::Normal,
     false, kStartMask, OperatingState::Starting, false, LifecycleState::Unknown, true, true, false,
     false, true, false, true},
    {OperationKind::Stop, OperatingMode::Unknown, OperatingMode::Unknown, AuthorityClass::Normal,
     true, kStopMask, OperatingState::Cooldown, false, LifecycleState::Unknown, false, false, false,
     false, false, false, false},
    {OperationKind::TestStart, OperatingMode::Test, OperatingMode::Test, AuthorityClass::Test,
     false, kStartMask, OperatingState::Starting, false, LifecycleState::Unknown, true, true, false,
     false, true, false, true},
    {OperationKind::TestStop, OperatingMode::Unknown, OperatingMode::Unknown, AuthorityClass::Test,
     true, kStopMask, OperatingState::Cooldown, false, LifecycleState::Unknown, false, false, false,
     false, false, false, false},
    {OperationKind::EmergencyStart, OperatingMode::Emergency, OperatingMode::Emergency,
     AuthorityClass::Emergency, false, kStartMask, OperatingState::Starting, false,
     LifecycleState::Unknown, true, true, false, false, true, true, true},
    {OperationKind::EmergencyStop, OperatingMode::Unknown, OperatingMode::Unknown,
     AuthorityClass::Emergency, true, kStopMask, OperatingState::Cooldown, false,
     LifecycleState::Unknown, false, false, false, false, false, false, false},
    {OperationKind::FaultReset, OperatingMode::Service, OperatingMode::Service, AuthorityClass::Service,
     false, kFaultResetMask, OperatingState::Stopped, false, LifecycleState::Unknown, false, false,
     false, false, true, false, false},
    {OperationKind::EnterMaintenance, OperatingMode::Service, OperatingMode::Service,
     AuthorityClass::Service, false, kMaintenanceEntryMask, OperatingState::Unknown, true,
     LifecycleState::Maintenance, false, false, false, false, false, false, false},
    {OperationKind::ExitMaintenance, OperatingMode::Service, OperatingMode::Normal,
     AuthorityClass::Service, false, kServiceMask, OperatingState::Unknown, true,
     LifecycleState::Commissioned, false, false, false, false, false, false, false},
    {OperationKind::Isolate, OperatingMode::Service, OperatingMode::Service, AuthorityClass::Service,
     false, kServiceMask, OperatingState::Unknown, true, LifecycleState::Isolated, false, false,
     false, false, false, false, false},
    {OperationKind::ReturnToService, OperatingMode::Service, OperatingMode::Normal,
     AuthorityClass::Service, false, kMaskAny, OperatingState::Stopped, true,
     LifecycleState::Commissioned, false, false, false, false, false, false, false},
    {OperationKind::Retire, OperatingMode::Service, OperatingMode::Service, AuthorityClass::Service,
     false, kMaskAny, OperatingState::Stopped, true, LifecycleState::Retired, false, false, false,
     false, false, false, false},
    {OperationKind::Synchronize, OperatingMode::Normal, OperatingMode::Normal, AuthorityClass::Normal,
     false, kSyncMask, OperatingState::Unknown, false, LifecycleState::Unknown, true, false, true,
     false, true, false, true},
    {OperationKind::Desynchronize, OperatingMode::Normal, OperatingMode::Normal,
     AuthorityClass::Normal, false, mask_of({OperatingState::Synchronized}), OperatingState::Unknown,
     false, LifecycleState::Unknown, false, false, false, false, true, false, true},
    {OperationKind::TransferToGenerator, OperatingMode::Normal, OperatingMode::Normal,
     AuthorityClass::Normal, false, kTransferMask, OperatingState::Unknown, false,
     LifecycleState::Unknown, true, false, false, true, true, false, true},
    {OperationKind::TransferToUtility, OperatingMode::Normal, OperatingMode::Normal,
     AuthorityClass::Normal, false, mask_of({OperatingState::Synchronized}), OperatingState::Unknown,
     false, LifecycleState::Unknown, true, false, false, true, true, false, true},
};

}  // namespace

std::string_view to_string(LifecycleState state) noexcept {
  switch (state) {
    case LifecycleState::Unknown: return "unknown";
    case LifecycleState::Commissioned: return "commissioned";
    case LifecycleState::Maintenance: return "maintenance";
    case LifecycleState::Isolated: return "isolated";
    case LifecycleState::Retired: return "retired";
  }
  return "unknown";
}

std::string_view to_string(OperatingState state) noexcept {
  switch (state) {
    case OperatingState::Unknown: return "unknown";
    case OperatingState::Stopped: return "stopped";
    case OperatingState::Starting: return "starting";
    case OperatingState::WarmUp: return "warm-up";
    case OperatingState::ReadyUnsynchronized: return "ready-unsynchronized";
    case OperatingState::Synchronized: return "synchronized";
    case OperatingState::Cooldown: return "cooldown";
    case OperatingState::Degraded: return "degraded";
    case OperatingState::Faulted: return "faulted";
  }
  return "unknown";
}

std::string_view to_string(OperatingMode mode) noexcept {
  switch (mode) {
    case OperatingMode::Unknown: return "unknown";
    case OperatingMode::Normal: return "normal";
    case OperatingMode::Test: return "test";
    case OperatingMode::Emergency: return "emergency";
    case OperatingMode::Service: return "service";
  }
  return "unknown";
}

std::string_view to_string(SynchronizationState state) noexcept {
  switch (state) {
    case SynchronizationState::Unknown: return "unknown";
    case SynchronizationState::NotSynchronized: return "not-synchronized";
    case SynchronizationState::Synchronizing: return "synchronizing";
    case SynchronizationState::Synchronized: return "synchronized";
    case SynchronizationState::Failed: return "failed";
  }
  return "unknown";
}

std::string_view to_string(BreakerPosition position) noexcept {
  switch (position) {
    case BreakerPosition::Unknown: return "unknown";
    case BreakerPosition::Open: return "open";
    case BreakerPosition::Closed: return "closed";
    case BreakerPosition::Between: return "between";
    case BreakerPosition::Faulted: return "faulted";
  }
  return "unknown";
}

std::string_view to_string(AuthorityClass cls) noexcept {
  switch (cls) {
    case AuthorityClass::None: return "none";
    case AuthorityClass::Normal: return "normal";
    case AuthorityClass::Test: return "test";
    case AuthorityClass::Service: return "service";
    case AuthorityClass::Emergency: return "emergency";
  }
  return "none";
}

std::string_view to_string(EligibilityOutcome outcome) noexcept {
  switch (outcome) {
    case EligibilityOutcome::Unknown: return "unknown";
    case EligibilityOutcome::Eligible: return "eligible";
    case EligibilityOutcome::Ineligible: return "ineligible";
    case EligibilityOutcome::Indeterminate: return "indeterminate";
  }
  return "unknown";
}

bool is_positive(EligibilityOutcome outcome) noexcept {
  return outcome == EligibilityOutcome::Eligible;
}

std::string_view to_string(OperationKind kind) noexcept {
  switch (kind) {
    case OperationKind::Unknown: return "unknown";
    case OperationKind::Start: return "start";
    case OperationKind::Stop: return "stop";
    case OperationKind::TestStart: return "test-start";
    case OperationKind::TestStop: return "test-stop";
    case OperationKind::EmergencyStart: return "emergency-start";
    case OperationKind::EmergencyStop: return "emergency-stop";
    case OperationKind::FaultReset: return "fault-reset";
    case OperationKind::EnterMaintenance: return "enter-maintenance";
    case OperationKind::ExitMaintenance: return "exit-maintenance";
    case OperationKind::Isolate: return "isolate";
    case OperationKind::ReturnToService: return "return-to-service";
    case OperationKind::Retire: return "retire";
    case OperationKind::Synchronize: return "synchronize";
    case OperationKind::Desynchronize: return "desynchronize";
    case OperationKind::TransferToGenerator: return "transfer-to-generator";
    case OperationKind::TransferToUtility: return "transfer-to-utility";
  }
  return "unknown";
}

template <typename Enum, std::size_t N>
Result<Enum> parse_enum(std::string_view text, const std::array<std::pair<std::string_view, Enum>, N>& table,
                        const char* what) {
  for (const auto& entry : table) {
    if (entry.first == text) return entry.second;
  }
  return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                     std::string("unrecognised ") + what + " '" + std::string(text) + "'");
}

Result<LifecycleState> parse_lifecycle_state(std::string_view text) {
  static const std::array<std::pair<std::string_view, LifecycleState>, 5> kTable = {{
      {"unknown", LifecycleState::Unknown},
      {"commissioned", LifecycleState::Commissioned},
      {"maintenance", LifecycleState::Maintenance},
      {"isolated", LifecycleState::Isolated},
      {"retired", LifecycleState::Retired},
  }};
  return parse_enum<LifecycleState>(text, kTable, "lifecycle state");
}

Result<OperatingState> parse_operating_state(std::string_view text) {
  static const std::array<std::pair<std::string_view, OperatingState>, 9> kTable = {{
      {"unknown", OperatingState::Unknown},
      {"stopped", OperatingState::Stopped},
      {"starting", OperatingState::Starting},
      {"warm-up", OperatingState::WarmUp},
      {"ready-unsynchronized", OperatingState::ReadyUnsynchronized},
      {"synchronized", OperatingState::Synchronized},
      {"cooldown", OperatingState::Cooldown},
      {"degraded", OperatingState::Degraded},
      {"faulted", OperatingState::Faulted},
  }};
  return parse_enum<OperatingState>(text, kTable, "operating state");
}

Result<OperatingMode> parse_operating_mode(std::string_view text) {
  static const std::array<std::pair<std::string_view, OperatingMode>, 5> kTable = {{
      {"unknown", OperatingMode::Unknown},
      {"normal", OperatingMode::Normal},
      {"test", OperatingMode::Test},
      {"emergency", OperatingMode::Emergency},
      {"service", OperatingMode::Service},
  }};
  return parse_enum<OperatingMode>(text, kTable, "operating mode");
}

Result<AuthorityClass> parse_authority_class(std::string_view text) {
  static const std::array<std::pair<std::string_view, AuthorityClass>, 5> kTable = {{
      {"none", AuthorityClass::None},
      {"normal", AuthorityClass::Normal},
      {"test", AuthorityClass::Test},
      {"service", AuthorityClass::Service},
      {"emergency", AuthorityClass::Emergency},
  }};
  return parse_enum<AuthorityClass>(text, kTable, "authority class");
}

Result<OperationKind> parse_operation_kind(std::string_view text) {
  static const std::array<std::pair<std::string_view, OperationKind>, 17> kTable = {{
      {"unknown", OperationKind::Unknown},
      {"start", OperationKind::Start},
      {"stop", OperationKind::Stop},
      {"test-start", OperationKind::TestStart},
      {"test-stop", OperationKind::TestStop},
      {"emergency-start", OperationKind::EmergencyStart},
      {"emergency-stop", OperationKind::EmergencyStop},
      {"fault-reset", OperationKind::FaultReset},
      {"enter-maintenance", OperationKind::EnterMaintenance},
      {"exit-maintenance", OperationKind::ExitMaintenance},
      {"isolate", OperationKind::Isolate},
      {"return-to-service", OperationKind::ReturnToService},
      {"retire", OperationKind::Retire},
      {"synchronize", OperationKind::Synchronize},
      {"desynchronize", OperationKind::Desynchronize},
      {"transfer-to-generator", OperationKind::TransferToGenerator},
      {"transfer-to-utility", OperationKind::TransferToUtility},
  }};
  return parse_enum<OperationKind>(text, kTable, "operation");
}

bool requires_observed_effect(OperationKind kind) noexcept {
  const TransitionRule* rule = rule_for(kind);
  return rule != nullptr && rule->require_observation;
}

bool requires_transfer_authority(OperationKind kind) noexcept {
  const TransitionRule* rule = rule_for(kind);
  return rule != nullptr && rule->require_transfer;
}

bool is_running_state(OperatingState state) noexcept {
  switch (state) {
    case OperatingState::Starting:
    case OperatingState::WarmUp:
    case OperatingState::ReadyUnsynchronized:
    case OperatingState::Synchronized:
    case OperatingState::Degraded:
      return true;
    default:
      return false;
  }
}

bool is_commandable_state(OperatingState state) noexcept {
  return state != OperatingState::Unknown;
}

bool is_fault_state(OperatingState state) noexcept { return state == OperatingState::Faulted; }

bool is_observation_only_state(OperatingState state) noexcept {
  return state == OperatingState::Synchronized;
}

const TransitionRule* rule_for(OperationKind kind) noexcept {
  for (const auto& rule : kRules) {
    if (rule.operation == kind) return &rule;
  }
  return nullptr;
}

const TransitionRule* transition_table(std::size_t* count) noexcept {
  if (count != nullptr) *count = sizeof(kRules) / sizeof(kRules[0]);
  return kRules;
}

AuthorityClass authority_class_for_mode(OperatingMode mode) noexcept {
  switch (mode) {
    case OperatingMode::Normal: return AuthorityClass::Normal;
    case OperatingMode::Test: return AuthorityClass::Test;
    case OperatingMode::Emergency: return AuthorityClass::Emergency;
    case OperatingMode::Service: return AuthorityClass::Service;
    case OperatingMode::Unknown:
    default:
      return AuthorityClass::None;
  }
}

bool authority_permits(AuthorityClass held, AuthorityClass required) noexcept {
  if (required == AuthorityClass::None) return held != AuthorityClass::None;
  return held == required;
}

ErrorCode lifecycle_error_for(LifecycleState state) noexcept {
  switch (state) {
    case LifecycleState::Commissioned: return ErrorCode::Ok;
    case LifecycleState::Maintenance: return ErrorCode::MaintenanceActive;
    case LifecycleState::Isolated: return ErrorCode::GeneratorIsolated;
    case LifecycleState::Retired: return ErrorCode::GeneratorRetired;
    case LifecycleState::Unknown: return ErrorCode::LifecycleClosed;
  }
  return ErrorCode::Internal;
}

StateGateResult gate_transition(const TransitionRule& rule, LifecycleState lifecycle,
                                OperatingState operating, OperatingMode mode,
                                ErrorCode lifecycle_error) {
  StateGateResult out{};

  bool lifecycle_allows = false;
  switch (lifecycle) {
    case LifecycleState::Commissioned:
      lifecycle_allows = true;
      break;
    case LifecycleState::Maintenance:
      lifecycle_allows = rule.operation == OperationKind::ExitMaintenance ||
                         rule.operation == OperationKind::Retire ||
                         rule.operation == OperationKind::Isolate;
      break;
    case LifecycleState::Isolated:
      lifecycle_allows = rule.operation == OperationKind::ReturnToService ||
                         rule.operation == OperationKind::Retire;
      break;
    case LifecycleState::Retired:
    case LifecycleState::Unknown:
    default:
      lifecycle_allows = false;
      break;
  }
  if (!lifecycle_allows) {
    out.allowed = false;
    out.provisional = OperatingState::Unknown;
    out.code = lifecycle_error == ErrorCode::Ok ? lifecycle_error_for(lifecycle) : lifecycle_error;
    out.detail = std::string("lifecycle state '") + std::string(to_string(lifecycle)) +
                 "' does not permit operation '" + std::string(to_string(rule.operation)) + "'";
    return out;
  }

  if (!rule.allows_from(operating)) {
    out.allowed = false;
    out.provisional = OperatingState::Unknown;
    out.code = operating == OperatingState::Unknown ? ErrorCode::OperatingStateUnknown
                                                    : ErrorCode::IllegalTransition;
    out.detail = std::string("operation '") + std::string(to_string(rule.operation)) +
                 "' is not defined from operating state '" + std::string(to_string(operating)) + "'";
    return out;
  }

  switch (rule.required_mode) {
    case OperatingMode::Unknown:
      break;
    case OperatingMode::Test:
      if (mode != OperatingMode::Test) {
        out.allowed = false;
        out.code = ErrorCode::TestOperationNotPermitted;
        out.detail = "test operations require the generator to be in test mode; current mode is '" +
                     std::string(to_string(mode)) + "'";
        return out;
      }
      break;
    case OperatingMode::Emergency:
      if (mode != OperatingMode::Emergency) {
        out.allowed = false;
        out.code = ErrorCode::EmergencyAuthorityRequired;
        out.detail = "emergency operations require the generator to be in emergency mode; current "
                     "mode is '" +
                     std::string(to_string(mode)) + "'";
        return out;
      }
      break;
    case OperatingMode::Service:
      if (mode == OperatingMode::Test) {
        out.allowed = false;
        out.code = ErrorCode::TestOperationNotPermitted;
        out.detail = "service operations are refused while the generator is in test mode";
        return out;
      }
      break;
    case OperatingMode::Normal:
    default:
      if (mode == OperatingMode::Test) {
        out.allowed = false;
        out.code = ErrorCode::TestOperationNotPermitted;
        out.detail = "production operations are refused while the generator is in test mode; return "
                     "it to normal mode first";
        return out;
      }
      if (mode == OperatingMode::Emergency) {
        out.allowed = false;
        out.code = ErrorCode::ModeNotPermitted;
        out.detail = "production operations are refused while the generator is in emergency mode; "
                     "clear the emergency mode explicitly first";
        return out;
      }
      break;
  }

  out.allowed = true;
  out.provisional = rule.provisional;
  out.code = ErrorCode::Ok;
  out.detail = std::string("operation '") + std::string(to_string(rule.operation)) +
               "' is defined from '" + std::string(to_string(operating)) + "'";
  return out;
}

}  // namespace genctl
