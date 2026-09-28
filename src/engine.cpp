// Generator Control - control engine implementation.
//
// Lock ownership, stated once and honoured everywhere below:
//   * mutation_mutex_ is the only lock that serialises state changes. It is held
//     across the durable publication of a mutation, which is a deliberate invariant:
//     two threads must never publish generations out of order. Inside that critical
//     section there is no callback, no adapter call, no user code, no sleep and no
//     nested lock other than the snapshot leaf lock.
//   * snapshot_mutex_ is a leaf. It is taken only to publish or copy a
//     std::shared_ptr<const EngineView>, never across a file operation, a clock read
//     or an adapter call. The order mutation -> snapshot is the only order used.
//   * adapter_ is called only with no engine lock held.
#include "genctl/engine.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include "codecs.hpp"
#include "genctl/authority.hpp"
#include "genctl/path_safety.hpp"

namespace genctl {
namespace {

// Classification of an observation against the effect a command asked for.
enum class EffectVerdict : std::uint8_t { Inconclusive = 0, Satisfied, Contradicted };

bool is_running_report(const EngineObservation& observation) {
  if (observation.running) return true;
  switch (observation.reported_state) {
    case OperatingState::Starting:
    case OperatingState::WarmUp:
    case OperatingState::ReadyUnsynchronized:
    case OperatingState::Synchronized:
    case OperatingState::Degraded:
      return true;
    default:
      return false;
  }
}

bool is_stopped_report(const EngineObservation& observation) {
  if (observation.cranking) return false;
  if (observation.running) return false;
  return observation.reported_state == OperatingState::Stopped ||
         observation.reported_state == OperatingState::Cooldown ||
         observation.reported_state == OperatingState::Unknown;
}

EffectVerdict classify_effect(OperationKind operation, const EngineObservation& observation) {
  switch (operation) {
    case OperationKind::Start:
    case OperationKind::TestStart:
    case OperationKind::EmergencyStart:
      if (is_running_report(observation)) return EffectVerdict::Satisfied;
      return observation.cranking ? EffectVerdict::Inconclusive : EffectVerdict::Contradicted;
    case OperationKind::Stop:
    case OperationKind::TestStop:
    case OperationKind::EmergencyStop:
      if (is_stopped_report(observation)) return EffectVerdict::Satisfied;
      return EffectVerdict::Inconclusive;
    case OperationKind::Synchronize:
      if (observation.synchronization == SynchronizationState::Synchronized) {
        return EffectVerdict::Satisfied;
      }
      return EffectVerdict::Inconclusive;
    case OperationKind::Desynchronize:
      if (observation.synchronization == SynchronizationState::NotSynchronized) {
        return EffectVerdict::Satisfied;
      }
      return EffectVerdict::Inconclusive;
    case OperationKind::TransferToGenerator:
      if (observation.breaker == BreakerPosition::Closed) return EffectVerdict::Satisfied;
      return observation.breaker == BreakerPosition::Open ? EffectVerdict::Contradicted
                                                          : EffectVerdict::Inconclusive;
    case OperationKind::TransferToUtility:
      if (observation.breaker == BreakerPosition::Open) return EffectVerdict::Satisfied;
      return observation.breaker == BreakerPosition::Closed ? EffectVerdict::Contradicted
                                                            : EffectVerdict::Inconclusive;
    case OperationKind::FaultReset:
      return observation.reported_state == OperatingState::Faulted ? EffectVerdict::Contradicted
                                                                   : EffectVerdict::Satisfied;
    default:
      // Lifecycle operations are administrative: the observation is recorded but no
      // electrical effect is claimed for them.
      return EffectVerdict::Satisfied;
  }
}

bool gate_is_mode_error(ErrorCode code) {
  switch (code) {
    case ErrorCode::ModeNotPermitted:
    case ErrorCode::TestOperationNotPermitted:
    case ErrorCode::EmergencyAuthorityRequired:
    case ErrorCode::AuthorityClassMismatch:
      return true;
    default:
      return false;
  }
}

bool gate_is_lifecycle_error(ErrorCode code) {
  switch (code) {
    case ErrorCode::MaintenanceActive:
    case ErrorCode::GeneratorIsolated:
    case ErrorCode::GeneratorRetired:
    case ErrorCode::LifecycleClosed:
      return true;
    default:
      return false;
  }
}

ValidationStage authority_stage(ErrorCode code) {
  switch (code) {
    case ErrorCode::EmergencyAuthorityNotExplicit:
    case ErrorCode::EmergencyAuthorityRequired:
    case ErrorCode::AuthorityMissing:
    case ErrorCode::AuthorityExpired:
    case ErrorCode::AuthorityClassMismatch:
      return ValidationStage::Authority;
    default:
      return ValidationStage::Authority;
  }
}

void touch(GeneratorState* state) { (void)state; }

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle of the engine
// ---------------------------------------------------------------------------
GeneratorControlEngine::~GeneratorControlEngine() { (void)close(); }

Result<std::unique_ptr<GeneratorControlEngine>> GeneratorControlEngine::open(
    const OpenOptions& options, const Clock& clock, GeneratorAdapter* adapter) {
  if (adapter == nullptr) {
    return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                       "an adapter is required; the engine never controls anything outside the "
                       "adapter boundary");
  }
  std::unique_ptr<GeneratorControlEngine> engine(new GeneratorControlEngine());
  engine->config_ = options.config;
  engine->clock_ = &clock;
  engine->adapter_ = adapter;

  StoreOpenOptions store_options{};
  store_options.directory = options.store_directory;
  store_options.read_only = options.read_only;
  store_options.create_if_missing = options.create_if_missing;
  store_options.recover_scan = options.recover_scan;
  store_options.accept_rollback = options.accept_rollback;
  store_options.rollback_acceptance_note = options.rollback_acceptance_note;
  store_options.requested_epoch = options.requested_epoch;
  store_options.generation_retention = options.config.generation_retention;
  store_options.max_record_bytes = options.config.max_record_bytes;
  store_options.crash_point = options.config.crash_point;
  GENCTL_TRY_ASSIGN(store, DurableStore::open(store_options, clock));
  engine->store_ = std::move(store);

  // The engine owns the adapter session: it opens the adapter once and closes it on
  // shutdown, so a command can never be issued against an unopened device.
  GENCTL_TRY(adapter->open());
  engine->adapter_opened_ = true;

  const StateImage& image = engine->store_->image();
  engine->generators_ = image.generators;
  engine->journal_.set_policy(image.journal_policy.idempotency_window == 0
                                  ? options.config.journal
                                  : image.journal_policy);
  engine->journal_.restore(image.next_attempt_id, image.next_journal_seq, image.retained_floor,
                           image.evicted_keys, image.attempts, image.replay);
  engine->key_window_.acknowledged_at_seq = image.key_window.acknowledged_at_seq;
  engine->key_window_.acknowledged_at = image.key_window.acknowledged_at;
  engine->key_window_.acknowledged_by = image.key_window.acknowledged_by;
  engine->key_window_.reason = image.key_window.reason;
  engine->key_window_.evicted_keys = image.key_window.evicted_keys;

  engine->controller_.epoch = engine->store_->epoch();
  engine->controller_.incarnation = engine->store_->incarnation();

  ReopenReport report{};
  report.incarnation = engine->controller_.incarnation;
  report.epoch = engine->controller_.epoch;
  report.adopted_commit_seq = engine->store_->commit_seq();
  report.read_only = engine->store_->read_only();
  report.residue_retired = engine->store_->residue_retired();
  report.attempts_recovered = engine->journal_.records().size();
  for (const auto& attempt : engine->journal_.records()) {
    if (command_is_unresolved(attempt.command_state)) ++report.unresolved_attempts;
  }
  if (report.unresolved_attempts > 0) {
    report.notes.push_back("recovered " + std::to_string(report.unresolved_attempts) +
                           " unresolved attempt(s): the runtime will not re-issue them; resolve "
                           "each one by observation");
  }
  if (image.commit_seq.is_set() && engine->store_->commit_seq() == image.commit_seq) {
    report.adopted_from_scan = !engine->store_->fence().highest_seq.is_set() && false;
  }

  if (engine->store_->created_new_store() && !options.read_only) {
    // Found the store with a first committed generation so that the head marker and
    // the fence exist from the outset. The commit point is the head replacement.
    GENCTL_TRY(engine->commit_locked(clock.now_millis()));
    report.adopted_commit_seq = engine->store_->commit_seq();
    report.notes.push_back("founded a new store at commit " +
                           std::to_string(engine->store_->commit_seq().value()));
  }

  // Recovery must never make a dynamic observation look fresh.
  if (!options.read_only && !options.config.disable_evidence_demotion) {
    std::size_t demoted = 0;
    GENCTL_TRY(engine->demote_volatile_evidence_locked(&demoted));
    report.demoted_evidence_records = demoted;
    if (demoted > 0) {
      report.notes.push_back("demoted " + std::to_string(demoted) +
                             " volatile evidence record(s) observed by a previous controller "
                             "incarnation");
      GENCTL_TRY(engine->commit_locked(clock.now_millis()));
    }
  }

