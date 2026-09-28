// Generator Control - durable store implementation.
#include "genctl/persistence.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "codecs.hpp"
#include "genctl/path_safety.hpp"

namespace genctl {
namespace {

CanonicalLimits payload_limits(std::size_t max_bytes) {
  CanonicalLimits limits{};
  limits.max_total_bytes = max_bytes;
  limits.max_string_bytes = 4096;
  limits.max_blob_bytes = max_bytes;
  limits.max_elements = 65536;
  return limits;
}

std::string zero_padded(std::uint64_t value) {
  std::string digits = std::to_string(value);
  if (digits.size() < 20) digits.insert(digits.begin(), 20 - digits.size(), '0');
  return digits;
}

bool ends_with(const std::string& text, const char* suffix) {
  const std::size_t length = std::strlen(suffix);
  return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
}

bool parse_generation_name(const std::string& name, CommitSeq* out) {
  const std::string prefix{kGenerationPrefix};
  const std::string suffix{kGenerationSuffix};
  if (name.size() != prefix.size() + 20 + suffix.size()) return false;
  if (name.compare(0, prefix.size(), prefix) != 0) return false;
  if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
  const std::string digits = name.substr(prefix.size(), 20);
  for (const char c : digits) {
    if (c < '0' || c > '9') return false;
  }
  *out = CommitSeq{std::stoull(digits)};
  return true;
}

ByteBuffer encode_head_payload(const StoreHead& head) {
  CanonicalWriter writer(payload_limits(kMaxHeadPayloadBytes));
  (void)writer.put_u32(kStoreFormatVersion);
  (void)writer.put_u64(head.commit_seq.value());
  (void)writer.put_digest(head.image_digest);
  (void)writer.put_u64(head.epoch.value());
  (void)writer.put_u64(head.incarnation.value());
  (void)writer.put_i64(head.published_at);
  return writer.take();
}

Result<StoreHead> decode_head_payload(const ByteBuffer& payload) {
  CanonicalReader reader(payload.data(), payload.size(), payload_limits(kMaxHeadPayloadBytes));
  GENCTL_TRY_ASSIGN(format, reader.get_u32());
  if (format != kStoreFormatVersion) {
    return make_status(ErrorCode::StoreVersionUnsupported, ValidationStage::Persistence,
                       "head marker declares an unsupported format " + std::to_string(format));
  }
  GENCTL_TRY_ASSIGN(seq, reader.get_u64());
  GENCTL_TRY_ASSIGN(digest, reader.get_digest());
  GENCTL_TRY_ASSIGN(epoch, reader.get_u64());
  GENCTL_TRY_ASSIGN(incarnation, reader.get_u64());
  GENCTL_TRY_ASSIGN(published_at, reader.get_i64());
  GENCTL_TRY(reader.expect_end());
  StoreHead head{};
  head.commit_seq = CommitSeq{seq};
  head.image_digest = digest;
  head.epoch = ControlEpoch{epoch};
  head.incarnation = IncarnationId{incarnation};
  head.published_at = published_at;
  return head;
}

ByteBuffer encode_fence_payload(const StoreFence& fence) {
  CanonicalWriter writer(payload_limits(kMaxHeadPayloadBytes));
  (void)writer.put_u64(fence.highest_seq.value());
  (void)writer.put_digest(fence.highest_digest);
  (void)writer.put_u64(fence.highest_epoch.value());
  return writer.take();
}

Result<StoreFence> decode_fence_payload(const ByteBuffer& payload) {
  CanonicalReader reader(payload.data(), payload.size(), payload_limits(kMaxHeadPayloadBytes));
  GENCTL_TRY_ASSIGN(seq, reader.get_u64());
  GENCTL_TRY_ASSIGN(digest, reader.get_digest());
  GENCTL_TRY_ASSIGN(epoch, reader.get_u64());
  GENCTL_TRY(reader.expect_end());
  StoreFence fence{};
  fence.highest_seq = CommitSeq{seq};
  fence.highest_digest = digest;
  fence.highest_epoch = ControlEpoch{epoch};
  return fence;
}

ByteBuffer encode_lease_payload(const StoreLease& lease) {
  CanonicalWriter writer(payload_limits(kMaxHeadPayloadBytes));
  (void)writer.put_u64(lease.epoch.value());
  (void)writer.put_u64(lease.incarnation.value());
  (void)writer.put_u32(lease.process_id);
  (void)writer.put_string(lease.process_token);
  (void)writer.put_i64(lease.acquired_at);
  (void)writer.put_u64(lease.observed_commit_seq.value());
  return writer.take();
}

Result<StoreLease> decode_lease_payload(const ByteBuffer& payload) {
  CanonicalReader reader(payload.data(), payload.size(), payload_limits(kMaxHeadPayloadBytes));
  GENCTL_TRY_ASSIGN(epoch, reader.get_u64());
  GENCTL_TRY_ASSIGN(incarnation, reader.get_u64());
  GENCTL_TRY_ASSIGN(pid, reader.get_u32());
  GENCTL_TRY_ASSIGN(token, reader.get_string());
  GENCTL_TRY_ASSIGN(acquired_at, reader.get_i64());
  GENCTL_TRY_ASSIGN(observed, reader.get_u64());
  GENCTL_TRY(reader.expect_end());
  StoreLease lease{};
  lease.epoch = ControlEpoch{epoch};
  lease.incarnation = IncarnationId{incarnation};
  lease.process_id = pid;
  lease.process_token = std::move(token);
  lease.acquired_at = acquired_at;
  lease.observed_commit_seq = CommitSeq{observed};
  return lease;
}

}  // namespace

std::string_view to_string(CrashPoint point) noexcept {
  switch (point) {
    case CrashPoint::None: return "none";
    case CrashPoint::BeforeReserve: return "before-reserve";
    case CrashPoint::AfterReserveBeforeStaging: return "after-reserve-before-staging";
    case CrashPoint::AfterStagingBeforeReadback: return "after-staging-before-readback";
    case CrashPoint::AfterReadbackBeforePublish: return "after-readback-before-publish";
    case CrashPoint::AfterPublishBeforeHeadCommit: return "after-publish-before-head-commit";
    case CrashPoint::AfterHeadCommitBeforeFence: return "after-head-commit-before-fence";
    case CrashPoint::AfterPublishBeforeActuation: return "after-publish-before-actuation";
    case CrashPoint::AfterActuationBeforeAckCommit: return "after-actuation-before-ack-commit";
    case CrashPoint::AfterAckCommit: return "after-ack-commit";
  }
  return "none";
}

Result<CrashPoint> parse_crash_point(std::string_view text) {
  static const CrashPoint kPoints[] = {
      CrashPoint::None,
      CrashPoint::BeforeReserve,
      CrashPoint::AfterReserveBeforeStaging,
      CrashPoint::AfterStagingBeforeReadback,
      CrashPoint::AfterReadbackBeforePublish,
      CrashPoint::AfterPublishBeforeHeadCommit,
      CrashPoint::AfterHeadCommitBeforeFence,
      CrashPoint::AfterPublishBeforeActuation,
      CrashPoint::AfterActuationBeforeAckCommit,
      CrashPoint::AfterAckCommit,
  };
  for (const CrashPoint point : kPoints) {
    if (to_string(point) == text) return point;
  }
  return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                     std::string("unrecognised crash point '") + std::string(text) + "'");
}

