// Proof obligations: readiness and interlock fail-closed behaviour, emergency waiver
// scope, and deterministic primary error selection.
#include "genctl/readiness.hpp"
#include "support.hpp"
#include "test_framework.hpp"

namespace {
using namespace genctl;
using namespace gctest;

const EpochMillis kNow = 5'000'000;

CheckRecord record_for(CheckKind kind, EvidenceState state, EvidenceSource source,
                       EpochMillis observed_at, TypedValue value = TypedValue{TypedValueKind::Boolean, 1}) {
  CheckRecord record{};
  record.kind = kind;
  record.required = true;
  record.state = state;
  record.source = source;
  record.observed_at = observed_at;
  record.max_age_millis = 60'000;
  record.value = value;
  return record;
}

ReadinessReport evaluate(const std::vector<CheckRecord>& records, bool waive = false,
                         EvidencePolicy policy = EvidencePolicy::measurement(-1)) {
  ReadinessReport report{};
  const Status status = evaluate_readiness(GeneratorId::parse("gen-1").value(), StateRevision{1},
                                           ControllerGeneration{ControlEpoch{1}, IncarnationId{1}},
                                           GeneratorGeneration{HardwareGeneration{1}, BindingEpoch{1}},
                                           default_required_checks(), records, kNow, policy, waive,
                                           &report);
  if (!status.ok()) fail("readiness evaluation failed: " + status.message());
  return report;
}

TEST(missing_safety_evidence_fails_closed) {
  const ReadinessReport report = evaluate({});
  CHECK(!report.satisfied);
  CHECK(!report.interlock_evidence_complete);
  CHECK_EQ(report.primary_error, ErrorCode::InterlockEvidenceUnavailable);
  CHECK(report.stage == ValidationStage::Interlock);
}

TEST(a_complete_binding_satisfies_readiness) {
  std::vector<CheckRecord> records;
  for (const CheckKind kind : default_required_checks()) {
    records.push_back(record_for(kind, EvidenceState::Present, EvidenceSource::VendorAdapter, kNow));
  }
  const ReadinessReport report = evaluate(records);
  CHECK(report.satisfied);
  CHECK(report.interlock_evidence_complete);
  CHECK_EQ(report.primary_error, ErrorCode::Ok);
  CHECK(!report.synthetic_evidence_used);
}

TEST(a_tripped_interlock_is_an_engaged_condition_not_a_permission) {
  std::vector<CheckRecord> records;
  for (const CheckKind kind : default_required_checks()) {
    TypedValue value{TypedValueKind::Boolean, 1};
    if (kind == CheckKind::EmergencyStopNotEngaged) value = TypedValue{TypedValueKind::Boolean, 0};
    records.push_back(
        record_for(kind, EvidenceState::Present, EvidenceSource::VendorAdapter, kNow, value));
  }
  const ReadinessReport report = evaluate(records);
  CHECK(!report.satisfied);
  CHECK_EQ(report.primary_error, ErrorCode::InterlockEngaged);
}

TEST(stale_interlock_evidence_fails_closed) {
  std::vector<CheckRecord> records;
  for (const CheckKind kind : default_required_checks()) {
    const EpochMillis at =
        kind == CheckKind::FireSuppressionClear ? kNow - 120'000 : kNow;
    records.push_back(record_for(kind, EvidenceState::Present, EvidenceSource::VendorAdapter, at));
  }
  const ReadinessReport report = evaluate(records);
  CHECK(!report.satisfied);
  CHECK_EQ(report.primary_error, ErrorCode::InterlockEvidenceUnavailable);
}

TEST(emergency_authority_waives_only_advisory_checks) {
  std::vector<CheckRecord> records;
  for (const CheckKind kind : default_required_checks()) {
    if (kind == CheckKind::MaintenanceWindowClear) continue;  // advisory, absent
    records.push_back(record_for(kind, EvidenceState::Present, EvidenceSource::VendorAdapter, kNow));
  }
  const ReadinessReport without_waiver = evaluate(records, false);
  CHECK(!without_waiver.satisfied);
  CHECK(without_waiver.stage == ValidationStage::Readiness);

  const ReadinessReport with_waiver = evaluate(records, true);
  CHECK(with_waiver.satisfied);
  CHECK(with_waiver.advisory_waived);
  CHECK(!with_waiver.findings.empty());

  // A missing safety interlock is never waivable, in any mode.
  std::vector<CheckRecord> missing_safety;
  for (const CheckKind kind : default_required_checks()) {
    if (kind == CheckKind::OverspeedTripClear) continue;
    missing_safety.push_back(
        record_for(kind, EvidenceState::Present, EvidenceSource::VendorAdapter, kNow));
  }
  const ReadinessReport safety_waiver = evaluate(missing_safety, true);
  CHECK(!safety_waiver.satisfied);
  CHECK_EQ(safety_waiver.primary_error, ErrorCode::InterlockEvidenceUnavailable);
}

TEST(waivable_classification_is_explicit) {
  CHECK(is_waivable(CheckKind::MaintenanceWindowClear));
  CHECK(is_waivable(CheckKind::EconomicReserveSatisfied));
  CHECK(!is_waivable(CheckKind::EmergencyStopNotEngaged));
  CHECK(!is_waivable(CheckKind::ProtectionRelayPermissive));
  CHECK(!is_waivable(CheckKind::BatteryVoltageSufficient));
  CHECK(check_class(CheckKind::FireSuppressionClear) == CheckClass::Safety);
  CHECK(check_class(CheckKind::GovernorAvrReady) == CheckClass::Protection);
  CHECK(check_class(CheckKind::CoolantLevelSufficient) == CheckClass::Readiness);
}

TEST(an_extra_recorded_check_is_reported_but_not_required) {
  std::vector<CheckRecord> records;
  for (const CheckKind kind : default_required_checks()) {
    records.push_back(record_for(kind, EvidenceState::Present, EvidenceSource::VendorAdapter, kNow));
  }
  CheckRecord extra = record_for(CheckKind::FuelQualityAttested, EvidenceState::Missing,
                                 EvidenceSource::Unknown, 0);
  extra.required = false;
  records.push_back(extra);
  const ReadinessReport report = evaluate(records);
  CHECK(report.satisfied);
  bool saw_optional = false;
  for (const auto& finding : report.findings) {
    if (finding.kind == CheckKind::FuelQualityAttested) {
      saw_optional = true;
      CHECK(!finding.required);
    }
  }
  CHECK(saw_optional);
}

TEST(a_command_acknowledgement_cannot_satisfy_a_check) {
  std::vector<CheckRecord> records;
  for (const CheckKind kind : default_required_checks()) {
    records.push_back(record_for(kind, EvidenceState::Present,
                                 EvidenceSource::CommandAcknowledgement, kNow));
  }
  const ReadinessReport report = evaluate(records);
  CHECK(!report.satisfied);
  CHECK(!report.interlock_evidence_complete);
  CHECK_EQ(report.primary_error, ErrorCode::ObservationNotAuthoritative);
}

TEST(the_primary_error_is_the_first_finding_in_class_order) {
  std::vector<CheckRecord> records;
  for (const CheckKind kind : default_required_checks()) {
    EvidenceState state = EvidenceState::Present;
    if (kind == CheckKind::CoolantLevelSufficient) state = EvidenceState::Stale;
    if (kind == CheckKind::MaintenanceWindowClear) state = EvidenceState::Missing;
    records.push_back(record_for(kind, state, EvidenceSource::VendorAdapter, kNow));
  }
  const ReadinessReport report = evaluate(records);
  CHECK(!report.satisfied);
  // Safety and protection findings come before machine readiness and advisory ones.
  CHECK(report.stage == ValidationStage::Readiness);
  CHECK_EQ(report.primary_error, ErrorCode::EvidenceStale);
}

TEST(binding_digest_is_deterministic_and_content_sensitive) {
  std::vector<CheckRecord> first;
  for (const CheckKind kind : default_required_checks()) {
    first.push_back(record_for(kind, EvidenceState::Present, EvidenceSource::VendorAdapter, kNow));
  }
  const ReadinessReport a = evaluate(first);
  const ReadinessReport b = evaluate(first);
  CHECK(a.binding_digest == b.binding_digest);

  std::vector<CheckRecord> changed = first;
  changed[0].state = EvidenceState::Stale;
  const ReadinessReport c = evaluate(changed);
  CHECK(a.binding_digest != c.binding_digest);
}

TEST(synthetic_evidence_is_flagged_for_labelling) {
  std::vector<CheckRecord> records;
  for (const CheckKind kind : default_required_checks()) {
    records.push_back(
        record_for(kind, EvidenceState::Present, EvidenceSource::SyntheticAdapter, kNow));
  }
  const ReadinessReport report = evaluate(records);
  CHECK(report.satisfied);
  CHECK(report.synthetic_evidence_used);
}

}  // namespace
