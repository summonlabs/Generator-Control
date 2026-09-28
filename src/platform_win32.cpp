// Generator Control - Win32 platform implementation.
//
// Only this translation unit (and path_safety.cpp) includes windows.h. The runtime
// uses LockFileEx for cross-process writer exclusion, MoveFileEx with
// MOVEFILE_WRITE_THROUGH for the atomic publication step, and FlushFileBuffers for
// durable content. Nothing here is interactive and nothing reports errors through a
// dialog: injected process death uses TerminateProcess, which cannot raise Windows
// Error Reporting.
#include "genctl/platform.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace genctl::platform {
namespace {

Status last_error_status(ErrorCode code, const char* what) {
  const DWORD error = ::GetLastError();
  return make_status(code, std::string(what) + " failed with Win32 error " +
                               std::to_string(static_cast<unsigned long>(error)));
}

Result<std::wstring> to_wide(const std::string& text) {
  if (text.empty()) return std::wstring{};
  if (text.size() > 32767) {
    return make_status(ErrorCode::PathTooLong, ValidationStage::Format,
                       "path exceeds the Windows wide-character limit");
  }
  const int needed = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
  if (needed <= 0) {
    return make_status(ErrorCode::PathEncodingInvalid, ValidationStage::Format,
                       "path is not valid UTF-8");
  }
  std::wstring out(static_cast<std::size_t>(needed), L'\0');
  const int written = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                            static_cast<int>(text.size()), out.data(), needed);
  if (written != needed) {
    return make_status(ErrorCode::PathEncodingInvalid, ValidationStage::Format,
                       "path UTF-8 conversion failed");
  }
  return out;
}

Result<std::string> to_utf8(const std::wstring& text) {
  if (text.empty()) return std::string{};
  const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
  if (needed <= 0) {
    return make_status(ErrorCode::PathEncodingInvalid, ValidationStage::Format,
                       "path is not convertible to UTF-8");
  }
  std::string out(static_cast<std::size_t>(needed), '\0');
  const int written = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                            out.data(), needed, nullptr, nullptr);
  if (written != needed) {
    return make_status(ErrorCode::PathEncodingInvalid, ValidationStage::Format,
                       "path UTF-8 conversion failed");
  }
  return out;
}

bool directory_exists(const std::wstring& path) {
  const DWORD attributes = ::GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring strip_trailing_separators(std::wstring path) {
  while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) {
    path.pop_back();
  }
  return path;
}

}  // namespace

ExclusiveFileLock::~ExclusiveFileLock() { (void)release(); }

ExclusiveFileLock::ExclusiveFileLock(ExclusiveFileLock&& other) noexcept
    : handle_(other.handle_), path_(std::move(other.path_)) {
  other.handle_ = nullptr;
  other.path_.clear();
}

ExclusiveFileLock& ExclusiveFileLock::operator=(ExclusiveFileLock&& other) noexcept {
  if (this != &other) {
    (void)release();
    handle_ = other.handle_;
    path_ = std::move(other.path_);
    other.handle_ = nullptr;
    other.path_.clear();
  }
  return *this;
}

Result<ExclusiveFileLock> ExclusiveFileLock::try_acquire(const std::string& path_utf8) {
  GENCTL_TRY_ASSIGN(wide, to_wide(path_utf8));
  HANDLE handle = ::CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return last_error_status(ErrorCode::StoreOpenFailed, "opening the lock file");
  }
  OVERLAPPED overlapped{};
  if (::LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0,
                   &overlapped) == 0) {
    const DWORD error = ::GetLastError();
    ::CloseHandle(handle);
    if (error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING) {
      return make_status(ErrorCode::StoreLocked, ValidationStage::Persistence,
                         "another process holds the writer lock for this store");
    }
    return make_status(ErrorCode::StoreOpenFailed,
                       std::string("locking the store failed with Win32 error ") +
                           std::to_string(static_cast<unsigned long>(error)));
  }
  ExclusiveFileLock lock;
  lock.handle_ = handle;
  lock.path_ = path_utf8;
  return lock;
}

Status ExclusiveFileLock::release() {
  if (handle_ == nullptr) return Status::success();
  OVERLAPPED overlapped{};
  (void)::UnlockFileEx(static_cast<HANDLE>(handle_), 0, 1, 0, &overlapped);
  ::CloseHandle(static_cast<HANDLE>(handle_));
  handle_ = nullptr;
  return Status::success();
}

