// Generator Control - canonical encoding implementation.
#include "genctl/canonical.hpp"

#include <cstring>

namespace genctl {

Status CanonicalWriter::reserve(std::size_t additional) {
  if (additional > limits_.max_total_bytes || bytes_.size() > limits_.max_total_bytes - additional) {
    return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Format,
                       "canonical encoding exceeds the configured maximum of " +
                           std::to_string(limits_.max_total_bytes) + " bytes");
  }
  bytes_.reserve(bytes_.size() + additional);
  return Status::success();
}

Status CanonicalWriter::put_bool(bool value) { return put_u8(value ? 1u : 0u); }

Status CanonicalWriter::put_u8(std::uint8_t value) {
  GENCTL_TRY(reserve(1));
  bytes_.push_back(value);
  return Status::success();
}

Status CanonicalWriter::put_u16(std::uint16_t value) {
  GENCTL_TRY(reserve(2));
  bytes_.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  bytes_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
  return Status::success();
}

Status CanonicalWriter::put_u32(std::uint32_t value) {
  GENCTL_TRY(reserve(4));
  for (int shift = 0; shift < 32; shift += 8) {
    bytes_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
  }
  return Status::success();
}

Status CanonicalWriter::put_u64(std::uint64_t value) {
  GENCTL_TRY(reserve(8));
  for (int shift = 0; shift < 64; shift += 8) {
    bytes_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
  }
  return Status::success();
}

Status CanonicalWriter::put_i64(std::int64_t value) {
  return put_u64(static_cast<std::uint64_t>(value));
}

Status CanonicalWriter::put_string(std::string_view value) {
  if (value.size() > limits_.max_string_bytes) {
    return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Format,
                       "string of " + std::to_string(value.size()) +
                           " bytes exceeds the configured maximum of " +
                           std::to_string(limits_.max_string_bytes));
  }
  GENCTL_TRY(put_u32(static_cast<std::uint32_t>(value.size())));
  GENCTL_TRY(reserve(value.size()));
  bytes_.insert(bytes_.end(), value.begin(), value.end());
  return Status::success();
}

Status CanonicalWriter::put_blob(const std::uint8_t* data, std::size_t size) {
  if (size > limits_.max_blob_bytes) {
    return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Format,
                       "blob of " + std::to_string(size) + " bytes exceeds the configured maximum");
  }
  GENCTL_TRY(put_u32(static_cast<std::uint32_t>(size)));
  GENCTL_TRY(reserve(size));
  if (size > 0) {
    bytes_.insert(bytes_.end(), data, data + size);
  }
  return Status::success();
}

Status CanonicalWriter::put_blob(const ByteBuffer& data) {
  return put_blob(data.data(), data.size());
}

Status CanonicalWriter::put_digest(const Digest256& digest) {
  return put_blob(digest.bytes.data(), digest.bytes.size());
}

Status CanonicalWriter::put_count(std::uint32_t count) {
  if (count > limits_.max_elements) {
    return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Format,
                       "collection of " + std::to_string(count) +
                           " elements exceeds the configured maximum of " +
                           std::to_string(limits_.max_elements));
  }
  return put_u32(count);
}

Status CanonicalWriter::put_zeros(std::size_t count) {
  GENCTL_TRY(reserve(count));
  bytes_.insert(bytes_.end(), count, static_cast<std::uint8_t>(0));
  return Status::success();
}

CanonicalReader::CanonicalReader(const std::uint8_t* data, std::size_t size, CanonicalLimits limits)
    : data_(data), size_(size), limits_(limits) {
  if (size_ > limits_.max_total_bytes) {
    // The caller is expected to have rejected an oversized buffer already; clamp so
    // that no read can walk past the configured bound.
    size_ = limits_.max_total_bytes;
  }
}

Status CanonicalReader::need(std::size_t count) const {
  if (count > remaining()) {
    return make_status(ErrorCode::StoreTruncated, ValidationStage::Persistence,
                       "canonical input ended after " + std::to_string(offset_) + " of " +
                           std::to_string(size_) + " bytes");
  }
  return Status::success();
}