void crash_now(CrashPoint point) noexcept {
  // Non-interactive immediate process death: no destructors, no atexit handlers, no
  // interactive error reporting, no modal window.
  const int code = 70 + static_cast<int>(point);
  platform::terminate_process_now(code);
}

void maybe_crash(CrashPoint configured, CrashPoint point) noexcept {
  if (configured != CrashPoint::None && configured == point) crash_now(point);
}

ByteBuffer encode_record(FileKind kind, CommitSeq commit_seq, const ByteBuffer& payload) {
  ByteBuffer out(kRecordHeaderBytes + payload.size() + kRecordTrailerBytes, 0);
  std::memcpy(out.data(), kStoreMagic, 8);
  auto put_u32 = [&](std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
      out[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
    }
  };
  auto put_u64 = [&](std::size_t offset, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
      out[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
    }
  };
  put_u32(8, kStoreFormatVersion);
  put_u32(12, static_cast<std::uint32_t>(kind));
  put_u64(16, commit_seq.value());
  put_u64(24, static_cast<std::uint64_t>(payload.size()));
  const Digest256 digest = sha256(payload);
  std::memcpy(out.data() + 32, digest.bytes.data(), digest.bytes.size());
  put_u32(64, crc32c(out.data(), 64));
  put_u32(68, 0);
  if (!payload.empty()) {
    std::memcpy(out.data() + kRecordHeaderBytes, payload.data(), payload.size());
  }
  put_u32(kRecordHeaderBytes + payload.size(), crc32c(payload));
  return out;
}

