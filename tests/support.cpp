// Generator Control - shared test fixtures implementation.
#include "support.hpp"

namespace gctest {

ScratchDir::ScratchDir(const std::string& label) {
  const Result<std::string> temp = platform::temporary_directory();
  const std::string base = temp.ok() ? temp.value() : std::string(".");
  path_ = base + "\\genctl-tests\\" + label + "-" +
          std::to_string(platform::current_process_id());
  reset();
}

ScratchDir::~ScratchDir() { (void)platform::remove_directory_tree(path_); }

void ScratchDir::reset() {
  (void)platform::remove_directory_tree(path_);
  const Status status = platform::ensure_directory(path_);
  if (!status.ok()) fail("cannot create the scratch directory: " + status.message());
}

bool wait_for_file(const std::string& path, int attempts) {
  for (int i = 0; i < attempts; ++i) {
    const Result<bool> exists = platform::path_exists(path);
    if (exists.ok() && exists.value()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

Lab::Lab(const LabConfig& config) : config_(config), directory_(config.label) {
  store_ = directory_.child("store");
  journal_ = directory_.child("lab-device");
  const Status created = platform::ensure_directory(store_);
  if (!created.ok()) fail("cannot create the store directory: " + created.message());
  if (config_.persist_device) {
    const Status device = platform::ensure_directory(journal_);
    if (!device.ok()) fail("cannot create the device directory: " + device.message());
    journal_ = journal_ + "\\synthetic-device.jrnl";
  } else {
    journal_.clear();
  }

  const Result<GeneratorId> id = GeneratorId::parse("gen-unit-1");
  if (!id.ok()) fail("fixture identity is invalid");
  generator_ = id.value();

  SyntheticAdapter::Options adapter_options{};
  adapter_options.journal_path = journal_;
  adapter_options.initial_breaker = BreakerPosition::Open;
  adapter_ = std::make_unique<SyntheticAdapter>(generator_, clock_, adapter_options);
}

OpenOptions Lab::open_options() const {
  OpenOptions options{};
  options.store_directory = store_;
  options.create_if_missing = true;
  options.config = config_.engine;
  options.config.evidence.allow_synthetic = config_.allow_synthetic;
  return options;
}

void Lab::close() {
  if (engine_ != nullptr) {
    (void)engine_->close();
    engine_.reset();
  }
}

Lab::~Lab() { close(); }

StateRevision Lab::revision() {
  const Result<GeneratorState> current = engine_->inspect(generator_);
  if (!current.ok()) fail("inspect failed: " + current.status().message());
  return current.value().revision;
}

GeneratorState Lab::state() {
  const Result<GeneratorState> current = engine_->inspect(generator_);
  if (!current.ok()) fail("inspect failed: " + current.status().message());
  return current.value();
}

Status Lab::record_check(CheckKind kind, EvidenceState state, EvidenceSource source,
                         TypedValue value, Millis max_age) {
  CheckUpdate update{};
  update.generator = generator_;
  update.controller = engine_->controller();
  update.revision = revision();
  const Result<IdempotencyKey> key = IdempotencyKey::parse(
      "lab-check-" + std::to_string(engine_->commit_seq().value()) + "-" +
      std::to_string(static_cast<unsigned>(kind)));
  if (!key.ok()) return key.status();
  update.idempotency_key = key.value();
  update.record.kind = kind;
  update.record.required = true;
  update.record.state = state;
  update.record.source = source;
  update.record.lifetime = EvidenceLifetime::VolatileObservation;
  update.record.observed_at = clock_.now_millis();
  update.record.max_age_millis = max_age;
  update.record.value = value;
  return engine_->record_check(update);
}

Status Lab::record_sync(SyncPreconditionKind kind, EvidenceState state, TypedValue value,
                        EvidenceSource source) {
  SyncPreconditionUpdate update{};
  update.generator = generator_;
  update.controller = engine_->controller();
  update.revision = revision();
  const Result<IdempotencyKey> key = IdempotencyKey::parse(
      "lab-sync-" + std::to_string(engine_->commit_seq().value()) + "-" +
      std::to_string(static_cast<unsigned>(kind)));
  if (!key.ok()) return key.status();
  update.idempotency_key = key.value();
  update.record.kind = kind;
  update.record.required = true;
  update.record.state = state;
  update.record.source = source;
  update.record.lifetime = EvidenceLifetime::VolatileObservation;
  update.record.observed_at = clock_.now_millis();
  update.record.max_age_millis = 60 * kMillisPerSecond;
  update.record.value = value;
  return engine_->record_sync_precondition(update);
}

Status Lab::record_transfer(TransferPreconditionKind kind, EvidenceState state, TypedValue value,
                            EvidenceSource source) {
  TransferPreconditionUpdate update{};
  update.generator = generator_;
  update.controller = engine_->controller();
  update.revision = revision();
  const Result<IdempotencyKey> key = IdempotencyKey::parse(
      "lab-transfer-" + std::to_string(engine_->commit_seq().value()) + "-" +
      std::to_string(static_cast<unsigned>(kind)));
  if (!key.ok()) return key.status();
  update.idempotency_key = key.value();
  update.record.kind = kind;
  update.record.required = true;
  update.record.state = state;
  update.record.source = source;
  update.record.lifetime = EvidenceLifetime::VolatileObservation;
  update.record.observed_at = clock_.now_millis();
  update.record.max_age_millis = 60 * kMillisPerSecond;
  update.record.value = value;
  return engine_->record_transfer_precondition(update);
}

void Lab::seed_checks() {
  const GeneratorState current = state();
  for (const CheckKind kind : current.required_checks) {
    const Status status =
        record_check(kind, EvidenceState::Present, EvidenceSource::SyntheticAdapter,
                     TypedValue{TypedValueKind::Boolean, 1});
    if (!status.ok()) fail("seeding check failed: " + status.message());
  }
}

void Lab::seed_resources() {
  ResourceUpdate update{};
  update.generator = generator_;
  update.controller = engine_->controller();
  update.revision = revision();
  const Result<IdempotencyKey> key =
      IdempotencyKey::parse("lab-resource-" + std::to_string(engine_->commit_seq().value()));
  if (!key.ok()) fail("resource key is invalid");
  update.idempotency_key = key.value();
  const EpochMillis observed = clock_.now_millis();
  const auto fill = [&](auto& evidence, auto quantity) {
    evidence.state = EvidenceState::Present;
    evidence.value = quantity;
    evidence.source = EvidenceSource::SyntheticAdapter;
    evidence.lifetime = EvidenceLifetime::VolatileObservation;
    evidence.observed_at = observed;
  };
  update.set_fuel_level = true;
  fill(update.fuel_level, FuelQuantity{FuelUnit::Millilitres, 900'000});
  update.set_fuel_capacity = true;
  fill(update.fuel_capacity, FuelQuantity{FuelUnit::Millilitres, 1'000'000});
  update.set_fuel_consumption = true;
  fill(update.fuel_consumption, FuelRate{FuelUnit::Millilitres, 60'000});
  update.set_lube_oil_pressure = true;
  fill(update.lube_oil_pressure, Pressure{410'000});
  update.set_coolant_temperature = true;
  fill(update.coolant_temperature, Temperature{78'000});
  update.set_coolant_level = true;
  fill(update.coolant_level, Percent{9'200});
  update.set_battery_voltage = true;
  fill(update.battery_voltage, Voltage{25'600});
  const Status status = engine_->record_resource(update);
  if (!status.ok()) fail("seeding resources failed: " + status.message());
}

void Lab::seed_sync() {
  const GeneratorState current = state();
  for (const SyncPreconditionKind kind : default_sync_preconditions(current.sync_policy)) {
    TypedValue value{TypedValueKind::Boolean, 1};
    if (kind == SyncPreconditionKind::VoltageMatch) {
      value = TypedValue{TypedValueKind::Millivolts, 1200};
    } else if (kind == SyncPreconditionKind::FrequencyMatch) {
      value = TypedValue{TypedValueKind::Millihertz, 40};
    } else if (kind == SyncPreconditionKind::PhaseAngleMatch) {
      value = TypedValue{TypedValueKind::Millidegrees, 1500};
    }
    const Status status =
        record_sync(kind, EvidenceState::Present, value, EvidenceSource::SyntheticAdapter);
    if (!status.ok()) fail("seeding synchronization precondition failed: " + status.message());
  }
}

void Lab::seed_transfer() {
  TransferPathUpdate path_update{};
  path_update.generator = generator_;
  path_update.controller = engine_->controller();
  path_update.revision = revision();
  const Result<IdempotencyKey> path_key =
      IdempotencyKey::parse("lab-path-" + std::to_string(engine_->commit_seq().value()));
  if (!path_key.ok()) fail("path key is invalid");
  path_update.idempotency_key = path_key.value();
  path_update.path.breaker = SwitchRef::parse("generator-primary-breaker").value();
  path_update.path.source_bus = SwitchRef::parse("utility-bus-a").value();
  path_update.path.target_bus = SwitchRef::parse("generator-bus-a").value();
  path_update.path.feeder = SwitchRef::parse("feeder-1").value();
  path_update.path.transfer_path = SwitchRef::parse("path-utility-to-generator").value();
  Status status = engine_->set_transfer_path(path_update);
  if (!status.ok()) fail("recording the transfer path failed: " + status.message());

  SwitchAuthorityUpdate token{};
  token.generator = generator_;
  token.controller = engine_->controller();
  token.revision = revision();
  const Result<IdempotencyKey> token_key =
      IdempotencyKey::parse("lab-token-" + std::to_string(engine_->commit_seq().value()));
  if (!token_key.ok()) fail("token key is invalid");
  token.idempotency_key = token_key.value();
  token.token.state = EvidenceState::Present;
  token.token.source = EvidenceSource::ExternalAuthority;
  token.token.lifetime = EvidenceLifetime::AttestedWithValidity;
  token.token.observed_at = clock_.now_millis();
  token.token.valid_until = clock_.now_millis() + 30 * kMillisPerMinute;
  token.token.value.authority = SwitchRef::parse("lab-switch-authority").value();
  token.token.value.subject_switch = SwitchRef::parse("generator-primary-breaker").value();
  token.token.value.subject_generator = generator_;
  token.token.value.subject_generation = state().generation;
  token.token.value.epoch = engine_->controller().epoch;
  token.token.value.issued_at = clock_.now_millis();
  token.token.value.valid_until = clock_.now_millis() + 30 * kMillisPerMinute;
  status = engine_->record_switch_authority(token);
  if (!status.ok()) fail("recording the switch authority failed: " + status.message());

  for (const TransferPreconditionKind kind : default_transfer_preconditions()) {
    const Status recorded =
        record_transfer(kind, EvidenceState::Present, TypedValue{TypedValueKind::Boolean, 1},
                        EvidenceSource::ExternalAuthority);
    if (!recorded.ok()) fail("seeding transfer precondition failed: " + recorded.message());
  }
}

OperationRequest Lab::request(OperationKind operation, const std::string& key, AuthorityClass cls,
                              const std::string& reason) {
  const GeneratorState current = state();
  OperationRequest built{};
  built.generator = generator_;
  built.operation = operation;
  built.controller = engine_->controller();
  built.generation = current.generation;
  built.revision = current.revision;
  built.authority.cls = cls;
  built.authority.epoch = engine_->controller().epoch;
  built.authority.granted_by = "test-harness";
  built.authority.granted_at = clock_.now_millis();
  built.authority.reason = reason;
  built.authority.explicit_grant = true;
  if (cls == AuthorityClass::Emergency) {
    built.authority.valid_until = clock_.now_millis() + 30 * kMillisPerMinute;
  }
  const Result<IdempotencyKey> parsed = IdempotencyKey::parse(key);
  if (!parsed.ok()) fail("test idempotency key is invalid");
  built.idempotency_key = parsed.value();
  built.requested_at = clock_.now_millis();
  return built;
}

Result<AttemptRecord> Lab::start(const std::string& key) {
  return engine_->execute(request(OperationKind::Start, key), false);
}

Result<AttemptRecord> Lab::stop(const std::string& key) {
  return engine_->execute(request(OperationKind::Stop, key), false);
}

void Lab::set_authority(AuthorityClass cls, const std::string& granted_by, EpochMillis valid_until,
                        const std::string& reason) {
  AuthorityUpdate update{};
  update.generator = generator_;
  update.controller = engine_->controller();
  update.revision = revision();
  const Result<IdempotencyKey> key =
      IdempotencyKey::parse("lab-authority-" + std::to_string(engine_->commit_seq().value()));
  if (!key.ok()) fail("authority key is invalid");
  update.idempotency_key = key.value();
  update.grant.cls = cls;
  update.grant.epoch = engine_->controller().epoch;
  update.grant.granted_by = granted_by;
  update.grant.granted_at = clock_.now_millis();
  update.grant.valid_until = valid_until;
  update.grant.reason = reason;
  update.grant.explicit_grant = true;
  const Status status = engine_->grant_authority(update);
  if (!status.ok()) fail("recording the authority grant failed: " + status.message());
}

ErrorCode attempt_code(GeneratorControlEngine& engine, const OperationRequest& request) {
  const Result<AttemptRecord> attempt = engine.execute(request, false);
  return attempt.ok() ? ErrorCode::Ok : attempt.code();
}

}  // namespace gctest
