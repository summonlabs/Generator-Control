// Generator Control - version and build identification.
//
// Part of Generator Control, a vendor-neutral standby generator control and
// lifecycle runtime. Apache License 2.0, Copyright 2026 Summon Software Labs.
#pragma once

#include <cstdint>
#include <string>

namespace genctl {

inline constexpr std::uint32_t kVersionMajor = 1;
inline constexpr std::uint32_t kVersionMinor = 0;
inline constexpr std::uint32_t kVersionPatch = 0;

// Version of the on-disk store format. Bumped only for incompatible layout or
// encoding changes; the store refuses to open a format it does not understand.
inline constexpr std::uint32_t kStoreFormatVersion = 1;

// Human readable product version, e.g. "1.0.0".
[[nodiscard]] std::string version_string();

// Version plus store format, e.g. "Generator Control 1.0.0 (store format 1)".
[[nodiscard]] std::string version_banner();

}  // namespace genctl
