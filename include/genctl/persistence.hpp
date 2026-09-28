// Generator Control - versioned, integrity checked, transactional persistence.
//
// Store layout (all names are fixed constants or derived from monotonic integers):
//
//   genctl.lock                       OS writer lock (LockFileEx / flock)
//   genctl.lease                      writer lease: epoch, incarnation, holder token
//   genctl.fence                      monotonic fence: highest committed sequence
//   genctl.head                       committed head marker  == the commit point
//   generation-<20 digits>.gcs        immutable published state images
//   *.gcs.staging / *.head.tmp        residue from an interrupted publication
//
// Record layout (format 1), little-endian, normative:
//
//   header, 72 bytes
//     0   magic[8]            "GENCTLST"
//     8   format_version u32
//     12  file_kind u32       (1 = state image, 2 = head, 3 = fence, 4 = lease)
//     16  commit_seq u64
//     24  payload_length u64
//     32  payload_sha256[32]
//     64  header_crc32c u32   CRC-32C over bytes [0,64)
//     68  reserved u32        must be zero
//   payload, payload_length bytes
//   trailer, 4 bytes         CRC-32C over the payload
//
// A file is accepted only when: its size is exactly 72 + payload_length + 4, the
// magic and format match, payload_length is within the configured bound, the header
// CRC, the payload CRC and the payload SHA-256 all verify, and the payload decodes
// canonically with no trailing bytes.
//
// Publication protocol, with the commit point stated explicitly:
//
//   1. reserve      revision/sequence under the mutation lock
//   2. stage        write generation-<seq>.gcs.staging, flush to durable storage
//   3. read back    re-open the staging file and verify size, CRCs and SHA-256
//   4. publish      atomic replace staging -> generation-<seq>.gcs, flush directory
//   5. COMMIT POINT atomic replace genctl.head.tmp -> genctl.head, flush directory
//   6. fence        advance genctl.fence to the committed sequence
//   7. retire       delete generations outside the retention window and residue
//
// Recovery adopts exactly one whole verified state image: the one named by the head
// marker. Partial generations are never stitched together. When the head marker is
// missing or damaged the store refuses to open unless an explicit recovery scan is
// requested, which is audited.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "genctl/canonical.hpp"
#include "genctl/digest.hpp"
#include "genctl/model.hpp"
#include "genctl/platform.hpp"
#include "genctl/time.hpp"

namespace genctl {

inline constexpr char kStoreMagic[9] = "GENCTLST";
inline constexpr std::size_t kRecordHeaderBytes = 72;
inline constexpr std::size_t kRecordTrailerBytes = 4;
inline constexpr std::size_t kMaxHeadPayloadBytes = 4096;

inline constexpr const char* kLockFileName = "genctl.lock";
inline constexpr const char* kLeaseFileName = "genctl.lease";
inline constexpr const char* kFenceFileName = "genctl.fence";
inline constexpr const char* kHeadFileName = "genctl.head";
inline constexpr const char* kGenerationPrefix = "generation-";
inline constexpr const char* kGenerationSuffix = ".gcs";
inline constexpr const char* kStagingSuffix = ".staging";
inline constexpr const char* kTempSuffix = ".tmp";

enum class FileKind : std::uint32_t {
  StateImage = 1,
  Head = 2,
  Fence = 3,
  Lease = 4,
};

// Deterministic crash injection points. Each one terminates the process
// immediately, without destructors or interactive error reporting.
enum class CrashPoint : std::uint8_t {
  None = 0,
  BeforeReserve = 1,
  AfterReserveBeforeStaging = 2,
  AfterStagingBeforeReadback = 3,
  AfterReadbackBeforePublish = 4,
  AfterPublishBeforeHeadCommit = 5,
  AfterHeadCommitBeforeFence = 6,
  AfterPublishBeforeActuation = 7,
  AfterActuationBeforeAckCommit = 8,
  AfterAckCommit = 9,
};

[[nodiscard]] std::string_view to_string(CrashPoint point) noexcept;
[[nodiscard]] Result<CrashPoint> parse_crash_point(std::string_view text);
// Terminates the process immediately when point is the configured crash point.
void maybe_crash(CrashPoint configured, CrashPoint point) noexcept;
[[noreturn]] void crash_now(CrashPoint point) noexcept;

struct StoreHead {
  CommitSeq commit_seq{};
  Digest256 image_digest{};
  ControlEpoch epoch{};
  IncarnationId incarnation{};
  EpochMillis published_at{0};

  friend bool operator==(const StoreHead&, const StoreHead&) noexcept = default;
};

struct StoreLease {
  ControlEpoch epoch{};
  IncarnationId incarnation{};
  std::uint32_t process_id{0};
  std::string process_token{};
  EpochMillis acquired_at{0};
  CommitSeq observed_commit_seq{};

  friend bool operator==(const StoreLease&, const StoreLease&) noexcept = default;
};

inline constexpr std::uint32_t kUnsetEpoch = 0;

struct StoreFence {
  CommitSeq highest_seq{};
  Digest256 highest_digest{};
  ControlEpoch highest_epoch{};

