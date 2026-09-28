// Generator Control - the control engine.
//
// The engine owns one generator domain object per registered generator and answers
// the core question:
//
//   Is this generator eligible and authorized to start, run, synchronize, transfer,
//   test or stop now, under which readiness/resource/safety evidence, and how do we
//   prove the requested state transition without conflating command acknowledgement
//   with electrical effect?
//
// Concurrency model and global lock order (documented and audited):
//
//   * There is exactly one mutation lock, mutation_mutex_. It serialises planning,
//     validation, durable publication and the in-memory commit of a mutation.
//   * snapshot_mutex_ is a leaf lock. It is held only to swap or copy a
//     std::shared_ptr, never across a file operation, an adapter call, a clock read
//     or any callback. Lock order is mutation_mutex_ -> snapshot_mutex_; the
//     reverse order never occurs and snapshot_mutex_ is never held while acquiring
//     mutation_mutex_.
//   * The adapter is called with no engine lock held. The engine records durable
//     write-ahead intent before the call and commits the acknowledgement after it.
//   * No user callback, virtual dispatch into external code, blocking wait or
//     sleep ever runs while a lock is held.
//   * Readers never block writers on a lock: a reader copies an immutable snapshot
//     pointer under the leaf lock and then works on the copy.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "genctl/adapter.hpp"
#include "genctl/attempt.hpp"
#include "genctl/authority.hpp"
#include "genctl/model.hpp"
#include "genctl/persistence.hpp"
#include "genctl/readiness.hpp"
#include "genctl/resource.hpp"
#include "genctl/state.hpp"
#include "genctl/sync.hpp"
#include "genctl/transfer.hpp"

