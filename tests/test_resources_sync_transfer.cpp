// Proof obligations: resource sufficiency, synchronization preconditions and transfer
// eligibility are evidence driven, individually inspectable and never optimistic.
#include "genctl/resource.hpp"
#include "genctl/sync.hpp"
#include "genctl/transfer.hpp"
#include "support.hpp"
#include "test_framework.hpp"

namespace {
using namespace genctl;
using namespace gctest;

const EpochMillis kNow = 9'000'000;

ResourceSnapshot snapshot_with_fuel() {
  ResourceSnapshot snapshot{};
  const auto fill = [&](auto& evidence, auto quantity) {
    evidence.state = EvidenceState::Present;
    evidence.value = quantity;
    evidence.source = EvidenceSource::SyntheticAdapter;
    evidence.observed_at = kNow;
  };
  fill(snapshot.fuel_level, FuelQuantity{FuelUnit::Millilitres, 900'000});
  fill(snapshot.fuel_capacity, FuelQuantity{FuelUnit::Millilitres, 1'000'000});
  fill(snapshot.fuel_consumption, FuelRate{FuelUnit::Millilitres, 60'000});
  fill(snapshot.lube_oil_pressure, Pressure{410'000});
  fill(snapshot.coolant_temperature, Temperature{78'000});
  fill(snapshot.coolant_level, Percent{9'200});
  fill(snapshot.battery_voltage, Voltage{25'600});
  fill(snapshot.compressed_air_pressure, Pressure{700'000});
  return snapshot;
}

ResourceAssessment assess(const ResourceSnapshot& snapshot, const ResourceRequirement& requirement,
                          EpochMillis now = kNow) {
  ResourceAssessment assessment{};
  const Status status = assess_resources(
      GeneratorId::parse("gen-1").value(), StateRevision{1},
      ControllerGeneration{ControlEpoch{1}, IncarnationId{1}}, snapshot, requirement, now,
      EvidencePolicy::measurement(-1), false, &assessment);
  if (!status.ok()) fail("resource assessment failed: " + status.message());
  return assessment;
}

TEST(fuel_sufficiency_is_evidence_driven) {
  ResourceRequirement requirement{};
  requirement.reserve = FuelQuantity{FuelUnit::Millilitres, 200'000};
  requirement.required_runtime_seconds = 1800;
  const ResourceAssessment assessment = assess(snapshot_with_fuel(), requirement);
  CHECK(assessment.sufficient);
  CHECK(assessment.runtime.known);
  CHECK_EQ(assessment.runtime.seconds, 42000);
  CHECK_EQ(assessment.primary_error, ErrorCode::Ok);
}

TEST(missing_fuel_evidence_is_not_zero_fuel_but_it_is_still_a_refusal) {
  ResourceSnapshot snapshot = snapshot_with_fuel();
  snapshot.fuel_level = Evidence<FuelQuantity>{};
  snapshot.fuel_level.state = EvidenceState::Missing;
  ResourceRequirement requirement{};
  requirement.required_runtime_seconds = 60;
  const ResourceAssessment assessment = assess(snapshot, requirement);
  CHECK(!assessment.sufficient);
  CHECK_EQ(assessment.primary_error, ErrorCode::ResourceEvidenceMissing);
  bool saw_level = false;
  for (const auto& finding : assessment.findings) {
    if (finding.kind == ResourceKind::FuelLevel) {
      saw_level = true;
      CHECK(finding.usability == EvidenceUsability::Missing);
    }
  }
  CHECK(saw_level);
}

TEST(unknown_consumption_never_becomes_an_adequate_runtime) {
  ResourceSnapshot snapshot = snapshot_with_fuel();
  snapshot.fuel_consumption = Evidence<FuelRate>{};
  snapshot.fuel_consumption.state = EvidenceState::Present;
  snapshot.fuel_consumption.value = FuelRate{FuelUnit::Millilitres, 0};
  snapshot.fuel_consumption.source = EvidenceSource::SyntheticAdapter;
  snapshot.fuel_consumption.observed_at = kNow;
  ResourceRequirement requirement{};
  requirement.required_runtime_seconds = 60;
  const ResourceAssessment assessment = assess(snapshot, requirement);
  CHECK(!assessment.sufficient);
  CHECK(!assessment.runtime.known);
}

TEST(a_unit_mismatch_is_refused_rather_than_converted) {
  ResourceSnapshot snapshot = snapshot_with_fuel();
  snapshot.fuel_consumption.value = FuelRate{FuelUnit::Grams, 60'000};
  ResourceRequirement requirement{};
  requirement.required_runtime_seconds = 60;
  const ResourceAssessment assessment = assess(snapshot, requirement);
  CHECK(!assessment.sufficient);
  CHECK(!assessment.runtime.known);
  CHECK(assessment.runtime.detail.find("conversion") != std::string::npos);
}

TEST(stale_resource_evidence_is_a_refusal) {
  ResourceSnapshot snapshot = snapshot_with_fuel();
  snapshot.fuel_level.observed_at = kNow - 5 * kMillisPerHour;
  ResourceRequirement requirement{};
  requirement.required_runtime_seconds = 60;
  const ResourceAssessment assessment = assess(snapshot, requirement);
  CHECK(!assessment.sufficient);
  CHECK_EQ(assessment.primary_error, ErrorCode::ResourceEvidenceStale);
}

TEST(insufficient_runtime_is_reported_with_both_numbers) {
  ResourceSnapshot snapshot = snapshot_with_fuel();
  ResourceRequirement requirement{};
  requirement.reserve = FuelQuantity{FuelUnit::Millilitres, 200'000};
  requirement.required_runtime_seconds = 86400;
  const ResourceAssessment assessment = assess(snapshot, requirement);
  CHECK(!assessment.sufficient);
  CHECK_EQ(assessment.primary_error, ErrorCode::ResourceInsufficient);
  CHECK(assessment.runtime.detail.find("42000s available") != std::string::npos);
}

TEST(synchronization_requires_every_named_precondition) {
  SyncPolicy policy{};
  std::vector<PreconditionRecord> records;
  for (const SyncPreconditionKind kind : default_sync_preconditions(policy)) {
    PreconditionRecord record{};
    record.kind = kind;
    record.required = true;
    record.state = EvidenceState::Present;
    record.source = EvidenceSource::SyntheticAdapter;
    record.observed_at = kNow;
    record.max_age_millis = 60'000;
    switch (kind) {
      case SyncPreconditionKind::VoltageMatch:
        record.value = TypedValue{TypedValueKind::Millivolts, 1200};
        break;
      case SyncPreconditionKind::FrequencyMatch:
        record.value = TypedValue{TypedValueKind::Millihertz, 40};
        break;
      case SyncPreconditionKind::PhaseAngleMatch:
        record.value = TypedValue{TypedValueKind::Millidegrees, 1500};
        break;
      default:
        record.value = TypedValue{TypedValueKind::Boolean, 1};
        break;
    }
    records.push_back(record);
  }

  SynchronizationEligibility eligibility{};
  Status status = evaluate_synchronization(
      GeneratorId::parse("gen-1").value(), StateRevision{1},
      ControllerGeneration{ControlEpoch{1}, IncarnationId{1}}, records, policy, kNow, &eligibility);
  CHECK(status.ok());
  CHECK(eligibility.eligible());
  CHECK(eligibility.findings.size() >= 9);

  // A phase angle outside the window is definitively ineligible.
  std::vector<PreconditionRecord> outside = records;
  for (auto& record : outside) {
    if (record.kind == SyncPreconditionKind::PhaseAngleMatch) {
      record.value = TypedValue{TypedValueKind::Millidegrees, 90'000};
    }
  }
  status = evaluate_synchronization(GeneratorId::parse("gen-1").value(), StateRevision{1},
                                    ControllerGeneration{ControlEpoch{1}, IncarnationId{1}}, outside,
                                    policy, kNow, &eligibility);
  CHECK(status.ok());
  CHECK(eligibility.outcome == EligibilityOutcome::Ineligible);
  CHECK_EQ(eligibility.primary_error, ErrorCode::SynchronizationPreconditionFailed);

  // Dropping a named precondition makes the answer indeterminate, never eligible.
  std::vector<PreconditionRecord> incomplete;
  for (const auto& record : records) {
    if (record.kind == SyncPreconditionKind::SyncRelayPermissive) continue;
    incomplete.push_back(record);
  }
  status = evaluate_synchronization(GeneratorId::parse("gen-1").value(), StateRevision{1},
                                    ControllerGeneration{ControlEpoch{1}, IncarnationId{1}},
                                    incomplete, policy, kNow, &eligibility);
  CHECK(status.ok());
  CHECK(eligibility.outcome == EligibilityOutcome::Indeterminate);
  CHECK_EQ(eligibility.primary_error, ErrorCode::SynchronizationEvidenceMissing);
}

TEST(a_command_acknowledgement_cannot_satisfy_a_synchronization_precondition) {
  SyncPolicy policy{};
  std::vector<PreconditionRecord> records;
  for (const SyncPreconditionKind kind : default_sync_preconditions(policy)) {
    PreconditionRecord record{};
    record.kind = kind;
    record.required = true;
    record.state = EvidenceState::Present;
    record.source = EvidenceSource::CommandAcknowledgement;
    record.observed_at = kNow;
    record.max_age_millis = 60'000;
    record.value = TypedValue{TypedValueKind::Boolean, 1};
    if (kind == SyncPreconditionKind::VoltageMatch) {
      record.value = TypedValue{TypedValueKind::Millivolts, 0};
    }
    records.push_back(record);
  }
  SynchronizationEligibility eligibility{};
  const Status status = evaluate_synchronization(
      GeneratorId::parse("gen-1").value(), StateRevision{1},
      ControllerGeneration{ControlEpoch{1}, IncarnationId{1}}, records, policy, kNow, &eligibility);
  CHECK(status.ok());
  CHECK(!eligibility.eligible());
  CHECK(eligibility.outcome == EligibilityOutcome::Indeterminate);
}

TEST(stale_synchronization_evidence_is_indeterminate_not_eligible) {
  SyncPolicy policy{};
  std::vector<PreconditionRecord> records;
  for (const SyncPreconditionKind kind : default_sync_preconditions(policy)) {
    PreconditionRecord record{};
    record.kind = kind;
    record.required = true;
    record.state = EvidenceState::Present;
    record.source = EvidenceSource::SyntheticAdapter;
    record.observed_at = kNow - 60'000;
    record.max_age_millis = 5'000;
    record.value = TypedValue{TypedValueKind::Boolean, 1};
    records.push_back(record);
  }
  SynchronizationEligibility eligibility{};
  const Status status = evaluate_synchronization(
      GeneratorId::parse("gen-1").value(), StateRevision{1},
      ControllerGeneration{ControlEpoch{1}, IncarnationId{1}}, records, policy, kNow, &eligibility);
  CHECK(status.ok());
  CHECK(eligibility.outcome == EligibilityOutcome::Indeterminate);
  CHECK_EQ(eligibility.primary_error, ErrorCode::SynchronizationEvidenceStale);
}

TEST(transfer_eligibility_needs_the_external_switch_authority) {
  TransferPath path{};
  path.breaker = SwitchRef::parse("breaker-1").value();
  path.source_bus = SwitchRef::parse("bus-utility").value();
  path.target_bus = SwitchRef::parse("bus-generator").value();
  path.feeder = SwitchRef::parse("feeder-1").value();
  path.transfer_path = SwitchRef::parse("path-1").value();

  std::vector<TransferPreconditionRecord> records;
  for (const TransferPreconditionKind kind : default_transfer_preconditions()) {
    TransferPreconditionRecord record{};
    record.kind = kind;
    record.required = true;
    record.state = EvidenceState::Present;
    record.source = EvidenceSource::ExternalAuthority;
    record.observed_at = kNow;
    record.max_age_millis = 60'000;
    record.value = TypedValue{TypedValueKind::Boolean, 1};
    records.push_back(record);
  }

  TransferPolicy policy{};
  TransferEligibility eligibility{};
  Status status = evaluate_transfer(
      GeneratorId::parse("gen-1").value(), StateRevision{1},
      ControllerGeneration{ControlEpoch{1}, IncarnationId{1}},
      GeneratorGeneration{HardwareGeneration{1}, BindingEpoch{1}}, path, records, policy, kNow,
      &eligibility);
  CHECK(status.ok());
  CHECK(eligibility.eligible());

  // An incomplete path reference is a refusal, not something the runtime invents.
  TransferPath incomplete = path;
  incomplete.breaker = SwitchRef{};
  status = evaluate_transfer(GeneratorId::parse("gen-1").value(), StateRevision{1},
                             ControllerGeneration{ControlEpoch{1}, IncarnationId{1}},
                             GeneratorGeneration{HardwareGeneration{1}, BindingEpoch{1}}, incomplete,
                             records, policy, kNow, &eligibility);
  CHECK(status.ok());
  CHECK(eligibility.outcome == EligibilityOutcome::Ineligible);
  CHECK_EQ(eligibility.primary_error, ErrorCode::TransferTopologyReferenceInvalid);

  // An internal source cannot stand in for the external switch authority.
  std::vector<TransferPreconditionRecord> internal_source = records;
  for (auto& record : internal_source) {
    if (record.kind == TransferPreconditionKind::FacilitySwitchAuthorityGranted) {
      record.source = EvidenceSource::VendorAdapter;
    }
  }
  status = evaluate_transfer(
      GeneratorId::parse("gen-1").value(), StateRevision{1},
      ControllerGeneration{ControlEpoch{1}, IncarnationId{1}},
      GeneratorGeneration{HardwareGeneration{1}, BindingEpoch{1}}, path, internal_source, policy,
      kNow, &eligibility);
  CHECK(status.ok());
  CHECK(eligibility.outcome == EligibilityOutcome::Indeterminate);
}

TEST(switch_reference_validation_is_strict) {
  CHECK(SwitchRef::parse("breaker-1").ok());
  CHECK_RESULT_CODE(SwitchRef::parse(""), ErrorCode::InvalidIdentifier);
  CHECK_RESULT_CODE(SwitchRef::parse("bad/name"), ErrorCode::InvalidIdentifier);
  CHECK_RESULT_CODE(SwitchRef::parse(std::string(65, 'x')), ErrorCode::LengthLimitExceeded);
}

}  // namespace
