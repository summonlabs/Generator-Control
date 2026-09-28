// Generator Control - narrow native operating system abstractions.
//
// Everything platform specific in this runtime lives behind these functions. The
// public headers stay OS free; only the translation units that implement these
// declarations include windows.h or POSIX headers.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "genctl/digest.hpp"
#include "genctl/result.hpp"

namespace genctl::platform {

// Cross-process writer exclusion backed by a real OS primitive:
//   * Windows: LockFileEx with LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY
//   * POSIX:   flock(LOCK_EX | LOCK_NB)
// The lock is released by the operating system when the owning process dies, so a
// killed writer never leaves the store permanently locked.
class ExclusiveFileLock {
 public:
  ExclusiveFileLock() = default;
  ~ExclusiveFileLock();

  ExclusiveFileLock(const ExclusiveFileLock&) = delete;
  ExclusiveFileLock& operator=(const ExclusiveFileLock&) = delete;
  ExclusiveFileLock(ExclusiveFileLock&& other) noexcept;
  ExclusiveFileLock& operator=(ExclusiveFileLock&& other) noexcept;

  [[nodiscard]] static Result<ExclusiveFileLock> try_acquire(const std::string& path_utf8);

  [[nodiscard]] bool held() const noexcept { return handle_ != nullptr; }
  [[nodiscard]] const std::string& path() const noexcept { return path_; }
  Status release();

 private:
  void* handle_{nullptr};
  std::string path_{};
};

// Creates the directory (and parents) when missing. Refuses to accept a regular
// file in place of a directory.
[[nodiscard]] Status ensure_directory(const std::string& path_utf8);
// Creates the parent directory of a file path that is about to be written.
[[nodiscard]] Status ensure_directory_parent(const std::string& path_utf8);

[[nodiscard]] Result<bool> path_exists(const std::string& path_utf8);
[[nodiscard]] Result<bool> is_directory(const std::string& path_utf8);
[[nodiscard]] Result<bool> is_reparse_point(const std::string& path_utf8);

// Writes a file and flushes it to durable storage before returning. When
// exclusive_create is true the call fails if the file already exists.
[[nodiscard]] Status durable_write_file(const std::string& path_utf8, const ByteBuffer& content,
                                        bool exclusive_create);
// Flushes an already open file to durable storage. Used after appending.
[[nodiscard]] Status append_durable(const std::string& path_utf8, const ByteBuffer& content);

// Reads at most max_bytes. Refuses to allocate beyond the bound: a file larger than
// the bound is reported as StoreOversize rather than read.
[[nodiscard]] Result<ByteBuffer> read_file_bounded(const std::string& path_utf8,
                                                   std::size_t max_bytes);

// Atomically replaces target with source (rename with replace semantics) and makes
// the rename itself durable.
[[nodiscard]] Status atomic_replace(const std::string& source_utf8, const std::string& target_utf8);
// Makes a directory entry change durable where the platform supports it.
[[nodiscard]] Status flush_directory(const std::string& path_utf8);
[[nodiscard]] Status remove_file(const std::string& path_utf8);
[[nodiscard]] Status remove_directory_tree(const std::string& path_utf8);

[[nodiscard]] Result<std::vector<std::string>> list_directory(const std::string& path_utf8);
[[nodiscard]] Result<std::uint64_t> file_size(const std::string& path_utf8);

[[nodiscard]] std::uint32_t current_process_id() noexcept;
// Bounded, non-secret, deterministic-per-process token identifying this writer.
[[nodiscard]] std::string current_process_token();

// Immediately terminates this process without running destructors, atexit handlers
// or any interactive error reporting. Used to inject process death in crash tests.
[[noreturn]] void terminate_process_now(int code) noexcept;

// Resolves a path to an absolute, normalised form.
[[nodiscard]] Result<std::string> absolute_path(const std::string& path_utf8);

// ---------------------------------------------------------------------------
// Independent process execution
//
// Used by the validation suite to prove cross-process writer exclusion, process
// death release and crash/reopen handoff with real, independent processes. Child
// standard output is redirected to a file so that no pipe is involved, and child
// standard input is not connected at all: a child can never block on input.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::string> current_executable_path();
// The operating system's temporary directory. Never used for authoritative state.
[[nodiscard]] Result<std::string> temporary_directory();

struct ChildProcess {
  void* handle{nullptr};
  std::uint32_t process_id{0};

  [[nodiscard]] bool valid() const noexcept { return handle != nullptr; }
};

// Starts a process and returns immediately. The child's standard output and
// standard error are written to output_path when it is non-empty.
[[nodiscard]] Result<ChildProcess> spawn_process(const std::string& executable_utf8,
                                                 const std::vector<std::string>& arguments,
                                                 const std::string& output_path_utf8);

// Waits for the process to finish and returns its exit code.
[[nodiscard]] Result<int> wait_process(ChildProcess& child);

// Terminates the process immediately. Used to inject abrupt death at a point the
// child controls.
[[nodiscard]] Status kill_process(ChildProcess& child);

[[nodiscard]] Status close_process(ChildProcess& child);

// Convenience: spawn, wait, close.
[[nodiscard]] Result<int> run_process(const std::string& executable_utf8,
                                      const std::vector<std::string>& arguments,
                                      const std::string& output_path_utf8);

// True when a process with this identifier is still running.
[[nodiscard]] bool process_is_running(std::uint32_t process_id);

}  // namespace genctl::platform