  engine->rebuild_view_locked();
  engine->reopen_report_ = std::move(report);
  return engine;
}

std::shared_ptr<const EngineView> GeneratorControlEngine::view() const {
  std::lock_guard<std::mutex> guard(snapshot_mutex_);
  return view_;
}

void GeneratorControlEngine::rebuild_view_locked() {
  auto next = std::make_shared<EngineView>();
  next->generators = generators_;
  next->attempts = journal_.records();
  next->next_attempt_id = journal_.next_attempt_id();
  next->commit_seq = store_ == nullptr ? CommitSeq{} : store_->commit_seq();
  std::lock_guard<std::mutex> guard(snapshot_mutex_);
  view_ = std::move(next);
}

const std::string& GeneratorControlEngine::store_directory() const noexcept {
  static const std::string kEmpty;
  return store_ == nullptr ? kEmpty : store_->directory();
}

bool GeneratorControlEngine::read_only() const noexcept {
  return store_ != nullptr && store_->read_only();
}

CommitSeq GeneratorControlEngine::commit_seq() const noexcept {
  return store_ == nullptr ? CommitSeq{} : store_->commit_seq();
}

GeneratorState* GeneratorControlEngine::find_locked(const GeneratorId& id) {
  const auto it = generators_.find(id);
  return it == generators_.end() ? nullptr : &it->second;
}

const GeneratorState* GeneratorControlEngine::find_locked(const GeneratorId& id) const {
  const auto it = generators_.find(id);
  return it == generators_.end() ? nullptr : &it->second;
}

Status GeneratorControlEngine::validate_controller_locked(const GeneratorId& id,
                                                          const ControllerGeneration& controller,
                                                          StateRevision revision) const {
  const GeneratorState* state = find_locked(id);
  if (state == nullptr) {
    return make_status(ErrorCode::UnknownGenerator, ValidationStage::Identity,
                       "generator '" + id.str() + "' is not registered in this store");
  }
  if (controller.epoch != controller_.epoch) {
    return make_status(ErrorCode::StaleControlEpoch, ValidationStage::Fencing,
                       "request was planned against control epoch " +
                           std::to_string(controller.epoch.value()) +
                           " but the current epoch is " +
                           std::to_string(controller_.epoch.value()));
  }
  if (controller.incarnation != controller_.incarnation) {
    return make_status(ErrorCode::StaleIncarnation, ValidationStage::Fencing,
                       "request was planned against controller incarnation " +
                           std::to_string(controller.incarnation.value()) +
                           " but the current incarnation is " +
                           std::to_string(controller_.incarnation.value()));
  }
  if (revision != state->revision) {
    return make_status(ErrorCode::StaleStateRevision, ValidationStage::Fencing,
                       "request was planned against revision " +
                           std::to_string(revision.value()) + " but generator '" + id.str() +
                           "' is at revision " + std::to_string(state->revision.value()));
  }
  return Status::success();
}

// ---------------------------------------------------------------------------
// Persistence helpers
// ---------------------------------------------------------------------------
StateImage GeneratorControlEngine::build_image_locked(EpochMillis now) const {
  StateImage image{};
  image.format_version = kStoreFormatVersion;
  image.created_at = now;
  image.generators = generators_;
  image.attempts = journal_.records();
  image.replay = journal_.replay_entries();
  image.next_attempt_id = journal_.next_attempt_id();
  image.next_journal_seq = journal_.next_seq();
  image.retained_floor = journal_.retained_floor();
  image.evicted_keys = journal_.evicted_keys();
  image.journal_policy = journal_.policy();
  image.key_window.acknowledged_at_seq = key_window_.acknowledged_at_seq;
  image.key_window.acknowledged_at = key_window_.acknowledged_at;
  image.key_window.acknowledged_by = key_window_.acknowledged_by;
  image.key_window.reason = key_window_.reason;
  image.key_window.evicted_keys = key_window_.evicted_keys;
  return image;
}

Status GeneratorControlEngine::commit_locked(EpochMillis now) {
  if (store_->read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only");
  }
  StateImage image = build_image_locked(now);
  GENCTL_TRY_ASSIGN(seq, store_->publish(std::move(image), now));
  (void)seq;
  rebuild_view_locked();
  return Status::success();
}

Status GeneratorControlEngine::bump_revision_locked(GeneratorState& state) {
  if (state.revision.at_max()) {
    return make_status(ErrorCode::ArithmeticOverflow, ValidationStage::Format,
                       "state revision counter is exhausted");
  }
  state.revision = state.revision.next();
  return Status::success();
}

namespace {

constexpr std::size_t kHistoryRetention = 512;

}  // namespace

void GeneratorControlEngine::append_history_locked(GeneratorState& state,
                                                   const HistoryEntry& entry) {
  state.history.push_back(entry);
  while (state.history.size() > kHistoryRetention) {
    state.history.erase(state.history.begin());
  }
}

// ---------------------------------------------------------------------------
// Query helpers
// ---------------------------------------------------------------------------
Result<ReadinessReport> GeneratorControlEngine::readiness_locked(const GeneratorState& state,
                                                                 EpochMillis now) const {
  ReadinessReport report{};
  const Status status =
      evaluate_readiness(state.id, state.revision, controller_, state.generation,
                         state.required_checks, state.checks, now, config_.evidence, false, &report);
  if (!status.ok()) return status;
  return report;
}

Result<ResourceAssessment> GeneratorControlEngine::resource_locked(const GeneratorState& state,
                                                                   EpochMillis now) const {
  ResourceAssessment assessment{};
  const Status status = assess_resources(state.id, state.revision, controller_, state.resources,
                                         state.requirement, now, config_.evidence, false,
                                         &assessment);
  if (!status.ok()) return status;
  return assessment;
}

Result<SynchronizationEligibility> GeneratorControlEngine::sync_locked(const GeneratorState& state,
                                                                       EpochMillis now) const {
  SynchronizationEligibility eligibility{};
  const Status status = evaluate_synchronization(state.id, state.revision, controller_,
                                                 state.sync_preconditions, state.sync_policy, now,
                                                 &eligibility);
  if (!status.ok()) return status;
  return eligibility;
}

Result<TransferEligibility> GeneratorControlEngine::transfer_locked(const GeneratorState& state,
                                                                    EpochMillis now) const {
  TransferEligibility eligibility{};
  std::vector<TransferPreconditionRecord> records = state.transfer_preconditions;

  // The externally granted switch authority and the generator's own synchronization
  // state are inputs to transfer eligibility; both are recorded here rather than
  // being assumed from the generator's own command history.
  const Evidence<SwitchAuthorityToken>& token = state.switch_authority;
  const EvidenceAssessment token_assessment =
      assess_evidence(token, now, EvidencePolicy::external(state.transfer_policy.max_token_age_millis));
  bool token_ok = token_assessment.usable();
  std::string token_detail = token_assessment.detail;
  if (token_ok) {
    if (!(token.value.subject_generator == state.id)) {
      token_ok = false;
      token_detail = "switch authority token names a different generator";
    } else if (!(token.value.subject_generation == state.generation)) {
      token_ok = false;
      token_detail = "switch authority token refers to a different generator generation";
    } else if (token.value.epoch != controller_.epoch) {
      token_ok = false;
      token_detail = "switch authority token was issued under control epoch " +
                     std::to_string(token.value.epoch.value()) + ", current epoch is " +
                     std::to_string(controller_.epoch.value());
    }
  }

  auto set_boolean = [&](TransferPreconditionKind kind, bool satisfied, ErrorCode code,
                         const std::string& detail, EvidenceSource source) {
    for (auto& record : records) {
      if (record.kind != kind) continue;
      record.state = satisfied ? EvidenceState::Present : EvidenceState::Contradictory;
      record.detail = detail;
      return;
    }
    TransferPreconditionRecord record{};
    record.kind = kind;
    record.required = true;
    record.state = satisfied ? EvidenceState::Present : EvidenceState::Contradictory;
    record.source = source;
    record.lifetime = EvidenceLifetime::VolatileObservation;
    record.observed_at = now;
    record.max_age_millis = state.transfer_policy.max_position_age_millis;
    record.value = TypedValue{TypedValueKind::Boolean, satisfied ? 1 : 0};
    record.detail = detail + " [" + std::string(to_string(code)) + "]";
    records.push_back(std::move(record));
  };

  set_boolean(TransferPreconditionKind::FacilitySwitchAuthorityGranted, token_ok,
              token_ok ? ErrorCode::Ok : ErrorCode::TransferAuthorityMissing, token_detail,
              EvidenceSource::ExternalAuthority);
  set_boolean(TransferPreconditionKind::FacilitySwitchAuthorityFresh, token_ok,
              token_ok ? ErrorCode::Ok : ErrorCode::TransferAuthorityStale, token_detail,
              EvidenceSource::ExternalAuthority);
  set_boolean(TransferPreconditionKind::GeneratorSynchronized,
              state.synchronization == SynchronizationState::Synchronized,
              state.synchronization == SynchronizationState::Synchronized
                  ? ErrorCode::Ok
                  : ErrorCode::TransferPreconditionFailed,
              state.synchronization == SynchronizationState::Synchronized
                  ? "generator is synchronized"
                  : "generator is not synchronized",
              EvidenceSource::VendorAdapter);
  {
    const Result<ReadinessReport> readiness = readiness_locked(state, now);
    const bool satisfied = readiness.ok() && readiness.value().satisfied;
    set_boolean(TransferPreconditionKind::GeneratorReadinessSatisfied, satisfied,
                satisfied ? ErrorCode::Ok : ErrorCode::TransferPreconditionFailed,
                satisfied ? "generator readiness is satisfied"
                          : "generator readiness is not satisfied",
                EvidenceSource::VendorAdapter);
  }

  const Status status = evaluate_transfer(state.id, state.revision, controller_, state.generation,
                                          state.transfer_path, records, state.transfer_policy, now,
                                          &eligibility);
  if (!status.ok()) return status;
  return eligibility;
}

// ---------------------------------------------------------------------------
// Evaluation (pure planning; no mutation, no actuation)
// ---------------------------------------------------------------------------
Result<EvaluationReport> GeneratorControlEngine::evaluate_locked(const GeneratorState& state,
                                                                 const OperationRequest& request,
                                                                 EpochMillis now,
                                                                 JournalSeq replay_floor) const {
  (void)replay_floor;
  EvaluationReport report{};
  report.generator = state.id;
  report.operation = request.operation;
  report.controller = controller_;
  report.generation = state.generation;
  report.revision = state.revision;
  report.evaluated_at = now;

  bool failed = false;
  auto add = [&](ValidationStage stage, ErrorCode code, std::string detail) {
    StageResult result{};
    result.stage = stage;
    result.code = code;
    result.detail = std::move(detail);
    report.stages.push_back(std::move(result));
    if (!failed && code != ErrorCode::Ok) {
      failed = true;
      report.primary_error = code;
      report.primary_stage = stage;
      report.primary_detail = report.stages.back().detail;
    }
  };

  // ---- Format ----------------------------------------------------------
  if (request.operation == OperationKind::Unknown) {
    add(ValidationStage::Format, ErrorCode::InvalidArgument, "operation is not specified");
    report.permitted = false;
    return report;
  }
  if (request.idempotency_key.empty()) {
    add(ValidationStage::Format, ErrorCode::IdempotencyKeyMissing,
        "an actuating request must carry an idempotency key so that a lost response cannot become "
        "a second actuation");
    report.permitted = false;
    return report;
  }
  add(ValidationStage::Format, ErrorCode::Ok, "request is well formed");

  // ---- Identity --------------------------------------------------------
  add(ValidationStage::Identity, ErrorCode::Ok,
      "generator '" + state.id.str() + "' is registered");

  // ---- Fencing ---------------------------------------------------------
  {
    ErrorCode code = ErrorCode::Ok;
    std::string detail = "planned against " + to_string(request.controller) + " revision " +
                         std::to_string(request.revision.value());
    if (request.controller.epoch != controller_.epoch) {
      code = ErrorCode::StaleControlEpoch;
      detail = "request was planned against epoch " +
               std::to_string(request.controller.epoch.value()) + " but the current epoch is " +
               std::to_string(controller_.epoch.value());
    } else if (request.controller.incarnation != controller_.incarnation) {
      code = ErrorCode::StaleIncarnation;
      detail = "request was planned against incarnation " +
               std::to_string(request.controller.incarnation.value()) +
               " but the current incarnation is " +
               std::to_string(controller_.incarnation.value());
    } else if (!(request.generation == state.generation)) {
      code = ErrorCode::StaleGeneratorGeneration;
      detail = "request was planned against generator generation " +
               to_string(request.generation) + " but the store holds " + to_string(state.generation);
    } else if (request.revision != state.revision) {
      code = ErrorCode::StaleStateRevision;
      detail = "request was planned against revision " + std::to_string(request.revision.value()) +
               " but generator '" + state.id.str() + "' is at revision " +
               std::to_string(state.revision.value());
    }
    add(ValidationStage::Fencing, code, std::move(detail));
    if (failed) {
      report.permitted = false;
      return report;
    }
  }

  const TransitionRule* rule = rule_for(request.operation);
  if (rule == nullptr) {
    add(ValidationStage::Transition, ErrorCode::NotImplemented,
        "operation has no transition rule in this build");
    report.permitted = false;
    return report;
  }

  // ---- Lifecycle / Transition / Mode -----------------------------------
  const StateGateResult gate = gate_transition(*rule, state.lifecycle, state.operating, state.mode,
                                               lifecycle_error_for(state.lifecycle));
  if (!gate.allowed) {
    ValidationStage stage = ValidationStage::Transition;
    if (gate_is_lifecycle_error(gate.code)) {
      stage = ValidationStage::Lifecycle;
      add(stage, gate.code, gate.detail);
    } else if (gate_is_mode_error(gate.code)) {
      stage = ValidationStage::Mode;
      add(ValidationStage::Transition, ErrorCode::Ok, "operating state permits this operation");
      add(stage, gate.code, gate.detail);
    } else {
      add(ValidationStage::Transition, gate.code, gate.detail);
    }
    report.permitted = false;
    return report;
  }
  add(ValidationStage::Lifecycle, ErrorCode::Ok,
      std::string("lifecycle '") + std::string(to_string(state.lifecycle)) + "' permits the "
      "operation");
  add(ValidationStage::Transition, ErrorCode::Ok, gate.detail);
  add(ValidationStage::Mode, ErrorCode::Ok,
      std::string("mode '") + std::string(to_string(state.mode)) + "' permits the operation");

  // ---- Authority -------------------------------------------------------
  bool advisory_waiver = false;
  if (rule->accept_any_authority) {
    add(ValidationStage::Authority, ErrorCode::Ok,
        "stop-type operations are accepted under any held authority class");
  } else {
    const Status authority_status = validate_authority_grant(
        request.authority, rule->required_authority, request.operation, now);
    if (!authority_status.ok()) {
      add(authority_stage(authority_status.code()), authority_status.code(),
          authority_status.message());
      report.permitted = false;
      return report;
    }
    advisory_waiver = rule->advisory_waivable &&
                      request.authority.cls == AuthorityClass::Emergency &&
                      request.authority.explicit_grant;
    add(ValidationStage::Authority, ErrorCode::Ok,
        std::string("authority '") + std::string(to_string(request.authority.cls)) +
            "' permits the operation" +
            (advisory_waiver ? "; advisory checks are waived by an explicit emergency grant"
                             : ""));
  }
  report.advisory_waived = advisory_waiver;

  // ---- Readiness and interlocks ----------------------------------------
  {
    ReadinessReport readiness{};
    const Status status = evaluate_readiness(state.id, state.revision, controller_,
                                             state.generation, state.required_checks, state.checks,
                                             now, config_.evidence, advisory_waiver, &readiness);
    if (!status.ok()) return status;
    report.readiness = readiness;
    for (const auto& finding : readiness.findings) {
      if (finding.waived) report.waived_checks.push_back(finding.kind);
    }
    if (!readiness.satisfied) {
      if (rule->require_readiness) {
        CheckKind failing = CheckKind::Unknown;
        std::string failing_detail = "no finding identified";
        for (const auto& finding : readiness.findings) {
          if (!finding.required || finding.satisfied || finding.waived) continue;
          failing = finding.kind;
          failing_detail = finding.detail;
          break;
        }
        add(readiness.stage, readiness.primary_error,
            std::string("readiness check '") + std::string(to_string(failing)) +
                "' is not satisfied: " + failing_detail);
        report.permitted = false;
        return report;
      }
      add(std::max(ValidationStage::Interlock, readiness.stage), ErrorCode::Ok,
          "readiness is not satisfied but this operation does not require it");
    } else if (rule->require_readiness) {
      add(ValidationStage::Readiness, ErrorCode::Ok, "every required readiness check is satisfied");
    }
  }

  // ---- Resources -------------------------------------------------------
  {
    ResourceAssessment resources{};
    const Status status = assess_resources(state.id, state.revision, controller_, state.resources,
                                           state.requirement, now, config_.evidence, false,
                                           &resources);
    if (!status.ok()) return status;
    report.resources = resources;
    if (rule->require_resource) {
      if (!resources.sufficient) {
        add(ValidationStage::Resource, resources.primary_error,
            "resource evidence does not support this operation: " +
                std::string(to_string(resources.primary_error)));
        report.permitted = false;
        return report;
      }
      add(ValidationStage::Resource, ErrorCode::Ok, "fuel and consumables are sufficient");
    }
  }

  // ---- Synchronization -------------------------------------------------
  {
    SynchronizationEligibility eligibility{};
    const Status status = evaluate_synchronization(state.id, state.revision, controller_,
                                                   state.sync_preconditions, state.sync_policy, now,
                                                   &eligibility);
    if (!status.ok()) return status;
    report.synchronization = eligibility;
    if (rule->require_synchronization) {
      if (eligibility.outcome != EligibilityOutcome::Eligible) {
        add(ValidationStage::Synchronization, eligibility.primary_error,
            std::string("synchronization eligibility is '") +
                std::string(to_string(eligibility.outcome)) + "'");
        report.permitted = false;
        return report;
      }
      add(ValidationStage::Synchronization, ErrorCode::Ok,
          "every synchronization precondition is satisfied");
    }
  }

  // ---- Transfer --------------------------------------------------------
  {
    TransferEligibility eligibility{};
    const Status status = evaluate_transfer(
        state.id, state.revision, controller_, state.generation, state.transfer_path,
        state.transfer_preconditions, state.transfer_policy, now, &eligibility);
    if (!status.ok()) return status;
    report.transfer = eligibility;
    if (rule->require_transfer) {
      if (eligibility.outcome != EligibilityOutcome::Eligible) {
        add(ValidationStage::Transfer, eligibility.primary_error,
            std::string("transfer eligibility is '") +
                std::string(to_string(eligibility.outcome)) + "'");
        report.permitted = false;
        return report;
      }
      add(ValidationStage::Transfer, ErrorCode::Ok, "transfer eligibility is satisfied");
    }
  }

  report.permitted = true;
  report.primary_error = ErrorCode::Ok;
  report.primary_stage = ValidationStage::None;

  CanonicalWriter writer(CanonicalLimits{1u << 18, 512, 1024, 256});
  (void)writer.put_string(state.id.str());
  (void)writer.put_u8(static_cast<std::uint8_t>(request.operation));
  (void)writer.put_u64(state.revision.value());
  (void)writer.put_digest(report.readiness.binding_digest);
  (void)writer.put_digest(report.resources.binding_digest);
  (void)writer.put_digest(report.synchronization.binding_digest);
  (void)writer.put_digest(report.transfer.binding_digest);
  report.report_digest = sha256(writer.bytes());
  return report;
}

// ---------------------------------------------------------------------------
// Admission and mutation
// ---------------------------------------------------------------------------
Result<ReadinessReport> GeneratorControlEngine::readiness(const GeneratorId& id,
                                                          EpochMillis now) const {
  auto snapshot = view();
  const auto it = snapshot->generators.find(id);
  if (it == snapshot->generators.end()) {
    return make_status(ErrorCode::UnknownGenerator, ValidationStage::Identity,
                       "generator '" + id.str() + "' is not registered in this store");
  }
  return readiness_locked(it->second, now);
}

Result<ResourceAssessment> GeneratorControlEngine::resource_assessment(const GeneratorId& id,
                                                                       EpochMillis now) const {
  auto snapshot = view();
  const auto it = snapshot->generators.find(id);
  if (it == snapshot->generators.end()) {
    return make_status(ErrorCode::UnknownGenerator, ValidationStage::Identity,
                       "generator '" + id.str() + "' is not registered in this store");
  }
  return resource_locked(it->second, now);
}

Result<SynchronizationEligibility> GeneratorControlEngine::sync_eligibility(const GeneratorId& id,
                                                                            EpochMillis now) const {
  auto snapshot = view();
  const auto it = snapshot->generators.find(id);
  if (it == snapshot->generators.end()) {
    return make_status(ErrorCode::UnknownGenerator, ValidationStage::Identity,
                       "generator '" + id.str() + "' is not registered in this store");
  }
  return sync_locked(it->second, now);
}

Result<TransferEligibility> GeneratorControlEngine::transfer_eligibility(const GeneratorId& id,
                                                                         EpochMillis now) const {
  auto snapshot = view();
  const auto it = snapshot->generators.find(id);
  if (it == snapshot->generators.end()) {
    return make_status(ErrorCode::UnknownGenerator, ValidationStage::Identity,
                       "generator '" + id.str() + "' is not registered in this store");
  }
  return transfer_locked(it->second, now);
}

Result<GeneratorState> GeneratorControlEngine::inspect(const GeneratorId& id) const {
  auto snapshot = view();
  const auto it = snapshot->generators.find(id);
  if (it == snapshot->generators.end()) {
    return make_status(ErrorCode::UnknownGenerator, ValidationStage::Identity,
                       "generator '" + id.str() + "' is not registered in this store");
  }
  return it->second;
}

Result<std::vector<GeneratorId>> GeneratorControlEngine::list_generators() const {
  auto snapshot = view();
  std::vector<GeneratorId> ids;
  ids.reserve(snapshot->generators.size());
  for (const auto& entry : snapshot->generators) ids.push_back(entry.first);
  return ids;
}

Result<AttemptRecord> GeneratorControlEngine::attempt(AttemptId id) const {
  auto snapshot = view();
  for (const auto& record : snapshot->attempts) {
    if (record.id == id) return record;
  }
  return make_status(ErrorCode::AttemptNotFound, ValidationStage::Actuation,
                     "attempt " + std::to_string(id.value()) + " is not in the journal");
}

Result<std::vector<AttemptRecord>> GeneratorControlEngine::attempts(const GeneratorId& id) const {
  auto snapshot = view();
  std::vector<AttemptRecord> out;
  for (const auto& record : snapshot->attempts) {
    if (record.generator == id) out.push_back(record);
  }
  return out;
}

Result<std::vector<HistoryEntry>> GeneratorControlEngine::history(const GeneratorId& id,
                                                                  std::size_t limit) const {
  auto snapshot = view();
  const auto it = snapshot->generators.find(id);
  if (it == snapshot->generators.end()) {
    return make_status(ErrorCode::UnknownGenerator, ValidationStage::Identity,
                       "generator '" + id.str() + "' is not registered in this store");
  }
  std::vector<HistoryEntry> out = it->second.history;
  if (limit > 0 && out.size() > limit) out.erase(out.begin(), out.end() - limit);
  return out;
}

Result<std::vector<AuthorityAuditEntry>> GeneratorControlEngine::authority_audit(
    const GeneratorId& id) const {
  auto snapshot = view();
  const auto it = snapshot->generators.find(id);
  if (it == snapshot->generators.end()) {
    return make_status(ErrorCode::UnknownGenerator, ValidationStage::Identity,
                       "generator '" + id.str() + "' is not registered in this store");
  }
  return it->second.audit;
}

Result<KeyWindowAcknowledgement> GeneratorControlEngine::key_window_acknowledgement() const {
  return key_window_;
}

Result<std::size_t> GeneratorControlEngine::count_evicted_keys() const {
  auto snapshot = view();
  (void)snapshot;
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  return static_cast<std::size_t>(journal_.evicted_keys());
}

Result<StoreAuditReport> GeneratorControlEngine::store_audit(bool accept_rollback,
                                                             const std::string& acceptance_note) {
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  std::size_t residue = 0;
  return store_->audit(accept_rollback, acceptance_note, &residue);
}

Result<EvaluationReport> GeneratorControlEngine::evaluate(const OperationRequest& request) const {
  auto snapshot = view();
  const auto it = snapshot->generators.find(request.generator);
  if (it == snapshot->generators.end()) {
    return make_status(ErrorCode::UnknownGenerator, ValidationStage::Identity,
                       "generator '" + request.generator.str() + "' is not registered in this store");
  }
  const EpochMillis now = request.requested_at != 0 ? request.requested_at : clock_->now_millis();
  return evaluate_locked(it->second, request, now, snapshot->commit_seq.value() == 0
                                                           ? JournalSeq{}
                                                           : JournalSeq{snapshot->commit_seq.value()});
}

// ---------------------------------------------------------------------------
// Registration and configuration
// ---------------------------------------------------------------------------
Result<GeneratorState> GeneratorControlEngine::register_generator(const GeneratorId& id,
                                                                  const RegisterOptions& options,
                                                                  EpochMillis now) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only");
  }
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  if (generators_.size() >= config_.limits.max_generators) {
    return make_status(ErrorCode::GeneratorLimitExceeded, ValidationStage::Identity,
                       "store already holds the maximum of " +
                           std::to_string(config_.limits.max_generators) + " generators");
  }
  if (find_locked(id) != nullptr) {
    return make_status(ErrorCode::DuplicateGenerator, ValidationStage::Identity,
                       "generator '" + id.str() + "' is already registered");
  }
  GeneratorState state{};
  state.id = id;
  state.generation.hardware = options.hardware_generation;
  state.generation.binding = BindingEpoch{1};
  state.commissioned = options.commissioned;
  state.lifecycle = options.lifecycle == LifecycleState::Unknown ? LifecycleState::Commissioned
                                                                 : options.lifecycle;
  state.operating = OperatingState::Stopped;
  state.mode = OperatingMode::Normal;
  state.synchronization = SynchronizationState::Unknown;
  state.breaker = BreakerPosition::Unknown;
  state.revision = StateRevision{1};
  state.requirement = options.requirement;
  state.sync_policy = options.sync_policy;
  state.transfer_policy = options.transfer_policy;

  // Safety and protection checks are always required, whatever the caller asked
  // for: an installation may add checks but never remove them.
  std::set<CheckKind> kinds;
  for (const CheckKind kind : options.required_checks) kinds.insert(kind);
  for (const CheckKind kind : default_required_checks()) {
    if (check_class(kind) == CheckClass::Safety || check_class(kind) == CheckClass::Protection) {
      kinds.insert(kind);
    }
  }
  state.required_checks.assign(kinds.begin(), kinds.end());

  HistoryEntry entry{};
  entry.seq = state.next_history_seq;
  state.next_history_seq = entry.seq.next();
  entry.at = now;
  entry.operation = OperationKind::Unknown;
  entry.to_lifecycle = state.lifecycle;
  entry.to_operating = state.operating;
  entry.mode = state.mode;
  entry.detail = "generator registered with hardware generation " +
                 std::to_string(state.generation.hardware.value());
  append_history_locked(state, entry);

  generators_.emplace(id, state);
  GENCTL_TRY(commit_locked(now));
  return state;
}