namespace genctl {

struct EngineConfig {
  ModelLimits limits{};
  JournalPolicy journal{};
  // Policy applied to measurements reported through the adapter.
  EvidencePolicy evidence{};
  // Policy applied to evidence that must come from an external authority.
  EvidencePolicy external_evidence{};
  std::uint32_t generation_retention{4};
  std::size_t max_record_bytes{4u * 1024u * 1024u};
  Millis default_check_max_age_millis{60 * kMillisPerSecond};
  // How long after a command an absent observed effect remains inconclusive rather
  // than contradictory. A device needs time to crank, warm up and close a breaker;
  // before this window elapses the effect is simply not observed yet.
  Millis effect_settle_millis{20 * kMillisPerSecond};
  // Fail closed when the idempotency window has evicted keys and the operator has
  // not acknowledged the resulting loss of replay detection.
  bool require_key_window_acknowledgement{true};
  // Deterministic crash injection used by the crash/recovery tests and examples.
  CrashPoint crash_point{CrashPoint::None};
  // Refuse to demote volatile evidence on reopen. Never enabled in production; it
  // exists so that a test can prove the demotion is what keeps recovery honest.
  bool disable_evidence_demotion{false};
};

struct OpenOptions {
  std::string store_directory{};
  bool read_only{false};
  bool create_if_missing{false};
  bool recover_scan{false};
  bool accept_rollback{false};
  std::string rollback_acceptance_note{};
  ControlEpoch requested_epoch{};
  EngineConfig config{};
};

struct RegisterOptions {
  HardwareGeneration hardware_generation{HardwareGeneration{1}};
  bool commissioned{false};
  LifecycleState lifecycle{LifecycleState::Commissioned};
  ResourceRequirement requirement{};
  SyncPolicy sync_policy{};
  TransferPolicy transfer_policy{};
  // Authoritative required readiness set. Safety and protection checks are always
  // added by the engine even when the caller omits them.
  std::vector<CheckKind> required_checks{};
};

struct LifecycleUpdate {
  GeneratorId generator{};
  ControllerGeneration controller{};
  StateRevision revision{};
  LifecycleState lifecycle{LifecycleState::Unknown};
  IdempotencyKey idempotency_key{};
  AuthorityGrant authority{};
  std::string reason{};
  EpochMillis requested_at{0};
};

struct ModeUpdate {
  GeneratorId generator{};
  ControllerGeneration controller{};
  StateRevision revision{};
  OperatingMode mode{OperatingMode::Unknown};
  IdempotencyKey idempotency_key{};
  AuthorityGrant authority{};
  EpochMillis requested_at{0};
};

struct ResourceUpdate {
  GeneratorId generator{};
  ControllerGeneration controller{};
  StateRevision revision{};
  IdempotencyKey idempotency_key{};
  // Only the fields that are set are replaced; the rest keep their previous value.
  bool set_fuel_level{false};
  Evidence<FuelQuantity> fuel_level{};
  bool set_fuel_capacity{false};
  Evidence<FuelQuantity> fuel_capacity{};
  bool set_fuel_consumption{false};
  Evidence<FuelRate> fuel_consumption{};
  bool set_lube_oil_pressure{false};
  Evidence<Pressure> lube_oil_pressure{};
  bool set_coolant_temperature{false};
  Evidence<Temperature> coolant_temperature{};
  bool set_coolant_level{false};
  Evidence<Percent> coolant_level{};
  bool set_battery_voltage{false};
  Evidence<Voltage> battery_voltage{};
  bool set_compressed_air{false};
  Evidence<Pressure> compressed_air_pressure{};
};

struct CheckUpdate {
  GeneratorId generator{};
  ControllerGeneration controller{};
  StateRevision revision{};
  IdempotencyKey idempotency_key{};
  CheckRecord record{};
};

struct SyncPreconditionUpdate {
  GeneratorId generator{};
  ControllerGeneration controller{};
  StateRevision revision{};
  IdempotencyKey idempotency_key{};
  PreconditionRecord record{};
};

struct TransferPreconditionUpdate {
  GeneratorId generator{};
  ControllerGeneration controller{};
  StateRevision revision{};
  IdempotencyKey idempotency_key{};
  TransferPreconditionRecord record{};
};

struct TransferPathUpdate {
  GeneratorId generator{};
  ControllerGeneration controller{};
  StateRevision revision{};
  IdempotencyKey idempotency_key{};
  TransferPath path{};
};

struct SwitchAuthorityUpdate {
  GeneratorId generator{};
  ControllerGeneration controller{};
  StateRevision revision{};
  IdempotencyKey idempotency_key{};
  Evidence<SwitchAuthorityToken> token{};
};

struct AuthorityUpdate {
  GeneratorId generator{};
  ControllerGeneration controller{};
  StateRevision revision{};
  IdempotencyKey idempotency_key{};
  AuthorityGrant grant{};
};

struct ObserveRequest {
  GeneratorId generator{};
  ControllerGeneration controller{};
  // When set, the observation is used to resolve this attempt's effect.
  AttemptId attempt{};
  bool resolve_attempt{false};
  EpochMillis requested_at{0};
};

struct ObserveOutcome {
  EngineObservation observation{};
  StateRevision revision{};
  bool attempt_resolved{false};
  AttemptRecord attempt{};
  EffectState effect{EffectState::Unknown};
  std::string detail{};
};

struct VerifyOptions {
  AttemptId attempt{};
  ControllerGeneration controller{};
  EpochMillis requested_at{0};
};

struct RevalidateReport {
  GeneratorId generator{};
  StateRevision revision{};
  std::size_t demoted_evidence_records{0};
  ReadinessReport readiness{};
  ResourceAssessment resources{};
  SynchronizationEligibility synchronization{};
  TransferEligibility transfer{};
  std::vector<std::string> notes{};
};

struct ReopenReport {
  IncarnationId incarnation{};
  ControlEpoch epoch{};
  CommitSeq adopted_commit_seq{};
  bool adopted_from_scan{false};
  bool read_only{false};
  std::size_t demoted_evidence_records{0};
  std::size_t residue_retired{0};
  std::size_t attempts_recovered{0};
  std::size_t unresolved_attempts{0};
  std::vector<std::string> notes{};
};

struct KeyWindowAcknowledgement {
  CommitSeq acknowledged_at_seq{};
  EpochMillis acknowledged_at{0};
  std::string acknowledged_by{};
  std::string reason{};
  std::uint64_t evicted_keys{0};

  friend bool operator==(const KeyWindowAcknowledgement&,
                         const KeyWindowAcknowledgement&) noexcept = default;
};

// Immutable snapshot handed to readers.
struct EngineView {
  std::map<GeneratorId, GeneratorState> generators{};
  std::vector<AttemptRecord> attempts{};
  AttemptId next_attempt_id{AttemptId{1}};
  CommitSeq commit_seq{};
};

class GeneratorControlEngine {
 public:
  ~GeneratorControlEngine();
  GeneratorControlEngine(const GeneratorControlEngine&) = delete;
  GeneratorControlEngine& operator=(const GeneratorControlEngine&) = delete;

  // The adapter is borrowed, must outlive the engine, and is never called while an
  // engine lock is held.
  [[nodiscard]] static Result<std::unique_ptr<GeneratorControlEngine>> open(
      const OpenOptions& options, const Clock& clock, GeneratorAdapter* adapter);

