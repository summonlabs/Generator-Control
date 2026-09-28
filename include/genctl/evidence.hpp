// Generator Control - evidence, provenance and freshness.
//
// Nothing in this runtime is "true" because it was stored. Every value that can
// influence a decision is evidence with an explicit state, an explicit source and
// an explicit observation instant, and is usable only while it is present, fresh
// and of a source that is allowed to speak about that subject.
//
// The core distinction enforced here:
//   * CommandAcknowledgement proves that a command was accepted, and nothing else.
//   * Only a measurement or an attestation can prove an electrical effect.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "genctl/canonical.hpp"
#include "genctl/ids.hpp"
#include "genctl/time.hpp"

namespace genctl {

enum class EvidenceState : std::uint8_t {
  Unknown = 0,       // nothing is known; distinct from absent and from zero
  Present = 1,       // a value was recorded
  Missing = 2,       // the subject was looked for and not found
  Stale = 3,         // a value exists but is too old to be used
  Unsupported = 4,   // the interface cannot report this at all
  Contradictory = 5, // two sources disagree, or the value is physically impossible
  Denied = 6,        // an authority explicitly refused
};

[[nodiscard]] std::string_view to_string(EvidenceState state) noexcept;

enum class EvidenceSource : std::uint8_t {
  Unknown = 0,
  // Deterministic simulator behind the adapter boundary. Labelled SYNTHETIC in
  // every report that consumes it.
  SyntheticAdapter = 1,
  // A real device reached through the adapter boundary. Labelled REAL.
  VendorAdapter = 2,
  // A human recorded the value; carries no automatic validity.
  OperatorAttestation = 3,
  // Computed from other evidence; the detail string must name the inputs.
  DerivedMeasurement = 4,
  // An external control plane granted or observed it (power control plane, feed
  // authority, protection relay, facility switch authority).
  ExternalAuthority = 5,
  // An adapter accepted a command. Proves acceptance only.
  CommandAcknowledgement = 6,
};

[[nodiscard]] std::string_view to_string(EvidenceSource source) noexcept;
[[nodiscard]] Result<EvidenceSource> parse_evidence_source(std::string_view text);

// True when evidence from this source may prove a physical/electrical effect.
[[nodiscard]] bool is_effect_authoritative(EvidenceSource source) noexcept;
// True when evidence from this source may grant control permission.
[[nodiscard]] bool is_control_authoritative(EvidenceSource source) noexcept;

// How long a piece of evidence is expected to remain meaningful.
enum class EvidenceLifetime : std::uint8_t {
  // A dynamic observation. It is demoted to Stale when the store is reopened by a
  // new controller incarnation, because a new process has no way to know that the
  // world did not move while the previous incarnation was gone.
  VolatileObservation = 0,
  // An attestation or grant with its own explicit validity window (valid_until).
  AttestedWithValidity = 1,
  // Static configuration that describes the installation rather than its state.
  StaticConfiguration = 2,
};

[[nodiscard]] std::string_view to_string(EvidenceLifetime lifetime) noexcept;

template <typename T>
struct Evidence {
  EvidenceState state{EvidenceState::Unknown};
  T value{};
  EvidenceSource source{EvidenceSource::Unknown};
  EvidenceLifetime lifetime{EvidenceLifetime::VolatileObservation};
  EpochMillis observed_at{0};
  // 0 means "no explicit validity window"; otherwise the instant after which an
  // AttestedWithValidity record is stale regardless of age.
  EpochMillis valid_until{0};
  EvidenceVersion version{};
  std::string detail{};

  [[nodiscard]] bool has_value() const noexcept { return state == EvidenceState::Present; }

  friend bool operator==(const Evidence& a, const Evidence& b) noexcept {
    return a.state == b.state && a.value == b.value && a.source == b.source &&
           a.lifetime == b.lifetime && a.observed_at == b.observed_at &&
           a.valid_until == b.valid_until && a.version == b.version && a.detail == b.detail;
  }
};

enum class EvidenceUsability : std::uint8_t {
  Usable = 0,
  Unknown,
  Missing,
  Stale,
  Unsupported,
  Contradictory,
  Denied,
  ProvenanceRejected,
};

[[nodiscard]] std::string_view to_string(EvidenceUsability usability) noexcept;

struct EvidenceAssessment {
  EvidenceUsability usability{EvidenceUsability::Unknown};
  ErrorCode code{ErrorCode::EvidenceUnknown};
  Millis age_millis{0};
  std::string detail{};