Status ensure_directory(const std::string& path_utf8) {
  GENCTL_TRY_ASSIGN(wide, to_wide(path_utf8));
  std::wstring current = strip_trailing_separators(wide);
  if (current.empty()) {
    return make_status(ErrorCode::PathInvalid, ValidationStage::Format,
                       "directory path must not be empty");
  }
  std::size_t position = 0;
  // Preserve a UNC prefix or a drive designator.
  if (current.size() >= 2 && current[1] == L':') {
    position = 2;
    if (current.size() > 2 && (current[2] == L'\\' || current[2] == L'/')) position = 3;
  } else if (current.size() >= 2 && current[0] == L'\\' && current[1] == L'\\') {
    position = 2;
  }
  while (position <= current.size()) {
    const std::size_t next = current.find_first_of(L"\\/", position);
    const std::size_t end = next == std::wstring::npos ? current.size() : next;
    if (end == 0) {
      position = end + 1;
      continue;
    }
    const std::wstring prefix = current.substr(0, end);
    if (!directory_exists(prefix)) {
      if (::CreateDirectoryW(prefix.c_str(), nullptr) == 0) {
        const DWORD error = ::GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
          return make_status(ErrorCode::StoreOpenFailed,
                             "creating directory failed with Win32 error " +
                                 std::to_string(static_cast<unsigned long>(error)));
        }
      }
    }
    if (next == std::wstring::npos) break;
    position = next + 1;
  }
  GENCTL_TRY_ASSIGN(ok, is_directory(path_utf8));
  if (!ok) {
    return make_status(ErrorCode::PathNotDirectory, ValidationStage::Format,
                       "path exists but is not a directory");
  }
  return Status::success();
}

Status ensure_directory_parent(const std::string& path_utf8) {
  const std::size_t split = path_utf8.find_last_of("\\/");
  if (split == std::string::npos) return Status::success();
  if (split == 0) return Status::success();
  return ensure_directory(path_utf8.substr(0, split));
}

Result<bool> path_exists(const std::string& path_utf8) {
  GENCTL_TRY_ASSIGN(wide, to_wide(path_utf8));
  const DWORD attributes = ::GetFileAttributesW(wide.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    const DWORD error = ::GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return false;
    return false;
  }
  return true;
}

