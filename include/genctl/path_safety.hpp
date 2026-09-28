// Generator Control - path handling under an explicit trust model.
//
// Trust model: the store directory and every ancestor of it are chosen by the
// operator and are trusted to be ordinary directories. Everything else is
// untrusted input:
//   * traversal components (".."), device names (CON, NUL, COM1..) and Windows
//     trailing-dot/space forms are rejected *before* any normalisation runs, so
//     normalisation can never erase the evidence of an attack;
//   * malformed or overlong input is rejected before it is converted to a wide
//     string;
//   * a reparse point (symlink, junction, mount point) anywhere in the resolved
//     store path is refused: writing authoritative state through an indirection
//     the runtime cannot audit would break the publication protocol;
//   * file names created by the runtime are fixed constants or derived from
//     monotonic integers, never from externally supplied text.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "genctl/result.hpp"

namespace genctl {

// Validates a directory path supplied by an operator (CLI or API).
[[nodiscard]] Result<std::string> validate_store_directory(std::string_view raw_utf8);

// Validates a file path (used for adapter journals and test artefacts).
[[nodiscard]] Result<std::string> validate_file_path(std::string_view raw_utf8);

// True when the raw text contains a component that must be rejected before
// normalisation. Exposed for tests.
[[nodiscard]] bool has_unsafe_component(std::string_view raw_utf8) noexcept;
// True when the text is a Windows reserved device name (including "NUL.txt").
[[nodiscard]] bool is_device_name(std::string_view component) noexcept;
// Strict UTF-8 validation: rejects overlong forms, surrogates and out-of-range
// code points.
[[nodiscard]] bool is_valid_utf8(std::string_view text) noexcept;

// Joins a validated base directory with a leaf name produced by the runtime and
// proves the result stays inside the base.
[[nodiscard]] Result<std::string> join_inside(const std::string& base_utf8, std::string_view leaf);

// Fails when any existing component of the path is a reparse point.
[[nodiscard]] Status reject_reparse_points(const std::string& path_utf8);

}  // namespace genctl