Status GeneratorControlEngine::configure_requirement(const GeneratorId& id, StateRevision revision,
                                                     const ResourceRequirement& requirement,
                                                     EpochMillis now) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only");
  }
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  GENCTL_TRY(validate_controller_locked(id, controller_, revision));
  GeneratorState* state = find_locked(id);
  state->requirement = requirement;
  GENCTL_TRY(bump_revision_locked(*state));
  return commit_locked(now);
}

Status GeneratorControlEngine::configure_sync_policy(const GeneratorId& id, StateRevision revision,
                                                     const SyncPolicy& policy, EpochMillis now) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only");
  }
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  GENCTL_TRY(validate_controller_locked(id, controller_, revision));
  GeneratorState* state = find_locked(id);
  state->sync_policy = policy;
  GENCTL_TRY(bump_revision_locked(*state));
  return commit_locked(now);
}

Status GeneratorControlEngine::configure_transfer_policy(const GeneratorId& id,
                                                         StateRevision revision,
                                                         const TransferPolicy& policy,
                                                         EpochMillis now) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only");
  }
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  GENCTL_TRY(validate_controller_locked(id, controller_, revision));
  GeneratorState* state = find_locked(id);
  state->transfer_policy = policy;
  GENCTL_TRY(bump_revision_locked(*state));
  return commit_locked(now);
}

