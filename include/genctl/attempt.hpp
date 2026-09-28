// Generator Control - operation requests, command/effect attempt records and the
// replay journal.
//
// The central separation of this header:
//
//   planned  -> what the caller asked for, and the authority/version it was planned
//               against;
//   authorized -> the evaluation outcome that permitted it;
//   issued   -> a command object was handed to an adapter;
//   acknowledged -> the adapter accepted the command;
//   observed -> the outside world reported a physical condition;
//   verified -> the observed condition satisfies the intended effect.
//
// These are separate fields with separate states. Nothing in this runtime advances
// one from another implicitly.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "genctl/adapter.hpp"
#include "genctl/authority.hpp"
#include "genctl/digest.hpp"
#include "genctl/ids.hpp"
#include "genctl/readiness.hpp"
#include "genctl/resource.hpp"
#include "genctl/state.hpp"
#include "genctl/sync.hpp"
#include "genctl/transfer.hpp"

namespace genctl {

enum class CommandState : std::uint8_t {
  Planned = 0,
  Authorized = 1,
  Issued = 2,
  Acknowledged = 3,
  Rejected = 4,
  Unsupported = 5,
  Busy = 6,
  AdapterUnavailable = 7,
  Abandoned = 8,
};

[[nodiscard]] std::string_view to_string(CommandState state) noexcept;
[[nodiscard]] bool command_is_terminal(CommandState state) noexcept;
// True when a command was handed to the adapter but its outcome is unresolved.
[[nodiscard]] bool command_is_unresolved(CommandState state) noexcept;

enum class EffectState : std::uint8_t {
  Unknown = 0,       // no effect question has been asked yet
  NotObserved = 1,   // a command exists but nothing has been observed since
  ObservedConsistent = 2,
  ObservedContradictory = 3,
  Verified = 4,
  VerificationFailed = 5,
};

[[nodiscard]] std::string_view to_string(EffectState state) noexcept;
[[nodiscard]] bool effect_is_conclusive(EffectState state) noexcept;

// The authority and versions a request was planned against. Every state dependent
// mutation states this and refuses to proceed when any part has moved on.
struct PlannedAgainst {
  ControllerGeneration controller{};
  GeneratorGeneration generation{};
  StateRevision revision{};
  Digest256 readiness_binding{};
  Digest256 resource_binding{};
  Digest256 sync_binding{};
  Digest256 transfer_binding{};

  friend bool operator==(const PlannedAgainst&, const PlannedAgainst&) noexcept = default;
};

struct OperationRequest {
  GeneratorId generator{};
  OperationKind operation{OperationKind::Unknown};
  ControllerGeneration controller{};
  GeneratorGeneration generation{};
  StateRevision revision{};
  AuthorityGrant authority{};
  IdempotencyKey idempotency_key{};
  EpochMillis requested_at{0};
  // Annotation only; deliberately excluded from the request fingerprint so that a
  // retry that only adjusts a note still resolves as the same attempt.
  std::string note{};
};

// Stable fingerprint of the authority-relevant parts of a request.
[[nodiscard]] Result<RequestFingerprint> fingerprint_request(const OperationRequest& request);

struct StageResult {
  ValidationStage stage{ValidationStage::None};
  ErrorCode code{ErrorCode::Ok};
  std::string detail{};

  [[nodiscard]] bool ok() const noexcept { return code == ErrorCode::Ok; }
};

struct EvaluationReport {
  GeneratorId generator{};
  OperationKind operation{OperationKind::Unknown};
  ControllerGeneration controller{};
  GeneratorGeneration generation{};
  StateRevision revision{};
  EpochMillis evaluated_at{0};
  bool permitted{false};
  ErrorCode primary_error{ErrorCode::Ok};
  ValidationStage primary_stage{ValidationStage::None};
  std::string primary_detail{};
  bool advisory_waived{false};
  std::vector<CheckKind> waived_checks{};
  std::vector<StageResult> stages{};
  ReadinessReport readiness{};
  ResourceAssessment resources{};
  SynchronizationEligibility synchronization{};
  TransferEligibility transfer{};
  Digest256 report_digest{};

  [[nodiscard]] bool ok() const noexcept { return permitted; }
};

struct AttemptRecord {
  AttemptId id{};
  JournalSeq seq{};
  GeneratorId generator{};
  OperationKind operation{OperationKind::Unknown};
  PlannedAgainst planned_against{};
  IdempotencyKey key{};
  RequestFingerprint fingerprint{};
  AuthorityClass authority{AuthorityClass::None};
  ControlEpoch authority_epoch{};
  std::string authority_reason{};
  CommandState command_state{CommandState::Planned};
  AdapterAckStatus ack_status{AdapterAckStatus::None};
  CommandId command_id{};
  EpochMillis created_at{0};
  EpochMillis updated_at{0};
  EpochMillis acknowledged_at{0};
  std::string ack_detail{};
  EffectState effect_state{EffectState::Unknown};
  ObservationSeq effect_observation_seq{};
  EpochMillis effect_observed_at{0};
  OperatingState observed_state{OperatingState::Unknown};
  SynchronizationState observed_synchronization{SynchronizationState::Unknown};
  BreakerPosition observed_breaker{BreakerPosition::Unknown};
  EvidenceSource effect_source{EvidenceSource::Unknown};
  Digest256 effect_digest{};
  std::string effect_detail{};
  // Set on the returned copy when the record was served from the replay journal
  // instead of performing new work.
  bool replayed{false};
  std::string note{};