Result<bool> is_directory(const std::string& path_utf8) {
  GENCTL_TRY_ASSIGN(wide, to_wide(path_utf8));
  const DWORD attributes = ::GetFileAttributesW(wide.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) return false;
  return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

Result<bool> is_reparse_point(const std::string& path_utf8) {
  GENCTL_TRY_ASSIGN(wide, to_wide(path_utf8));
  const DWORD attributes = ::GetFileAttributesW(wide.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) return false;
  return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

Status durable_write_file(const std::string& path_utf8, const ByteBuffer& content,
                          bool exclusive_create) {
  GENCTL_TRY_ASSIGN(wide, to_wide(path_utf8));
  const DWORD disposition = exclusive_create ? CREATE_NEW : CREATE_ALWAYS;
  HANDLE handle = ::CreateFileW(wide.c_str(), GENERIC_WRITE, 0, nullptr, disposition,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return last_error_status(exclusive_create ? ErrorCode::StorePublicationFailed
                                              : ErrorCode::StoreOpenFailed,
                             "creating a store file");
  }
  std::size_t written_total = 0;
  while (written_total < content.size()) {
    const DWORD chunk = static_cast<DWORD>(
        (content.size() - written_total) > 0x40000000u ? 0x40000000u : (content.size() - written_total));
    DWORD written = 0;
    if (::WriteFile(handle, content.data() + written_total, chunk, &written, nullptr) == 0) {
      const Status failure = last_error_status(ErrorCode::StorePublicationFailed, "writing");
      ::CloseHandle(handle);
      return failure;
    }
    if (written == 0) {
      ::CloseHandle(handle);
      return make_status(ErrorCode::StorePublicationFailed, ValidationStage::Persistence,
                         "writing the store file made no progress");
    }
    written_total += written;
  }
  if (::FlushFileBuffers(handle) == 0) {
    const Status failure = last_error_status(ErrorCode::StoreFlushFailed, "flushing");
    ::CloseHandle(handle);
    return failure;
  }
  ::CloseHandle(handle);
  return Status::success();
}

Status append_durable(const std::string& path_utf8, const ByteBuffer& content) {
  GENCTL_TRY_ASSIGN(wide, to_wide(path_utf8));
  HANDLE handle = ::CreateFileW(wide.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return last_error_status(ErrorCode::StoreOpenFailed, "opening a file for append");
  }
  std::size_t written_total = 0;
  while (written_total < content.size()) {
    const DWORD chunk = static_cast<DWORD>(content.size() - written_total);
    DWORD written = 0;
    if (::WriteFile(handle, content.data() + written_total, chunk, &written, nullptr) == 0) {
      const Status failure = last_error_status(ErrorCode::StorePublicationFailed, "appending");
      ::CloseHandle(handle);
      return failure;
    }
    written_total += written;
  }
  if (::FlushFileBuffers(handle) == 0) {
    const Status failure = last_error_status(ErrorCode::StoreFlushFailed, "flushing an append");
    ::CloseHandle(handle);
    return failure;
  }
  ::CloseHandle(handle);
  return Status::success();
}

Result<ByteBuffer> read_file_bounded(const std::string& path_utf8, std::size_t max_bytes) {
  GENCTL_TRY_ASSIGN(wide, to_wide(path_utf8));
  HANDLE handle =
      ::CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return last_error_status(ErrorCode::StoreOpenFailed, "opening a store file for reading");
  }
  LARGE_INTEGER size{};
  if (::GetFileSizeEx(handle, &size) == 0) {
    const Status failure = last_error_status(ErrorCode::StoreOpenFailed, "reading the file size");
    ::CloseHandle(handle);
    return failure;
  }
  if (size.QuadPart < 0) {
    ::CloseHandle(handle);
    return make_status(ErrorCode::StoreCorrupt, ValidationStage::Persistence,
                       "file reports a negative size");
  }
  if (static_cast<std::uint64_t>(size.QuadPart) > max_bytes) {
    ::CloseHandle(handle);
    return make_status(ErrorCode::StoreOversize, ValidationStage::Persistence,
                       "file of " + std::to_string(size.QuadPart) +
                           " bytes exceeds the configured maximum of " +
                           std::to_string(max_bytes) + " bytes");
  }
  ByteBuffer buffer(static_cast<std::size_t>(size.QuadPart));
  std::size_t read_total = 0;
  while (read_total < buffer.size()) {
    const DWORD chunk = static_cast<DWORD>(buffer.size() - read_total);
    DWORD read = 0;
    if (::ReadFile(handle, buffer.data() + read_total, chunk, &read, nullptr) == 0) {
      const Status failure = last_error_status(ErrorCode::StoreOpenFailed, "reading");
      ::CloseHandle(handle);
      return failure;
    }
    if (read == 0) break;
    read_total += read;
  }
  ::CloseHandle(handle);
  buffer.resize(read_total);
  return buffer;
}

Status atomic_replace(const std::string& source_utf8, const std::string& target_utf8) {
  GENCTL_TRY_ASSIGN(source, to_wide(source_utf8));
  GENCTL_TRY_ASSIGN(target, to_wide(target_utf8));
  if (::MoveFileExW(source.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
    return last_error_status(ErrorCode::StorePublicationFailed, "atomically replacing");
  }
  return Status::success();
}

Status flush_directory(const std::string& path_utf8) {
  // Windows does not expose a portable directory fsync. The publication step uses
  // MOVEFILE_WRITE_THROUGH, which forces the rename itself to disk before it
  // returns, so the commit point is durable without a directory flush.
  (void)path_utf8;
  return Status::success();
}

Status remove_file(const std::string& path_utf8) {
  GENCTL_TRY_ASSIGN(wide, to_wide(path_utf8));
  if (::DeleteFileW(wide.c_str()) == 0) {
    const DWORD error = ::GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return Status::success();
    return make_status(ErrorCode::StorePublicationFailed,
                       "deleting a store file failed with Win32 error " +
                           std::to_string(static_cast<unsigned long>(error)));
  }
  return Status::success();
}

Status remove_directory_tree(const std::string& path_utf8) {
  GENCTL_TRY_ASSIGN(entries, list_directory(path_utf8));
  for (const auto& name : entries) {
    const std::string child = path_utf8 + "\\" + name;
    GENCTL_TRY_ASSIGN(child_is_dir, is_directory(child));
    if (child_is_dir) {
      GENCTL_TRY(remove_directory_tree(child));
    } else {
      GENCTL_TRY(remove_file(child));
    }
  }
  GENCTL_TRY_ASSIGN(wide, to_wide(path_utf8));
  if (::RemoveDirectoryW(wide.c_str()) == 0) {
    const DWORD error = ::GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return Status::success();
    return make_status(ErrorCode::StorePublicationFailed,
                       "removing a directory failed with Win32 error " +
                           std::to_string(static_cast<unsigned long>(error)));
  }
  return Status::success();
}

Result<std::vector<std::string>> list_directory(const std::string& path_utf8) {
  GENCTL_TRY_ASSIGN(wide, to_wide(path_utf8 + "\\*"));
  WIN32_FIND_DATAW data{};
  HANDLE handle = ::FindFirstFileW(wide.c_str(), &data);
  std::vector<std::string> out;
  if (handle == INVALID_HANDLE_VALUE) {
    const DWORD error = ::GetLastError();
    if (error == ERROR_FILE_NOT_FOUND) return out;
    return make_status(ErrorCode::StoreOpenFailed,
                       "listing a directory failed with Win32 error " +
                           std::to_string(static_cast<unsigned long>(error)));
  }
  do {
    const std::wstring name(data.cFileName);
    if (name == L"." || name == L"..") continue;
    GENCTL_TRY_ASSIGN(utf8, to_utf8(name));
    out.push_back(utf8);
  } while (::FindNextFileW(handle, &data) != 0);
  ::FindClose(handle);
  return out;
}

Result<std::uint64_t> file_size(const std::string& path_utf8) {
  GENCTL_TRY_ASSIGN(wide, to_wide(path_utf8));
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (::GetFileAttributesExW(wide.c_str(), GetFileExInfoStandard, &data) == 0) {
    return last_error_status(ErrorCode::StoreOpenFailed, "reading file attributes");
  }
  const std::uint64_t size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) |
                             static_cast<std::uint64_t>(data.nFileSizeLow);
  return size;
}

std::uint32_t current_process_id() noexcept {
  return static_cast<std::uint32_t>(::GetCurrentProcessId());
}

std::string current_process_token() {
  return "pid-" + std::to_string(current_process_id());
}

void terminate_process_now(int code) noexcept {
  // TerminateProcess cannot raise Windows Error Reporting and runs no destructors,
  // no atexit handlers and no static destructors: process death is immediate.
  ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(code));
  // Unreachable in practice; if the call is refused, leave immediately anyway.
  ::_exit(code);
}