Status GeneratorControlEngine::set_transfer_path(const TransferPathUpdate& update) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only");
  }
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  GENCTL_TRY(validate_controller_locked(update.generator, update.controller, update.revision));
  GeneratorState* state = find_locked(update.generator);
  state->transfer_path = update.path;
  GENCTL_TRY(bump_revision_locked(*state));
  return commit_locked(clock_->now_millis());
}

Status GeneratorControlEngine::grant_authority(const AuthorityUpdate& update) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only");
  }
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  GENCTL_TRY(validate_controller_locked(update.generator, update.controller, update.revision));
  const EpochMillis now = clock_->now_millis();
  if (update.grant.cls == AuthorityClass::Emergency) {
    GENCTL_TRY(validate_emergency_grant(update.grant, default_emergency_policy(), now));
  } else if (update.grant.cls == AuthorityClass::None) {
    return make_status(ErrorCode::AuthorityMissing, ValidationStage::Authority,
                       "an authority grant must carry a class");
  }
  GeneratorState* state = find_locked(update.generator);
  state->authority = update.grant;
  AuthorityAuditEntry entry{};
  entry.seq = state->next_history_seq;
  state->next_history_seq = entry.seq.next();
  entry.at = now;
  entry.generator = update.generator;
  entry.operation = OperationKind::Unknown;
  entry.cls = update.grant.cls;
  entry.epoch = update.grant.epoch;
  entry.granted_by = update.grant.granted_by;
  entry.reason = update.grant.reason;
  state->audit.push_back(entry);
  while (state->audit.size() > config_.limits.max_audit_entries) {
    state->audit.erase(state->audit.begin());
  }
  GENCTL_TRY(bump_revision_locked(*state));
  return commit_locked(now);
}

