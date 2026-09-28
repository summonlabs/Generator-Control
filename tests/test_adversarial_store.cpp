// Proof obligations: the durable store rejects malformed, truncated, oversized and
// tampered input instead of guessing, and the canonical decoder refuses every
// declared length that does not match reality.
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma warning(push)
#pragma warning(disable : 4668)
#include <windows.h>
#include <winioctl.h>
#pragma warning(pop)

#include "codecs.hpp"
#include "genctl/path_safety.hpp"
#include "genctl/persistence.hpp"
#include "genctl/platform.hpp"
#include "support.hpp"
#include "test_framework.hpp"

namespace {
using namespace genctl;
using namespace gctest;

Result<std::wstring> to_wide(const std::string& text) {
  if (text.empty()) return std::wstring{};
  const int needed = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
  if (needed <= 0) {
    return make_status(ErrorCode::PathEncodingInvalid, ValidationStage::Format, "not UTF-8");
  }
  std::wstring out(static_cast<std::size_t>(needed), L'\0');
  ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), out.data(), needed);
  return out;
}

// Builds a store with a handful of committed generations and a registered generator.
struct StoreFixture {
  explicit StoreFixture(const std::string& label) : lab(make_config(label)) {
    const Result<GeneratorId> parsed = GeneratorId::parse("gen-unit-1");
    if (!parsed.ok()) fail("identity invalid");
    id = parsed.value();
    adapter = std::make_unique<SyntheticAdapter>(id, lab.clock(), SyntheticAdapter::Options{});
    OpenOptions options{};
    options.store_directory = lab.store();
    options.create_if_missing = true;
    options.requested_epoch = ControlEpoch{1};
    Result<std::unique_ptr<GeneratorControlEngine>> opened =
        GeneratorControlEngine::open(options, lab.clock(), adapter.get());
    if (!opened.ok()) fail("open failed: " + opened.status().message());
    engine = std::move(opened.value());
    const Result<GeneratorState> state = engine->register_generator(id, RegisterOptions{}, 1'000);
    if (!state.ok()) fail("registration failed: " + state.status().message());
    for (int i = 0; i < 3; ++i) {
      ResourceUpdate update{};
      update.generator = id;
      update.controller = engine->controller();
      const Result<GeneratorState> current = engine->inspect(id);
      update.revision = current.value().revision;
      const Result<IdempotencyKey> key =
          IdempotencyKey::parse("adv-" + std::to_string(i) + "-" +
                                std::to_string(engine->commit_seq().value()));
      update.idempotency_key = key.value();
      update.set_fuel_level = true;
      update.fuel_level.state = EvidenceState::Present;
      update.fuel_level.value = FuelQuantity{FuelUnit::Millilitres, 900'000 - i};
      update.fuel_level.source = EvidenceSource::VendorAdapter;
      update.fuel_level.observed_at = lab.clock().now_millis();
      if (!engine->record_resource(update).ok()) fail("mutation failed");
    }
    if (!engine->close().ok()) fail("close failed");
    engine.reset();

    // Locate the committed generation so the tests can tamper with it.
    const Result<ByteBuffer> head_bytes =
        platform::read_file_bounded(lab.store() + "\\genctl.head", 1u << 20);
    if (!head_bytes.ok()) fail("head unreadable");
    const Result<StoreAuditReport> audit = store_audit();
    if (!audit.ok()) fail("audit failed");
    head_sequence = audit.value().head_seq;
    for (const auto& generation : audit.value().generations) {
      if (generation.commit_seq == head_sequence) generation_path = lab.store() + "\\" + generation.name;
    }
    if (generation_path.empty()) fail("committed generation not found");
  }

  Result<StoreAuditReport> store_audit() {
    OpenOptions options{};
    options.store_directory = lab.store();
    options.requested_epoch = ControlEpoch{1};
    SyntheticAdapter probe(id, lab.clock(), SyntheticAdapter::Options{});
    const Result<std::unique_ptr<GeneratorControlEngine>> opened =
        GeneratorControlEngine::open(options, lab.clock(), &probe);
    if (!opened.ok()) return opened.status();
    Result<StoreAuditReport> report = opened.value()->store_audit(false, {});
    (void)opened.value()->close();
    return report;
  }

  Result<std::unique_ptr<GeneratorControlEngine>> reopen(bool recover_scan = false) {
    OpenOptions options{};
    options.store_directory = lab.store();
    options.requested_epoch = ControlEpoch{1};
    options.recover_scan = recover_scan;
    return GeneratorControlEngine::open(options, lab.clock(), adapter.get());
  }

  static LabConfig make_config(const std::string& label) {
    LabConfig config{};
    config.label = label;
    return config;
  }

  Lab lab;
  GeneratorId id{};
  std::unique_ptr<SyntheticAdapter> adapter;
  std::unique_ptr<GeneratorControlEngine> engine;
  CommitSeq head_sequence{};
  std::string generation_path{};
};

TEST(a_truncated_committed_generation_is_refused) {
  StoreFixture fixture("adv-truncated");
  const Result<ByteBuffer> bytes = platform::read_file_bounded(fixture.generation_path, 1u << 20);
  CHECK(bytes.ok());
  ByteBuffer truncated(bytes.value().begin(), bytes.value().end() - 8);
  CHECK(platform::durable_write_file(fixture.generation_path, truncated, false).ok());
  const Result<std::unique_ptr<GeneratorControlEngine>> opened = fixture.reopen();
  CHECK(!opened.ok());
  CHECK(opened.status().code() == ErrorCode::StoreCorrupt ||
        opened.status().code() == ErrorCode::StoreIntegrityFailure ||
        opened.status().code() == ErrorCode::StoreTruncated);
}

TEST(a_tampered_payload_is_detected_by_the_record_checksum) {
  StoreFixture fixture("adv-tampered");
  Result<ByteBuffer> bytes = platform::read_file_bounded(fixture.generation_path, 1u << 20);
  CHECK(bytes.ok());
  ByteBuffer tampered = bytes.value();
  CHECK(tampered.size() > 200);
  tampered[tampered.size() / 2] ^= 0x5Au;
  CHECK(platform::durable_write_file(fixture.generation_path, tampered, false).ok());
  const Result<std::unique_ptr<GeneratorControlEngine>> opened = fixture.reopen();
  CHECK(!opened.ok());
  CHECK(opened.status().code() == ErrorCode::StoreIntegrityFailure ||
        opened.status().code() == ErrorCode::StoreCorrupt);
}

TEST(a_tampered_header_is_detected) {
  StoreFixture fixture("adv-header");
  Result<ByteBuffer> bytes = platform::read_file_bounded(fixture.generation_path, 1u << 20);
  CHECK(bytes.ok());
  ByteBuffer tampered = bytes.value();
  tampered[2] = 'X';
  CHECK(platform::durable_write_file(fixture.generation_path, tampered, false).ok());
  const Result<std::unique_ptr<GeneratorControlEngine>> opened = fixture.reopen();
  CHECK(!opened.ok());
  CHECK_EQ(opened.status().code(), ErrorCode::StoreCorrupt);
}

TEST(an_unsupported_format_version_is_refused_explicitly) {
  StoreFixture fixture("adv-version");
  Result<ByteBuffer> bytes = platform::read_file_bounded(fixture.generation_path, 1u << 20);
  CHECK(bytes.ok());
  ByteBuffer tampered = bytes.value();
  // The format field sits at offset 8 and must be reported, not silently accepted.
  tampered[8] = 9;
  tampered[9] = 0;
  tampered[10] = 0;
  tampered[11] = 0;
  CHECK(platform::durable_write_file(fixture.generation_path, tampered, false).ok());
  const Result<std::unique_ptr<GeneratorControlEngine>> opened = fixture.reopen();
  CHECK(!opened.ok());
  // Either the header checksum catches it first or the version check does; both are
  // explicit refusals.
  CHECK(opened.status().code() == ErrorCode::StoreIntegrityFailure ||
        opened.status().code() == ErrorCode::StoreVersionUnsupported);
}

TEST(an_oversized_store_file_is_refused_before_it_is_read) {
  StoreFixture fixture("adv-oversize");
  // A generation file larger than the configured record bound must be rejected
  // without being loaded into memory.
  ByteBuffer huge(6u * 1024u * 1024u, 0x41);
  const std::string path = fixture.lab.store() + "\\generation-00000000000000099000.gcs";
  CHECK(platform::durable_write_file(path, huge, false).ok());
  OpenOptions options{};
  options.store_directory = fixture.lab.store();
  options.requested_epoch = ControlEpoch{1};
  options.config.max_record_bytes = 1u * 1024u * 1024u;
  SyntheticAdapter probe(fixture.id, fixture.lab.clock(), SyntheticAdapter::Options{});
  const Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(options, fixture.lab.clock(), &probe);
  CHECK(opened.ok());
  const Result<StoreAuditReport> audit = opened.value()->store_audit(false, {});
  CHECK(audit.ok());
  bool saw_oversize = false;
  for (const auto& generation : audit.value().generations) {
    if (generation.commit_seq.value() == 99000) {
      saw_oversize = true;
      CHECK(!generation.valid);
    }
  }
  CHECK(saw_oversize);
  CHECK(opened.value()->close().ok());
}

TEST(a_missing_committed_generation_is_refused) {
  StoreFixture fixture("adv-missing");
  CHECK(platform::remove_file(fixture.generation_path).ok());
  const Result<std::unique_ptr<GeneratorControlEngine>> opened = fixture.reopen();
  CHECK(!opened.ok());
  CHECK(opened.status().code() == ErrorCode::StoreCorrupt ||
        opened.status().code() == ErrorCode::StoreGenerationMissing);
}

TEST(a_directory_without_a_store_is_not_silently_adopted) {
  ScratchDir directory("adv-empty");
  OpenOptions options{};
  options.store_directory = directory.path();
  ManualClock clock;
  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  CHECK(id.ok());
  SyntheticAdapter adapter(id.value(), clock, SyntheticAdapter::Options{});
  const Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(options, clock, &adapter);
  CHECK_RESULT_CODE(opened, ErrorCode::StoreNotFound);
}

TEST(the_record_frame_rejects_every_declared_length_boundary) {
  const ByteBuffer payload{'p', 'a', 'y', 'l', 'o', 'a', 'd'};
  const ByteBuffer record = encode_record(FileKind::StateImage, CommitSeq{7}, payload);
  CHECK_EQ(record.size(), kRecordHeaderBytes + payload.size() + kRecordTrailerBytes);

  FileKind kind = FileKind::StateImage;
  CommitSeq seq{};
  ByteBuffer decoded;
  Digest256 digest{};
  CHECK(decode_record(record, 1024, &kind, &seq, &decoded, &digest).ok());
  CHECK_EQ(seq.value(), 7u);
  CHECK(decoded == payload);

  // A declared payload length larger than the buffer.
  ByteBuffer lying = record;
  lying[24] = 0xFFu;
  CHECK_CODE(decode_record(lying, 1024, &kind, &seq, &decoded, &digest),
             ErrorCode::StoreTruncated);

  // A payload longer than the configured bound.
  CHECK_CODE(decode_record(record, 4, &kind, &seq, &decoded, &digest), ErrorCode::StoreOversize);

  // A record shorter than the frame itself.
  const ByteBuffer tiny(10, 0);
  CHECK_CODE(decode_record(tiny, 1024, &kind, &seq, &decoded, &digest), ErrorCode::StoreTruncated);

  // A non-zero reserved field.
  ByteBuffer reserved = record;
  reserved[68] = 1;
  // The header checksum covers bytes 0..63, so the reserved field must be checked
  // separately.
  CHECK_CODE(decode_record(reserved, 1024, &kind, &seq, &decoded, &digest),
             ErrorCode::StoreCorrupt);

  // An unknown record kind.
  ByteBuffer unknown = record;
  unknown[12] = 42;
  CHECK_CODE(decode_record(unknown, 1024, &kind, &seq, &decoded, &digest),
             ErrorCode::StoreCorrupt);
}

TEST(the_decoder_refuses_a_state_image_with_trailing_bytes) {
  CanonicalWriter writer(CanonicalLimits{1u << 20, 512, 4096, 64});
  CHECK(writer.put_u32(kStoreFormatVersion).ok());
  CHECK(writer.put_u64(1).ok());
  CHECK(writer.put_u64(0).ok());
  CHECK(writer.put_u64(1).ok());
  CHECK(writer.put_digest(Digest256::zero()).ok());
  CHECK(writer.put_i64(0).ok());
  CHECK(writer.put_count(0).ok());  // generators
  CHECK(writer.put_count(0).ok());  // attempts
  CHECK(writer.put_count(0).ok());  // replay
  CHECK(writer.put_u64(1).ok());
  CHECK(writer.put_u64(1).ok());
  CHECK(writer.put_u64(0).ok());
  CHECK(writer.put_u64(0).ok());
  CHECK(writer.put_u32(256).ok());
  CHECK(writer.put_u32(1024).ok());
  CHECK(writer.put_u64(0).ok());
  CHECK(writer.put_i64(0).ok());
  CHECK(writer.put_string("").ok());
  CHECK(writer.put_string("").ok());
  CHECK(writer.put_u64(0).ok());
  ByteBuffer hostile = writer.bytes();
  hostile.push_back(0x00u);
  CanonicalReader reader(hostile.data(), hostile.size(), CanonicalLimits{1u << 20, 512, 4096, 64});
  const Result<StateImage> decoded = decode_value<StateImage>(reader);
  CHECK(decoded.ok());
  CHECK(!reader.at_end());
  CHECK_CODE(reader.expect_end(), ErrorCode::MalformedEncoding);
}

TEST(a_state_image_that_decodes_is_structurally_validated) {
  StateImage image{};
  CHECK(validate_state_image(image).ok());

  StateImage wrong_format{};
  wrong_format.format_version = 99;
  CHECK_CODE(validate_state_image(wrong_format), ErrorCode::StoreVersionUnsupported);

  StateImage out_of_order{};
  AttemptRecord first{};
  first.id = AttemptId{2};
  first.generator = GeneratorId::parse("gen-1").value();
  AttemptRecord second{};
  second.id = AttemptId{1};
  second.generator = GeneratorId::parse("gen-1").value();
  out_of_order.attempts = {first, second};
  CHECK_CODE(validate_state_image(out_of_order), ErrorCode::MalformedEncoding);

  StateImage duplicate_keys{};
  ReplayEntry a{};
  a.key = IdempotencyKey::parse("duplicate-key-1").value();
  ReplayEntry b{};
  b.key = IdempotencyKey::parse("duplicate-key-1").value();
  duplicate_keys.replay = {a, b};
  CHECK_CODE(validate_state_image(duplicate_keys), ErrorCode::DuplicateIdentity);
}

TEST(the_store_path_rejects_traversal_and_device_names) {
  CHECK(has_unsafe_component("..\\escape"));
  CHECK(has_unsafe_component("a/../b"));
  CHECK(has_unsafe_component("NUL"));
  CHECK(has_unsafe_component("nul.txt"));
  CHECK(has_unsafe_component("COM1"));
  CHECK(has_unsafe_component("trailing."));
  CHECK(has_unsafe_component("trailing "));
  CHECK(has_unsafe_component("stream:name"));
  CHECK(has_unsafe_component(std::string("control\x01char")));
  CHECK(!has_unsafe_component("C:\\stores\\genctl"));
  CHECK(!has_unsafe_component("plain-name"));
  CHECK(is_device_name("CON"));
  CHECK(is_device_name("LPT9"));
  CHECK(!is_device_name("CONSOLE"));
}

TEST(utf8_validation_rejects_overlong_and_surrogate_forms) {
  CHECK(is_valid_utf8("plain"));
  CHECK(is_valid_utf8("\xC3\xA9"));
  CHECK(!is_valid_utf8("\xC0\xAF"));            // overlong two byte form
  CHECK(!is_valid_utf8("\xE0\x80\xAF"));       // overlong three byte form
  CHECK(!is_valid_utf8("\xED\xA0\x80"));       // surrogate half
  CHECK(!is_valid_utf8("\xF5\x80\x80\x80"));  // beyond U+10FFFF
  CHECK(!is_valid_utf8("\x80"));                 // stray continuation byte
  CHECK(!is_valid_utf8("\xC3"));                 // truncated sequence
}

TEST(store_paths_reject_unsafe_and_relative_forms) {
  CHECK_RESULT_CODE(validate_store_directory(""), ErrorCode::PathInvalid);
  CHECK_RESULT_CODE(validate_store_directory("relative\\path"), ErrorCode::PathNotAbsolute);
  CHECK_RESULT_CODE(validate_store_directory("C:relative"), ErrorCode::PathTraversal);
  CHECK_RESULT_CODE(validate_store_directory("..\\escape"), ErrorCode::PathTraversal);
  CHECK_RESULT_CODE(validate_store_directory(std::string(5000, 'a')), ErrorCode::PathTooLong);
  CHECK_RESULT_CODE(join_inside("C:\\base", "..\\escape"), ErrorCode::PathTraversal);
  CHECK_RESULT_CODE(join_inside("C:\\base", "sub/file"), ErrorCode::PathTraversal);
  CHECK(join_inside("C:\\base", "genctl.head").ok());
}

// Creates a directory junction with the operating system's own tool, driven by a raw
// command line so that no shell quoting layer is involved. This is test scaffolding:
// the runtime never creates a reparse point, it only refuses to write authoritative
// state through one.
Status create_junction(const std::string& link_utf8, const std::string& target_utf8,
                       const std::string& error_path) {
  std::wstring command = L"cmd.exe /c mklink /J \"";
  for (const char c : link_utf8) command.push_back(static_cast<wchar_t>(c));
  command += L"\" \"";
  for (const char c : target_utf8) command.push_back(static_cast<wchar_t>(c));
  command += L"\"";

  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  HANDLE output = ::CreateFileW(std::wstring(error_path.begin(), error_path.end()).c_str(),
                                GENERIC_WRITE, FILE_SHARE_READ, &attributes, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  if (output != INVALID_HANDLE_VALUE) {
    startup.dwFlags |= STARTF_USESTDHANDLES;
    startup.hStdOutput = output;
    startup.hStdError = output;
    startup.hStdInput = INVALID_HANDLE_VALUE;
  }
  std::vector<wchar_t> line(command.begin(), command.end());
  line.push_back(L'\0');
  PROCESS_INFORMATION information{};
  const BOOL created = ::CreateProcessW(nullptr, line.data(), nullptr, nullptr,
                                        output != INVALID_HANDLE_VALUE ? TRUE : FALSE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                                        &information);
  if (output != INVALID_HANDLE_VALUE) ::CloseHandle(output);
  if (created == 0) {
    return make_status(ErrorCode::Internal, ValidationStage::Internal,
                       "cannot start the junction helper (Win32 error " +
                           std::to_string(static_cast<unsigned long>(::GetLastError())) + ")");
  }
  ::CloseHandle(information.hThread);
  ::WaitForSingleObject(information.hProcess, INFINITE);
  DWORD code = 1;
  ::GetExitCodeProcess(information.hProcess, &code);
  ::CloseHandle(information.hProcess);
  if (code != 0) {
    std::string detail = "no output captured";
    const Result<ByteBuffer> captured = platform::read_file_bounded(error_path, 64u * 1024u);
    if (captured.ok()) detail.assign(captured.value().begin(), captured.value().end());
    return make_status(ErrorCode::Internal, ValidationStage::Internal,
                       "the junction helper exited with " + std::to_string(code) + ": " + detail);
  }
  return Status::success();
}

TEST(store_paths_reject_a_reparse_point_substitution) {
  ScratchDir directory("adv-reparse");
  const std::string target = directory.child("real-store");
  const std::string link = directory.child("linked-store");
  CHECK(platform::ensure_directory(target).ok());
  // mklink /J creates the link directory itself; it must not already exist.
  const Status created = create_junction(link, target, directory.child("junction.txt"));
  if (!created.ok()) fail(created.message());

  const Result<bool> is_link = platform::is_reparse_point(link);
  CHECK(is_link.ok());
  CHECK(is_link.value());
  CHECK_RESULT_CODE(validate_store_directory(link), ErrorCode::PathReparsePoint);

  // The real directory is still accepted.
  CHECK(validate_store_directory(target).ok());

  // A store cannot be founded through the junction either.
  OpenOptions options{};
  options.store_directory = link;
  options.create_if_missing = true;
  ManualClock clock;
  SyntheticAdapter adapter(GeneratorId::parse("gen-unit-1").value(), clock,
                           SyntheticAdapter::Options{});
  const Result<std::unique_ptr<GeneratorControlEngine>> opened =
      GeneratorControlEngine::open(options, clock, &adapter);
  CHECK_RESULT_CODE(opened, ErrorCode::PathReparsePoint);
}

}  // namespace