namespace {

std::wstring quote_argument(const std::wstring& argument) {
  // Minimal, correct Windows command line quoting.
  std::wstring out = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t c : argument) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    if (c == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
      backslashes = 0;
      continue;
    }
    out.append(backslashes, L'\\');
    backslashes = 0;
    out.push_back(c);
  }
  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

}  // namespace

Result<std::string> temporary_directory() {
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const DWORD written = ::GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
    if (written == 0) {
      return make_status(ErrorCode::PathInvalid, ValidationStage::Internal,
                         "cannot determine the temporary directory");
    }
    if (written < buffer.size()) {
      buffer.resize(written);
      break;
    }
    if (buffer.size() > 32768) {
      return make_status(ErrorCode::PathTooLong, ValidationStage::Internal,
                         "temporary directory path is unreasonably long");
    }
    buffer.resize(buffer.size() * 2);
  }
  return to_utf8(buffer);
}

Result<std::string> current_executable_path() {
  std::wstring buffer(1024, L'\0');
  for (;;) {
    const DWORD written = ::GetModuleFileNameW(nullptr, buffer.data(),
                                               static_cast<DWORD>(buffer.size()));
    if (written == 0) {
      return make_status(ErrorCode::Internal, ValidationStage::Internal,
                         "cannot determine the current executable path");
    }
    if (written < buffer.size()) {
      buffer.resize(written);
      break;
    }
    if (buffer.size() > 32768) {
      return make_status(ErrorCode::PathTooLong, ValidationStage::Internal,
                         "current executable path is unreasonably long");
    }
    buffer.resize(buffer.size() * 2);
  }
  return to_utf8(buffer);
}