Status GeneratorControlEngine::record_check(const CheckUpdate& update) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only");
  }
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  GENCTL_TRY(validate_controller_locked(update.generator, update.controller, update.revision));
  GeneratorState* state = find_locked(update.generator);
  bool replaced = false;
  for (auto& record : state->checks) {
    if (record.kind == update.record.kind) {
      record = update.record;
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    if (state->checks.size() >= config_.limits.max_checks) {
      return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Readiness,
                         "check binding is full");
    }
    state->checks.push_back(update.record);
  }
  GENCTL_TRY(bump_revision_locked(*state));
  return commit_locked(update.record.observed_at != 0 ? update.record.observed_at
                                                      : clock_->now_millis());
}

Status GeneratorControlEngine::record_resource(const ResourceUpdate& update) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only");
  }
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  GENCTL_TRY(validate_controller_locked(update.generator, update.controller, update.revision));
  GeneratorState* state = find_locked(update.generator);
  if (update.set_fuel_level) state->resources.fuel_level = update.fuel_level;
  if (update.set_fuel_capacity) state->resources.fuel_capacity = update.fuel_capacity;
  if (update.set_fuel_consumption) state->resources.fuel_consumption = update.fuel_consumption;
  if (update.set_lube_oil_pressure) state->resources.lube_oil_pressure = update.lube_oil_pressure;
  if (update.set_coolant_temperature) {
    state->resources.coolant_temperature = update.coolant_temperature;
  }
  if (update.set_coolant_level) state->resources.coolant_level = update.coolant_level;
  if (update.set_battery_voltage) state->resources.battery_voltage = update.battery_voltage;
  if (update.set_compressed_air) state->resources.compressed_air_pressure = update.compressed_air_pressure;
  GENCTL_TRY(bump_revision_locked(*state));
  return commit_locked(clock_->now_millis());
}

Status GeneratorControlEngine::record_sync_precondition(const SyncPreconditionUpdate& update) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only");
  }
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  GENCTL_TRY(validate_controller_locked(update.generator, update.controller, update.revision));
  GeneratorState* state = find_locked(update.generator);
  bool replaced = false;
  for (auto& record : state->sync_preconditions) {
    if (record.kind == update.record.kind) {
      record = update.record;
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    if (state->sync_preconditions.size() >= config_.limits.max_preconditions) {
      return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Synchronization,
                         "synchronization precondition binding is full");
    }
    state->sync_preconditions.push_back(update.record);
  }
  GENCTL_TRY(bump_revision_locked(*state));
  return commit_locked(update.record.observed_at != 0 ? update.record.observed_at
                                                      : clock_->now_millis());
}

Status GeneratorControlEngine::record_transfer_precondition(
    const TransferPreconditionUpdate& update) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only");
  }
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  GENCTL_TRY(validate_controller_locked(update.generator, update.controller, update.revision));
  GeneratorState* state = find_locked(update.generator);
  bool replaced = false;
  for (auto& record : state->transfer_preconditions) {
    if (record.kind == update.record.kind) {
      record = update.record;
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    if (state->transfer_preconditions.size() >= config_.limits.max_preconditions) {
      return make_status(ErrorCode::LengthLimitExceeded, ValidationStage::Transfer,
                         "transfer precondition binding is full");
    }
    state->transfer_preconditions.push_back(update.record);
  }
  GENCTL_TRY(bump_revision_locked(*state));
  return commit_locked(update.record.observed_at != 0 ? update.record.observed_at
                                                      : clock_->now_millis());
}

Status GeneratorControlEngine::record_switch_authority(const SwitchAuthorityUpdate& update) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only");
  }
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  GENCTL_TRY(validate_controller_locked(update.generator, update.controller, update.revision));
  GeneratorState* state = find_locked(update.generator);
  state->switch_authority = update.token;
  GENCTL_TRY(bump_revision_locked(*state));
  return commit_locked(clock_->now_millis());
}

Status GeneratorControlEngine::acknowledge_key_window(const std::string& acknowledged_by,
                                                      const std::string& reason,
                                                      EpochMillis now) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only");
  }
  if (acknowledged_by.empty()) {
    return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                       "the key window acknowledgement must name who accepted it");
  }
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  key_window_.acknowledged_at_seq = store_->commit_seq();
  key_window_.acknowledged_at = now;
  key_window_.acknowledged_by = acknowledged_by;
  key_window_.reason = reason;
  key_window_.evicted_keys = journal_.evicted_keys();
  return commit_locked(now);
}

// ---------------------------------------------------------------------------
// Lifecycle and mode transitions route through the same rule table
// ---------------------------------------------------------------------------
namespace {

Result<OperationKind> lifecycle_operation(LifecycleState current, LifecycleState target) {
  if (current == target) {
    return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                       "lifecycle already matches the requested value");
  }
  switch (target) {
    case LifecycleState::Maintenance: return OperationKind::EnterMaintenance;
    case LifecycleState::Isolated: return OperationKind::Isolate;
    case LifecycleState::Retired: return OperationKind::Retire;
    case LifecycleState::Commissioned:
      return current == LifecycleState::Isolated ? OperationKind::ReturnToService
                                                 : OperationKind::ExitMaintenance;
    case LifecycleState::Unknown:
    default:
      return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                         "lifecycle cannot be set to 'unknown' by an operator");
  }
}

}  // namespace

Status GeneratorControlEngine::update_lifecycle(const LifecycleUpdate& update) {
  auto snapshot = view();
  const auto it = snapshot->generators.find(update.generator);
  if (it == snapshot->generators.end()) {
    return make_status(ErrorCode::UnknownGenerator, ValidationStage::Identity,
                       "generator '" + update.generator.str() + "' is not registered in this store");
  }
  GENCTL_TRY_ASSIGN(operation, lifecycle_operation(it->second.lifecycle, update.lifecycle));
  OperationRequest request{};
  request.generator = update.generator;
  request.operation = operation;
  request.controller = update.controller;
  request.generation = it->second.generation;
  request.revision = update.revision;
  request.authority = update.authority;
  request.idempotency_key = update.idempotency_key;
  request.requested_at = update.requested_at;
  request.note = update.reason;
  GENCTL_TRY_ASSIGN(record, execute(request, false));
  (void)record;
  return Status::success();
}

Status GeneratorControlEngine::update_mode(const ModeUpdate& update) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only");
  }
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  GENCTL_TRY(validate_controller_locked(update.generator, update.controller, update.revision));
  GeneratorState* state = find_locked(update.generator);
  const EpochMillis now =
      update.requested_at != 0 ? update.requested_at : clock_->now_millis();

  if (state->lifecycle == LifecycleState::Retired || state->lifecycle == LifecycleState::Unknown) {
    return make_status(ErrorCode::LifecycleClosed, ValidationStage::Lifecycle,
                       std::string("lifecycle '") + std::string(to_string(state->lifecycle)) +
                           "' does not permit a mode change");
  }
  if (update.mode == OperatingMode::Unknown) {
    return make_status(ErrorCode::InvalidArgument, ValidationStage::Format,
                       "operating mode cannot be set to 'unknown'");
  }

  const Status authority_status =
      validate_authority_grant(update.authority, authority_class_for_mode(update.mode),
                               OperationKind::Unknown, now);
  if (!authority_status.ok()) return authority_status;

  if ((update.mode == OperatingMode::Test || update.mode == OperatingMode::Service) &&
      is_running_state(state->operating)) {
    return make_status(ErrorCode::ModeNotPermitted, ValidationStage::Mode,
                       "the generator is running; a test or service mode change would silently "
                       "reinterpret a production run");
  }
  if (state->mode == OperatingMode::Test && update.mode != OperatingMode::Normal &&
      update.mode != OperatingMode::Test) {
    return make_status(ErrorCode::TestOperationNotPermitted, ValidationStage::Mode,
                       "leave test mode before selecting another mode");
  }

  const OperatingMode previous = state->mode;
  state->mode = update.mode;

  HistoryEntry entry{};
  entry.seq = state->next_history_seq;
  state->next_history_seq = entry.seq.next();
  entry.at = now;
  entry.operation = OperationKind::Unknown;
  entry.from_lifecycle = state->lifecycle;
  entry.to_lifecycle = state->lifecycle;
  entry.from_operating = state->operating;
  entry.to_operating = state->operating;
  entry.mode = state->mode;
  entry.detail = std::string("operating mode changed from '") + std::string(to_string(previous)) +
                 "' to '" + std::string(to_string(state->mode)) + "' by " +
                 (update.authority.granted_by.empty() ? std::string("an unnamed authority")
                                                      : update.authority.granted_by);
  append_history_locked(*state, entry);
  GENCTL_TRY(bump_revision_locked(*state));
  return commit_locked(now);
}