  friend bool operator==(const StoreFence&, const StoreFence&) noexcept = default;
};

struct StoreOpenOptions {
  std::string directory{};
  bool read_only{false};
  bool create_if_missing{false};
  // Rebuild the head marker from the newest intact generation when the head is
  // missing or damaged. Audited; never performed implicitly.
  bool recover_scan{false};
  // Accept a head whose commit sequence is below the fence marker's. This is the
  // documented operator action for a deliberate restore; it is recorded.
  bool accept_rollback{false};
  std::string rollback_acceptance_note{};
  // Authority epoch the caller intends to hold. A caller presenting an epoch below
  // the lease's is refused as stale.
  ControlEpoch requested_epoch{};
  std::uint32_t generation_retention{4};
  std::size_t max_record_bytes{4u * 1024u * 1024u};
  CrashPoint crash_point{CrashPoint::None};
};

struct GenerationFileStatus {
  std::string name{};
  CommitSeq commit_seq{};
  std::uint64_t size_bytes{0};
  bool valid{false};
  Digest256 digest{};
  std::string detail{};
};

struct StoreAuditReport {
  std::string directory{};
  bool read_only{false};
  bool writer_lock_held{false};
  bool head_present{false};
  bool head_valid{false};
  bool rollback_detected{false};
  bool rollback_accepted{false};
  bool recovered_from_scan{false};
  CommitSeq head_seq{};
  Digest256 head_digest{};
  CommitSeq fence_seq{};
  ControlEpoch fence_epoch{};
  StoreLease lease{};
  std::size_t generations_scanned{0};
  std::size_t generations_valid{0};
  std::size_t generations_retired{0};
  std::size_t staging_files_retired{0};
  // Files retired as disposable residue (interrupted staging and out of window
  // generations) during the most recent open or publication.
  std::size_t residue{0};
  std::vector<GenerationFileStatus> generations{};
  std::vector<std::string> anomalies{};
  std::string summary{};
};

// Encodes the fixed record frame. Exposed for adversarial parser tests.
[[nodiscard]] ByteBuffer encode_record(FileKind kind, CommitSeq commit_seq, const ByteBuffer& payload);
// Strictly decodes a record frame. Every declared length is checked against the
// actual buffer before anything is read from it.
[[nodiscard]] Status decode_record(const ByteBuffer& raw, std::size_t max_payload_bytes,
                                   FileKind* kind, CommitSeq* commit_seq, ByteBuffer* payload,
                                   Digest256* payload_digest);

class DurableStore {
 public:
  ~DurableStore();
  DurableStore(const DurableStore&) = delete;
  DurableStore& operator=(const DurableStore&) = delete;

  [[nodiscard]] static Result<std::unique_ptr<DurableStore>> open(const StoreOpenOptions& options,
                                                                 const Clock& clock);

  [[nodiscard]] const StoreOpenOptions& options() const noexcept { return options_; }
  [[nodiscard]] const std::string& directory() const noexcept { return options_.directory; }
  [[nodiscard]] bool read_only() const noexcept { return options_.read_only; }
  [[nodiscard]] bool writer_lock_held() const noexcept { return lock_.held(); }
  [[nodiscard]] IncarnationId incarnation() const noexcept { return incarnation_; }
  [[nodiscard]] ControlEpoch epoch() const noexcept { return epoch_; }
  [[nodiscard]] CommitSeq commit_seq() const noexcept { return commit_seq_; }
  [[nodiscard]] const StoreFence& fence() const noexcept { return fence_; }
  [[nodiscard]] const StoreLease& lease() const noexcept { return lease_; }
  [[nodiscard]] const StateImage& image() const noexcept { return image_; }
  [[nodiscard]] std::size_t residue_retired() const noexcept { return residue_retired_; }
  // True when this open founded a previously non-existent store.
  [[nodiscard]] bool created_new_store() const noexcept { return created_new_store_; }

  // Publishes a whole state image through the documented protocol. The commit
  // point is the atomic replacement of the head marker.
  [[nodiscard]] Result<CommitSeq> publish(StateImage image, EpochMillis now);

  [[nodiscard]] Result<StoreAuditReport> audit(bool accept_rollback,
                                               const std::string& acceptance_note,
                                               std::size_t* residue_retired);
  [[nodiscard]] Status close();
  [[nodiscard]] bool closed() const noexcept { return closed_; }

  // File name helpers (fixed prefixes, integer derived names only).
  [[nodiscard]] std::string generation_file_name(CommitSeq seq) const;

 private:
  DurableStore() = default;

  [[nodiscard]] Status open_store();
  [[nodiscard]] Status adopt_committed_image(bool allow_scan);
  [[nodiscard]] Status acquire_writer_lease();
  [[nodiscard]] Status write_fence(const StoreFence& fence);
  [[nodiscard]] Status retire_residue();
  [[nodiscard]] Result<StateImage> read_image_file(const std::string& name, CommitSeq* seq,
                                                   Digest256* digest, bool* valid,
                                                   std::string* detail,
                                                   Status* failure = nullptr) const;
  [[nodiscard]] Status scan_generations(std::vector<GenerationFileStatus>* out) const;

  StoreOpenOptions options_{};
  const Clock* clock_{nullptr};
  platform::ExclusiveFileLock lock_{};
  StateImage image_{};
  StoreHead head_{};
  StoreFence fence_{};
  StoreLease lease_{};
  CommitSeq commit_seq_{};
  IncarnationId incarnation_{};
  ControlEpoch epoch_{};
  bool head_present_{false};
  bool closed_{false};
  bool created_new_store_{false};
  std::size_t residue_retired_{0};
};

}  // namespace genctl
