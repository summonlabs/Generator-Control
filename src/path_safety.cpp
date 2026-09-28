// Generator Control - path validation under the documented trust model.
#include "genctl/path_safety.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "genctl/platform.hpp"

namespace genctl {
namespace {

constexpr std::size_t kMaxPathBytes = 4096;
constexpr std::size_t kMaxComponentBytes = 255;

char ascii_upper(char c) noexcept {
  return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

std::string ascii_upper_copy(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = ascii_upper(c);
  return out;
}

std::vector<std::string_view> split_components(std::string_view raw) {
  std::vector<std::string_view> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= raw.size(); ++i) {
    const bool at_end = i == raw.size();
    if (!at_end && raw[i] != '\\' && raw[i] != '/') continue;
    out.push_back(raw.substr(start, i - start));
    start = i + 1;
  }
  return out;
}

bool contains_control(std::string_view raw) noexcept {
  for (const char c : raw) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 0x20u || uc == 0x7Fu) return true;
  }
  return false;
}

}  // namespace

bool is_valid_utf8(std::string_view text) noexcept {
  std::size_t i = 0;
  while (i < text.size()) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    std::size_t extra = 0;
    std::uint32_t code_point = 0;
    std::uint32_t minimum = 0;
    if (lead < 0x80u) {
      ++i;
      continue;
    } else if ((lead & 0xE0u) == 0xC0u) {
      extra = 1;
      code_point = lead & 0x1Fu;
      minimum = 0x80u;
    } else if ((lead & 0xF0u) == 0xE0u) {
      extra = 2;
      code_point = lead & 0x0Fu;
      minimum = 0x800u;
    } else if ((lead & 0xF8u) == 0xF0u) {
      extra = 3;
      code_point = lead & 0x07u;
      minimum = 0x10000u;
    } else {
      return false;  // continuation byte as lead, or an overlong/invalid lead
    }
    if (i + extra >= text.size()) return false;
    for (std::size_t k = 1; k <= extra; ++k) {
      const unsigned char continuation = static_cast<unsigned char>(text[i + k]);
      if ((continuation & 0xC0u) != 0x80u) return false;
      code_point = (code_point << 6) | (continuation & 0x3Fu);
    }
    if (code_point < minimum) return false;                    // overlong form
    if (code_point > 0x10FFFFu) return false;                  // out of range
    if (code_point >= 0xD800u && code_point <= 0xDFFFu) return false;  // surrogate
    i += extra + 1;
  }
  return true;
}

