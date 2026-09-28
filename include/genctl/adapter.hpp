// Generator Control - the narrow vendor/synthetic adapter boundary.
//
// This is the only place where the runtime touches the outside world for control.
// The interface is deliberately small: describe, issue, observe, close. There is no
// hidden autonomous control path, no background thread, no vendor credential and no
// network endpoint anywhere in this runtime.
//
// Adapter contract:
//   * issue() returns whether the device *accepted* the command. It must never be
//     interpreted as an electrical effect.
//   * observe() returns what the device and the surrounding electrical system
//     actually report, including breaker position, which this runtime does not own.
//   * Every command id is unique and is never reused, so an adapter that keeps a
//     journal can prove that a command was issued exactly once.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "genctl/evidence.hpp"
#include "genctl/ids.hpp"
#include "genctl/state.hpp"
#include "genctl/time.hpp"
#include "genctl/units.hpp"

namespace genctl {

struct AdapterDescriptor {
  std::string name{};
  std::string vendor{};
  std::string model{};
  HardwareGeneration hardware_generation{};
  // True when this adapter is a deterministic simulator. Every report derived from
  // its evidence is labelled SYNTHETIC.
  bool synthetic{false};
  bool supports_observation{false};
  bool supports_synchronization{false};
  bool supports_transfer_request{false};
};

struct CommandRequest {
  GeneratorId generator{};
  OperationKind operation{OperationKind::Unknown};
  CommandId command_id{};
  AttemptId attempt_id{};
  ControlEpoch epoch{};
  EpochMillis issued_at{0};
  // Advisory only: the runtime never blocks waiting for a device.
  Duration advisory_timeout{};
  bool test_mode{false};
  // Bounded opaque parameters for the adapter. Never contains credentials.
  std::string parameters{};
};

enum class AdapterAckStatus : std::uint8_t {
  None = 0,       // no acknowledgement was received
  Accepted = 1,   // the device accepted the command
  Rejected = 2,   // the device refused it
  Unsupported = 3,
  Busy = 4,
  Invalid = 5,    // the device rejected the command as malformed
};

[[nodiscard]] std::string_view to_string(AdapterAckStatus status) noexcept;

struct AdapterAck {
  AdapterAckStatus status{AdapterAckStatus::None};
  CommandId command_id{};
  EpochMillis acknowledged_at{0};
  std::string detail{};

  [[nodiscard]] bool accepted() const noexcept { return status == AdapterAckStatus::Accepted; }
};

struct ElectricalObservation {
  Voltage line_voltage{};
  Voltage bus_voltage{};
  Frequency frequency{};
  Frequency bus_frequency{};
  PhaseAngle phase_angle{};
  ActivePower active_power{};
  ReactivePower reactive_power{};
  Current current{};
  Percent load{};
};

// What the outside world reports. Never constructed by this runtime's own command
// path: the engine stamps the source and refuses command-echo provenance when the
// observation is used to prove an effect.
struct EngineObservation {
  GeneratorId generator{};
  EpochMillis observed_at{0};
  EvidenceSource source{EvidenceSource::Unknown};
  bool cranking{false};
  bool running{false};
  OperatingState reported_state{OperatingState::Unknown};
  SynchronizationState synchronization{SynchronizationState::Unknown};
  BreakerPosition breaker{BreakerPosition::Unknown};
  ElectricalObservation electrical{};
  Temperature coolant_temperature{};
  Pressure lube_oil_pressure{};
  Percent coolant_level{};
  bool fuel_valve_open{false};
  std::string detail{};
};

class GeneratorAdapter {
 public:
  virtual ~GeneratorAdapter();
  GeneratorAdapter(const GeneratorAdapter&) = delete;
  GeneratorAdapter& operator=(const GeneratorAdapter&) = delete;

  [[nodiscard]] virtual AdapterDescriptor describe() const = 0;
  // Opens the connection. Called once by the engine before any command is issued.
  [[nodiscard]] virtual Status open() = 0;
  [[nodiscard]] virtual Status close() = 0;
  // Issues a command. Returns an ack, never an effect.
  [[nodiscard]] virtual Result<AdapterAck> issue(const CommandRequest& request) = 0;
  // Reads the current state of the device and the surrounding electrical system.
  [[nodiscard]] virtual Result<EngineObservation> observe(const GeneratorId& generator) = 0;

