// Generator Control - transfer and breaker eligibility by reference.
//
// This runtime does not own facility switching topology. It holds *references* to
// breakers, buses, feeders and transfer paths that are defined and operated by the
// external power topology and switch authority, and it evaluates whether the
// externally granted switch authority, the observed breaker positions and the
// generator's own synchronization state add up to eligibility.
//
// A successful transfer command produces a *request*, never a transfer effect. The
// effect is proven only by an external observation of the breaker positions.
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

class SwitchRef {
 public:
  static constexpr std::size_t kMaxLength = 64;

  SwitchRef() = default;
  [[nodiscard]] static Result<SwitchRef> parse(std::string_view text);
  [[nodiscard]] const std::string& str() const noexcept { return value_; }
  [[nodiscard]] bool empty() const noexcept { return value_.empty(); }
  friend bool operator==(const SwitchRef&, const SwitchRef&) noexcept = default;
  friend std::strong_ordering operator<=>(const SwitchRef&, const SwitchRef&) noexcept = default;

 private:
  std::string value_{};
};

// The complete external reference set a transfer needs. Missing references are a
// configuration error, not something the runtime may invent.
struct TransferPath {
  SwitchRef breaker{};      // the breaker this runtime may ask to operate
  SwitchRef source_bus{};
  SwitchRef target_bus{};
  SwitchRef feeder{};
  SwitchRef transfer_path{};

  friend bool operator==(const TransferPath&, const TransferPath&) noexcept = default;
};

enum class TransferPreconditionKind : std::uint16_t {
  Unknown = 0,
  FacilitySwitchAuthorityGranted = 1,
  FacilitySwitchAuthorityFresh = 2,
  BreakerPositionObserved = 3,
  BreakerClosePermissive = 4,
  ProtectionRelayPermissive = 5,
  GeneratorSynchronized = 6,
  GeneratorReadinessSatisfied = 7,
  TransferPathIdentified = 8,
  TargetBusEnergized = 9,
  SourceBusEnergized = 10,
  NoConflictingTransferPending = 11,
};

[[nodiscard]] std::string_view to_string(TransferPreconditionKind kind) noexcept;
[[nodiscard]] Result<TransferPreconditionKind> parse_transfer_precondition_kind(std::string_view text);
[[nodiscard]] bool transfer_requires_external(TransferPreconditionKind kind) noexcept;
[[nodiscard]] bool transfer_requires_generator_sync(TransferPreconditionKind kind) noexcept;

struct TransferPreconditionRecord {
  TransferPreconditionKind kind{TransferPreconditionKind::Unknown};
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

  friend bool operator==(const TransferPreconditionRecord&,
                         const TransferPreconditionRecord&) noexcept = default;
};

// An external token that grants this runtime the right to request a specific
// switch operation for a specific generator generation. The token is issued by the
// external switch authority; this runtime validates it and never mints one.
struct SwitchAuthorityToken {
  SwitchRef authority{};          // identity of the issuing authority
  SwitchRef subject_switch{};     // the breaker the token is valid for
  GeneratorId subject_generator{};
  GeneratorGeneration subject_generation{};
  ControlEpoch epoch{};
  EpochMillis issued_at{0};
  EpochMillis valid_until{0};
  Digest256 token_digest{};

  friend bool operator==(const SwitchAuthorityToken&, const SwitchAuthorityToken&) noexcept = default;
};

struct TransferPolicy {
  Millis max_token_age_millis{60 * kMillisPerSecond};
  Millis max_position_age_millis{2 * kMillisPerSecond};
  bool require_protection_permissive{true};
  bool require_generator_synchronized{true};
};

struct TransferFinding {
  TransferPreconditionKind kind{TransferPreconditionKind::Unknown};
  bool required{true};
  bool satisfied{false};
  EligibilityOutcome outcome{EligibilityOutcome::Indeterminate};
  ErrorCode code{ErrorCode::Ok};
  EvidenceSource source{EvidenceSource::Unknown};
  std::string detail{};
};

struct TransferEligibility {
  GeneratorId generator{};
  StateRevision revision{};
  ControllerGeneration controller{};
  GeneratorGeneration generation{};
  EpochMillis evaluated_at{0};
  EligibilityOutcome outcome{EligibilityOutcome::Unknown};
  ErrorCode primary_error{ErrorCode::Ok};
  ValidationStage stage{ValidationStage::None};
  bool synthetic_evidence_used{false};
  std::vector<TransferFinding> findings{};
  Digest256 binding_digest{};

  [[nodiscard]] bool eligible() const noexcept { return outcome == EligibilityOutcome::Eligible; }
};

[[nodiscard]] Status evaluate_transfer(const GeneratorId& generator, StateRevision revision,
                                       const ControllerGeneration& controller,
                                       const GeneratorGeneration& generation,
                                       const TransferPath& path,
                                       const std::vector<TransferPreconditionRecord>& records,
                                       const TransferPolicy& policy, EpochMillis now,
                                       TransferEligibility* out);

[[nodiscard]] std::vector<TransferPreconditionKind> default_transfer_preconditions();

}  // namespace genctl
