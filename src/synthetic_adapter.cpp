// Generator Control - deterministic synthetic generating set.
//
// This adapter is a simulator. It exists so the control semantics of the runtime
// can be exercised and proven without hardware, and every report it feeds is
// labelled SYNTHETIC. It models a single standby set: cranking, warm-up, ready,
// externally operated breaker, cooldown and stop.
//
// Its actuation journal is append-only and flushed to durable storage, so an
// independent process can count exactly how many commands were issued across a
// crash and a restart. That is what makes "no duplicate external actuation" a
// measurable claim rather than an assertion.
#include "genctl/adapter.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "genctl/platform.hpp"

namespace genctl {
namespace {

std::string join_line(const std::vector<std::string>& fields) {
  std::string out;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (i > 0) out.push_back('|');
    out += fields[i];
  }
  return out;
}

std::vector<std::string> split_line(const std::string& line) {
  std::vector<std::string> out;
  std::string current;
  for (const char c : line) {
    if (c == '|') {
      out.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  out.push_back(current);
  return out;
}

std::string state_path_for(const std::string& journal_path) {
  return journal_path.empty() ? std::string{} : journal_path + ".state";
}

std::string encode_state(bool cranking, bool running, bool fuel_valve, BreakerPosition breaker,
                         bool synchronized, EpochMillis start_at, EpochMillis stop_at) {
  std::string out;
  out += "version=1\n";
  out += std::string("cranking=") + (cranking ? "1" : "0") + "\n";
  out += std::string("running=") + (running ? "1" : "0") + "\n";
  out += std::string("fuel-valve=") + (fuel_valve ? "1" : "0") + "\n";
  out += std::string("breaker=") + std::string(to_string(breaker)) + "\n";
  out += std::string("synchronized=") + (synchronized ? "1" : "0") + "\n";
  out += "start-at=" + std::to_string(start_at) + "\n";
  out += "stop-at=" + std::to_string(stop_at) + "\n";
  return out;
}

}  // namespace

SyntheticAdapter::SyntheticAdapter(GeneratorId generator, const Clock& clock, Options options)
    : generator_(std::move(generator)), clock_(&clock), options_(std::move(options)) {
  faults_ = options_.faults;
  breaker_ = options_.initial_breaker;
  running_ = options_.initial_running;
}

AdapterDescriptor SyntheticAdapter::describe() const {
  AdapterDescriptor descriptor{};
  descriptor.name = "synthetic-generating-set";
  descriptor.vendor = "Summon Software Labs";
  descriptor.model = "synthetic-standby-set";
  descriptor.hardware_generation = options_.profile.hardware_generation;
  descriptor.synthetic = true;
  descriptor.supports_observation = true;
  descriptor.supports_synchronization = true;
  descriptor.supports_transfer_request = true;
  return descriptor;
}

Status SyntheticAdapter::open() {
  if (!options_.journal_path.empty()) {
    GENCTL_TRY(platform::ensure_directory_parent(options_.journal_path));
    const std::string state_path = state_path_for(options_.journal_path);
    GENCTL_TRY_ASSIGN(exists, platform::path_exists(state_path));
    if (exists) {
      GENCTL_TRY_ASSIGN(bytes, platform::read_file_bounded(state_path, 64u * 1024u));
      const std::string text(bytes.begin(), bytes.end());
      std::size_t offset = 0;
      while (offset < text.size()) {
        const std::size_t end = text.find('\n', offset);
        const std::string line =
            text.substr(offset, end == std::string::npos ? std::string::npos : end - offset);
        offset = end == std::string::npos ? text.size() : end + 1;
        const std::size_t split = line.find('=');
        if (split == std::string::npos) continue;
        const std::string key = line.substr(0, split);
        const std::string value = line.substr(split + 1);
        if (key == "cranking") cranking_ = value == "1";
        else if (key == "running") running_ = value == "1";
        else if (key == "fuel-valve") fuel_valve_open_ = value == "1";
        else if (key == "synchronized") synchronized_ = value == "1";
        else if (key == "breaker") {
          if (value == "open") breaker_ = BreakerPosition::Open;
          else if (value == "closed") breaker_ = BreakerPosition::Closed;
          else if (value == "between") breaker_ = BreakerPosition::Between;
          else if (value == "faulted") breaker_ = BreakerPosition::Faulted;
          else breaker_ = BreakerPosition::Unknown;
        } else if (key == "start-at") {
          start_accepted_at_ = std::strtoll(value.c_str(), nullptr, 10);
        } else if (key == "stop-at") {
          stop_accepted_at_ = std::strtoll(value.c_str(), nullptr, 10);
        }
      }
    }
  }
  open_ = true;
  return Status::success();
}

Status SyntheticAdapter::persist_state() const {
  if (options_.journal_path.empty()) return Status::success();
  const std::string text = encode_state(cranking_, running_, fuel_valve_open_, breaker_,
                                        synchronized_, start_accepted_at_, stop_accepted_at_);
  const ByteBuffer bytes(text.begin(), text.end());
  return platform::durable_write_file(state_path_for(options_.journal_path), bytes, false);
}

Status SyntheticAdapter::close() {
  open_ = false;
  return Status::success();
}

Result<std::size_t> SyntheticAdapter::replay_journal() {
  actuations_.clear();
  issued_count_ = 0;
  accepted_count_ = 0;
  rejected_count_ = 0;
  if (options_.journal_path.empty()) return std::size_t{0};

  Result<bool> exists = platform::path_exists(options_.journal_path);
  if (!exists.ok()) return exists.status();
  if (!exists.value()) return std::size_t{0};

  GENCTL_TRY_ASSIGN(bytes, platform::read_file_bounded(options_.journal_path, 4u * 1024u * 1024u));
  const std::string text(bytes.begin(), bytes.end());
  std::size_t offset = 0;
  while (offset < text.size()) {
    const std::size_t end = text.find('\n', offset);
    const std::string line =
        text.substr(offset, end == std::string::npos ? std::string::npos : end - offset);
    offset = end == std::string::npos ? text.size() : end + 1;
    if (line.empty()) continue;
    const std::vector<std::string> fields = split_line(line);
    if (fields.size() < 5) continue;
    ActuationRecord record{};
    record.command_id = CommandId{std::stoull(fields[1])};
    record.attempt_id = AttemptId{std::stoull(fields[2])};
    record.operation = parse_operation_kind(fields[3]).value_or(OperationKind::Unknown);
    const std::string& status = fields[4];
    if (status == "accepted") {
      record.status = AdapterAckStatus::Accepted;
      ++accepted_count_;
    } else if (status == "rejected") {
      record.status = AdapterAckStatus::Rejected;
      ++rejected_count_;
    } else if (status == "busy") {
      record.status = AdapterAckStatus::Busy;
      ++rejected_count_;
    } else if (status == "unsupported") {
      record.status = AdapterAckStatus::Unsupported;
      ++rejected_count_;
    } else {
      record.status = AdapterAckStatus::Invalid;
      ++rejected_count_;
    }
    if (fields.size() > 5) record.at = std::stoll(fields[5]);
    ++issued_count_;
    actuations_.push_back(record);
  }
  return actuations_.size();
}

Status SyntheticAdapter::append_journal(const ActuationRecord& record) {
  if (options_.journal_path.empty()) return Status::success();
  const std::string line =
      join_line({generator_.str(), std::to_string(record.command_id.value()),
                 std::to_string(record.attempt_id.value()),
                 std::string(to_string(record.operation)),
                 std::string(to_string(record.status)), std::to_string(record.at)}) +
      "\n";
  const ByteBuffer bytes(line.begin(), line.end());
  return platform::append_durable(options_.journal_path, bytes);
}

bool SyntheticAdapter::still_cranking(EpochMillis now) const {
  if (!cranking_) return false;
  if (faults_.stuck_cranking) return true;
  const Result<Millis> elapsed = sub_millis(now, start_accepted_at_);
  if (!elapsed.ok()) return true;
  return elapsed.value() < options_.profile.crank_millis;
}

OperatingState SyntheticAdapter::derive_state(EpochMillis now) const {
  if (still_cranking(now)) return OperatingState::Starting;
  if (running_) {
    const Result<Millis> elapsed = sub_millis(now, start_accepted_at_);
    if (elapsed.ok() &&
        elapsed.value() >= options_.profile.crank_millis + options_.profile.warmup_millis) {
      // A device that the external switch actor has paralleled reports the
      // synchronized condition; until then it is ready but unsynchronized.
      return synchronized_ ? OperatingState::Synchronized : OperatingState::ReadyUnsynchronized;
    }
    return OperatingState::WarmUp;
  }
  const Result<Millis> stopped = sub_millis(now, stop_accepted_at_);
  if (stopped.ok() && stopped.value() < options_.profile.cooldown_millis) {
    return OperatingState::Cooldown;
  }
  return OperatingState::Stopped;
}

void SyntheticAdapter::apply_accepted(const CommandRequest& request, EpochMillis now) {
  switch (request.operation) {
    case OperationKind::Start:
    case OperationKind::TestStart:
    case OperationKind::EmergencyStart:
      cranking_ = true;
      running_ = true;
      fuel_valve_open_ = true;
      start_accepted_at_ = now;
      break;
    case OperationKind::Stop:
    case OperationKind::TestStop:
    case OperationKind::EmergencyStop:
      cranking_ = false;
      running_ = false;
      fuel_valve_open_ = false;
      synchronized_ = false;
      stop_accepted_at_ = now;
      break;
    case OperationKind::Synchronize:
    case OperationKind::Desynchronize:
    case OperationKind::TransferToGenerator:
    case OperationKind::TransferToUtility:
      // The device does not and cannot change the external breaker: those actions
      // belong to the facility switch authority and are modelled by
      // external_set_breaker()/external_set_synchronized().
      break;
    case OperationKind::FaultReset:
      break;
    default:
      break;
  }
}

Result<AdapterAck> SyntheticAdapter::issue(const CommandRequest& request) {
  if (!open_) {
    return make_status(ErrorCode::AdapterUnavailable, ValidationStage::Actuation,
                       "synthetic adapter is not open");
  }
  if (request.generator != generator_) {
    return make_status(ErrorCode::UnknownGenerator, ValidationStage::Actuation,
                       "synthetic adapter is bound to a different generator");
  }
  const EpochMillis now = clock_->now_millis();

  ActuationRecord record{};
  record.command_id = request.command_id;
  record.attempt_id = request.attempt_id;
  record.operation = request.operation;
  record.at = now;

  if (faults_.busy) {
    record.status = AdapterAckStatus::Busy;
    ++issued_count_;
    ++rejected_count_;
    actuations_.push_back(record);
    GENCTL_TRY(append_journal(record));
    return AdapterAck{AdapterAckStatus::Busy, request.command_id, now,
                      "device reports itself busy; the command was not applied"};
  }
  if (faults_.reject_next) {
    record.status = AdapterAckStatus::Rejected;
    ++issued_count_;
    ++rejected_count_;
    actuations_.push_back(record);
    GENCTL_TRY(append_journal(record));
    return AdapterAck{AdapterAckStatus::Rejected, request.command_id, now,
                      "device refused the command"};
  }

  switch (request.operation) {
    case OperationKind::Start:
    case OperationKind::Stop:
    case OperationKind::TestStart:
    case OperationKind::TestStop:
    case OperationKind::EmergencyStart:
    case OperationKind::EmergencyStop:
    case OperationKind::FaultReset:
    case OperationKind::Synchronize:
    case OperationKind::Desynchronize:
    case OperationKind::TransferToGenerator:
    case OperationKind::TransferToUtility:
      break;
    default:
      record.status = AdapterAckStatus::Unsupported;
      ++issued_count_;
      ++rejected_count_;
      actuations_.push_back(record);
      GENCTL_TRY(append_journal(record));
      return AdapterAck{AdapterAckStatus::Unsupported, request.command_id, now,
                        "operation is not supported by a standby generating set"};
  }

  record.status = AdapterAckStatus::Accepted;
  ++issued_count_;
  ++accepted_count_;
  actuations_.push_back(record);
  GENCTL_TRY(append_journal(record));

  if (!faults_.ack_without_effect) {
    apply_accepted(request, now);
    GENCTL_TRY(persist_state());
  }
  return AdapterAck{AdapterAckStatus::Accepted, request.command_id, now,
                    faults_.ack_without_effect
                        ? "accepted; the device deliberately does not change state (fault "
                          "injection)"
                        : "accepted"};
}

Result<EngineObservation> SyntheticAdapter::observe(const GeneratorId& generator) {
  if (!open_) {
    return make_status(ErrorCode::AdapterUnavailable, ValidationStage::Observation,
                       "synthetic adapter is not open");
  }
  if (generator != generator_) {
    return make_status(ErrorCode::UnknownGenerator, ValidationStage::Observation,
                       "synthetic adapter is bound to a different generator");
  }
  if (faults_.observation_unavailable) {
    return make_status(ErrorCode::AdapterUnavailable, ValidationStage::Observation,
                       "device observation is unavailable (fault injection)");
  }
  const EpochMillis now = clock_->now_millis();
  ++observation_count_;

  EngineObservation observation{};
  observation.generator = generator_;
  observation.observed_at = now;
  observation.source = EvidenceSource::SyntheticAdapter;
  observation.cranking = still_cranking(now);
  observation.running = running_;
  observation.reported_state = derive_state(now);
  if (faults_.contradictory_observation) {
    observation.reported_state = OperatingState::Synchronized;
    observation.synchronization = SynchronizationState::Synchronized;
    observation.breaker = BreakerPosition::Closed;
    observation.detail = "device reports a state that contradicts the acknowledged command "
                         "(fault injection)";
    return observation;
  }
  observation.breaker = breaker_;
  observation.synchronization = synchronized_ ? SynchronizationState::Synchronized
                                              : SynchronizationState::NotSynchronized;
  observation.coolant_temperature = Temperature{78'000};
  observation.lube_oil_pressure = Pressure{410'000};
  observation.coolant_level = Percent{9'200};
  observation.fuel_valve_open = fuel_valve_open_;
  observation.electrical.line_voltage =
      running_ ? options_.profile.rated_voltage : Voltage{0};
  observation.electrical.bus_voltage = options_.profile.bus_voltage;
  observation.electrical.frequency = running_ ? options_.profile.rated_frequency : Frequency{0};
  observation.electrical.bus_frequency = options_.profile.bus_frequency;
  observation.electrical.phase_angle = options_.profile.phase_angle;
  observation.electrical.load = running_ ? Percent{4'500} : Percent{0};
  observation.detail = "synthetic device report";
  return observation;
}

void SyntheticAdapter::external_set_breaker(BreakerPosition position) noexcept {
  breaker_ = position;
  (void)persist_state();
}
void SyntheticAdapter::external_set_synchronized(bool synchronized) noexcept {
  synchronized_ = synchronized;
  (void)persist_state();
}
void SyntheticAdapter::external_set_running(bool running) noexcept {
  running_ = running;
  cranking_ = false;
  (void)persist_state();
}
void SyntheticAdapter::external_set_fuel_valve(bool open) noexcept {
  fuel_valve_open_ = open;
  (void)persist_state();
}
void SyntheticAdapter::set_faults(const SyntheticFaults& faults) noexcept { faults_ = faults; }

std::size_t SyntheticAdapter::accepted_count(OperationKind operation) const noexcept {
  std::size_t count = 0;
  for (const auto& record : actuations_) {
    if (record.operation == operation && record.status == AdapterAckStatus::Accepted) ++count;
  }
  return count;
}

std::vector<std::string> SyntheticAdapter::journal_lines() const {
  std::vector<std::string> out;
  out.reserve(actuations_.size());
  for (const auto& record : actuations_) {
    out.push_back(join_line({generator_.str(), std::to_string(record.command_id.value()),
                             std::to_string(record.attempt_id.value()),
                             std::string(to_string(record.operation)),
                             std::string(to_string(record.status)), std::to_string(record.at)}));
  }
  return out;
}

}  // namespace genctl
