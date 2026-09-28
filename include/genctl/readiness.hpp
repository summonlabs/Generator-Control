// Generator Control - readiness checks and safety interlocks.
//
// A check is satisfied only when its evidence is present, fresh, of an acceptable
// source and not contradicted. Safety and protection checks can never be waived by
// any authority class, including emergency authority: emergency authority changes
// which operating states an operation may start from and may waive advisory
// (non-safety) checks only, and every waiver is recorded in the audit trail.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "genctl/digest.hpp"
#include "genctl/evidence.hpp"
#include "genctl/ids.hpp"
#include "genctl/state.hpp"
#include "genctl/units.hpp"

namespace genctl {

enum class CheckKind : std::uint16_t {
  Unknown = 0,

  // ---- safety interlocks -------------------------------------------------
  EmergencyStopNotEngaged = 1,
  FireSuppressionClear = 2,
  GasLeakDetectionClear = 3,
  RoomVentilationRunning = 4,
  PersonnelGuardsInPlace = 5,
  OverspeedTripClear = 6,
  OvercrankLockoutClear = 7,
  LowOilPressureTripClear = 8,
  HighCoolantTemperatureTripClear = 9,
  BreakerFailureLockoutClear = 10,
  ExhaustPathClear = 11,

  // ---- external protection permissives -----------------------------------
  ProtectionRelayPermissive = 20,
  UtilityAntiIslandingPermissive = 21,
  SyncRelayAvailable = 22,
  GovernorAvrReady = 23,
  FuelValveAvailable = 24,

  // ---- machine readiness -------------------------------------------------
  BatteryVoltageSufficient = 40,
  CoolantTemperatureInRange = 41,
  LubeOilPressureInRange = 42,
  CoolantLevelSufficient = 43,
  StartingAirPressureSufficient = 44,
  CrankLimiterClear = 45,

  // ---- advisory (non-safety) ---------------------------------------------
  MaintenanceWindowClear = 60,
  EconomicReserveSatisfied = 61,
  LoadHeadroomAvailable = 62,
  FuelQualityAttested = 63,
};

enum class CheckClass : std::uint8_t {
  Safety = 0,      // never waivable
  Protection = 1,  // never waivable
  Readiness = 2,   // never waivable
  Advisory = 3,    // waivable by an explicitly authorized operation
};

[[nodiscard]] std::string_view to_string(CheckKind kind) noexcept;
[[nodiscard]] Result<CheckKind> parse_check_kind(std::string_view text);
[[nodiscard]] CheckClass check_class(CheckKind kind) noexcept;
[[nodiscard]] bool is_waivable(CheckKind kind) noexcept;

// A recorded check. Persisted as part of the readiness binding so that an
// evaluation can be reproduced and audited after a restart.
struct CheckRecord {
  CheckKind kind{CheckKind::Unknown};
  bool required{true};
  EvidenceState state{EvidenceState::Unknown};
  EvidenceSource source{EvidenceSource::Unknown};
  EvidenceLifetime lifetime{EvidenceLifetime::VolatileObservation};
  EpochMillis observed_at{0};
  EpochMillis valid_until{0};
  Millis max_age_millis{60 * kMillisPerSecond};
  EvidenceVersion version{};
  TypedValue value{};
  bool latching{false};
  std::string detail{};

  friend bool operator==(const CheckRecord&, const CheckRecord&) noexcept = default;
};

struct CheckFinding {
  CheckKind kind{CheckKind::Unknown};
  CheckClass cls{CheckClass::Safety};
  bool required{true};
  bool satisfied{false};
  bool waived{false};
  EvidenceUsability usability{EvidenceUsability::Unknown};
  ErrorCode code{ErrorCode::Ok};
  EvidenceSource source{EvidenceSource::Unknown};
  std::string detail{};
};

struct ReadinessReport {
  GeneratorId generator{};
  StateRevision revision{};
  ControllerGeneration controller{};
  GeneratorGeneration generation{};
  EpochMillis evaluated_at{0};
  bool satisfied{false};
  bool advisory_waived{false};
  ErrorCode primary_error{ErrorCode::Ok};
  ValidationStage stage{ValidationStage::None};
  bool synthetic_evidence_used{false};
  bool interlock_evidence_complete{false};
  std::vector<CheckFinding> findings{};
  Digest256 binding_digest{};

  [[nodiscard]] bool ok() const noexcept { return satisfied; }
};

// Evaluates the readiness binding. Findings are emitted in CheckKind order so the
// report is byte-for-byte reproducible for equivalent state.
// The required set is authoritative: a required check with no record is reported as
// missing and fails closed. Records outside the required set are reported as
// informational findings with required == false.
[[nodiscard]] Status evaluate_readiness(const GeneratorId& generator, StateRevision revision,
                                        const ControllerGeneration& controller,
                                        const GeneratorGeneration& generation,
                                        const std::vector<CheckKind>& required,
                                        const std::vector<CheckRecord>& records, EpochMillis now,
                                        const EvidencePolicy& policy, bool waive_advisory,
                                        ReadinessReport* out);

// Default required-check set for an ordinary production start. Installation
// configuration may add checks but may not remove safety or protection checks.
[[nodiscard]] std::vector<CheckKind> default_required_checks();
// Default checks that must be satisfied before the generator may be paralleled.
[[nodiscard]] std::vector<CheckKind> default_parallel_checks();

}  // namespace genctl