// ---------------------------------------------------------------------------
// Actuation
// ---------------------------------------------------------------------------
namespace {

bool requires_actuation(OperationKind operation) {
  switch (operation) {
    case OperationKind::EnterMaintenance:
    case OperationKind::ExitMaintenance:
    case OperationKind::Isolate:
    case OperationKind::ReturnToService:
    case OperationKind::Retire:
      return false;
    case OperationKind::Unknown:
      return false;
    default:
      return true;
  }
}

Status apply_command_effect(GeneratorState& state, const TransitionRule& rule,
                            const AttemptRecord& attempt, EpochMillis now, bool provisional) {
  if (rule.provisional == OperatingState::Synchronized) {
    return make_status(ErrorCode::Internal, ValidationStage::Internal,
                       "transition table would allow a command to assert synchronization");
  }
  const LifecycleState from_lifecycle = state.lifecycle;
  const OperatingState from_operating = state.operating;

  if (rule.changes_lifecycle && rule.lifecycle_target != LifecycleState::Unknown) {
    state.lifecycle = rule.lifecycle_target;
  }
  if (rule.provisional != OperatingState::Unknown) state.operating = rule.provisional;
  if (rule.mode_after != OperatingMode::Unknown) state.mode = rule.mode_after;
  if (attempt.operation == OperationKind::Synchronize) {
    state.synchronization = SynchronizationState::Synchronizing;
  } else if (attempt.operation == OperationKind::Desynchronize) {
    state.synchronization = SynchronizationState::NotSynchronized;
  }

  HistoryEntry entry{};
  entry.seq = state.next_history_seq;
  state.next_history_seq = entry.seq.next();
  entry.at = now;
  entry.operation = attempt.operation;
  entry.attempt = attempt.id;
  entry.from_lifecycle = from_lifecycle;
  entry.to_lifecycle = state.lifecycle;
  entry.from_operating = from_operating;
  entry.to_operating = state.operating;
  entry.mode = state.mode;
  entry.provisional = provisional;
  entry.detail = provisional
                     ? "command recorded; the operating state is provisional until an observation "
                       "proves the physical effect"
                     : "administrative transition applied; no electrical effect is claimed";
  CanonicalWriter writer(CanonicalLimits{4096, 512, 256, 32});
  (void)writer.put_string(state.id.str());
  (void)writer.put_u64(entry.seq.value());
  (void)writer.put_u8(static_cast<std::uint8_t>(attempt.operation));
  (void)writer.put_u64(attempt.id.value());
  (void)writer.put_u8(static_cast<std::uint8_t>(entry.to_operating));
  (void)writer.put_u8(static_cast<std::uint8_t>(entry.to_lifecycle));
  entry.digest = sha256(writer.bytes());
  state.history.push_back(entry);
  while (state.history.size() > kHistoryRetention) state.history.erase(state.history.begin());
  return Status::success();
}

}  // namespace

Result<AttemptRecord> GeneratorControlEngine::execute(const OperationRequest& request,
                                                      bool dry_run) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only; actuation is refused");
  }
  bool actuating = false;
  AttemptRecord attempt{};
  {
    std::lock_guard<std::mutex> guard(mutation_mutex_);
    const EpochMillis now =
        request.requested_at != 0 ? request.requested_at : clock_->now_millis();

    // ---- stage 1: format ------------------------------------------------
    if (request.idempotency_key.empty()) {
      return make_status(ErrorCode::IdempotencyKeyMissing, ValidationStage::Format,
                         "an actuating request must carry an idempotency key");
    }
    GENCTL_TRY_ASSIGN(fingerprint, fingerprint_request(request));

    // ---- stage 3: idempotent replay, before any staleness check ---------
    {
      Result<ReplayEntry> replay = journal_.lookup_key(request.idempotency_key);
      if (replay.ok()) {
        if (!(replay.value().fingerprint == fingerprint)) {
          return make_status(
              ErrorCode::IdempotencyKeyConflict, ValidationStage::IdempotencyConflict,
              "idempotency key '" + request.idempotency_key.str() +
                  "' was already used for a different request; a conflicting retry is refused "
                  "rather than treated as a new actuation");
        }
        const AttemptRecord* prior = journal_.find(replay.value().attempt);
        if (prior == nullptr) {
          return make_status(
              ErrorCode::IdempotencyWindowExpired, ValidationStage::IdempotencyReplay,
              "idempotency key '" + request.idempotency_key.str() +
                  "' is inside the replay window but its attempt record has been retired; the "
                  "runtime will not actuate again for this key");
        }
        AttemptRecord copy = *prior;
        copy.replayed = true;
        return copy;
      }
    }

    if (!dry_run && config_.require_key_window_acknowledgement &&
        journal_.evicted_keys() > key_window_.evicted_keys) {
      return make_status(
          ErrorCode::IdempotencyWindowExpired, ValidationStage::IdempotencyReplay,
          "the idempotency window has evicted " +
              std::to_string(journal_.evicted_keys() - key_window_.evicted_keys) +
              " key(s) since the last acknowledgement; a retry of an evicted key would be "
              "indistinguishable from a new request, so new actuation is refused until an "
              "operator acknowledges the window with 'key-window --accept'");
    }

    const GeneratorState* state = find_locked(request.generator);
    if (state == nullptr) {
      return make_status(ErrorCode::UnknownGenerator, ValidationStage::Identity,
                         "generator '" + request.generator.str() +
                             "' is not registered in this store");
    }

    if (const AttemptRecord* blocking = journal_.blocking_for(request.generator)) {
      return make_status(
          ErrorCode::AttemptUnresolved, ValidationStage::Actuation,
          "attempt " + std::to_string(blocking->id.value()) + " (" +
              std::string(to_string(blocking->operation)) +
              ") has an outstanding effect that no observation has resolved; resolve it by "
              "observation before issuing another command to this generator");
    }

    GENCTL_TRY_ASSIGN(report, evaluate_locked(*state, request, now, journal_.retained_floor()));
    if (!report.permitted) {
      return make_status(report.primary_error, report.primary_stage, report.primary_detail);
    }
    const TransitionRule* rule = rule_for(request.operation);
    if (rule == nullptr) {
      return make_status(ErrorCode::NotImplemented, ValidationStage::Transition,
                         "operation has no transition rule in this build");
    }
    actuating = requires_actuation(request.operation);

    if (dry_run) {
      AttemptRecord planned{};
      planned.generator = request.generator;
      planned.operation = request.operation;
      planned.planned_against.controller = controller_;
      planned.planned_against.generation = state->generation;
      planned.planned_against.revision = state->revision;
      planned.planned_against.readiness_binding = report.readiness.binding_digest;
      planned.planned_against.resource_binding = report.resources.binding_digest;
      planned.planned_against.sync_binding = report.synchronization.binding_digest;
      planned.planned_against.transfer_binding = report.transfer.binding_digest;
      planned.key = request.idempotency_key;
      planned.fingerprint = fingerprint;
      planned.authority = rule->accept_any_authority ? request.authority.cls
                                                     : rule->required_authority;
      planned.authority_epoch = controller_.epoch;
      planned.command_state = CommandState::Authorized;
      planned.effect_state = EffectState::NotObserved;
      planned.created_at = now;
      planned.updated_at = now;
      planned.note = request.note;
      return planned;
    }

    maybe_crash(config_.crash_point, CrashPoint::BeforeReserve);

    const AttemptId predicted = journal_.next_attempt_id();
    attempt.generator = request.generator;
    attempt.operation = request.operation;
    attempt.planned_against.controller = controller_;
    attempt.planned_against.generation = state->generation;
    attempt.planned_against.revision = state->revision;
    attempt.planned_against.readiness_binding = report.readiness.binding_digest;
    attempt.planned_against.resource_binding = report.resources.binding_digest;
    attempt.planned_against.sync_binding = report.synchronization.binding_digest;
    attempt.planned_against.transfer_binding = report.transfer.binding_digest;
    attempt.key = request.idempotency_key;
    attempt.fingerprint = fingerprint;
    attempt.authority =
        rule->accept_any_authority ? request.authority.cls : rule->required_authority;
    attempt.authority_epoch = controller_.epoch;
    attempt.authority_reason = request.authority.reason;
    attempt.command_id = CommandId{predicted.value()};
    attempt.created_at = now;
    attempt.updated_at = now;
    attempt.note = request.note;
    if (actuating) {
      // Write-ahead intent: the durable record says the command is being issued
      // BEFORE the adapter is called, so a crash during actuation can never lead to
      // a silent second issuance.
      attempt.command_state = CommandState::Issued;
      attempt.ack_status = AdapterAckStatus::None;
      attempt.effect_state = EffectState::NotObserved;
    } else {
      attempt.command_state = CommandState::Acknowledged;
      attempt.ack_status = AdapterAckStatus::Accepted;
      attempt.acknowledged_at = now;
      attempt.ack_detail = "administrative operation applied by this runtime; no electrical effect "
                           "is claimed";
      attempt.effect_state = EffectState::Verified;
      attempt.effect_detail = "administrative transition verified by the resulting recorded state";
    }

    // Apply to a copy so that a failed publication leaves nothing half applied.
    GeneratorState staged = *state;
    {
      const Status applied = apply_command_effect(staged, *rule, attempt, now, actuating);
      if (!applied.ok()) return applied;
    }
    GENCTL_TRY(bump_revision_locked(staged));
    if (report.advisory_waived) {
      AuthorityAuditEntry waiver{};
      waiver.seq = staged.next_history_seq;
      staged.next_history_seq = waiver.seq.next();
      waiver.at = now;
      waiver.generator = request.generator;
      waiver.operation = request.operation;
      waiver.cls = request.authority.cls;
      waiver.epoch = controller_.epoch;
      waiver.granted_by = request.authority.granted_by;
      waiver.reason = request.authority.reason;
      waiver.waived_checks = report.waived_checks;
      staged.audit.push_back(waiver);
      while (staged.audit.size() > config_.limits.max_audit_entries) {
        staged.audit.erase(staged.audit.begin());
      }
    }

    GeneratorState previous_state = *state;
    const std::vector<AttemptRecord> previous_records = journal_.records();
    const std::vector<ReplayEntry> previous_replay = journal_.replay_entries();
    const AttemptId previous_next_id = journal_.next_attempt_id();
    const JournalSeq previous_next_seq = journal_.next_seq();
    const JournalSeq previous_floor = journal_.retained_floor();
    const std::uint64_t previous_evicted = journal_.evicted_keys();

    *find_locked(request.generator) = staged;
    GENCTL_TRY_ASSIGN(stored, journal_.append(attempt));
    attempt = stored;

    const Status published = commit_locked(now);
    if (!published.ok()) {
      *find_locked(request.generator) = previous_state;
      journal_.restore(previous_next_id, previous_next_seq, previous_floor, previous_evicted,
                       previous_records, previous_replay);
      rebuild_view_locked();
      return published;
    }
  }

  if (!actuating) {
    maybe_crash(config_.crash_point, CrashPoint::AfterAckCommit);
    return attempt;
  }

  maybe_crash(config_.crash_point, CrashPoint::AfterPublishBeforeActuation);

  CommandRequest command{};
  command.generator = attempt.generator;
  command.operation = attempt.operation;
  command.command_id = attempt.command_id;
  command.attempt_id = attempt.id;
  command.epoch = controller_.epoch;
  command.issued_at = attempt.created_at;
  command.advisory_timeout = Duration{30};
  command.test_mode = attempt.operation == OperationKind::TestStart ||
                      attempt.operation == OperationKind::TestStop;

  Result<AdapterAck> ack = adapter_->issue(command);
  maybe_crash(config_.crash_point, CrashPoint::AfterActuationBeforeAckCommit);

  {
    std::lock_guard<std::mutex> guard(mutation_mutex_);
    GeneratorState* state = find_locked(attempt.generator);
    const AttemptRecord* current = journal_.find(attempt.id);
    if (state == nullptr || current == nullptr) {
      return make_status(ErrorCode::Internal, ValidationStage::Internal,
                         "attempt disappeared between issuance and acknowledgement commit");
    }
    AttemptRecord updated = *current;
    const EpochMillis now = clock_->now_millis();
    updated.updated_at = now;
    if (ack.ok()) {
      updated.ack_status = ack.value().status;
      updated.acknowledged_at = ack.value().acknowledged_at != 0 ? ack.value().acknowledged_at : now;
      updated.ack_detail = ack.value().detail;
      switch (ack.value().status) {
        case AdapterAckStatus::Accepted:
          updated.command_state = CommandState::Acknowledged;
          break;
        case AdapterAckStatus::Rejected:
          updated.command_state = CommandState::Rejected;
          break;
        case AdapterAckStatus::Unsupported:
          updated.command_state = CommandState::Unsupported;
          break;
        case AdapterAckStatus::Busy:
          updated.command_state = CommandState::Busy;
          break;
        case AdapterAckStatus::Invalid:
        case AdapterAckStatus::None:
        default:
          updated.command_state = CommandState::AdapterUnavailable;
          break;
      }
    } else {
      updated.command_state = CommandState::AdapterUnavailable;
      updated.ack_status = AdapterAckStatus::None;
      updated.ack_detail = ack.status().message();
    }
    if (!command_is_unresolved(updated.command_state) &&
        updated.command_state != CommandState::Acknowledged) {
      // A refused command cannot have produced the commanded effect, but the
      // runtime still refuses to claim anything about the physical state.
      updated.effect_state = EffectState::Unknown;
      updated.effect_detail = "the command was not accepted; no effect is claimed";
    }
    GENCTL_TRY(journal_.update(updated));
    GENCTL_TRY(commit_locked(now));
    attempt = updated;
  }

  maybe_crash(config_.crash_point, CrashPoint::AfterAckCommit);
  return attempt;
}

