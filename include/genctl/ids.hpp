// Generator Control - strongly typed identities, generations, epochs and revisions.
//
// Semantically distinct counters are distinct C++ types. This is deliberate: a
// control epoch must never be assignable to a state revision, an incarnation must
// never be assignable to an attempt id, and a hardware generation must never be
// interchangeable with a store commit sequence. Conversions are explicit.
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "genctl/result.hpp"

namespace genctl {

// ---------------------------------------------------------------------------
// Strongly typed integral identity
// ---------------------------------------------------------------------------
template <typename Tag, typename Rep>
class StrongId {
 public:
  using rep_type = Rep;
  using tag_type = Tag;

  constexpr StrongId() = default;
  constexpr explicit StrongId(Rep value) : value_(value) {}

  [[nodiscard]] constexpr Rep value() const noexcept { return value_; }
  [[nodiscard]] constexpr bool is_set() const noexcept { return value_ != Rep{0}; }
  [[nodiscard]] constexpr bool is_unset() const noexcept { return value_ == Rep{0}; }

  // Monotonic successor. Saturating at the maximum representable value: an
  // exhausted counter is refused explicitly by the caller rather than wrapping.
  [[nodiscard]] constexpr StrongId next() const noexcept {
    if (value_ == (std::numeric_limits<Rep>::max)()) return *this;
    return StrongId(static_cast<Rep>(value_ + Rep{1}));
  }
  [[nodiscard]] constexpr bool at_max() const noexcept {
    return value_ == (std::numeric_limits<Rep>::max)();
  }

  friend constexpr bool operator==(StrongId a, StrongId b) noexcept { return a.value_ == b.value_; }
  friend constexpr std::strong_ordering operator<=>(StrongId a, StrongId b) noexcept {
    return a.value_ <=> b.value_;
  }

 private:
  Rep value_{0};
};

// ---------------------------------------------------------------------------
// Distinct counter types
// ---------------------------------------------------------------------------
// Vendor/hardware revision of the generating set and its controller.
using HardwareGeneration = StrongId<struct HardwareGenerationTag, std::uint32_t>;
// Epoch under which this controller is bound to a specific generator instance.
using BindingEpoch = StrongId<struct BindingEpochTag, std::uint64_t>;
// Authority epoch granted by the external power control plane.
using ControlEpoch = StrongId<struct ControlEpochTag, std::uint64_t>;
// Runtime incarnation: one successful writer acquisition of a store.
using IncarnationId = StrongId<struct IncarnationIdTag, std::uint64_t>;
// Mutable per-generator state revision.
using StateRevision = StrongId<struct StateRevisionTag, std::uint64_t>;
// Monotonic attempt identifier (per store).
using AttemptId = StrongId<struct AttemptIdTag, std::uint64_t>;
// Store commit sequence: one per durably published state image.
using CommitSeq = StrongId<struct CommitSeqTag, std::uint64_t>;
// Monotonic observation sequence assigned by the runtime to every accepted
// observation. Independent of wall-clock time.
using ObservationSeq = StrongId<struct ObservationSeqTag, std::uint64_t>;
// Version of an evidence record; bumped whenever the evidence is replaced.
using EvidenceVersion = StrongId<struct EvidenceVersionTag, std::uint64_t>;
// Monotonic history/journal sequence within a generator.
using JournalSeq = StrongId<struct JournalSeqTag, std::uint64_t>;
// Identifier of a single command handed to an adapter. Never reused.
using CommandId = StrongId<struct CommandIdTag, std::uint64_t>;
// Version of a readiness binding set.
using BindingVersion = StrongId<struct BindingVersionTag, std::uint64_t>;

// Non-cryptographic request fingerprint used to detect idempotency key reuse
// with a different payload.
using RequestFingerprint = StrongId<struct RequestFingerprintTag, std::uint64_t>;

template <typename Tag, typename Rep>
[[nodiscard]] std::string id_to_string(StrongId<Tag, Rep> id) {
  return std::to_string(id.value());
}

// ---------------------------------------------------------------------------
// GeneratorId: stable, validated, filesystem-neutral identity
// ---------------------------------------------------------------------------
class GeneratorId {
 public:
  static constexpr std::size_t kMaxLength = 64;

  GeneratorId() = default;

  // Accepted form: 1..64 characters, first and last alphanumeric, interior
  // characters from [A-Za-z0-9._:-]. ".." is rejected so that an identity can
  // never masquerade as a path component.
  [[nodiscard]] static Result<GeneratorId> parse(std::string_view text);

  [[nodiscard]] const std::string& str() const noexcept { return value_; }
  [[nodiscard]] bool empty() const noexcept { return value_.empty(); }

  friend bool operator==(const GeneratorId& a, const GeneratorId& b) noexcept {
    return a.value_ == b.value_;
  }
  friend std::strong_ordering operator<=>(const GeneratorId& a, const GeneratorId& b) noexcept {
    return a.value_ <=> b.value_;
  }

 private:
  std::string value_{};
};

// ---------------------------------------------------------------------------
// IdempotencyKey: caller supplied correlation for at-most-once actuation
// ---------------------------------------------------------------------------
class IdempotencyKey {
 public:
  static constexpr std::size_t kMaxLength = 64;
  static constexpr std::size_t kMinLength = 8;

  IdempotencyKey() = default;

  // Accepted form: 8..64 printable, non-space ASCII characters (0x21..0x7E).
  [[nodiscard]] static Result<IdempotencyKey> parse(std::string_view text);

  [[nodiscard]] const std::string& str() const noexcept { return value_; }
  [[nodiscard]] bool empty() const noexcept { return value_.empty(); }

  friend bool operator==(const IdempotencyKey& a, const IdempotencyKey& b) noexcept {
    return a.value_ == b.value_;
  }
  friend std::strong_ordering operator<=>(const IdempotencyKey& a,
                                          const IdempotencyKey& b) noexcept {
    return a.value_ <=> b.value_;
  }

 private:
  std::string value_{};
};

// ---------------------------------------------------------------------------
// Composite generations
// ---------------------------------------------------------------------------
// Identifies the physical generating set and the binding under which the runtime
// is allowed to control it. State that belongs to one generator generation must
// never be applied to another.
struct GeneratorGeneration {
  HardwareGeneration hardware{};
  BindingEpoch binding{};

  friend bool operator==(const GeneratorGeneration&, const GeneratorGeneration&) noexcept = default;
  friend std::strong_ordering operator<=>(const GeneratorGeneration&,
                                          const GeneratorGeneration&) noexcept = default;
  [[nodiscard]] bool is_set() const noexcept { return hardware.is_set() && binding.is_set(); }
};

// Identifies the controller authority instance: which external authority epoch is
// held, and which runtime incarnation holds it.
struct ControllerGeneration {
  ControlEpoch epoch{};
  IncarnationId incarnation{};

  friend bool operator==(const ControllerGeneration&, const ControllerGeneration&) noexcept = default;
  friend std::strong_ordering operator<=>(const ControllerGeneration&,
                                          const ControllerGeneration&) noexcept = default;
  [[nodiscard]] bool is_set() const noexcept { return epoch.is_set() && incarnation.is_set(); }
};

[[nodiscard]] std::string to_string(const GeneratorGeneration& generation);
[[nodiscard]] std::string to_string(const ControllerGeneration& generation);
[[nodiscard]] std::string to_string(const GeneratorId& id);

}  // namespace genctl