Result<bool> CanonicalReader::get_bool() {
  GENCTL_TRY_ASSIGN(raw, get_u8());
  if (raw > 1u) {
    return make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence,
                       "boolean byte must be 0 or 1, found " + std::to_string(raw));
  }
  return raw == 1u;
}

Result<std::uint8_t> CanonicalReader::get_u8() {
  GENCTL_TRY(need(1));
  return data_[offset_++];
}

Result<std::uint16_t> CanonicalReader::get_u16() {
  GENCTL_TRY(need(2));
  const std::uint16_t value =
      static_cast<std::uint16_t>(static_cast<std::uint16_t>(data_[offset_]) |
                                 static_cast<std::uint16_t>(
                                     static_cast<std::uint16_t>(data_[offset_ + 1]) << 8));
  offset_ += 2;
  return value;
}

Result<std::uint32_t> CanonicalReader::get_u32() {
  GENCTL_TRY(need(4));
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(data_[offset_ + static_cast<std::size_t>(i)]) << (8 * i);
  }
  offset_ += 4;
  return value;
}

Result<std::uint64_t> CanonicalReader::get_u64() {
  GENCTL_TRY(need(8));
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(data_[offset_ + static_cast<std::size_t>(i)]) << (8 * i);
  }
  offset_ += 8;
  return value;
}

Result<std::int64_t> CanonicalReader::get_i64() {
  GENCTL_TRY_ASSIGN(raw, get_u64());
  return static_cast<std::int64_t>(raw);
}

Result<std::string> CanonicalReader::get_string() {
  GENCTL_TRY_ASSIGN(length, get_u32());
  if (length > limits_.max_string_bytes) {
    return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Persistence,
                       "string length " + std::to_string(length) +
                           " exceeds the configured maximum of " +
                           std::to_string(limits_.max_string_bytes));
  }
  GENCTL_TRY(need(length));
  std::string out(reinterpret_cast<const char*>(data_ + offset_), length);
  offset_ += length;
  return out;
}

Result<ByteBuffer> CanonicalReader::get_blob() {
  GENCTL_TRY_ASSIGN(length, get_u32());
  if (length > limits_.max_blob_bytes) {
    return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Persistence,
                       "blob length " + std::to_string(length) +
                           " exceeds the configured maximum of " +
                           std::to_string(limits_.max_blob_bytes));
  }
  GENCTL_TRY(need(length));
  ByteBuffer out(data_ + offset_, data_ + offset_ + length);
  offset_ += length;
  return out;
}

Result<Digest256> CanonicalReader::get_digest() {
  GENCTL_TRY_ASSIGN(blob, get_blob());
  if (blob.size() != Sha256::kDigestBytes) {
    return make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence,
                       "digest field must be 32 bytes, found " + std::to_string(blob.size()));
  }
  Digest256 out{};
  std::memcpy(out.bytes.data(), blob.data(), blob.size());
  return out;
}

Result<std::uint32_t> CanonicalReader::get_count() {
  GENCTL_TRY_ASSIGN(count, get_u32());
  if (count > limits_.max_elements) {
    return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Persistence,
                       "collection count " + std::to_string(count) +
                           " exceeds the configured maximum of " +
                           std::to_string(limits_.max_elements));
  }
  return count;
}

Status CanonicalReader::skip(std::size_t count) {
  GENCTL_TRY(need(count));
  offset_ += count;
  return Status::success();
}

Status CanonicalReader::expect_end() const {
  if (!at_end()) {
    return make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence,
                       "canonical input has " + std::to_string(remaining()) +
                           " unexpected trailing bytes");
  }
  return Status::success();
}

Digest256 canonical_digest(const ByteBuffer& canonical_bytes) noexcept {
  return sha256(canonical_bytes);
}

}  // namespace genctl
