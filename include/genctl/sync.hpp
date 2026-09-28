// Generator Control - synchronization eligibility as explicit preconditions.
//
// This runtime does not implement a synchronization algorithm, a governor, an AVR
// or anti-islanding protection. It records, bounds and evaluates the external
// evidence those functions must produce, and it refuses to describe a generator as
// synchronized on the strength of anything other than an observation made after
// the synchronizing command.
//
// Eligibility is a list of individually inspectable preconditions, not a boolean.
// Every precondition names the physical quantity or external permission it stands
// for, the window it must fall inside and the source that reported it.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "genctl/digest.hpp"
#include "genctl/evidence.hpp"
#include "genctl/ids.hpp"
#include "genctl/state.hpp"
#include "genctl/units.hpp"

namespace genctl {

enum class SyncPreconditionKind : std::uint16_t {
  Unknown = 0,
  BusEnergized = 1,
  GeneratorExcited = 2,
  VoltageMatch = 3,          // observed generator/bus voltage difference
  FrequencyMatch = 4,        // observed generator/bus frequency difference
  PhaseAngleMatch = 5,       // observed phase angle difference
  PhaseSequenceMatch = 6,    // rotation agreed between generator and bus
  SlipFrequencyAcceptable = 7,
  SyncWindowStable = 8,       // the window held for the configured dwell time
  SyncRelayPermissive = 20,   // external synchronizer permissive
  ProtectionRelayPermissive = 21,
  GovernorAvrReady = 22,
  BreakerClosePermissive = 23,
  AntiIslandingPermissive = 24,  // utility interconnect permissive
};

[[nodiscard]] std::string_view to_string(SyncPreconditionKind kind) noexcept;
[[nodiscard]] Result<SyncPreconditionKind> parse_sync_precondition_kind(std::string_view text);
// True when the precondition can only be answered by an external authority.
[[nodiscard]] bool sync_requires_external(SyncPreconditionKind kind) noexcept;

struct PreconditionRecord {
  SyncPreconditionKind kind{SyncPreconditionKind::Unknown};
  bool required{true};
  EvidenceState state{EvidenceState::Unknown};
  EvidenceSource source{EvidenceSource::Unknown};
  EvidenceLifetime lifetime{EvidenceLifetime::VolatileObservation};
  EpochMillis observed_at{0};
  EpochMillis valid_until{0};
  Millis max_age_millis{5 * kMillisPerSecond};
  EvidenceVersion version{};
  TypedValue value{};
  std::string detail{};

  friend bool operator==(const PreconditionRecord&, const PreconditionRecord&) noexcept = default;
};

// Windows come from installation configuration; observations come from evidence.
struct SyncPolicy {
  Window<Voltage> voltage_difference{Voltage{-5000}, Voltage{5000}};          // +/- 5 V
  Window<Frequency> frequency_difference{Frequency{-200}, Frequency{200}};    // +/- 0.2 Hz
  Window<PhaseAngle> phase_angle_difference{PhaseAngle{-10000}, PhaseAngle{10000}};  // +/- 10 deg
  Millis max_age_millis{5 * kMillisPerSecond};
  // True when the installation parallels with the utility and therefore requires
  // an anti-islanding permissive.
  bool utility_parallel{true};
  // True when the synchronizer is an automatic device whose permissive is a
  // precondition; false when the operator synchronizes manually.
  bool require_sync_relay{true};
};

struct SyncFinding {
  SyncPreconditionKind kind{SyncPreconditionKind::Unknown};
  bool required{true};
  bool satisfied{false};
  EligibilityOutcome outcome{EligibilityOutcome::Indeterminate};
  ErrorCode code{ErrorCode::Ok};
  EvidenceSource source{EvidenceSource::Unknown};
  std::string detail{};
};

struct SynchronizationEligibility {
  GeneratorId generator{};
  StateRevision revision{};
  ControllerGeneration controller{};
  EpochMillis evaluated_at{0};
  EligibilityOutcome outcome{EligibilityOutcome::Unknown};
  ErrorCode primary_error{ErrorCode::Ok};
  ValidationStage stage{ValidationStage::None};
  bool synthetic_evidence_used{false};
  std::vector<SyncFinding> findings{};
  Digest256 binding_digest{};

  [[nodiscard]] bool eligible() const noexcept { return outcome == EligibilityOutcome::Eligible; }
};

// Evaluates the configured preconditions. Deterministic precedence: any required
// precondition that is definitively not satisfied makes the result Ineligible;
// otherwise any required precondition whose evidence is missing, stale,
// unsupported or contradictory makes it Indeterminate; otherwise Eligible.
[[nodiscard]] Status evaluate_synchronization(const GeneratorId& generator, StateRevision revision,
                                              const ControllerGeneration& controller,
                                              const std::vector<PreconditionRecord>& records,
                                              const SyncPolicy& policy, EpochMillis now,
                                              SynchronizationEligibility* out);

// Preconditions required for a given installation policy, in declaration order.
[[nodiscard]] std::vector<SyncPreconditionKind> default_sync_preconditions(const SyncPolicy& policy);

}  // namespace genctl
