// Generator Control - adapter boundary base definitions.
#include "genctl/adapter.hpp"

namespace genctl {

GeneratorAdapter::~GeneratorAdapter() = default;

std::string_view to_string(AdapterAckStatus status) noexcept {
  switch (status) {
    case AdapterAckStatus::None: return "none";
    case AdapterAckStatus::Accepted: return "accepted";
    case AdapterAckStatus::Rejected: return "rejected";
    case AdapterAckStatus::Unsupported: return "unsupported";
    case AdapterAckStatus::Busy: return "busy";
    case AdapterAckStatus::Invalid: return "invalid";
  }
  return "none";
}

}  // namespace genctl