  // ---- identity and configuration ---------------------------------------
  [[nodiscard]] Result<GeneratorState> register_generator(const GeneratorId& id,
                                                          const RegisterOptions& options,
                                                          EpochMillis now);
  [[nodiscard]] Status update_lifecycle(const LifecycleUpdate& update);
  [[nodiscard]] Status update_mode(const ModeUpdate& update);
  [[nodiscard]] Status configure_requirement(const GeneratorId& id, StateRevision revision,
                                             const ResourceRequirement& requirement, EpochMillis now);
  [[nodiscard]] Status configure_sync_policy(const GeneratorId& id, StateRevision revision,
                                             const SyncPolicy& policy, EpochMillis now);
  [[nodiscard]] Status configure_transfer_policy(const GeneratorId& id, StateRevision revision,
                                                 const TransferPolicy& policy, EpochMillis now);
  [[nodiscard]] Status set_transfer_path(const TransferPathUpdate& update);
  [[nodiscard]] Status grant_authority(const AuthorityUpdate& update);
  [[nodiscard]] Status record_check(const CheckUpdate& update);
  [[nodiscard]] Status record_resource(const ResourceUpdate& update);
  [[nodiscard]] Status record_sync_precondition(const SyncPreconditionUpdate& update);
  [[nodiscard]] Status record_transfer_precondition(const TransferPreconditionUpdate& update);
  [[nodiscard]] Status record_switch_authority(const SwitchAuthorityUpdate& update);
  [[nodiscard]] Status acknowledge_key_window(const std::string& acknowledged_by,
                                              const std::string& reason, EpochMillis now);

  // ---- queries -----------------------------------------------------------
  [[nodiscard]] Result<GeneratorState> inspect(const GeneratorId& id) const;
  [[nodiscard]] Result<std::vector<GeneratorId>> list_generators() const;
  [[nodiscard]] Result<ReadinessReport> readiness(const GeneratorId& id, EpochMillis now) const;
  [[nodiscard]] Result<ResourceAssessment> resource_assessment(const GeneratorId& id,
                                                               EpochMillis now) const;
  [[nodiscard]] Result<SynchronizationEligibility> sync_eligibility(const GeneratorId& id,
                                                                    EpochMillis now) const;
  [[nodiscard]] Result<TransferEligibility> transfer_eligibility(const GeneratorId& id,
                                                                 EpochMillis now) const;
  [[nodiscard]] Result<EvaluationReport> evaluate(const OperationRequest& request) const;
  [[nodiscard]] Result<AttemptRecord> attempt(AttemptId id) const;
  [[nodiscard]] Result<std::vector<AttemptRecord>> attempts(const GeneratorId& id) const;
  [[nodiscard]] Result<std::vector<HistoryEntry>> history(const GeneratorId& id,
                                                          std::size_t limit) const;
  [[nodiscard]] Result<std::vector<AuthorityAuditEntry>> authority_audit(const GeneratorId& id) const;
  [[nodiscard]] Result<KeyWindowAcknowledgement> key_window_acknowledgement() const;
  [[nodiscard]] Result<StoreAuditReport> store_audit(bool accept_rollback,
                                                     const std::string& acceptance_note);
  [[nodiscard]] Result<std::size_t> count_evicted_keys() const;

  // ---- mutations ---------------------------------------------------------
  // Plans, authorizes and, unless dry_run is set, issues a command through the
  // adapter. A retry carrying a known idempotency key returns the prior accepted
  // result and performs no new actuation, before any staleness check.
  [[nodiscard]] Result<AttemptRecord> execute(const OperationRequest& request, bool dry_run);
  // Observes the device and, when requested, resolves the attempt's effect. This is
  // the only path that can establish an observation-only state such as
  // Synchronized.
  [[nodiscard]] Result<ObserveOutcome> observe(const ObserveRequest& request);
  [[nodiscard]] Result<AttemptRecord> verify(const VerifyOptions& options);
  // Marks an unresolved attempt abandoned after an observation proved that the
  // commanded effect did not occur. Explicit and audited; never automatic.
  [[nodiscard]] Result<AttemptRecord> abandon(AttemptId id, const std::string& reason,
                                              EpochMillis now);
  [[nodiscard]] Result<RevalidateReport> revalidate(const GeneratorId& id, EpochMillis now);

  [[nodiscard]] Status close();