// ---------------------------------------------------------------------------
// Observation, verification and recovery
// ---------------------------------------------------------------------------
Result<ObserveOutcome> GeneratorControlEngine::observe(const ObserveRequest& request) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only; observation would not be persisted");
  }
  {
    std::lock_guard<std::mutex> guard(mutation_mutex_);
    if (find_locked(request.generator) == nullptr) {
      return make_status(ErrorCode::UnknownGenerator, ValidationStage::Identity,
                         "generator '" + request.generator.str() +
                             "' is not registered in this store");
    }
    if (request.controller.epoch != controller_.epoch) {
      return make_status(ErrorCode::StaleControlEpoch, ValidationStage::Fencing,
                         "observation was requested under control epoch " +
                             std::to_string(request.controller.epoch.value()) +
                             " but the current epoch is " +
                             std::to_string(controller_.epoch.value()));
    }
    if (request.controller.incarnation != controller_.incarnation) {
      return make_status(ErrorCode::StaleIncarnation, ValidationStage::Fencing,
                         "observation was requested by an older controller incarnation");
    }
  }

  // The adapter is called with no engine lock held.
  Result<EngineObservation> observed = adapter_->observe(request.generator);
  if (!observed.ok()) return observed.status();
  const EngineObservation& observation = observed.value();
  if (!(observation.generator == request.generator)) {
    return make_status(ErrorCode::InvalidArgument, ValidationStage::Observation,
                       "adapter returned an observation for a different generator");
  }
  if (!is_effect_authoritative(observation.source)) {
    return make_status(ErrorCode::ObservationNotAuthoritative, ValidationStage::Observation,
                       std::string("observation source '") +
                           std::string(to_string(observation.source)) +
                           "' cannot prove a physical effect");
  }
  if (observation.observed_at < 0) {
    return make_status(ErrorCode::EvidenceMissing, ValidationStage::Observation,
                       "adapter returned an observation with an unusable instant");
  }

  std::lock_guard<std::mutex> guard(mutation_mutex_);
  const EpochMillis now =
      request.requested_at != 0 ? request.requested_at : clock_->now_millis();
  GeneratorState* state = find_locked(request.generator);
  if (state == nullptr) {
    return make_status(ErrorCode::UnknownGenerator, ValidationStage::Identity,
                       "generator disappeared between observation and commit");
  }

  if (observation.reported_state == OperatingState::Synchronized &&
      observation.synchronization != SynchronizationState::Synchronized) {
    return make_status(ErrorCode::EvidenceContradictory, ValidationStage::Observation,
                       "observation reports a synchronized machine but a non-synchronized "
                       "synchronization state; the observation is refused rather than merged");
  }

  const Result<Millis> freshness = sub_millis(now, observation.observed_at);
  if (!freshness.ok() || freshness.value() > config_.evidence.freshness.max_age_millis) {
    // An observation that is somehow older than the configured bound is not merged.
    if (!freshness.ok()) {
      return make_status(ErrorCode::EvidenceFutureDated, ValidationStage::Observation,
                         "observation instant cannot be compared with the current instant");
    }
  }

  state->last_observed_at = observation.observed_at;
  state->last_observation_seq = state->last_observation_seq.next();
  state->breaker = observation.breaker;
  state->breaker_observed_at = observation.observed_at;
  state->synchronization = observation.synchronization;
  if (observation.reported_state != OperatingState::Unknown) {
    state->operating = observation.reported_state;
  }

  ObserveOutcome outcome{};
  outcome.observation = observation;
  outcome.detail = observation.detail;

  if (request.resolve_attempt) {
    const AttemptRecord* record = journal_.find(request.attempt);
    if (record == nullptr) {
      return make_status(ErrorCode::AttemptNotFound, ValidationStage::Actuation,
                         "attempt " + std::to_string(request.attempt.value()) +
                             " is not in the journal");
    }
    AttemptRecord updated = *record;
    const EffectVerdict verdict = classify_effect(updated.operation, observation);
    bool settled = true;
    if (verdict == EffectVerdict::Inconclusive) {
      const Result<Millis> age = sub_millis(now, updated.created_at);
      settled = age.ok() && age.value() >= config_.effect_settle_millis;
    }
    switch (verdict) {
      case EffectVerdict::Satisfied:
        updated.effect_state = EffectState::Verified;
        updated.effect_detail = "observed effect matches the commanded operation";
        break;
      case EffectVerdict::Contradicted:
        updated.effect_state = EffectState::ObservedContradictory;
        updated.effect_detail = "observation contradicts the commanded operation";
        break;
      case EffectVerdict::Inconclusive:
        updated.effect_state =
            settled ? EffectState::ObservedContradictory : EffectState::NotObserved;
        updated.effect_detail =
            settled ? "the commanded effect is absent after the settle window; the attempt is "
                      "recorded as contradictory so it can be abandoned explicitly"
                    : "the commanded effect is not visible yet; the attempt stays unresolved";
        break;
    }
    updated.observed_state = observation.reported_state;
    updated.observed_synchronization = observation.synchronization;
    updated.observed_breaker = observation.breaker;
    updated.effect_source = observation.source;
    updated.effect_observed_at = observation.observed_at;
    updated.effect_observation_seq = state->last_observation_seq;
    updated.updated_at = now;
    CanonicalWriter writer(CanonicalLimits{4096, 512, 256, 32});
    (void)writer.put_string(observation.generator.str());
    (void)writer.put_i64(observation.observed_at);
    (void)writer.put_u8(static_cast<std::uint8_t>(observation.source));
    (void)writer.put_u8(static_cast<std::uint8_t>(observation.reported_state));
    (void)writer.put_u8(static_cast<std::uint8_t>(observation.synchronization));
    (void)writer.put_u8(static_cast<std::uint8_t>(observation.breaker));
    (void)writer.put_i64(observation.electrical.line_voltage.millivolts);
    (void)writer.put_i64(observation.electrical.frequency.millihertz);
    (void)writer.put_i64(observation.electrical.phase_angle.millidegrees);
    updated.effect_digest = sha256(writer.bytes());
    GENCTL_TRY(journal_.update(updated));
    outcome.attempt_resolved = true;
    outcome.attempt = updated;
    outcome.effect = updated.effect_state;
  }

  GENCTL_TRY(bump_revision_locked(*state));
  GENCTL_TRY(commit_locked(now));
  outcome.revision = state->revision;
  return outcome;
}