 protected:
  GeneratorAdapter() = default;
};

// ---------------------------------------------------------------------------
// Deterministic synthetic device
// ---------------------------------------------------------------------------
// Models a standby generating set behind the same interface a vendor adapter would
// use. Its actuation journal is append-only and flushed, so an independent process
// can count exactly how many commands were issued across a crash and a restart.
struct SyntheticProfile {
  HardwareGeneration hardware_generation{HardwareGeneration{1}};
  // Time from an accepted start to the device reporting "running".
  Millis crank_millis{4000};
  // Time from "running" to "ready, unsynchronized" (rated voltage and frequency).
  Millis warmup_millis{6000};
  // Time from an accepted stop to the device reporting "stopped".
  Millis cooldown_millis{3000};
  Voltage rated_voltage{Voltage{400'000}};
  Frequency rated_frequency{Frequency{50'000}};
  Voltage bus_voltage{Voltage{400'000}};
  Frequency bus_frequency{Frequency{50'000}};
  PhaseAngle phase_angle{PhaseAngle{1500}};
};

struct SyntheticFaults {
  // The adapter acknowledges the command but the device does not change state.
  // This is the acknowledgement-without-effect case.
  bool ack_without_effect{false};
  // The adapter rejects the next command.
  bool reject_next{false};
  // The adapter reports itself busy.
  bool busy{false};
  // The device cranks but never reaches running.
  bool stuck_cranking{false};
  // observe() fails.
  bool observation_unavailable{false};
  // The device reports a state that contradicts the acknowledged command.
  bool contradictory_observation{false};
};

class SyntheticAdapter final : public GeneratorAdapter {
 public:
  struct Options {
    // Empty means "do not persist an actuation journal".
    std::string journal_path{};
    SyntheticProfile profile{};
    SyntheticFaults faults{};
    // Initial position of the externally operated breaker.
    BreakerPosition initial_breaker{BreakerPosition::Open};
    bool initial_running{false};
  };

  SyntheticAdapter(GeneratorId generator, const Clock& clock, Options options);

  // Rebuilds the in-memory device model from a persisted journal. Returns the
  // number of actuation records recovered.
  [[nodiscard]] Result<std::size_t> replay_journal();

  [[nodiscard]] AdapterDescriptor describe() const override;
  [[nodiscard]] Status open() override;
  [[nodiscard]] Status close() override;
  [[nodiscard]] Result<AdapterAck> issue(const CommandRequest& request) override;
  [[nodiscard]] Result<EngineObservation> observe(const GeneratorId& generator) override;

  // ---- test/external-actor controls (not part of the adapter contract) ----
  // Models the external topology actor operating the breaker. The engine never
  // calls this; it only ever observes the consequence.
  void external_set_breaker(BreakerPosition position) noexcept;
  void external_set_synchronized(bool synchronized) noexcept;
  void external_set_running(bool running) noexcept;
  void external_set_fuel_valve(bool open) noexcept;
  void set_faults(const SyntheticFaults& faults) noexcept;
  [[nodiscard]] const SyntheticFaults& faults() const noexcept { return faults_; }

  [[nodiscard]] std::size_t issued_count() const noexcept { return issued_count_; }
  [[nodiscard]] std::size_t accepted_count() const noexcept { return accepted_count_; }
  [[nodiscard]] std::size_t rejected_count() const noexcept { return rejected_count_; }
  [[nodiscard]] std::size_t observation_count() const noexcept { return observation_count_; }
  // Number of accepted commands of a given operation, across processes when a
  // journal is configured.
  [[nodiscard]] std::size_t accepted_count(OperationKind operation) const noexcept;
  [[nodiscard]] const std::string& journal_path() const noexcept { return options_.journal_path; }
  [[nodiscard]] std::vector<std::string> journal_lines() const;

 private:
  struct ActuationRecord {
    CommandId command_id{};
    AttemptId attempt_id{};
    OperationKind operation{OperationKind::Unknown};
    AdapterAckStatus status{AdapterAckStatus::None};
    EpochMillis at{0};
  };

  [[nodiscard]] Status append_journal(const ActuationRecord& record);
  // Persists the device model so that an independent process observes the same
  // device condition. This is laboratory device state and never authoritative
  // control state.
  [[nodiscard]] Status persist_state() const;
  void apply_accepted(const CommandRequest& request, EpochMillis now);
  [[nodiscard]] OperatingState derive_state(EpochMillis now) const;
  [[nodiscard]] bool still_cranking(EpochMillis now) const;

  GeneratorId generator_{};
  const Clock* clock_{nullptr};
  Options options_{};
  SyntheticFaults faults_{};
  bool open_{false};
  bool cranking_{false};
  bool running_{false};
  bool fuel_valve_open_{false};
  BreakerPosition breaker_{BreakerPosition::Open};
  bool synchronized_{false};
  EpochMillis start_accepted_at_{0};
  EpochMillis stop_accepted_at_{0};
  std::size_t issued_count_{0};
  std::size_t accepted_count_{0};
  std::size_t rejected_count_{0};
  std::size_t observation_count_{0};
  std::vector<ActuationRecord> actuations_{};
};

}  // namespace genctl