Status decode_record(const ByteBuffer& raw, std::size_t max_payload_bytes, FileKind* kind,
                     CommitSeq* commit_seq, ByteBuffer* payload, Digest256* payload_digest) {
  if (raw.size() < kRecordHeaderBytes + kRecordTrailerBytes) {
    return make_status(ErrorCode::StoreTruncated, ValidationStage::Persistence,
                       "record is shorter than the minimum framed size");
  }
  if (std::memcmp(raw.data(), kStoreMagic, 8) != 0) {
    return make_status(ErrorCode::StoreCorrupt, ValidationStage::Persistence,
                       "record magic does not match");
  }
  auto get_u32 = [&](std::size_t offset) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      value |= static_cast<std::uint32_t>(raw[offset + static_cast<std::size_t>(i)]) << (8 * i);
    }
    return value;
  };
  auto get_u64 = [&](std::size_t offset) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
      value |= static_cast<std::uint64_t>(raw[offset + static_cast<std::size_t>(i)]) << (8 * i);
    }
    return value;
  };

  const std::uint32_t format = get_u32(8);
  if (format != kStoreFormatVersion) {
    return make_status(ErrorCode::StoreVersionUnsupported, ValidationStage::Persistence,
                       "record declares format " + std::to_string(format) + ", this build supports " +
                           std::to_string(kStoreFormatVersion));
  }
  const std::uint32_t raw_kind = get_u32(12);
  if (raw_kind < 1 || raw_kind > 4) {
    return make_status(ErrorCode::StoreCorrupt, ValidationStage::Persistence,
                       "record kind " + std::to_string(raw_kind) + " is not recognised");
  }
  const std::uint64_t length = get_u64(24);
  if (length > max_payload_bytes) {
    return make_status(ErrorCode::StoreOversize, ValidationStage::Persistence,
                       "record declares a payload of " + std::to_string(length) +
                           " bytes, above the configured maximum of " +
                           std::to_string(max_payload_bytes));
  }
  const std::uint64_t expected_size =
      static_cast<std::uint64_t>(kRecordHeaderBytes) + length + kRecordTrailerBytes;
  if (raw.size() != expected_size) {
    return make_status(ErrorCode::StoreTruncated, ValidationStage::Persistence,
                       "record size " + std::to_string(raw.size()) + " does not match the declared " +
                           std::to_string(expected_size) + " bytes");
  }
  const std::uint32_t header_crc = get_u32(64);
  if (header_crc != crc32c(raw.data(), 64)) {
    return make_status(ErrorCode::StoreIntegrityFailure, ValidationStage::Persistence,
                       "record header checksum does not match");
  }
  if (get_u32(68) != 0) {
    return make_status(ErrorCode::StoreCorrupt, ValidationStage::Persistence,
                       "record reserved header field is not zero");
  }
  const std::uint8_t* payload_begin = raw.data() + kRecordHeaderBytes;
  const std::size_t payload_size = static_cast<std::size_t>(length);
  if (crc32c(payload_begin, payload_size) !=
      get_u32(kRecordHeaderBytes + payload_size)) {
    return make_status(ErrorCode::StoreIntegrityFailure, ValidationStage::Persistence,
                       "record payload checksum does not match");
  }
  const Digest256 digest = sha256(payload_begin, payload_size);
  if (std::memcmp(digest.bytes.data(), raw.data() + 32, digest.bytes.size()) != 0) {
    return make_status(ErrorCode::StoreIntegrityFailure, ValidationStage::Persistence,
                       "record payload digest does not match the recorded digest");
  }
  if (kind != nullptr) *kind = static_cast<FileKind>(raw_kind);
  if (commit_seq != nullptr) *commit_seq = CommitSeq{get_u64(16)};
  if (payload != nullptr) *payload = ByteBuffer(payload_begin, payload_begin + payload_size);
  if (payload_digest != nullptr) *payload_digest = digest;
  return Status::success();
}

DurableStore::~DurableStore() { (void)close(); }

Result<std::unique_ptr<DurableStore>> DurableStore::open(const StoreOpenOptions& options,
                                                         const Clock& clock) {
  std::unique_ptr<DurableStore> store(new DurableStore());
  store->options_ = options;
  store->clock_ = &clock;
  GENCTL_TRY_ASSIGN(directory, validate_store_directory(options.directory));
  store->options_.directory = directory;
  GENCTL_TRY(store->open_store());
  return store;
}