bool is_device_name(std::string_view component) noexcept {
  if (component.empty()) return false;
  // A device name is reserved with or without an extension ("NUL", "NUL.txt").
  const std::size_t dot = component.find('.');
  const std::string_view stem = dot == std::string_view::npos ? component : component.substr(0, dot);
  const std::string upper = ascii_upper_copy(stem);
  static const std::array<const char*, 22> kNames = {
      "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
      "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
  for (const char* name : kNames) {
    if (upper == name) return true;
  }
  return upper == "CONIN$" || upper == "CONOUT$";
}

bool has_unsafe_component(std::string_view raw_utf8) noexcept {
  if (raw_utf8.empty()) return true;
  if (contains_control(raw_utf8)) return true;
  const std::vector<std::string_view> components = split_components(raw_utf8);
  std::size_t index = 0;
  for (const std::string_view component : components) {
    const bool first = index == 0;
    ++index;
    if (component.empty()) continue;  // leading separator, UNC prefix or trailing separator
    if (component == "." || component == "..") return true;
    if (component.size() > kMaxComponentBytes) return true;
    // "C:" as the first component is a drive designator, not an alternate data
    // stream. Any other colon introduces one.
    if (component.find(':') != std::string_view::npos) {
      const bool drive = first && component.size() == 2 &&
                         ((component[0] >= 'A' && component[0] <= 'Z') ||
                          (component[0] >= 'a' && component[0] <= 'z'));
      if (!drive) return true;
    }
    if (is_device_name(component)) return true;
    // Windows silently strips trailing dots and spaces, which would let two
    // different inputs name the same file.
    if (component.back() == '.' || component.back() == ' ') return true;
  }
  return false;
}

namespace {

// A path is absolute when it starts with a drive designator or a UNC prefix. A
// relative path is refused rather than resolved against the process working
// directory: the working directory must never decide which store is opened.
bool is_absolute(std::string_view raw) noexcept {
  if (raw.size() >= 3 && ((raw[0] >= 'A' && raw[0] <= 'Z') || (raw[0] >= 'a' && raw[0] <= 'z')) &&
      raw[1] == ':' && (raw[2] == '\\' || raw[2] == '/')) {
    return true;
  }
  return raw.size() >= 2 && raw[0] == '\\' && raw[1] == '\\';
}

// Text level checks: encoding, size and control characters.
Status validate_text(std::string_view raw_utf8, const char* what) {
  if (raw_utf8.empty()) {
    return make_status(ErrorCode::PathInvalid, ValidationStage::Format,
                       std::string(what) + " must not be empty");
  }
  if (raw_utf8.size() > kMaxPathBytes) {
    return make_status(ErrorCode::PathTooLong, ValidationStage::Format,
                       std::string(what) + " exceeds " + std::to_string(kMaxPathBytes) + " bytes");
  }
  if (!is_valid_utf8(raw_utf8)) {
    return make_status(ErrorCode::PathEncodingInvalid, ValidationStage::Format,
                       std::string(what) + " is not valid UTF-8");
  }
  if (contains_control(raw_utf8)) {
    return make_status(ErrorCode::PathInvalid, ValidationStage::Format,
                       std::string(what) + " contains a control character");
  }
  return Status::success();
}

// Component level checks. Traversal, device names, trailing dot/space and alternate
// data streams are detected on the raw text, before any normalisation could erase
// the evidence of an attack, and they outrank the relative-path refusal because they
// describe what the input is actually trying to do.
Status validate_components(std::string_view raw_utf8, const char* what) {
  if (has_unsafe_component(raw_utf8)) {
    return make_status(ErrorCode::PathTraversal, ValidationStage::Format,
                       std::string(what) +
                           " contains a traversal component, a reserved device name, a trailing "
                           "dot or space, or an alternate data stream");
  }
  return Status::success();
}

Status require_absolute(std::string_view raw_utf8, const char* what) {
  if (is_absolute(raw_utf8)) return Status::success();
  return make_status(ErrorCode::PathNotAbsolute, ValidationStage::Format,
                     std::string(what) +
                         " must be an absolute path so that the process working directory can "
                         "never select a different location");
}

}  // namespace

Result<std::string> validate_store_directory(std::string_view raw_utf8) {
  GENCTL_TRY(validate_text(raw_utf8, "store directory"));
  GENCTL_TRY(validate_components(raw_utf8, "store directory"));
  GENCTL_TRY(require_absolute(raw_utf8, "store directory"));
  GENCTL_TRY_ASSIGN(resolved, platform::absolute_path(std::string(raw_utf8)));
  GENCTL_TRY(reject_reparse_points(resolved));
  return resolved;
}

Result<std::string> validate_file_path(std::string_view raw_utf8) {
  GENCTL_TRY(validate_text(raw_utf8, "file path"));
  GENCTL_TRY(validate_components(raw_utf8, "file path"));
  GENCTL_TRY(require_absolute(raw_utf8, "file path"));
  GENCTL_TRY_ASSIGN(resolved, platform::absolute_path(std::string(raw_utf8)));
  GENCTL_TRY(reject_reparse_points(resolved));
  return resolved;
}

Result<std::string> join_inside(const std::string& base_utf8, std::string_view leaf) {
  if (leaf.empty()) {
    return make_status(ErrorCode::PathInvalid, ValidationStage::Format, "leaf name must not be empty");
  }
  if (leaf.find('\\') != std::string_view::npos || leaf.find('/') != std::string_view::npos ||
      leaf == "." || leaf == ".." || leaf.find(':') != std::string_view::npos ||
      is_device_name(leaf)) {
    return make_status(ErrorCode::PathTraversal, ValidationStage::Format,
                       "leaf name is not a plain file name");
  }
  std::string base = base_utf8;
  while (!base.empty() && (base.back() == '\\' || base.back() == '/')) base.pop_back();
  const std::string joined = base + "\\" + std::string(leaf);
  const std::string upper_base = ascii_upper_copy(base);
  const std::string upper_joined = ascii_upper_copy(joined);
  if (upper_joined.size() <= upper_base.size() ||
      upper_joined.compare(0, upper_base.size(), upper_base) != 0) {
    return make_status(ErrorCode::PathTraversal, ValidationStage::Format,
                       "joined path escapes the store directory");
  }
  return joined;
}

Status reject_reparse_points(const std::string& path_utf8) {
  std::string current;
  std::size_t position = 0;
  const bool unc = path_utf8.size() >= 2 && path_utf8[0] == '\\' && path_utf8[1] == '\\';
  if (unc) {
    // \\server\share\... - check the share root and then each component.
    const std::size_t server_end = path_utf8.find('\\', 2);
    if (server_end == std::string::npos) return Status::success();
    const std::size_t share_end = path_utf8.find('\\', server_end + 1);
    const std::size_t root_end = share_end == std::string::npos ? path_utf8.size() : share_end;
    current = path_utf8.substr(0, root_end);
    position = root_end;
  } else if (path_utf8.size() >= 2 && path_utf8[1] == ':') {
    current = path_utf8.substr(0, 2);
    position = 2;
  } else {
    return make_status(ErrorCode::PathNotAbsolute, ValidationStage::Format,
                       "path must be absolute before reparse points are checked");
  }

  while (position < path_utf8.size()) {
    if (path_utf8[position] == '\\' || path_utf8[position] == '/') {
      ++position;
      continue;
    }
    const std::size_t next = path_utf8.find_first_of("\\/", position);
    const std::size_t end = next == std::string::npos ? path_utf8.size() : next;
    current += "\\";
    current += path_utf8.substr(position, end - position);
    GENCTL_TRY_ASSIGN(reparse, platform::is_reparse_point(current));
    if (reparse) {
      return make_status(ErrorCode::PathReparsePoint, ValidationStage::Format,
                         "path component '" + current +
                             "' is a reparse point (symlink, junction or mount point); the store "
                             "refuses to publish authoritative state through an indirection it "
                             "cannot audit");
    }
    position = end;
  }
  return Status::success();
}

}  // namespace genctl
