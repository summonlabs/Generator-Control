// Generator Control - shared test fixtures.
//
// A Lab is one store, one controller incarnation, one synthetic generating set and
// one deterministic clock. Every fixture is built through the public API only: the
// tests exercise the same surface a deployment would.
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "genctl/adapter.hpp"
#include "genctl/authority.hpp"
#include "genctl/engine.hpp"
#include "genctl/platform.hpp"
#include "test_framework.hpp"

namespace gctest {

using namespace genctl;

// A scratch directory under the system temporary directory, removed on destruction.
class ScratchDir {
 public:
  explicit ScratchDir(const std::string& label);
  ~ScratchDir();
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;

  [[nodiscard]] const std::string& path() const noexcept { return path_; }
  [[nodiscard]] std::string child(const std::string& name) const { return path_ + "\\" + name; }
  void reset();

 private:
  std::string path_{};
};

// Waits for a file to appear. This is cooperation between two processes, not a test
// timeout: a missing file is reported as a failure of the scenario, never as a pass.
[[nodiscard]] bool wait_for_file(const std::string& path, int attempts = 2000);

struct LabConfig {
  std::string label{"lab"};
  bool seed_checks{true};
  bool seed_resources{true};
  bool seed_sync{false};
  bool seed_transfer{false};
  bool persist_device{true};
  bool allow_synthetic{true};
  Millis start_millis{1'700'000'000'000LL};
  EngineConfig engine{};
};

class Lab {
 public:
  explicit Lab(const LabConfig& config = LabConfig{});
  ~Lab();

  Lab(const Lab&) = delete;
  Lab& operator=(const Lab&) = delete;

  [[nodiscard]] GeneratorControlEngine& engine() { return *engine_; }
  [[nodiscard]] SyntheticAdapter& adapter() { return *adapter_; }
  [[nodiscard]] ManualClock& clock() { return clock_; }
  [[nodiscard]] const GeneratorId& generator() const { return generator_; }
  [[nodiscard]] const std::string& store() const { return store_; }
  [[nodiscard]] const std::string& journal() const { return journal_; }

  [[nodiscard]] EpochMillis now() const { return clock_.now_millis(); }
  [[nodiscard]] StateRevision revision();
  [[nodiscard]] GeneratorState state();
  [[nodiscard]] OpenOptions open_options() const;

  void seed_checks();
  void seed_resources();
  void seed_sync();
  void seed_transfer();

  // Builds a request planned against the current controller and revision.
  [[nodiscard]] OperationRequest request(OperationKind operation, const std::string& key,
                                         AuthorityClass cls = AuthorityClass::Normal,
                                         const std::string& reason = {});

  [[nodiscard]] Status record_check(CheckKind kind, EvidenceState state, EvidenceSource source,
                                    TypedValue value = TypedValue{TypedValueKind::Boolean, 1},
                                    Millis max_age = 60 * kMillisPerSecond);
  [[nodiscard]] Status record_sync(SyncPreconditionKind kind, EvidenceState state,
                                   TypedValue value, EvidenceSource source);
  [[nodiscard]] Status record_transfer(TransferPreconditionKind kind, EvidenceState state,
                                       TypedValue value, EvidenceSource source);
  [[nodiscard]] Result<AttemptRecord> start(const std::string& key);
  [[nodiscard]] Result<AttemptRecord> stop(const std::string& key);

  void set_authority(AuthorityClass cls, const std::string& granted_by, EpochMillis valid_until,
                     const std::string& reason = {});

  [[nodiscard]] const std::string& label() const { return config_.label; }

 private:
  void close();

  LabConfig config_{};
  ScratchDir directory_;
  std::string store_{};
  std::string journal_{};
  ManualClock clock_;
  GeneratorId generator_{};
  std::unique_ptr<SyntheticAdapter> adapter_{};
  std::unique_ptr<GeneratorControlEngine> engine_{};
};

// Convenience: run a request and return its status code, discarding the record.
[[nodiscard]] ErrorCode attempt_code(GeneratorControlEngine& engine,
                                     const OperationRequest& request);

}  // namespace gctest
