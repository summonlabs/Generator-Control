// Generator Control - evidence vocabulary and source authority.
#include "genctl/evidence.hpp"

namespace genctl {

std::string_view to_string(EvidenceState state) noexcept {
  switch (state) {
    case EvidenceState::Unknown: return "unknown";
    case EvidenceState::Present: return "present";
    case EvidenceState::Missing: return "missing";
    case EvidenceState::Stale: return "stale";
    case EvidenceState::Unsupported: return "unsupported";
    case EvidenceState::Contradictory: return "contradictory";
    case EvidenceState::Denied: return "denied";
  }
  return "unknown";
}

std::string_view to_string(EvidenceSource source) noexcept {
  switch (source) {
    case EvidenceSource::Unknown: return "unknown";
    case EvidenceSource::SyntheticAdapter: return "synthetic-adapter";
    case EvidenceSource::VendorAdapter: return "vendor-adapter";
    case EvidenceSource::OperatorAttestation: return "operator-attestation";
    case EvidenceSource::DerivedMeasurement: return "derived-measurement";
    case EvidenceSource::ExternalAuthority: return "external-authority";
    case EvidenceSource::CommandAcknowledgement: return "command-acknowledgement";
  }
  return "unknown";
}

Result<EvidenceSource> parse_evidence_source(std::string_view text) {
  if (text == "synthetic-adapter" || text == "synthetic") return EvidenceSource::SyntheticAdapter;
  if (text == "vendor-adapter" || text == "vendor") return EvidenceSource::VendorAdapter;
  if (text == "operator-attestation" || text == "operator") {
    return EvidenceSource::OperatorAttestation;
  }
  if (text == "derived-measurement" || text == "derived") {
    return EvidenceSource::DerivedMeasurement;
  }
  if (text == "external-authority" || text == "external") return EvidenceSource::ExternalAuthority;
  if (text == "command-acknowledgement" || text == "ack") {
    return EvidenceSource::CommandAcknowledgement;
  }
  return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                     std::string("unrecognised evidence source '") + std::string(text) + "'");
}

bool is_effect_authoritative(EvidenceSource source) noexcept {
  switch (source) {
    case EvidenceSource::VendorAdapter:
    case EvidenceSource::SyntheticAdapter:
    case EvidenceSource::ExternalAuthority:
      return true;
    case EvidenceSource::Unknown:
    case EvidenceSource::OperatorAttestation:
    case EvidenceSource::DerivedMeasurement:
    case EvidenceSource::CommandAcknowledgement:
      return false;
  }
  return false;
}

bool is_control_authoritative(EvidenceSource source) noexcept {
  switch (source) {
    case EvidenceSource::VendorAdapter:
    case EvidenceSource::SyntheticAdapter:
    case EvidenceSource::ExternalAuthority:
    case EvidenceSource::OperatorAttestation:
      return true;
    case EvidenceSource::Unknown:
    case EvidenceSource::DerivedMeasurement:
    case EvidenceSource::CommandAcknowledgement:
      return false;
  }
  return false;
}

std::string_view to_string(EvidenceLifetime lifetime) noexcept {
  switch (lifetime) {
    case EvidenceLifetime::VolatileObservation: return "volatile-observation";
    case EvidenceLifetime::AttestedWithValidity: return "attested-with-validity";
    case EvidenceLifetime::StaticConfiguration: return "static-configuration";
  }
  return "volatile-observation";
}

std::string_view to_string(EvidenceUsability usability) noexcept {
  switch (usability) {
    case EvidenceUsability::Usable: return "usable";
    case EvidenceUsability::Unknown: return "unknown";
    case EvidenceUsability::Missing: return "missing";
    case EvidenceUsability::Stale: return "stale";
    case EvidenceUsability::Unsupported: return "unsupported";
    case EvidenceUsability::Contradictory: return "contradictory";
    case EvidenceUsability::Denied: return "denied";
    case EvidenceUsability::ProvenanceRejected: return "provenance-rejected";
  }
  return "unknown";
}

}  // namespace genctl