  // ---- introspection -----------------------------------------------------
  [[nodiscard]] const ReopenReport& reopen_report() const noexcept { return reopen_report_; }
  [[nodiscard]] ControllerGeneration controller() const noexcept { return controller_; }
  [[nodiscard]] CommitSeq commit_seq() const noexcept;
  [[nodiscard]] const EngineConfig& config() const noexcept { return config_; }
  [[nodiscard]] const std::string& store_directory() const noexcept;
  [[nodiscard]] bool read_only() const noexcept;

 private:
  GeneratorControlEngine() = default;

  [[nodiscard]] std::shared_ptr<const EngineView> view() const;
  void rebuild_view_locked();
  [[nodiscard]] GeneratorState* find_locked(const GeneratorId& id);
  [[nodiscard]] const GeneratorState* find_locked(const GeneratorId& id) const;
  [[nodiscard]] Status validate_controller_locked(const GeneratorId& id,
                                                  const ControllerGeneration& controller,
                                                  StateRevision revision) const;

  // Builds the persistable image from the current in-memory state.
  [[nodiscard]] StateImage build_image_locked(EpochMillis now) const;
  // Publishes the current in-memory state and commits the new revision. Must be
  // called with mutation_mutex_ held.
  [[nodiscard]] Status commit_locked(EpochMillis now);
  [[nodiscard]] Status bump_revision_locked(GeneratorState& state);
  void append_history_locked(GeneratorState& state, const HistoryEntry& entry);
  [[nodiscard]] Status demote_volatile_evidence_locked(std::size_t* demoted);
  [[nodiscard]] Result<ReadinessReport> readiness_locked(const GeneratorState& state,
                                                         EpochMillis now) const;
  [[nodiscard]] Result<ResourceAssessment> resource_locked(const GeneratorState& state,
                                                           EpochMillis now) const;
  [[nodiscard]] Result<SynchronizationEligibility> sync_locked(const GeneratorState& state,
                                                               EpochMillis now) const;
  [[nodiscard]] Result<TransferEligibility> transfer_locked(const GeneratorState& state,
                                                            EpochMillis now) const;
  [[nodiscard]] Result<EvaluationReport> evaluate_locked(const GeneratorState& state,
                                                         const OperationRequest& request,
                                                         EpochMillis now,
                                                         JournalSeq replay_floor) const;
  [[nodiscard]] Result<AttemptRecord> replay_or_execute(const OperationRequest& request,
                                                        bool dry_run);

  // mutation_mutex_ serialises planning, publication and the in-memory commit.
  mutable std::mutex mutation_mutex_{};
  // snapshot_mutex_ is a leaf lock guarding only the snapshot pointer.
  mutable std::mutex snapshot_mutex_{};
  mutable std::shared_ptr<const EngineView> view_{};

  std::map<GeneratorId, GeneratorState> generators_{};
  AttemptJournal journal_{};
  std::unique_ptr<DurableStore> store_{};
  const Clock* clock_{nullptr};
  GeneratorAdapter* adapter_{nullptr};
  EngineConfig config_{};
  ControllerGeneration controller_{};
  ReopenReport reopen_report_{};
  KeyWindowAcknowledgement key_window_{};
  std::uint64_t evicted_keys_{0};
  bool adapter_opened_{false};
  bool closed_{false};
};

// ---------------------------------------------------------------------------
// Canonical codecs for persisted records
// ---------------------------------------------------------------------------
template <>
struct ValueCodec<GeneratorId> {
  static Status encode(CanonicalWriter& writer, const GeneratorId& value) {
    return writer.put_string(value.str());
  }
  static Result<GeneratorId> decode(CanonicalReader& reader) {
    GENCTL_TRY_ASSIGN(text, reader.get_string());
    return GeneratorId::parse(text);
  }
};

template <>
struct ValueCodec<IdempotencyKey> {
  static Status encode(CanonicalWriter& writer, const IdempotencyKey& value) {
    return writer.put_string(value.str());
  }
  static Result<IdempotencyKey> decode(CanonicalReader& reader) {
    GENCTL_TRY_ASSIGN(text, reader.get_string());
    return IdempotencyKey::parse(text);
  }
};

template <>
struct ValueCodec<SwitchRef> {
  static Status encode(CanonicalWriter& writer, const SwitchRef& value) {
    return writer.put_string(value.str());
  }
  static Result<SwitchRef> decode(CanonicalReader& reader) {
    GENCTL_TRY_ASSIGN(text, reader.get_string());
    if (text.empty()) {
      // An unset reference is a legitimate state: the installation has not yet
      // declared which breaker this runtime may ask to operate.
      return SwitchRef{};
    }
    return SwitchRef::parse(text);
  }
};

}  // namespace genctl
