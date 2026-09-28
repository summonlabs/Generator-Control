// Generator Control - identity parsing and validation.
#include "genctl/ids.hpp"

#include <cctype>

namespace genctl {
namespace {

bool is_ascii_alnum(char c) noexcept {
  const unsigned char uc = static_cast<unsigned char>(c);
  return std::isalnum(uc) != 0;
}

bool is_identifier_interior(char c) noexcept {
  if (is_ascii_alnum(c)) return true;
  return c == '.' || c == '_' || c == ':' || c == '-';
}

bool is_printable_no_space(char c) noexcept {
  const unsigned char uc = static_cast<unsigned char>(c);
  return uc >= 0x21u && uc <= 0x7Eu;
}

}  // namespace

Result<GeneratorId> GeneratorId::parse(std::string_view text) {
  if (text.empty()) {
    return make_status(ErrorCode::InvalidIdentifier, ValidationStage::Format,
                       "generator id must not be empty");
  }
  if (text.size() > kMaxLength) {
    return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Format,
                       "generator id of " + std::to_string(text.size()) +
                           " characters exceeds the maximum of " + std::to_string(kMaxLength));
  }
  if (!is_ascii_alnum(text.front()) || !is_ascii_alnum(text.back())) {
    return make_status(ErrorCode::InvalidIdentifier, ValidationStage::Format,
                       "generator id must start and end with an alphanumeric character");
  }
  for (const char c : text) {
    if (!is_identifier_interior(c)) {
      return make_status(ErrorCode::InvalidIdentifier, ValidationStage::Format,
                         std::string("generator id contains an unsupported character: '") + c + "'");
    }
  }
  if (text.find("..") != std::string_view::npos) {
    return make_status(ErrorCode::InvalidIdentifier, ValidationStage::Format,
                       "generator id must not contain a \"..\" sequence");
  }
  GeneratorId out;
  out.value_.assign(text);
  return out;
}

Result<IdempotencyKey> IdempotencyKey::parse(std::string_view text) {
  if (text.empty()) {
    return make_status(ErrorCode::IdempotencyKeyMissing, ValidationStage::Format,
                       "idempotency key must not be empty");
  }
  if (text.size() < kMinLength) {
    return make_status(ErrorCode::InvalidIdentifier, ValidationStage::Format,
                       "idempotency key must be at least " + std::to_string(kMinLength) +
                           " characters; short keys collide in practice");
  }
  if (text.size() > kMaxLength) {
    return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Format,
                       "idempotency key of " + std::to_string(text.size()) +
                           " characters exceeds the maximum of " + std::to_string(kMaxLength));
  }
  for (const char c : text) {
    if (!is_printable_no_space(c)) {
      return make_status(ErrorCode::InvalidIdentifier, ValidationStage::Format,
                         "idempotency key must contain only printable non-space ASCII characters");
    }
  }
  IdempotencyKey out;
  out.value_.assign(text);
  return out;
}

std::string to_string(const GeneratorGeneration& generation) {
  return "hw" + std::to_string(generation.hardware.value()) + "/binding" +
         std::to_string(generation.binding.value());
}

std::string to_string(const ControllerGeneration& generation) {
  return "epoch" + std::to_string(generation.epoch.value()) + "/incarnation" +
         std::to_string(generation.incarnation.value());
}

std::string to_string(const GeneratorId& id) { return id.str(); }

}  // namespace genctl