  [[nodiscard]] bool usable() const noexcept { return usability == EvidenceUsability::Usable; }
};

struct EvidencePolicy {
  FreshnessPolicy freshness{};
  // When false, synthetic-sourced evidence is refused. Enabled by default so that
  // a lab deployment without hardware can exercise the control semantics; every
  // report that consumes it is labelled SYNTHETIC.
  bool allow_synthetic{true};
  // When false, operator attestations are refused for this subject.
  bool allow_attestation{true};
  // When true the evidence must come from an external authority (for example a
  // protection relay permissive or a facility switch authority token).
  bool require_external_authority{false};
  // Refuse evidence observed by a source that is not allowed to speak about this
  // subject at all (for example a command acknowledgement used as an effect).
  bool require_effect_authority{false};

  [[nodiscard]] static EvidencePolicy measurement(Millis max_age) noexcept {
    return EvidencePolicy{FreshnessPolicy::within(max_age), true, false, false, false};
  }
  [[nodiscard]] static EvidencePolicy attestation(Millis max_age) noexcept {
    return EvidencePolicy{FreshnessPolicy::within(max_age), false, true, false, false};
  }
  [[nodiscard]] static EvidencePolicy external(Millis max_age) noexcept {
    return EvidencePolicy{FreshnessPolicy::within(max_age), false, false, true, false};
  }
};

// Assesses one evidence record. Never converts a missing/stale/contradictory
// record into a usable one, and never treats a present zero as absent.
template <typename T>
[[nodiscard]] EvidenceAssessment assess_evidence(const Evidence<T>& evidence, EpochMillis now,
                                                 const EvidencePolicy& policy) {
  EvidenceAssessment out{};
  switch (evidence.state) {
    case EvidenceState::Unknown:
      out.usability = EvidenceUsability::Unknown;
      out.code = ErrorCode::EvidenceUnknown;
      out.detail = evidence.detail.empty() ? "evidence state is unknown" : evidence.detail;
      return out;
    case EvidenceState::Missing:
      out.usability = EvidenceUsability::Missing;
      out.code = ErrorCode::EvidenceMissing;
      out.detail = evidence.detail.empty() ? "no evidence was recorded" : evidence.detail;
      return out;
    case EvidenceState::Stale:
      out.usability = EvidenceUsability::Stale;
      out.code = ErrorCode::EvidenceStale;
      out.detail = evidence.detail.empty() ? "evidence is marked stale" : evidence.detail;
      return out;
    case EvidenceState::Unsupported:
      out.usability = EvidenceUsability::Unsupported;
      out.code = ErrorCode::EvidenceUnsupported;
      out.detail = evidence.detail.empty() ? "the interface cannot report this value" : evidence.detail;
      return out;
    case EvidenceState::Contradictory:
      out.usability = EvidenceUsability::Contradictory;
      out.code = ErrorCode::EvidenceContradictory;
      out.detail = evidence.detail.empty() ? "evidence is self contradictory" : evidence.detail;
      return out;
    case EvidenceState::Denied:
      out.usability = EvidenceUsability::Denied;
      out.code = ErrorCode::EvidenceDenied;
      out.detail = evidence.detail.empty() ? "an authority denied this value" : evidence.detail;
      return out;
    case EvidenceState::Present:
      break;
  }

  if (policy.require_effect_authority && !is_effect_authoritative(evidence.source)) {
    out.usability = EvidenceUsability::ProvenanceRejected;
    out.code = ErrorCode::ObservationNotAuthoritative;
    out.detail = std::string("source '") + std::string(to_string(evidence.source)) +
                 "' cannot prove a physical effect";
    return out;
  }
  if (policy.require_external_authority && evidence.source != EvidenceSource::ExternalAuthority) {
    out.usability = EvidenceUsability::ProvenanceRejected;
    out.code = ErrorCode::EvidenceProvenanceRejected;
    out.detail = std::string("source '") + std::string(to_string(evidence.source)) +
                 "' is not an external authority";
    return out;
  }
  if (!policy.allow_synthetic && evidence.source == EvidenceSource::SyntheticAdapter) {
    out.usability = EvidenceUsability::ProvenanceRejected;
    out.code = ErrorCode::EvidenceProvenanceRejected;
    out.detail = "synthetic evidence is refused by policy for this subject";
    return out;
  }
  if (!policy.allow_attestation && evidence.source == EvidenceSource::OperatorAttestation) {
    out.usability = EvidenceUsability::ProvenanceRejected;
    out.code = ErrorCode::EvidenceProvenanceRejected;
    out.detail = "operator attestation is refused by policy for this subject";
    return out;
  }

  if (evidence.lifetime == EvidenceLifetime::AttestedWithValidity && evidence.valid_until != 0 &&
      now > evidence.valid_until) {
    out.usability = EvidenceUsability::Stale;
    out.code = ErrorCode::EvidenceStale;
    out.age_millis = now - evidence.valid_until;
    out.detail = "attested evidence expired at " + format_epoch_millis(evidence.valid_until);
    return out;
  }

  const FreshnessAssessment freshness = assess_freshness(evidence.observed_at, now, policy.freshness);
  out.age_millis = freshness.age_millis;
  switch (freshness.verdict) {
    case FreshnessVerdict::Fresh:
    case FreshnessVerdict::Unbounded:
      out.usability = EvidenceUsability::Usable;
      out.code = ErrorCode::Ok;
      out.detail = freshness.detail;
      return out;
    case FreshnessVerdict::Stale:
      out.usability = EvidenceUsability::Stale;
      out.code = ErrorCode::EvidenceStale;
      out.detail = freshness.detail;
      return out;
    case FreshnessVerdict::FutureDated:
      out.usability = EvidenceUsability::Contradictory;
      out.code = ErrorCode::EvidenceFutureDated;
      out.detail = freshness.detail;
      return out;
    case FreshnessVerdict::Unknown:
      break;
  }
  out.usability = EvidenceUsability::Unknown;
  out.code = ErrorCode::EvidenceUnknown;
  out.detail = "freshness could not be established";
  return out;
}

// ---------------------------------------------------------------------------
// Generic evidence codec. T must specialise ValueCodec.
// ---------------------------------------------------------------------------
template <typename T>
struct ValueCodec<Evidence<T>> {
  static Status encode(CanonicalWriter& writer, const Evidence<T>& value) {
    return encode_evidence(writer, value);
  }
  static Result<Evidence<T>> decode(CanonicalReader& reader) { return decode_evidence<T>(reader); }
};
template <typename T>
[[nodiscard]] Status encode_evidence(CanonicalWriter& writer, const Evidence<T>& evidence) {
  GENCTL_TRY(writer.put_u8(static_cast<std::uint8_t>(evidence.state)));
  GENCTL_TRY(writer.put_u8(static_cast<std::uint8_t>(evidence.source)));
  GENCTL_TRY(writer.put_u8(static_cast<std::uint8_t>(evidence.lifetime)));
  GENCTL_TRY(writer.put_i64(evidence.observed_at));
  GENCTL_TRY(writer.put_i64(evidence.valid_until));
  GENCTL_TRY(writer.put_u64(evidence.version.value()));
  GENCTL_TRY(writer.put_string(evidence.detail));
  return encode_value(writer, evidence.value);
}

template <typename T>
[[nodiscard]] Result<Evidence<T>> decode_evidence(CanonicalReader& reader) {
  Evidence<T> evidence{};
  GENCTL_TRY_ASSIGN(state, reader.get_u8());
  GENCTL_TRY_ASSIGN(source, reader.get_u8());
  GENCTL_TRY_ASSIGN(lifetime, reader.get_u8());
  GENCTL_TRY_ASSIGN(observed_at, reader.get_i64());
  GENCTL_TRY_ASSIGN(valid_until, reader.get_i64());
  GENCTL_TRY_ASSIGN(version, reader.get_u64());
  GENCTL_TRY_ASSIGN(detail, reader.get_string());
  GENCTL_TRY_ASSIGN(value, decode_value<T>(reader));
  if (state > static_cast<std::uint8_t>(EvidenceState::Denied)) {
    return make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence,
                       "evidence state out of range");
  }
  if (source > static_cast<std::uint8_t>(EvidenceSource::CommandAcknowledgement)) {
    return make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence,
                       "evidence source out of range");
  }
  if (lifetime > static_cast<std::uint8_t>(EvidenceLifetime::StaticConfiguration)) {
    return make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence,
                       "evidence lifetime out of range");
  }
  evidence.state = static_cast<EvidenceState>(state);
  evidence.source = static_cast<EvidenceSource>(source);
  evidence.lifetime = static_cast<EvidenceLifetime>(lifetime);
  evidence.observed_at = observed_at;
  evidence.valid_until = valid_until;
  evidence.version = EvidenceVersion{version};
  evidence.detail = std::move(detail);
  evidence.value = value;
  return evidence;
}

}  // namespace genctl