std::string DurableStore::generation_file_name(CommitSeq seq) const {
  return std::string(kGenerationPrefix) + zero_padded(seq.value()) + std::string(kGenerationSuffix);
}

Status DurableStore::open_store() {
  GENCTL_TRY_ASSIGN(exists, platform::path_exists(options_.directory));
  if (!exists) {
    if (!options_.create_if_missing || options_.read_only) {
      return make_status(ErrorCode::StoreNotFound, ValidationStage::Persistence,
                         "store directory does not exist: " + options_.directory);
    }
    GENCTL_TRY(platform::ensure_directory(options_.directory));
  }
  GENCTL_TRY_ASSIGN(is_dir, platform::is_directory(options_.directory));
  if (!is_dir) {
    return make_status(ErrorCode::PathNotDirectory, ValidationStage::Persistence,
                       "store path is not a directory");
  }

  if (!options_.read_only) {
    GENCTL_TRY_ASSIGN(lock_path, join_inside(options_.directory, kLockFileName));
    GENCTL_TRY_ASSIGN(lock, platform::ExclusiveFileLock::try_acquire(lock_path));
    lock_ = std::move(lock);
  }

  // Lease.
  GENCTL_TRY_ASSIGN(lease_path, join_inside(options_.directory, kLeaseFileName));
  GENCTL_TRY_ASSIGN(lease_exists, platform::path_exists(lease_path));
  if (lease_exists) {
    GENCTL_TRY_ASSIGN(bytes, platform::read_file_bounded(lease_path, kMaxHeadPayloadBytes + 128));
    FileKind kind = FileKind::Lease;
    CommitSeq seq{};
    ByteBuffer payload;
    GENCTL_TRY(decode_record(bytes, kMaxHeadPayloadBytes, &kind, &seq, &payload, nullptr));
    if (kind != FileKind::Lease) {
      return make_status(ErrorCode::StoreCorrupt, ValidationStage::Persistence,
                         "writer lease file carries the wrong record kind");
    }
    GENCTL_TRY_ASSIGN(decoded, decode_lease_payload(payload));
    lease_ = decoded;
  }

  // Fence.
  GENCTL_TRY_ASSIGN(fence_path, join_inside(options_.directory, kFenceFileName));
  GENCTL_TRY_ASSIGN(fence_exists, platform::path_exists(fence_path));
  if (fence_exists) {
    GENCTL_TRY_ASSIGN(bytes, platform::read_file_bounded(fence_path, kMaxHeadPayloadBytes + 128));
    FileKind kind = FileKind::Fence;
    CommitSeq seq{};
    ByteBuffer payload;
    GENCTL_TRY(decode_record(bytes, kMaxHeadPayloadBytes, &kind, &seq, &payload, nullptr));
    if (kind != FileKind::Fence) {
      return make_status(ErrorCode::StoreCorrupt, ValidationStage::Persistence,
                         "fence file carries the wrong record kind");
    }
    GENCTL_TRY_ASSIGN(decoded, decode_fence_payload(payload));
    fence_ = decoded;
  }

  GENCTL_TRY(adopt_committed_image(options_.recover_scan));

  if (!options_.read_only) {
    GENCTL_TRY(acquire_writer_lease());
    if (fence_.highest_seq < commit_seq_) {
      StoreFence raised{};
      raised.highest_seq = commit_seq_;
      raised.highest_digest = head_.image_digest;
      raised.highest_epoch = epoch_;
      GENCTL_TRY(write_fence(raised));
    }
  } else {
    incarnation_ = lease_.incarnation;
    epoch_ = lease_.epoch;
  }

  GENCTL_TRY(retire_residue());
  return Status::success();
}

