// Generator Control - deterministic digests and integrity checks.
//
// Two primitives, both implemented locally with no third-party dependency:
//   * CRC-32C (Castagnoli) for fast framing/integrity checks of stored records;
//   * SHA-256 for state digests, audit bindings and the publication chain.
// Both are pure functions of the input bytes, so equivalent canonical bytes always
// produce equivalent digests.
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "genctl/result.hpp"

namespace genctl {

using ByteBuffer = std::vector<std::uint8_t>;

// ---------------------------------------------------------------------------
// CRC-32C (Castagnoli, reflected polynomial 0x1EDC6F41)
// ---------------------------------------------------------------------------
[[nodiscard]] std::uint32_t crc32c(const std::uint8_t* data, std::size_t size) noexcept;
[[nodiscard]] std::uint32_t crc32c(const ByteBuffer& data) noexcept;
[[nodiscard]] std::uint32_t crc32c(std::string_view data) noexcept;

// ---------------------------------------------------------------------------
// FNV-1a 64: request fingerprinting and small map keys.
// ---------------------------------------------------------------------------
[[nodiscard]] std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size) noexcept;
[[nodiscard]] std::uint64_t fnv1a64(const ByteBuffer& data) noexcept;
[[nodiscard]] std::uint64_t fnv1a64(std::string_view data) noexcept;

// ---------------------------------------------------------------------------
// SHA-256
// ---------------------------------------------------------------------------
class Sha256 {
 public:
  static constexpr std::size_t kDigestBytes = 32;

  Sha256();

  void update(const std::uint8_t* data, std::size_t size) noexcept;
  void update(std::string_view data) noexcept;
  void update(const ByteBuffer& data) noexcept;
  // Finalises the digest. Further update() calls are refused by contract; the
  // object must not be reused after finalisation.
  [[nodiscard]] std::array<std::uint8_t, kDigestBytes> finish() noexcept;

 private:
  void compress(const std::uint8_t* block) noexcept;

  std::array<std::uint32_t, 8> state_{};
  std::array<std::uint8_t, 64> buffer_{};
  std::uint64_t total_bytes_{0};
  std::size_t buffer_used_{0};
  bool finalised_{false};
};

struct Digest256 {
  std::array<std::uint8_t, Sha256::kDigestBytes> bytes{};

  friend bool operator==(const Digest256&, const Digest256&) noexcept = default;
  friend std::strong_ordering operator<=>(const Digest256&, const Digest256&) noexcept = default;

  [[nodiscard]] bool is_zero() const noexcept;
  [[nodiscard]] std::string hex() const;
  // Short form used in human reports: first 16 hex characters.
  [[nodiscard]] std::string short_hex() const;
  [[nodiscard]] static Digest256 zero() noexcept { return Digest256{}; }
  // Parses 64 lowercase/uppercase hex characters.
  [[nodiscard]] static Result<Digest256> parse_hex(std::string_view text);
};

[[nodiscard]] Digest256 sha256(const std::uint8_t* data, std::size_t size) noexcept;
[[nodiscard]] Digest256 sha256(std::string_view data) noexcept;
[[nodiscard]] Digest256 sha256(const ByteBuffer& data) noexcept;

// Chains a previous digest with new content: SHA256(domain || prev || content).
// Used for the storage publication chain and the audit trail.
[[nodiscard]] Digest256 digest_chain(std::string_view domain, const Digest256& previous,
                                     const ByteBuffer& content) noexcept;

// Verifies the local digest implementations against published test vectors.
// Returns Ok, or Internal with an explanation of the first mismatch found.
[[nodiscard]] Status digest_self_test();

}  // namespace genctl
