// Generator Control - deterministic canonical binary encoding.
//
// Encoding rules (format version 1), normative:
//   * unsigned integers: little-endian, fixed width, no padding, no alignment
//   * signed integers: two's complement, little-endian, fixed width
//   * bool: one byte, 0x00 or 0x01; any other byte is rejected on read
//   * enum: fixed width declared by the field (u8 unless stated otherwise)
//   * string: u32 byte length followed by raw bytes; length counts bytes, not
//     characters; the reader rejects anything above the configured bound
//   * blob: u32 byte length followed by raw bytes, bounded by the reader
//   * collection: u32 element count followed by the elements; producers must
//     write elements in a deterministic order (sorted key order or sequence order)
//
// There are no variable-length integers, no compression or deduplication tables and
// no optional trailing sections: a canonical encoding is a pure function of the
// logical value. Readers are strict: any leftover byte after the expected fields is
// a MalformedEncoding failure rather than being ignored.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "genctl/digest.hpp"
#include "genctl/result.hpp"

namespace genctl {

struct CanonicalLimits {
  std::size_t max_total_bytes{4u * 1024u * 1024u};
  std::size_t max_string_bytes{512};
  std::size_t max_blob_bytes{1u * 1024u * 1024u};
  std::size_t max_elements{4096};
};

class CanonicalWriter {
 public:
  explicit CanonicalWriter(CanonicalLimits limits = {}) : limits_(limits) {}

  [[nodiscard]] Status put_bool(bool value);
  [[nodiscard]] Status put_u8(std::uint8_t value);
  [[nodiscard]] Status put_u16(std::uint16_t value);
  [[nodiscard]] Status put_u32(std::uint32_t value);
  [[nodiscard]] Status put_u64(std::uint64_t value);
  [[nodiscard]] Status put_i64(std::int64_t value);
  [[nodiscard]] Status put_string(std::string_view value);
  [[nodiscard]] Status put_blob(const std::uint8_t* data, std::size_t size);
  [[nodiscard]] Status put_blob(const ByteBuffer& data);
  [[nodiscard]] Status put_digest(const Digest256& digest);
  [[nodiscard]] Status put_count(std::uint32_t count);

  // Repeats a single byte. Used for fixed-length reserved regions.
  [[nodiscard]] Status put_zeros(std::size_t count);

  [[nodiscard]] const ByteBuffer& bytes() const noexcept { return bytes_; }
  [[nodiscard]] ByteBuffer take() noexcept { return std::move(bytes_); }
  [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
  [[nodiscard]] const CanonicalLimits& limits() const noexcept { return limits_; }

 private:
  [[nodiscard]] Status reserve(std::size_t additional);

  CanonicalLimits limits_{};
  ByteBuffer bytes_{};
};

class CanonicalReader {
 public:
  CanonicalReader(const std::uint8_t* data, std::size_t size, CanonicalLimits limits = {});

  [[nodiscard]] Result<bool> get_bool();
  [[nodiscard]] Result<std::uint8_t> get_u8();
  [[nodiscard]] Result<std::uint16_t> get_u16();
  [[nodiscard]] Result<std::uint32_t> get_u32();
  [[nodiscard]] Result<std::uint64_t> get_u64();
  [[nodiscard]] Result<std::int64_t> get_i64();
  [[nodiscard]] Result<std::string> get_string();
  [[nodiscard]] Result<ByteBuffer> get_blob();
  [[nodiscard]] Result<Digest256> get_digest();
  // Reads an element count and refuses anything above max_elements before the
  // caller allocates or loops.
  [[nodiscard]] Result<std::uint32_t> get_count();
  [[nodiscard]] Status skip(std::size_t count);

  [[nodiscard]] std::size_t remaining() const noexcept { return size_ - offset_; }
  [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
  [[nodiscard]] bool at_end() const noexcept { return offset_ == size_; }
  [[nodiscard]] const CanonicalLimits& limits() const noexcept { return limits_; }
  // Fails unless every byte of the input was consumed.
  [[nodiscard]] Status expect_end() const;

 private:
  [[nodiscard]] Status need(std::size_t count) const;

  const std::uint8_t* data_{nullptr};
  std::size_t size_{0};
  std::size_t offset_{0};
  CanonicalLimits limits_{};
};

// Convenience: digest of the canonical encoding of a value.
[[nodiscard]] Digest256 canonical_digest(const ByteBuffer& canonical_bytes) noexcept;

// ---------------------------------------------------------------------------
// Value codec dispatch
//
// Each value type that can appear in canonical content specialises ValueCodec.
// The primary template is deliberately undefined so that a missing specialisation
// is a compile error rather than a silently truncated encoding.
// ---------------------------------------------------------------------------
template <typename T>
struct ValueCodec;

template <typename T>
[[nodiscard]] Status encode_value(CanonicalWriter& writer, const T& value) {
  return ValueCodec<T>::encode(writer, value);
}

template <typename T>
[[nodiscard]] Result<T> decode_value(CanonicalReader& reader) {
  return ValueCodec<T>::decode(reader);
}

template <>
struct ValueCodec<bool> {
  static Status encode(CanonicalWriter& writer, bool value) { return writer.put_bool(value); }
  static Result<bool> decode(CanonicalReader& reader) { return reader.get_bool(); }
};

template <>
struct ValueCodec<std::int64_t> {
  static Status encode(CanonicalWriter& writer, std::int64_t value) { return writer.put_i64(value); }
  static Result<std::int64_t> decode(CanonicalReader& reader) { return reader.get_i64(); }
};

template <>
struct ValueCodec<std::string> {
  static Status encode(CanonicalWriter& writer, const std::string& value) {
    return writer.put_string(value);
  }
  static Result<std::string> decode(CanonicalReader& reader) { return reader.get_string(); }
};

}  // namespace genctl