Status DurableStore::adopt_committed_image(bool allow_scan) {
  GENCTL_TRY_ASSIGN(head_path, join_inside(options_.directory, kHeadFileName));
  GENCTL_TRY_ASSIGN(head_exists, platform::path_exists(head_path));
  Status head_failure = Status::success();
  if (head_exists) {
    GENCTL_TRY_ASSIGN(bytes, platform::read_file_bounded(head_path, kMaxHeadPayloadBytes + 128));
    FileKind kind = FileKind::Head;
    CommitSeq seq{};
    ByteBuffer payload;
    head_failure = decode_record(bytes, kMaxHeadPayloadBytes, &kind, &seq, &payload, nullptr);
    if (head_failure.ok() && kind != FileKind::Head) {
      head_failure = make_status(ErrorCode::StoreCorrupt, ValidationStage::Persistence,
                                 "head marker carries the wrong record kind");
    }
    if (head_failure.ok()) {
      Result<StoreHead> decoded = decode_head_payload(payload);
      if (!decoded.ok()) {
        head_failure = decoded.status();
      } else {
        head_ = decoded.value();
        head_present_ = true;
      }
    }
  } else {
    head_failure = make_status(ErrorCode::StoreGenerationMissing, ValidationStage::Persistence,
                               "store has no head marker");
  }

  if (head_present_) {
    CommitSeq seq{};
    Digest256 digest{};
    bool valid = false;
    std::string detail;
    Status failure = Status::success();
    GENCTL_TRY_ASSIGN(image,
                      read_image_file(generation_file_name(head_.commit_seq), &seq, &digest, &valid,
                                      &detail, &failure));
    if (!valid) {
      // The precise reason (unsupported version, integrity failure, truncation) is
      // preserved so that an operator sees what actually went wrong.
      if (failure.code() != ErrorCode::Ok) {
        return make_status(failure.code(), ValidationStage::Persistence,
                           "committed generation is not readable: " + failure.message());
      }
      return make_status(ErrorCode::StoreCorrupt, ValidationStage::Persistence,
                         "committed generation is not readable: " + detail);
    }
    if (digest != head_.image_digest) {
      return make_status(ErrorCode::StoreIntegrityFailure, ValidationStage::Persistence,
                         "committed generation digest does not match the head marker");
    }
    image_ = std::move(image);
    commit_seq_ = head_.commit_seq;
    epoch_ = head_.epoch;
    incarnation_ = head_.incarnation;
  } else {
    std::vector<GenerationFileStatus> generations;
    GENCTL_TRY(scan_generations(&generations));
    if (generations.empty()) {
      // No committed generation and no generation file at all: this is a store that
      // has never been founded. Founding it requires an explicit create request.
      if (!options_.create_if_missing) {
        return make_status(
            ErrorCode::StoreNotFound, ValidationStage::Persistence,
            "directory does not contain a Generator Control store: " + head_failure.message() +
                "; pass --create to found a new store");
      }
      commit_seq_ = CommitSeq{0};
      epoch_ = options_.requested_epoch;
      image_ = StateImage{};
      created_new_store_ = true;
      return Status::success();
    }
    if (!allow_scan) {
      return make_status(head_failure.code() == ErrorCode::StoreGenerationMissing
                             ? ErrorCode::StoreCorrupt
                             : head_failure.code(),
                         ValidationStage::Persistence,
                         std::string("cannot adopt a committed state: ") + head_failure.message() +
                             "; an explicit recovery scan is required");
    }
    const GenerationFileStatus* best = nullptr;
    for (const auto& generation : generations) {
      if (!generation.valid) continue;
      if (best == nullptr || generation.commit_seq > best->commit_seq) best = &generation;
    }
    if (best == nullptr) {
      return make_status(ErrorCode::StoreRecoveryRefused, ValidationStage::Persistence,
                         "recovery scan found no intact generation");
    }
    CommitSeq seq{};
    Digest256 digest{};
    bool valid = false;
    std::string detail;
    GENCTL_TRY_ASSIGN(decoded,
                      read_image_file(best->name, &seq, &digest, &valid, &detail));
    if (!valid) {
      return make_status(ErrorCode::StoreRecoveryRefused, ValidationStage::Persistence,
                         "recovery scan candidate is no longer readable: " + detail);
    }
    image_ = std::move(decoded);
    commit_seq_ = seq;
    epoch_ = image_.epoch;
    incarnation_ = image_.incarnation;
  }

  if (fence_.highest_seq.is_set() && commit_seq_ < fence_.highest_seq) {
    if (!options_.accept_rollback) {
      return make_status(ErrorCode::StoreRollbackDetected, ValidationStage::Persistence,
                         "store head is at commit " + std::to_string(commit_seq_.value()) +
                             " but the monotonic fence records commit " +
                             std::to_string(fence_.highest_seq.value()) +
                             "; the store appears to have been rolled back. Accept the rollback "
                             "explicitly after verifying the installation.");
    }
  }
  return Status::success();
}

