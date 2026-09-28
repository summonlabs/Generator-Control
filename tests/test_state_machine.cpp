// Proof obligations: the documented state machine, lifecycle fail-closed behaviour,
// authority class disjointness and the observation-only nature of synchronization.
#include "genctl/engine.hpp"
#include "genctl/state.hpp"
#include "support.hpp"
#include "test_framework.hpp"

namespace {
using namespace genctl;
using namespace gctest;

const TransitionRule& rule(OperationKind kind) {
  const TransitionRule* found = rule_for(kind);
  CHECK(found != nullptr);
  return *found;
}

TEST(no_transition_rule_can_assert_synchronization_from_a_command) {
  std::size_t count = 0;
  const TransitionRule* table = transition_table(&count);
  CHECK(count >= 16);
  for (std::size_t i = 0; i < count; ++i) {
    if (table[i].provisional == OperatingState::Synchronized) {
      fail(std::string("operation '") + std::string(to_string(table[i].operation)) +
           "' would let a command assert synchronization");
    }
  }
  CHECK(is_observation_only_state(OperatingState::Synchronized));
}

TEST(every_actuating_rule_requires_interlocks_and_observation) {
  std::size_t count = 0;
  const TransitionRule* table = transition_table(&count);
  for (std::size_t i = 0; i < count; ++i) {
    const TransitionRule& entry = table[i];
    if (entry.operation == OperationKind::EnterMaintenance ||
        entry.operation == OperationKind::ExitMaintenance ||
        entry.operation == OperationKind::Isolate ||
        entry.operation == OperationKind::ReturnToService ||
        entry.operation == OperationKind::Retire) {
      continue;
    }
    if (entry.operation == OperationKind::Stop || entry.operation == OperationKind::TestStop ||
        entry.operation == OperationKind::EmergencyStop) {
      // Stopping is never gated on interlocks: refusing to stop is not fail-safe.
      CHECK(!entry.require_interlocks);
      continue;
    }
    CHECK(entry.require_interlocks);
  }
}

TEST(no_rule_allows_a_retired_or_unknown_lifecycle) {
  const StateGateResult retired =
      gate_transition(rule(OperationKind::Start), LifecycleState::Retired,
                      OperatingState::Stopped, OperatingMode::Normal,
                      lifecycle_error_for(LifecycleState::Retired));
  CHECK(!retired.allowed);
  CHECK_EQ(retired.code, ErrorCode::GeneratorRetired);

  const StateGateResult unknown =
      gate_transition(rule(OperationKind::Start), LifecycleState::Unknown,
                      OperatingState::Stopped, OperatingMode::Normal,
                      lifecycle_error_for(LifecycleState::Unknown));
  CHECK(!unknown.allowed);
  CHECK_EQ(unknown.code, ErrorCode::LifecycleClosed);
}

TEST(maintenance_isolated_and_faulted_fail_closed_except_listed_recovery) {
  const auto gate = [](OperationKind operation, LifecycleState lifecycle,
                       OperatingState operating, OperatingMode mode) {
    return gate_transition(rule(operation), lifecycle, operating, mode,
                           lifecycle_error_for(lifecycle));
  };

  CHECK(!gate(OperationKind::Start, LifecycleState::Maintenance, OperatingState::Stopped,
              OperatingMode::Normal)
             .allowed);
  CHECK(gate(OperationKind::ExitMaintenance, LifecycleState::Maintenance, OperatingState::Stopped,
             OperatingMode::Service)
            .allowed);
  CHECK(gate(OperationKind::Retire, LifecycleState::Maintenance, OperatingState::Stopped,
             OperatingMode::Service)
            .allowed);
  CHECK(!gate(OperationKind::Synchronize, LifecycleState::Maintenance,
              OperatingState::ReadyUnsynchronized, OperatingMode::Normal)
             .allowed);

  CHECK(!gate(OperationKind::Start, LifecycleState::Isolated, OperatingState::Stopped,
              OperatingMode::Normal)
             .allowed);
  CHECK(gate(OperationKind::ReturnToService, LifecycleState::Isolated, OperatingState::Stopped,
             OperatingMode::Service)
            .allowed);
  CHECK(rule(OperationKind::ReturnToService).changes_lifecycle);
  CHECK(!gate(OperationKind::ExitMaintenance, LifecycleState::Isolated, OperatingState::Stopped,
              OperatingMode::Service).allowed);

  CHECK(!gate(OperationKind::Start, LifecycleState::Commissioned, OperatingState::Faulted,
              OperatingMode::Normal)
             .allowed);
  CHECK(gate(OperationKind::FaultReset, LifecycleState::Commissioned, OperatingState::Faulted,
             OperatingMode::Service)
            .allowed);
  CHECK(!gate(OperationKind::EmergencyStart, LifecycleState::Commissioned,
              OperatingState::Faulted, OperatingMode::Emergency)
             .allowed);
}

TEST(lifecycle_targets_match_their_operations) {
  CHECK_EQ(rule(OperationKind::EnterMaintenance).lifecycle_target, LifecycleState::Maintenance);
  CHECK_EQ(rule(OperationKind::ExitMaintenance).lifecycle_target, LifecycleState::Commissioned);
  CHECK_EQ(rule(OperationKind::Isolate).lifecycle_target, LifecycleState::Isolated);
  CHECK_EQ(rule(OperationKind::ReturnToService).lifecycle_target, LifecycleState::Commissioned);
  CHECK_EQ(rule(OperationKind::Retire).lifecycle_target, LifecycleState::Retired);
  CHECK(rule(OperationKind::EnterMaintenance).changes_lifecycle);
  CHECK(!rule(OperationKind::Start).changes_lifecycle);
}

TEST(start_stop_transitions_follow_the_documented_table) {
  const auto gate = [](OperationKind operation, OperatingState operating) {
    return gate_transition(rule(operation), LifecycleState::Commissioned, operating,
                           OperatingMode::Normal, ErrorCode::Ok);
  };
  CHECK(gate(OperationKind::Start, OperatingState::Stopped).allowed);
  CHECK(gate(OperationKind::Start, OperatingState::Cooldown).allowed);
  CHECK(gate(OperationKind::Start, OperatingState::Degraded).allowed);
  CHECK(!gate(OperationKind::Start, OperatingState::Starting).allowed);
  CHECK(!gate(OperationKind::Start, OperatingState::WarmUp).allowed);
  CHECK(!gate(OperationKind::Start, OperatingState::ReadyUnsynchronized).allowed);
  CHECK(!gate(OperationKind::Start, OperatingState::Synchronized).allowed);
  CHECK(!gate(OperationKind::Start, OperatingState::Faulted).allowed);
  CHECK(!gate(OperationKind::Start, OperatingState::Unknown).allowed);

  CHECK(gate(OperationKind::Stop, OperatingState::Starting).allowed);
  CHECK(gate(OperationKind::Stop, OperatingState::WarmUp).allowed);
  CHECK(gate(OperationKind::Stop, OperatingState::ReadyUnsynchronized).allowed);
  CHECK(gate(OperationKind::Stop, OperatingState::Synchronized).allowed);
  CHECK(gate(OperationKind::Stop, OperatingState::Degraded).allowed);
  CHECK(gate(OperationKind::Stop, OperatingState::Stopped).allowed);
  CHECK(!gate(OperationKind::Stop, OperatingState::Unknown).allowed);
  CHECK_EQ(rule(OperationKind::Stop).provisional, OperatingState::Cooldown);

  CHECK(gate(OperationKind::Synchronize, OperatingState::ReadyUnsynchronized).allowed);
  CHECK(!gate(OperationKind::Synchronize, OperatingState::Stopped).allowed);
  CHECK_EQ(rule(OperationKind::Synchronize).provisional, OperatingState::Unknown);
  CHECK(gate(OperationKind::Desynchronize, OperatingState::Synchronized).allowed);
}

TEST(unknown_operating_state_is_never_treated_as_stopped) {
  const StateGateResult result =
      gate_transition(rule(OperationKind::Start), LifecycleState::Commissioned,
                      OperatingState::Unknown, OperatingMode::Normal, ErrorCode::Ok);
  CHECK(!result.allowed);
  CHECK_EQ(result.code, ErrorCode::OperatingStateUnknown);
}

TEST(authority_classes_are_disjoint) {
  CHECK(authority_permits(AuthorityClass::Normal, AuthorityClass::Normal));
  CHECK(authority_permits(AuthorityClass::Test, AuthorityClass::Test));
  CHECK(authority_permits(AuthorityClass::Service, AuthorityClass::Service));
  CHECK(authority_permits(AuthorityClass::Emergency, AuthorityClass::Emergency));

  // Test authority never authorizes a production operation, in either direction.
  CHECK(!authority_permits(AuthorityClass::Test, AuthorityClass::Normal));
  CHECK(!authority_permits(AuthorityClass::Normal, AuthorityClass::Test));
  CHECK(!authority_permits(AuthorityClass::Emergency, AuthorityClass::Test));
  CHECK(!authority_permits(AuthorityClass::Test, AuthorityClass::Emergency));
  CHECK(!authority_permits(AuthorityClass::Service, AuthorityClass::Normal));
  CHECK(!authority_permits(AuthorityClass::Normal, AuthorityClass::Service));
  CHECK(!authority_permits(AuthorityClass::None, AuthorityClass::Normal));
  CHECK_EQ(authority_class_for_mode(OperatingMode::Test), AuthorityClass::Test);
  CHECK_EQ(authority_class_for_mode(OperatingMode::Emergency), AuthorityClass::Emergency);
}

TEST(test_mode_cannot_drive_a_production_operation) {
  const StateGateResult blocked =
      gate_transition(rule(OperationKind::Start), LifecycleState::Commissioned,
                      OperatingState::Stopped, OperatingMode::Test, ErrorCode::Ok);
  CHECK(!blocked.allowed);
  CHECK_EQ(blocked.code, ErrorCode::TestOperationNotPermitted);

  const StateGateResult transfer_blocked =
      gate_transition(rule(OperationKind::TransferToGenerator), LifecycleState::Commissioned,
                      OperatingState::ReadyUnsynchronized, OperatingMode::Test, ErrorCode::Ok);
  CHECK(!transfer_blocked.allowed);

  const StateGateResult test_start =
      gate_transition(rule(OperationKind::TestStart), LifecycleState::Commissioned,
                      OperatingState::Stopped, OperatingMode::Test, ErrorCode::Ok);
  CHECK(test_start.allowed);
  CHECK_EQ(rule(OperationKind::TestStart).mode_after, OperatingMode::Test);

  const StateGateResult test_start_wrong_mode =
      gate_transition(rule(OperationKind::TestStart), LifecycleState::Commissioned,
                      OperatingState::Stopped, OperatingMode::Normal, ErrorCode::Ok);
  CHECK(!test_start_wrong_mode.allowed);
  CHECK_EQ(test_start_wrong_mode.code, ErrorCode::TestOperationNotPermitted);
}

TEST(emergency_mode_requires_the_emergency_mode_explicitly) {
  const StateGateResult blocked =
      gate_transition(rule(OperationKind::EmergencyStart), LifecycleState::Commissioned,
                      OperatingState::Stopped, OperatingMode::Normal, ErrorCode::Ok);
  CHECK(!blocked.allowed);
  CHECK_EQ(blocked.code, ErrorCode::EmergencyAuthorityRequired);

  const StateGateResult allowed =
      gate_transition(rule(OperationKind::EmergencyStart), LifecycleState::Commissioned,
                      OperatingState::Stopped, OperatingMode::Emergency, ErrorCode::Ok);
  CHECK(allowed.allowed);
  CHECK(rule(OperationKind::EmergencyStart).advisory_waivable);
  CHECK(rule(OperationKind::EmergencyStart).require_interlocks);
  CHECK(rule(OperationKind::Start).advisory_waivable == false);
}

TEST(stop_type_operations_accept_any_authority) {
  CHECK(rule(OperationKind::Stop).accept_any_authority);
  CHECK(rule(OperationKind::EmergencyStop).accept_any_authority);
  CHECK(rule(OperationKind::TestStop).accept_any_authority);
  CHECK(!rule(OperationKind::Start).accept_any_authority);
  CHECK(!rule(OperationKind::Synchronize).accept_any_authority);
}

TEST(operation_and_state_names_round_trip) {
  const OperationKind operations[] = {
      OperationKind::Start,      OperationKind::Stop,          OperationKind::TestStart,
      OperationKind::TestStop,   OperationKind::EmergencyStart, OperationKind::EmergencyStop,
      OperationKind::FaultReset, OperationKind::EnterMaintenance, OperationKind::ExitMaintenance,
      OperationKind::Isolate,    OperationKind::ReturnToService, OperationKind::Retire,
      OperationKind::Synchronize, OperationKind::Desynchronize,
      OperationKind::TransferToGenerator, OperationKind::TransferToUtility};
  for (const OperationKind operation : operations) {
    CHECK_EQ(parse_operation_kind(to_string(operation)).value(), operation);
  }
  const OperatingState states[] = {OperatingState::Unknown, OperatingState::Stopped,
                                   OperatingState::Starting, OperatingState::WarmUp,
                                   OperatingState::ReadyUnsynchronized,
                                   OperatingState::Synchronized, OperatingState::Cooldown,
                                   OperatingState::Degraded, OperatingState::Faulted};
  for (const OperatingState state : states) {
    CHECK_EQ(parse_operating_state(to_string(state)).value(), state);
  }
  CHECK_RESULT_CODE(parse_operation_kind("frobnicate"), ErrorCode::InvalidArgument);
}

}  // namespace
