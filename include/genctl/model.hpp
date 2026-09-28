// Generator Control - the authoritative, persistable state model.
//
// Identity is separated from metadata: a generator's identity and generation never
// change, while its lifecycle, operating state, evidence bindings and audit trail
// move under a monotonic state revision. Every collection is either a std::map
// (ordered by key) or a sequence ordered by a monotonic sequence number, so the
// canonical encoding of equivalent logical state is byte-for-byte identical.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "genctl/attempt.hpp"
#include "genctl/authority.hpp"
#include "genctl/digest.hpp"
#include "genctl/ids.hpp"
#include "genctl/readiness.hpp"
#include "genctl/resource.hpp"
#include "genctl/state.hpp"
#include "genctl/sync.hpp"
#include "genctl/transfer.hpp"
#include "genctl/version.hpp"

namespace genctl {

struct HistoryEntry {
  JournalSeq seq{};
  EpochMillis at{0};
  OperationKind operation{OperationKind::Unknown};
  AttemptId attempt{};
  LifecycleState from_lifecycle{LifecycleState::Unknown};
  LifecycleState to_lifecycle{LifecycleState::Unknown};
  OperatingState from_operating{OperatingState::Unknown};
  OperatingState to_operating{OperatingState::Unknown};
  OperatingMode mode{OperatingMode::Unknown};
  bool provisional{false};
  std::string detail{};
  Digest256 digest{};

  friend bool operator==(const HistoryEntry&, const HistoryEntry&) noexcept = default;
};

struct GeneratorState {
  // ---- stable identity ---------------------------------------------------
  GeneratorId id{};
  GeneratorGeneration generation{};
  bool commissioned{false};

  // ---- mutable state -----------------------------------------------------
  StateRevision revision{};
  LifecycleState lifecycle{LifecycleState::Unknown};
  OperatingState operating{OperatingState::Unknown};
  OperatingMode mode{OperatingMode::Unknown};
  SynchronizationState synchronization{SynchronizationState::Unknown};
  BreakerPosition breaker{BreakerPosition::Unknown};
  EpochMillis breaker_observed_at{0};
  EpochMillis last_observed_at{0};
  ObservationSeq last_observation_seq{};

  // ---- evidence bindings -------------------------------------------------
  ResourceSnapshot resources{};
  ResourceRequirement requirement{};
  std::vector<CheckKind> required_checks{};
  std::vector<CheckRecord> checks{};
  std::vector<PreconditionRecord> sync_preconditions{};
  std::vector<TransferPreconditionRecord> transfer_preconditions{};
  TransferPath transfer_path{};
  Evidence<SwitchAuthorityToken> switch_authority{};

  // ---- configuration -----------------------------------------------------
  SyncPolicy sync_policy{};
  TransferPolicy transfer_policy{};
  AuthorityGrant authority{};

  // ---- audit trail -------------------------------------------------------
  std::vector<HistoryEntry> history{};
  std::vector<AuthorityAuditEntry> audit{};
  JournalSeq next_history_seq{JournalSeq{1}};
};

struct StateImage {
  std::uint32_t format_version{kStoreFormatVersion};
  CommitSeq commit_seq{};
  ControlEpoch epoch{};
  IncarnationId incarnation{};
  Digest256 previous_digest{};
  EpochMillis created_at{0};
  // Ordered by GeneratorId: canonical order is the identity order, never insertion
  // order.
  std::map<GeneratorId, GeneratorState> generators{};
  // Ordered by AttemptId.
  std::vector<AttemptRecord> attempts{};
  // Compact replay entries retained after full attempt records are retired. The
  // window is what makes a lost-response retry detectable rather than a second
  // actuation.
  std::vector<ReplayEntry> replay{};
  AttemptId next_attempt_id{AttemptId{1}};
  JournalSeq next_journal_seq{JournalSeq{1}};
  JournalSeq retained_floor{JournalSeq{0}};
  std::uint64_t evicted_keys{0};
  JournalPolicy journal_policy{};
  // Explicit operator acknowledgement that replay detection has a horizon.
  struct KeyWindowAck {
    CommitSeq acknowledged_at_seq{};
    EpochMillis acknowledged_at{0};
    std::string acknowledged_by{};
    std::string reason{};
    std::uint64_t evicted_keys{0};
    friend bool operator==(const KeyWindowAck&, const KeyWindowAck&) noexcept = default;
  } key_window{};

  [[nodiscard]] bool empty() const noexcept { return generators.empty() && attempts.empty(); }
};

// Canonical digest of a state image. Excludes nothing that is authority relevant:
// evidence values, provenance, lifetimes and observation instants are all state.
// Excludes everything volatile: process ids, addresses, iteration artefacts.
[[nodiscard]] Digest256 digest_state_image(const StateImage& image);

// Structural validation applied before publication and after recovery. Returns the
// first violation in a deterministic order.
[[nodiscard]] Status validate_state_image(const StateImage& image);

// Bounds applied to externally influenced collection sizes before allocation.
struct ModelLimits {
  std::size_t max_generators{64};
  std::size_t max_attempts{256};
  std::size_t max_history{512};
  std::size_t max_audit_entries{256};
  std::size_t max_checks{64};
  std::size_t max_preconditions{64};
  std::size_t max_string_bytes{256};
};

[[nodiscard]] const ModelLimits& default_model_limits() noexcept;

// ---------------------------------------------------------------------------
// Canonical codecs
// ---------------------------------------------------------------------------
template <>
struct ValueCodec<Window<Voltage>> {
  static Status encode(CanonicalWriter& writer, const Window<Voltage>& value) {
    GENCTL_TRY(encode_value(writer, value.minimum));
    return encode_value(writer, value.maximum);
  }
  static Result<Window<Voltage>> decode(CanonicalReader& reader) {
    Window<Voltage> out{};
    GENCTL_TRY_ASSIGN(minimum, decode_value<Voltage>(reader));
    GENCTL_TRY_ASSIGN(maximum, decode_value<Voltage>(reader));
    out.minimum = minimum;
    out.maximum = maximum;
    return out;
  }
};

template <>
struct ValueCodec<Window<Frequency>> {
  static Status encode(CanonicalWriter& writer, const Window<Frequency>& value) {
    GENCTL_TRY(encode_value(writer, value.minimum));
    return encode_value(writer, value.maximum);
  }
  static Result<Window<Frequency>> decode(CanonicalReader& reader) {
    Window<Frequency> out{};
    GENCTL_TRY_ASSIGN(minimum, decode_value<Frequency>(reader));
    GENCTL_TRY_ASSIGN(maximum, decode_value<Frequency>(reader));
    out.minimum = minimum;
    out.maximum = maximum;
    return out;
  }
};

template <>
struct ValueCodec<Window<PhaseAngle>> {
  static Status encode(CanonicalWriter& writer, const Window<PhaseAngle>& value) {
    GENCTL_TRY(encode_value(writer, value.minimum));
    return encode_value(writer, value.maximum);
  }
  static Result<Window<PhaseAngle>> decode(CanonicalReader& reader) {
    Window<PhaseAngle> out{};
    GENCTL_TRY_ASSIGN(minimum, decode_value<PhaseAngle>(reader));
    GENCTL_TRY_ASSIGN(maximum, decode_value<PhaseAngle>(reader));
    out.minimum = minimum;
    out.maximum = maximum;
    return out;
  }
};

}  // namespace genctl