Status DurableStore::acquire_writer_lease() {
  ControlEpoch requested = options_.requested_epoch;
  if (requested.is_set() && lease_.epoch.is_set() && requested < lease_.epoch) {
    return make_status(ErrorCode::StaleControlEpoch, ValidationStage::Fencing,
                       "requested control epoch " + std::to_string(requested.value()) +
                           " is below the stored epoch " + std::to_string(lease_.epoch.value()) +
                           "; authority has moved on and this request is stale");
  }
  if (requested.is_unset()) requested = lease_.epoch;
  if (requested.value() > kMaxAcceptedControlEpoch) {
    return make_status(ErrorCode::ValueOutOfRange, ValidationStage::Fencing,
                       "control epoch " + std::to_string(requested.value()) +
                           " is above the maximum this runtime will accept");
  }
  if (lease_.incarnation.at_max()) {
    return make_status(ErrorCode::FencedWriter, ValidationStage::Fencing,
                       "writer incarnation counter is exhausted");
  }
  StoreLease lease{};
  lease.epoch = requested;
  lease.incarnation = lease_.incarnation.next();
  lease.process_id = platform::current_process_id();
  lease.process_token = platform::current_process_token();
  lease.acquired_at = clock_->now_millis();
  lease.observed_commit_seq = commit_seq_;

  const ByteBuffer payload = encode_lease_payload(lease);
  const ByteBuffer record = encode_record(FileKind::Lease, CommitSeq{0}, payload);
  GENCTL_TRY_ASSIGN(path, join_inside(options_.directory, kLeaseFileName));
  GENCTL_TRY(platform::durable_write_file(path, record, false));
  lease_ = lease;
  incarnation_ = lease.incarnation;
  epoch_ = lease.epoch;
  return Status::success();
}

Status DurableStore::write_fence(const StoreFence& fence) {
  const ByteBuffer payload = encode_fence_payload(fence);
  const ByteBuffer record = encode_record(FileKind::Fence, fence.highest_seq, payload);
  GENCTL_TRY_ASSIGN(target, join_inside(options_.directory, kFenceFileName));
  GENCTL_TRY_ASSIGN(staging, join_inside(options_.directory, "genctl.fence.tmp"));
  GENCTL_TRY(platform::durable_write_file(staging, record, false));
  GENCTL_TRY(platform::atomic_replace(staging, target));
  fence_ = fence;
  return Status::success();
}

Result<StateImage> DurableStore::read_image_file(const std::string& name, CommitSeq* seq,
                                                 Digest256* digest, bool* valid,
                                                 std::string* detail, Status* failure) const {
  *valid = false;
  GENCTL_TRY_ASSIGN(path, join_inside(options_.directory, name));
  Result<bool> exists = platform::path_exists(path);
  if (!exists.ok()) return exists.status();
  if (!exists.value()) {
    *detail = "generation file is missing";
    return StateImage{};
  }
  Result<ByteBuffer> bytes = platform::read_file_bounded(path, options_.max_record_bytes + 4096);
  if (!bytes.ok()) {
    *detail = bytes.status().message();
    if (failure != nullptr) *failure = bytes.status();
    return StateImage{};
  }
  FileKind kind = FileKind::StateImage;
  CommitSeq record_seq{};
  ByteBuffer payload;
  Digest256 payload_digest{};
  const Status framed = decode_record(bytes.value(), options_.max_record_bytes, &kind, &record_seq,
                                      &payload, &payload_digest);
  if (!framed.ok()) {
    *detail = framed.message();
    if (failure != nullptr) *failure = framed;
    return StateImage{};
  }
  if (kind != FileKind::StateImage) {
    *detail = "generation file carries the wrong record kind";
    return StateImage{};
  }
  CanonicalReader reader(payload.data(), payload.size(), payload_limits(options_.max_record_bytes));
  Result<StateImage> decoded = decode_value<StateImage>(reader);
  if (!decoded.ok()) {
    *detail = decoded.status().message();
    if (failure != nullptr) *failure = decoded.status();
    return StateImage{};
  }
  if (!reader.at_end()) {
    *detail = "generation payload has trailing bytes";
    if (failure != nullptr) {
      *failure = make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence, *detail);
    }
    return StateImage{};
  }
  const Status structural = validate_state_image(decoded.value());
  if (!structural.ok()) {
    *detail = structural.message();
    if (failure != nullptr) *failure = structural;
    return StateImage{};
  }
  *valid = true;
  if (seq != nullptr) *seq = record_seq;
  if (digest != nullptr) *digest = payload_digest;
  return decoded;
}