Result<AttemptRecord> GeneratorControlEngine::verify(const VerifyOptions& options) {
  ObserveRequest request{};
  request.controller = options.controller;
  request.attempt = options.attempt;
  request.resolve_attempt = true;
  request.requested_at = options.requested_at;

  {
    std::lock_guard<std::mutex> guard(mutation_mutex_);
    const AttemptRecord* record = journal_.find(options.attempt);
    if (record == nullptr) {
      return make_status(ErrorCode::AttemptNotFound, ValidationStage::Actuation,
                         "attempt " + std::to_string(options.attempt.value()) +
                             " is not in the journal");
    }
    request.generator = record->generator;
  }
  GENCTL_TRY_ASSIGN(outcome, observe(request));
  if (!outcome.attempt_resolved) {
    return make_status(ErrorCode::Internal, ValidationStage::Internal,
                       "verification did not resolve the attempt");
  }
  return outcome.attempt;
}

Result<AttemptRecord> GeneratorControlEngine::abandon(AttemptId id, const std::string& reason,
                                                      EpochMillis now) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only");
  }
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  const AttemptRecord* record = journal_.find(id);
  if (record == nullptr) {
    return make_status(ErrorCode::AttemptNotFound, ValidationStage::Actuation,
                       "attempt " + std::to_string(id.value()) + " is not in the journal");
  }
  if (record->effect_state != EffectState::ObservedContradictory) {
    return make_status(
        ErrorCode::EffectNotObserved, ValidationStage::Verification,
        "attempt " + std::to_string(id.value()) +
            " may only be abandoned after an observation proved that the commanded effect did not "
            "occur; the runtime never abandons an attempt on a timeout or on its own initiative");
  }
  AttemptRecord updated = *record;
  updated.command_state = CommandState::Abandoned;
  updated.effect_state = EffectState::VerificationFailed;
  updated.effect_detail = reason.empty() ? "abandoned after a contradictory observation" : reason;
  updated.updated_at = now != 0 ? now : clock_->now_millis();
  GENCTL_TRY(journal_.update(updated));
  GENCTL_TRY(commit_locked(updated.updated_at));
  return updated;
}

Status GeneratorControlEngine::demote_volatile_evidence_locked(std::size_t* demoted) {
  std::size_t count = 0;
  const std::string detail =
      "evidence was observed by a previous controller incarnation; a new incarnation must "
      "re-observe it before it can be used";
  auto demote = [&](auto& evidence) {
    if (evidence.lifetime != EvidenceLifetime::VolatileObservation) return;
    if (evidence.state != EvidenceState::Present) return;
    evidence.state = EvidenceState::Stale;
    evidence.detail = detail;
    ++count;
  };

  for (auto& generator_entry : generators_) {
    GeneratorState& state = generator_entry.second;
    for (auto& check : state.checks) {
      if (check.lifetime != EvidenceLifetime::VolatileObservation) continue;
      if (check.state != EvidenceState::Present) continue;
      check.state = EvidenceState::Stale;
      check.detail = detail;
      ++count;
    }
    for (auto& precondition : state.sync_preconditions) {
      if (precondition.lifetime != EvidenceLifetime::VolatileObservation) continue;
      if (precondition.state != EvidenceState::Present) continue;
      precondition.state = EvidenceState::Stale;
      precondition.detail = detail;
      ++count;
    }
    for (auto& precondition : state.transfer_preconditions) {
      if (precondition.lifetime != EvidenceLifetime::VolatileObservation) continue;
      if (precondition.state != EvidenceState::Present) continue;
      precondition.state = EvidenceState::Stale;
      precondition.detail = detail;
      ++count;
    }
    demote(state.resources.fuel_level);
    demote(state.resources.fuel_capacity);
    demote(state.resources.fuel_consumption);
    demote(state.resources.fuel_temperature);
    demote(state.resources.lube_oil_pressure);
    demote(state.resources.lube_oil_temperature);
    demote(state.resources.coolant_temperature);
    demote(state.resources.coolant_level);
    demote(state.resources.battery_voltage);
    demote(state.resources.compressed_air_pressure);
    demote(state.switch_authority);

    // Synchronization cannot survive a restart: it is a live electrical condition,
    // not a stored fact.
    if (state.synchronization != SynchronizationState::Unknown) {
      state.synchronization = SynchronizationState::Unknown;
      ++count;
    }
    if (state.breaker != BreakerPosition::Unknown) {
      state.breaker = BreakerPosition::Unknown;
      ++count;
    }
    if (state.operating == OperatingState::Synchronized) {
      const LifecycleState lifecycle = state.lifecycle;
      state.operating = OperatingState::Unknown;
      HistoryEntry reopen{};
      reopen.seq = state.next_history_seq;
      state.next_history_seq = reopen.seq.next();
      reopen.at = clock_->now_millis();
      reopen.operation = OperationKind::Unknown;
      reopen.from_lifecycle = lifecycle;
      reopen.to_lifecycle = state.lifecycle;
      reopen.from_operating = OperatingState::Synchronized;
      reopen.to_operating = OperatingState::Unknown;
      reopen.mode = state.mode;
      reopen.detail = "controller incarnation changed: a synchronized condition is not carried "
                     "across a restart and must be re-observed";
      append_history_locked(state, reopen);
      ++count;
    }
  }
  if (demoted != nullptr) *demoted = count;
  return Status::success();
}

Result<RevalidateReport> GeneratorControlEngine::revalidate(const GeneratorId& id,
                                                            EpochMillis now) {
  if (read_only()) {
    return make_status(ErrorCode::StoreReadOnly, ValidationStage::Persistence,
                       "store is open read-only");
  }
  std::lock_guard<std::mutex> guard(mutation_mutex_);
  GeneratorState* state = find_locked(id);
  if (state == nullptr) {
    return make_status(ErrorCode::UnknownGenerator, ValidationStage::Identity,
                       "generator '" + id.str() + "' is not registered in this store");
  }
  RevalidateReport report{};
  report.generator = id;

  std::size_t demoted = 0;
  auto expire = [&](auto& evidence, Millis max_age) {
    if (evidence.state != EvidenceState::Present) return;
    const FreshnessAssessment freshness =
        assess_freshness(evidence.observed_at, now, FreshnessPolicy::within(max_age));
    if (freshness.verdict == FreshnessVerdict::Stale) {
      evidence.state = EvidenceState::Stale;
      evidence.detail = freshness.detail;
      ++demoted;
    } else if (freshness.verdict == FreshnessVerdict::FutureDated) {
      evidence.state = EvidenceState::Contradictory;
      evidence.detail = freshness.detail;
      ++demoted;
    }
  };

  for (auto& check : state->checks) expire(check, check.max_age_millis);
  for (auto& precondition : state->sync_preconditions) expire(precondition, precondition.max_age_millis);
  for (auto& precondition : state->transfer_preconditions) {
    expire(precondition, precondition.max_age_millis);
  }
  expire(state->resources.fuel_level, state->requirement.fuel_max_age_millis);
  expire(state->resources.fuel_capacity, state->requirement.fuel_max_age_millis);
  expire(state->resources.fuel_consumption, state->requirement.fuel_max_age_millis);
  expire(state->resources.lube_oil_pressure, state->requirement.machinery_max_age_millis);
  expire(state->resources.coolant_temperature, state->requirement.machinery_max_age_millis);
  expire(state->resources.coolant_level, state->requirement.machinery_max_age_millis);
  expire(state->resources.battery_voltage, state->requirement.machinery_max_age_millis);
  expire(state->resources.compressed_air_pressure, state->requirement.machinery_max_age_millis);
  expire(state->switch_authority, state->transfer_policy.max_token_age_millis);

  report.demoted_evidence_records = demoted;
  if (demoted > 0) {
    report.notes.push_back("marked " + std::to_string(demoted) +
                           " evidence record(s) stale or contradictory against the current "
                           "instant");
  }
  GENCTL_TRY(bump_revision_locked(*state));
  GENCTL_TRY(commit_locked(now));
  report.revision = state->revision;

  GENCTL_TRY_ASSIGN(readiness_report, readiness_locked(*state, now));
  report.readiness = readiness_report;
  GENCTL_TRY_ASSIGN(resources, resource_locked(*state, now));
  report.resources = resources;
  GENCTL_TRY_ASSIGN(synchronization, sync_locked(*state, now));
  report.synchronization = synchronization;
  GENCTL_TRY_ASSIGN(transfer, transfer_locked(*state, now));
  report.transfer = transfer;
  return report;
}

Status GeneratorControlEngine::close() {
  if (closed_) return Status::success();
  closed_ = true;
  if (adapter_opened_ && adapter_ != nullptr) {
    adapter_opened_ = false;
    GENCTL_TRY(adapter_->close());
  }
  if (store_ != nullptr) GENCTL_TRY(store_->close());
  return Status::success();
}

}  // namespace genctl