Result<ChildProcess> spawn_process(const std::string& executable_utf8,
                                   const std::vector<std::string>& arguments,
                                   const std::string& output_path_utf8) {
  GENCTL_TRY_ASSIGN(executable, to_wide(executable_utf8));
  std::wstring command_line = quote_argument(executable);
  for (const auto& argument : arguments) {
    GENCTL_TRY_ASSIGN(wide, to_wide(argument));
    command_line.push_back(L' ');
    command_line += quote_argument(wide);
  }
  std::vector<wchar_t> mutable_line(command_line.begin(), command_line.end());
  mutable_line.push_back(L'\0');

  HANDLE output = INVALID_HANDLE_VALUE;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  if (!output_path_utf8.empty()) {
    GENCTL_TRY_ASSIGN(output_path, to_wide(output_path_utf8));
    output = ::CreateFileW(output_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
      return make_status(ErrorCode::StoreOpenFailed, ValidationStage::Internal,
                         "cannot open the child output file");
    }
    startup.dwFlags |= STARTF_USESTDHANDLES;
    startup.hStdOutput = output;
    startup.hStdError = output;
    startup.hStdInput = INVALID_HANDLE_VALUE;
  }

  PROCESS_INFORMATION information{};
  const BOOL created = ::CreateProcessW(nullptr, mutable_line.data(), nullptr, nullptr,
                                        output_path_utf8.empty() ? FALSE : TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                                        &information);
  if (output != INVALID_HANDLE_VALUE) ::CloseHandle(output);
  if (created == 0) {
    return make_status(ErrorCode::Internal, ValidationStage::Internal,
                       "cannot start a child process (Win32 error " +
                           std::to_string(static_cast<unsigned long>(::GetLastError())) + ")");
  }
  ::CloseHandle(information.hThread);
  ChildProcess child{};
  child.handle = information.hProcess;
  child.process_id = static_cast<std::uint32_t>(information.dwProcessId);
  return child;
}

Result<int> wait_process(ChildProcess& child) {
  if (!child.valid()) {
    return make_status(ErrorCode::InvalidArgument, ValidationStage::Internal,
                       "no child process handle");
  }
  const DWORD waited = ::WaitForSingleObject(static_cast<HANDLE>(child.handle), INFINITE);
  if (waited != WAIT_OBJECT_0) {
    return make_status(ErrorCode::Internal, ValidationStage::Internal,
                       "waiting for a child process failed");
  }
  DWORD code = 0;
  if (::GetExitCodeProcess(static_cast<HANDLE>(child.handle), &code) == 0) {
    return make_status(ErrorCode::Internal, ValidationStage::Internal,
                       "cannot read a child process exit code");
  }
  return static_cast<int>(code);
}

Status kill_process(ChildProcess& child) {
  if (!child.valid()) return Status::success();
  if (::TerminateProcess(static_cast<HANDLE>(child.handle), 137) == 0) {
    const DWORD error = ::GetLastError();
    if (error != ERROR_ACCESS_DENIED) {
      return make_status(ErrorCode::Internal, ValidationStage::Internal,
                         "terminating a child process failed with Win32 error " +
                             std::to_string(static_cast<unsigned long>(error)));
    }
  }
  (void)::WaitForSingleObject(static_cast<HANDLE>(child.handle), INFINITE);
  return Status::success();
}

Status close_process(ChildProcess& child) {
  if (!child.valid()) return Status::success();
  ::CloseHandle(static_cast<HANDLE>(child.handle));
  child.handle = nullptr;
  return Status::success();
}

Result<int> run_process(const std::string& executable_utf8,
                        const std::vector<std::string>& arguments,
                        const std::string& output_path_utf8) {
  GENCTL_TRY_ASSIGN(child, spawn_process(executable_utf8, arguments, output_path_utf8));
  GENCTL_TRY_ASSIGN(code, wait_process(child));
  GENCTL_TRY(close_process(child));
  return code;
}

bool process_is_running(std::uint32_t process_id) {
  HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
  if (handle == nullptr) return false;
  DWORD code = 0;
  const BOOL ok = ::GetExitCodeProcess(handle, &code);
  ::CloseHandle(handle);
  return ok != 0 && code == STILL_ACTIVE;
}

Result<std::string> absolute_path(const std::string& path_utf8) {
  GENCTL_TRY_ASSIGN(wide, to_wide(path_utf8));
  const DWORD needed = ::GetFullPathNameW(wide.c_str(), 0, nullptr, nullptr);
  if (needed == 0) {
    return last_error_status(ErrorCode::PathInvalid, "resolving the absolute path");
  }
  if (needed > 32767) {
    return make_status(ErrorCode::PathTooLong, ValidationStage::Format,
                       "resolved path exceeds the Windows limit");
  }
  std::wstring buffer(static_cast<std::size_t>(needed), L'\0');
  const DWORD written = ::GetFullPathNameW(wide.c_str(), needed, buffer.data(), nullptr);
  if (written == 0 || written >= needed) {
    return last_error_status(ErrorCode::PathInvalid, "resolving the absolute path");
  }
  buffer.resize(written);
  return to_utf8(buffer);
}

}  // namespace genctl::platform