Status DurableStore::scan_generations(std::vector<GenerationFileStatus>* out) const {
  GENCTL_TRY_ASSIGN(entries, platform::list_directory(options_.directory));
  std::sort(entries.begin(), entries.end());
  for (const auto& name : entries) {
    CommitSeq seq{};
    if (!parse_generation_name(name, &seq)) continue;
    GenerationFileStatus status{};
    status.name = name;
    status.commit_seq = seq;
    Result<std::uint64_t> size = platform::file_size(options_.directory + "\\" + name);
    if (size.ok()) status.size_bytes = size.value();
    bool valid = false;
    std::string detail;
    CommitSeq read_seq{};
    Digest256 digest{};
    Status failure = Status::success();
    Result<StateImage> decoded =
        read_image_file(name, &read_seq, &digest, &valid, &detail, &failure);
    if (!decoded.ok() && !valid) detail = decoded.status().message();
    status.valid = valid;
    status.digest = digest;
    status.detail = valid ? "intact" : detail;
    out->push_back(std::move(status));
  }
  return Status::success();
}

Status DurableStore::retire_residue() {
  if (options_.read_only) return Status::success();
  GENCTL_TRY_ASSIGN(entries, platform::list_directory(options_.directory));
  std::sort(entries.begin(), entries.end());
  for (const auto& name : entries) {
    const bool residue = ends_with(name, ".staging") || ends_with(name, ".tmp") ||
                         ends_with(name, ".tmp.staging");
    if (residue) {
      GENCTL_TRY_ASSIGN(path, join_inside(options_.directory, name));
      const Status removed = platform::remove_file(path);
      if (removed.ok()) ++residue_retired_;
      continue;
    }
    CommitSeq seq{};
    if (!parse_generation_name(name, &seq)) continue;
    if (options_.generation_retention == 0) continue;
    if (seq.value() + options_.generation_retention > commit_seq_.value()) continue;
    if (seq > commit_seq_) continue;
    GENCTL_TRY_ASSIGN(path, join_inside(options_.directory, name));
    const Status removed = platform::remove_file(path);
    if (removed.ok()) ++residue_retired_;
  }
  return Status::success();
}

Result<CommitSeq> DurableStore::publish(StateImage image, EpochMillis now) {
  if (closed_) {
    return make_status(ErrorCode::StoreClosed, ValidationStage::Persistence,
                       "store has been closed");
  }
  if (options_.read_only) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store was opened read-only; mutations are refused");
  }
  if (commit_seq_.at_max()) {
    return make_status(ErrorCode::StorePublicationFailed, ValidationStage::Persistence,
                       "commit sequence is exhausted");
  }
  const CommitSeq next = CommitSeq{commit_seq_.value() + 1};
  image.commit_seq = next;
  image.format_version = kStoreFormatVersion;
  image.previous_digest = head_present_ ? head_.image_digest : Digest256::zero();
  image.epoch = epoch_;
  image.incarnation = incarnation_;
  image.created_at = now;

  maybe_crash(options_.crash_point, CrashPoint::AfterReserveBeforeStaging);

  const Status structural = validate_state_image(image);
  if (!structural.ok()) return structural;

  const Digest256 digest = digest_state_image(image);
  CanonicalWriter writer(payload_limits(options_.max_record_bytes));
  GENCTL_TRY(encode_value(writer, image));
  const ByteBuffer payload = writer.take();
  const ByteBuffer record = encode_record(FileKind::StateImage, next, payload);

  const std::string published_name = generation_file_name(next);
  GENCTL_TRY_ASSIGN(published_path, join_inside(options_.directory, published_name));
  GENCTL_TRY_ASSIGN(staging_path,
                    join_inside(options_.directory, published_name + kStagingSuffix));

  GENCTL_TRY(platform::durable_write_file(staging_path, record, false));
  maybe_crash(options_.crash_point, CrashPoint::AfterStagingBeforeReadback);

  // Read back and verify what actually reached durable storage.
  GENCTL_TRY_ASSIGN(readback,
                    platform::read_file_bounded(staging_path, options_.max_record_bytes + 4096));
  FileKind kind = FileKind::StateImage;
  CommitSeq read_seq{};
  ByteBuffer read_payload;
  Digest256 read_digest{};
  GENCTL_TRY(decode_record(readback, options_.max_record_bytes, &kind, &read_seq, &read_payload,
                           &read_digest));
  if (kind != FileKind::StateImage || read_seq != next || read_digest != digest ||
      read_payload != payload) {
    return make_status(ErrorCode::StoreIntegrityFailure, ValidationStage::Persistence,
                       "staged generation did not read back identically");
  }
  maybe_crash(options_.crash_point, CrashPoint::AfterReadbackBeforePublish);

  GENCTL_TRY(platform::atomic_replace(staging_path, published_path));
  GENCTL_TRY(platform::flush_directory(options_.directory));
  maybe_crash(options_.crash_point, CrashPoint::AfterPublishBeforeHeadCommit);

  // ---- COMMIT POINT: atomic replacement of the head marker ----
  StoreHead head{};
  head.commit_seq = next;
  head.image_digest = digest;
  head.epoch = epoch_;
  head.incarnation = incarnation_;
  head.published_at = now;
  const ByteBuffer head_payload = encode_head_payload(head);
  const ByteBuffer head_record = encode_record(FileKind::Head, next, head_payload);
  GENCTL_TRY_ASSIGN(head_path, join_inside(options_.directory, kHeadFileName));
  GENCTL_TRY_ASSIGN(head_tmp, join_inside(options_.directory, std::string(kHeadFileName) + kTempSuffix));
  GENCTL_TRY(platform::durable_write_file(head_tmp, head_record, false));
  GENCTL_TRY(platform::atomic_replace(head_tmp, head_path));
  GENCTL_TRY(platform::flush_directory(options_.directory));

  head_ = head;
  head_present_ = true;
  commit_seq_ = next;
  image_ = std::move(image);
  maybe_crash(options_.crash_point, CrashPoint::AfterHeadCommitBeforeFence);

  StoreFence raised{};
  raised.highest_seq = next;
  raised.highest_digest = digest;
  raised.highest_epoch = epoch_;
  GENCTL_TRY(write_fence(raised));

  GENCTL_TRY(retire_residue());
  return next;
}

