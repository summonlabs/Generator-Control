// Generator Control - CRC-32C, FNV-1a and SHA-256.
//
// All three primitives are implemented here so that the runtime has no third-party
// dependency for integrity or identity. SHA-256 follows FIPS 180-4 exactly and is
// verified against published vectors by digest_self_test().
#include "genctl/digest.hpp"

#include <array>
#include <cstring>

namespace genctl {
namespace {

constexpr std::uint32_t kCrc32cPolynomial = 0x82F63B78u;

constexpr std::array<std::uint32_t, 256> make_crc32c_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t i = 0; i < 256; ++i) {
    std::uint32_t crc = i;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1u) != 0u ? (crc >> 1) ^ kCrc32cPolynomial : (crc >> 1);
    }
    table[i] = crc;
  }
  return table;
}

constexpr std::array<std::uint32_t, 256> kCrc32cTable = make_crc32c_table();

constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

constexpr std::array<std::uint32_t, 8> kSha256H = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                                                   0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                                                   0x1f83d9abu, 0x5be0cd19u};

constexpr std::uint32_t rotr(std::uint32_t value, unsigned bits) noexcept {
  return (value >> bits) | (value << (32u - bits));
}

}  // namespace

std::uint32_t crc32c(const std::uint8_t* data, std::size_t size) noexcept {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < size; ++i) {
    crc = kCrc32cTable[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

std::uint32_t crc32c(const ByteBuffer& data) noexcept {
  return crc32c(data.data(), data.size());
}

std::uint32_t crc32c(std::string_view data) noexcept {
  return crc32c(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
}

std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size) noexcept {
  std::uint64_t hash = 0xcbf29ce484222325ull;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= static_cast<std::uint64_t>(data[i]);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::uint64_t fnv1a64(std::string_view data) noexcept {
  return fnv1a64(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
}

std::uint64_t fnv1a64(const ByteBuffer& data) noexcept {
  return fnv1a64(data.data(), data.size());
}

Sha256::Sha256() : state_(kSha256H) {}

void Sha256::compress(const std::uint8_t* block) noexcept {
  std::uint32_t w[64];
  for (std::size_t i = 0; i < 16; ++i) {
    w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
           (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
           (static_cast<std::uint32_t>(block[i * 4 + 3]));
  }
  for (std::size_t i = 16; i < 64; ++i) {
    const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  std::uint32_t a = state_[0];
  std::uint32_t b = state_[1];
  std::uint32_t c = state_[2];
  std::uint32_t d = state_[3];
  std::uint32_t e = state_[4];
  std::uint32_t f = state_[5];
  std::uint32_t g = state_[6];
  std::uint32_t h = state_[7];
  for (std::size_t i = 0; i < 64; ++i) {
    const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const std::uint32_t ch = (e & f) ^ ((~e) & g);
    const std::uint32_t temp1 = h + s1 + ch + kSha256K[i] + w[i];
    const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t temp2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

void Sha256::update(const std::uint8_t* data, std::size_t size) noexcept {
  if (finalised_ || size == 0) return;
  total_bytes_ += static_cast<std::uint64_t>(size);
  std::size_t offset = 0;
  if (buffer_used_ > 0) {
    const std::size_t fill = 64 - buffer_used_;
    const std::size_t take = size < fill ? size : fill;
    std::memcpy(buffer_.data() + buffer_used_, data, take);
    buffer_used_ += take;
    offset += take;
    if (buffer_used_ == 64) {
      compress(buffer_.data());
      buffer_used_ = 0;
    }
  }
  while (offset + 64 <= size) {
    compress(data + offset);
    offset += 64;
  }
  if (offset < size) {
    std::memcpy(buffer_.data(), data + offset, size - offset);
    buffer_used_ = size - offset;
  }
}

void Sha256::update(std::string_view data) noexcept {
  update(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
}

void Sha256::update(const ByteBuffer& data) noexcept {
  update(data.data(), data.size());
}

std::array<std::uint8_t, Sha256::kDigestBytes> Sha256::finish() noexcept {
  std::array<std::uint8_t, Sha256::kDigestBytes> digest{};
  if (!finalised_) {
    const std::uint64_t bit_length = total_bytes_ * 8ull;
    std::uint8_t pad = 0x80u;
    update(&pad, 1);
    // update() advanced total_bytes_; the length word uses the pre-padding length.
    const std::uint8_t zero = 0x00u;
    std::size_t padding = 0;
    // Pad with zeroes until 8 bytes remain in the current block.
    padding = (buffer_used_ == 56) ? 0 : ((buffer_used_ < 56) ? (56 - buffer_used_) : (120 - buffer_used_));
    for (std::size_t i = 0; i < padding; ++i) update(&zero, 1);
    std::uint8_t length_bytes[8];
    for (int i = 7; i >= 0; --i) {
      length_bytes[7 - i] = static_cast<std::uint8_t>((bit_length >> (i * 8)) & 0xFFu);
    }
    update(length_bytes, 8);
    finalised_ = true;
    for (std::size_t i = 0; i < 8; ++i) {
      digest[i * 4] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xFFu);
      digest[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xFFu);
      digest[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xFFu);
      digest[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xFFu);
    }
  }
  return digest;
}

bool Digest256::is_zero() const noexcept {
  for (const auto byte : bytes) {
    if (byte != 0) return false;
  }
  return true;
}

std::string Digest256::hex() const {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (const auto byte : bytes) {
    out.push_back(kHex[(byte >> 4) & 0x0Fu]);
    out.push_back(kHex[byte & 0x0Fu]);
  }
  return out;
}

std::string Digest256::short_hex() const { return hex().substr(0, 16); }

Result<Digest256> Digest256::parse_hex(std::string_view text) {
  if (text.size() != Sha256::kDigestBytes * 2) {
    return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                       "digest must be 64 hexadecimal characters");
  }
  Digest256 out{};
  for (std::size_t i = 0; i < Sha256::kDigestBytes; ++i) {
    unsigned value = 0;
    for (int nibble = 0; nibble < 2; ++nibble) {
      const char c = text[i * 2 + static_cast<std::size_t>(nibble)];
      unsigned digit = 0;
      if (c >= '0' && c <= '9') {
        digit = static_cast<unsigned>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        digit = static_cast<unsigned>(c - 'a') + 10u;
      } else if (c >= 'A' && c <= 'F') {
        digit = static_cast<unsigned>(c - 'A') + 10u;
      } else {
        return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                           "digest contains a non-hexadecimal character");
      }
      value = (value << 4) | digit;
    }
    out.bytes[i] = static_cast<std::uint8_t>(value);
  }
  return out;
}

Digest256 sha256(const std::uint8_t* data, std::size_t size) noexcept {
  Sha256 hasher;
  hasher.update(data, size);
  Digest256 out{};
  out.bytes = hasher.finish();
  return out;
}

Digest256 sha256(std::string_view data) noexcept {
  return sha256(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
}

Digest256 sha256(const ByteBuffer& data) noexcept { return sha256(data.data(), data.size()); }

Digest256 digest_chain(std::string_view domain, const Digest256& previous,
                       const ByteBuffer& content) noexcept {
  Sha256 hasher;
  hasher.update(domain);
  hasher.update(previous.bytes.data(), previous.bytes.size());
  hasher.update(content);
  Digest256 out{};
  out.bytes = hasher.finish();
  return out;
}

Status digest_self_test() {
  struct Vector {
    const char* input;
    const char* expected;
  };
  static const Vector kSha256Vectors[] = {
      {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
      {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
      {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
       "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
      {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
       "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
  };
  for (const auto& vector : kSha256Vectors) {
    const Digest256 actual = sha256(std::string_view{vector.input});
    if (actual.hex() != vector.expected) {
      return make_status(ErrorCode::Internal, ValidationStage::Internal,
                         std::string("SHA-256 self test mismatch for input of length ") +
                             std::to_string(std::strlen(vector.input)) + ": got " + actual.hex() +
                             " expected " + vector.expected);
    }
  }
  struct CrcVector {
    const char* input;
    std::uint32_t expected;
  };
  static const CrcVector kCrcVectors[] = {
      {"", 0x00000000u},
      {"a", 0xC1D04330u},
      {"abc", 0x364B3FB7u},
      {"123456789", 0xE3069283u},
  };
  for (const auto& vector : kCrcVectors) {
    const std::uint32_t actual = crc32c(std::string_view{vector.input});
    if (actual != vector.expected) {
      return make_status(ErrorCode::Internal, ValidationStage::Internal,
                         std::string("CRC-32C self test mismatch for input \"") + vector.input +
                             "\": got " + std::to_string(actual) + " expected " +
                             std::to_string(vector.expected));
    }
  }
  // FNV-1a of the empty input is the offset basis by definition; the remaining
  // checks confirm the function is deterministic and input sensitive.
  if (fnv1a64(std::string_view{""}) != 0xcbf29ce484222325ull) {
    return make_status(ErrorCode::Internal, ValidationStage::Internal,
                       "FNV-1a self test failed: empty input is not the offset basis");
  }
  if (fnv1a64(std::string_view{"a"}) == fnv1a64(std::string_view{"b"}) ||
      fnv1a64(std::string_view{"abc"}) == fnv1a64(std::string_view{"abd"}) ||
      fnv1a64(std::string_view{"abc"}) == fnv1a64(std::string_view{"abc"} ) == false) {
    return make_status(ErrorCode::Internal, ValidationStage::Internal,
                       "FNV-1a self test failed: hash is not deterministic or not input sensitive");
  }
  return Status::success();
}

}  // namespace genctl