  [[nodiscard]] bool resolved() const noexcept;
  [[nodiscard]] bool effect_verified() const noexcept { return effect_state == EffectState::Verified; }
};

// Bounded replay journal. Retention semantics, stated exactly:
//
//   * full attempt records are retained up to policy.max_attempts;
//   * compact replay entries - key, fingerprint, operation, command id and the
//     terminal command/effect states - are retained up to policy.idempotency_window,
//     which is normally larger than max_attempts;
//   * a retry whose key is still in the replay window and whose fingerprint matches
//     returns the prior accepted result and performs no new actuation;
//   * a retry whose key is in the replay window but whose fingerprint differs is
//     refused with IdempotencyKeyConflict;
//   * a retry whose key is in the replay window but whose full record has already
//     been retired is refused with IdempotencyWindowExpired - the runtime knows the
//     key was used and refuses to actuate again;
//   * keys evicted from the replay window are counted. New actuating work is then
//     refused with IdempotencyWindowExpired until an operator explicitly
//     acknowledges the loss of replay detection, because a retry of an evicted key
//     is otherwise indistinguishable from a fresh request.
struct JournalPolicy {
  std::size_t max_attempts{256};
  std::size_t idempotency_window{1024};
};

// Compact entry retained after the full attempt record has been retired.
struct ReplayEntry {
  IdempotencyKey key{};
  AttemptId attempt{};
  RequestFingerprint fingerprint{};
  OperationKind operation{OperationKind::Unknown};
  CommandState command_state{CommandState::Planned};
  EffectState effect_state{EffectState::Unknown};
  CommandId command_id{};
  EpochMillis recorded_at{0};

  friend bool operator==(const ReplayEntry&, const ReplayEntry&) noexcept = default;
};

class AttemptJournal {
 public:
  AttemptJournal() = default;
  explicit AttemptJournal(JournalPolicy policy) : policy_(policy) {}

  [[nodiscard]] const JournalPolicy& policy() const noexcept { return policy_; }
  void set_policy(JournalPolicy policy) noexcept { policy_ = policy; }

  [[nodiscard]] Result<AttemptRecord> append(AttemptRecord record);
  [[nodiscard]] Status update(const AttemptRecord& record);
  [[nodiscard]] const AttemptRecord* find(AttemptId id) const noexcept;
  // Returns the compact replay entry for a key, or AttemptNotFound when the key has
  // never been seen inside the retained window.
  [[nodiscard]] Result<ReplayEntry> lookup_key(const IdempotencyKey& key) const;
  [[nodiscard]] const std::vector<AttemptRecord>& records() const noexcept { return records_; }
  [[nodiscard]] const std::vector<ReplayEntry>& replay_entries() const noexcept { return replay_; }
  [[nodiscard]] std::vector<AttemptRecord> for_generator(const GeneratorId& generator) const;
  // The attempt, if any, that blocks a new actuation for this generator: either a
  // command whose outcome is still open, or an actuated operation whose effect has
  // not yet been proven or contradicted by an observation.
  [[nodiscard]] const AttemptRecord* blocking_for(const GeneratorId& generator) const noexcept;
  [[nodiscard]] AttemptId next_attempt_id() const noexcept { return next_id_; }
  [[nodiscard]] JournalSeq next_seq() const noexcept { return next_seq_; }
  [[nodiscard]] JournalSeq retained_floor() const noexcept { return retained_floor_; }
  [[nodiscard]] std::size_t retained_keys() const noexcept { return replay_.size(); }
  // Number of keys evicted from the replay window since the store was created.
  [[nodiscard]] std::uint64_t evicted_keys() const noexcept { return evicted_keys_; }

  void restore(AttemptId next_id, JournalSeq next_seq, JournalSeq retained_floor,
               std::uint64_t evicted_keys, std::vector<AttemptRecord> records,
               std::vector<ReplayEntry> replay);
  [[nodiscard]] Status rebuild_replay();

 private:
  void enforce_retention();

  JournalPolicy policy_{};
  std::vector<AttemptRecord> records_{};
  std::vector<ReplayEntry> replay_{};
  AttemptId next_id_{AttemptId{1}};
  JournalSeq next_seq_{JournalSeq{1}};
  JournalSeq retained_floor_{JournalSeq{0}};
  std::uint64_t evicted_keys_{0};
};

}  // namespace genctl