Result<StoreAuditReport> DurableStore::audit(bool accept_rollback,
                                             const std::string& acceptance_note,
                                             std::size_t* residue_retired) {
  (void)accept_rollback;
  (void)acceptance_note;
  StoreAuditReport report{};
  report.directory = options_.directory;
  report.read_only = options_.read_only;
  report.writer_lock_held = lock_.held();
  report.head_present = head_present_;
  report.head_valid = head_present_;
  report.head_seq = head_.commit_seq;
  report.head_digest = head_.image_digest;
  report.fence_seq = fence_.highest_seq;
  report.fence_epoch = fence_.highest_epoch;
  report.lease = lease_;
  report.residue = residue_retired == nullptr ? residue_retired_ : *residue_retired;

  std::vector<GenerationFileStatus> generations;
  GENCTL_TRY(scan_generations(&generations));
  report.generations_scanned = generations.size();
  for (const auto& generation : generations) {
    if (generation.valid) ++report.generations_valid;
    if (!generation.valid) {
      report.anomalies.push_back("generation " + generation.name + " is not usable: " +
                                 generation.detail);
    }
  }
  report.generations = generations;

  if (fence_.highest_seq.is_set() && commit_seq_ < fence_.highest_seq) {
    report.rollback_detected = true;
    report.anomalies.push_back("head commit " + std::to_string(commit_seq_.value()) +
                               " is behind the monotonic fence " +
                               std::to_string(fence_.highest_seq.value()));
  }
  if (!head_present_) {
    report.anomalies.push_back("store has no committed head marker");
  }
  if (!image_.empty() && head_present_) {
    const Digest256 recomputed = digest_state_image(image_);
    if (recomputed != head_.image_digest) {
      report.rollback_detected = false;
      report.anomalies.push_back("adopted image digest does not reproduce the head digest");
    }
  }

  report.summary = "head=" + std::to_string(report.head_seq.value()) + " fence=" +
                   std::to_string(report.fence_seq.value()) + " generations=" +
                   std::to_string(report.generations_valid) + "/" +
                   std::to_string(report.generations_scanned) + " anomalies=" +
                   std::to_string(report.anomalies.size());
  return report;
}

Status DurableStore::close() {
  if (closed_) return Status::success();
  closed_ = true;
  GENCTL_TRY(lock_.release());
  return Status::success();
}

}  // namespace genctl
