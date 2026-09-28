// Proof obligations: stale, missing, unknown, unsupported, denied and contradictory
// evidence never become permission, and a command acknowledgement never becomes an
// electrical effect.
#include "genctl/evidence.hpp"
#include "support.hpp"
#include "test_framework.hpp"

namespace {
using namespace genctl;
using namespace gctest;

Evidence<Voltage> measured(EvidenceState state, EpochMillis observed_at, EvidenceSource source) {
  Evidence<Voltage> evidence{};
  evidence.state = state;
  evidence.value = Voltage{400'000};
  evidence.source = source;
  evidence.lifetime = EvidenceLifetime::VolatileObservation;
  evidence.observed_at = observed_at;
  return evidence;
}

const EpochMillis kNow = 2'000'000;

TEST(a_present_zero_is_not_a_missing_value) {
  Evidence<Voltage> zero{};
  zero.state = EvidenceState::Present;
  zero.value = Voltage{0};
  zero.source = EvidenceSource::VendorAdapter;
  zero.observed_at = kNow;
  const EvidenceAssessment assessment =
      assess_evidence(zero, kNow, EvidencePolicy::measurement(1000));
  CHECK(assessment.usable());
  CHECK(zero.has_value());

  Evidence<Voltage> missing{};
  missing.state = EvidenceState::Missing;
  const EvidenceAssessment missing_assessment =
      assess_evidence(missing, kNow, EvidencePolicy::measurement(1000));
  CHECK(!missing_assessment.usable());
  CHECK_EQ(missing_assessment.code, ErrorCode::EvidenceMissing);
}

TEST(every_non_present_state_is_refused_with_its_own_code) {
  struct Case {
    EvidenceState state;
    ErrorCode code;
  };
  const Case cases[] = {
      {EvidenceState::Unknown, ErrorCode::EvidenceUnknown},
      {EvidenceState::Missing, ErrorCode::EvidenceMissing},
      {EvidenceState::Stale, ErrorCode::EvidenceStale},
      {EvidenceState::Unsupported, ErrorCode::EvidenceUnsupported},
      {EvidenceState::Contradictory, ErrorCode::EvidenceContradictory},
      {EvidenceState::Denied, ErrorCode::EvidenceDenied},
  };
  for (const Case& item : cases) {
    const EvidenceAssessment assessment = assess_evidence(
        measured(item.state, kNow, EvidenceSource::VendorAdapter), kNow,
        EvidencePolicy::measurement(1000));
    CHECK(!assessment.usable());
    CHECK_EQ(assessment.code, item.code);
  }
}

TEST(stale_evidence_never_becomes_permission) {
  const EvidenceAssessment assessment = assess_evidence(
      measured(EvidenceState::Present, kNow - 10'000, EvidenceSource::VendorAdapter), kNow,
      EvidencePolicy::measurement(1000));
  CHECK(!assessment.usable());
  CHECK_EQ(assessment.code, ErrorCode::EvidenceStale);
}

TEST(future_dated_evidence_is_contradictory_not_fresh) {
  const EvidenceAssessment assessment = assess_evidence(
      measured(EvidenceState::Present, kNow + 60'000, EvidenceSource::VendorAdapter), kNow,
      EvidencePolicy::measurement(1000));
  CHECK(!assessment.usable());
  CHECK_EQ(assessment.code, ErrorCode::EvidenceFutureDated);
}

TEST(a_command_acknowledgement_can_never_prove_an_effect) {
  CHECK(!is_effect_authoritative(EvidenceSource::CommandAcknowledgement));
  CHECK(!is_effect_authoritative(EvidenceSource::OperatorAttestation));
  CHECK(!is_effect_authoritative(EvidenceSource::DerivedMeasurement));
  CHECK(!is_effect_authoritative(EvidenceSource::Unknown));
  CHECK(is_effect_authoritative(EvidenceSource::VendorAdapter));
  CHECK(is_effect_authoritative(EvidenceSource::SyntheticAdapter));
  CHECK(is_effect_authoritative(EvidenceSource::ExternalAuthority));

  EvidencePolicy policy = EvidencePolicy::measurement(1000);
  policy.require_effect_authority = true;
  const EvidenceAssessment assessment =
      assess_evidence(measured(EvidenceState::Present, kNow, EvidenceSource::CommandAcknowledgement),
                      kNow, policy);
  CHECK(!assessment.usable());
  CHECK_EQ(assessment.code, ErrorCode::ObservationNotAuthoritative);
}

TEST(external_authority_requirements_are_enforced) {
  const EvidenceAssessment refused = assess_evidence(
      measured(EvidenceState::Present, kNow, EvidenceSource::OperatorAttestation), kNow,
      EvidencePolicy::external(1000));
  CHECK(!refused.usable());
  CHECK_EQ(refused.code, ErrorCode::EvidenceProvenanceRejected);

  const EvidenceAssessment accepted = assess_evidence(
      measured(EvidenceState::Present, kNow, EvidenceSource::ExternalAuthority), kNow,
      EvidencePolicy::external(1000));
  CHECK(accepted.usable());
}

TEST(synthetic_evidence_can_be_refused_by_policy) {
  const EvidenceAssessment refused = assess_evidence(
      measured(EvidenceState::Present, kNow, EvidenceSource::SyntheticAdapter), kNow,
      EvidencePolicy::measurement(1000));
  CHECK(refused.usable());  // accepted by default so a laboratory can run

  EvidencePolicy strict = EvidencePolicy::measurement(1000);
  strict.allow_synthetic = false;
  const EvidenceAssessment strict_assessment =
      assess_evidence(measured(EvidenceState::Present, kNow, EvidenceSource::SyntheticAdapter),
                      kNow, strict);
  CHECK(!strict_assessment.usable());
  CHECK_EQ(strict_assessment.code, ErrorCode::EvidenceProvenanceRejected);
}

TEST(attested_evidence_respects_its_explicit_validity_window) {
  Evidence<Voltage> attested{};
  attested.state = EvidenceState::Present;
  attested.value = Voltage{400'000};
  attested.source = EvidenceSource::ExternalAuthority;
  attested.lifetime = EvidenceLifetime::AttestedWithValidity;
  attested.observed_at = kNow;
  attested.valid_until = kNow + 5'000;

  CHECK(assess_evidence(attested, kNow + 1'000, EvidencePolicy::measurement(-1)).usable());
  const EvidenceAssessment expired =
      assess_evidence(attested, kNow + 6'000, EvidencePolicy::measurement(-1));
  CHECK(!expired.usable());
  CHECK_EQ(expired.code, ErrorCode::EvidenceStale);
}

TEST(unbounded_freshness_is_an_explicit_policy_not_a_default) {
  const EvidenceAssessment unlimited = assess_evidence(
      measured(EvidenceState::Present, kNow - 10'000'000, EvidenceSource::VendorAdapter), kNow,
      EvidencePolicy::measurement(-1));
  CHECK(unlimited.usable());

  const EvidenceAssessment bounded = assess_evidence(
      measured(EvidenceState::Present, kNow - 10'000'000, EvidenceSource::VendorAdapter), kNow,
      EvidencePolicy::measurement(1000));
  CHECK(!bounded.usable());
}

}  // namespace
