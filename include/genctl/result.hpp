// Generator Control - explicit error model and result carrier.
//
// Every externally visible operation reports an explicit, deterministic outcome.
// Errors carry (a) a stable code, (b) the validation stage that produced them and
// (c) a human readable explanation. The stage ordering defined here is the
// documented validation precedence: when a request violates several rules at
// once, the earliest stage in ValidationStage order is the primary error, so the
// same invalid request always produces the same primary error.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace genctl {

enum class ErrorCode : std::uint16_t {
  Ok = 0,

  // ---- format / argument validation -------------------------------------
  InvalidArgument = 100,
  InvalidIdentifier,
  LengthLimitExceeded,
  MalformedEncoding,
  ValueOutOfRange,
  ArithmeticOverflow,
  DivisionByZero,
  UnsupportedUnit,
  UnsupportedField,

  // ---- identity ----------------------------------------------------------
  UnknownGenerator = 200,
  DuplicateGenerator,
  GeneratorLimitExceeded,
  GeneratorNotBound,
  DuplicateIdentity,

  // ---- idempotency -------------------------------------------------------
  IdempotencyKeyMissing = 300,
  IdempotencyKeyConflict,
  IdempotencyWindowExpired,

  // ---- fencing / stale authority ----------------------------------------
  StaleControlEpoch = 400,
  StaleIncarnation,
  StaleStateRevision,
  StaleGeneratorGeneration,
  StaleEvidenceBinding,
  FencedWriter,
  StaleAttempt,

  // ---- lifecycle / transition -------------------------------------------
  LifecycleClosed = 500,
  IllegalTransition,
  OperatingStateUnknown,
  AttemptUnresolved,
  FaultResetRequired,
  MaintenanceActive,
  GeneratorIsolated,
  GeneratorRetired,

  // ---- mode / authority --------------------------------------------------
  ModeNotPermitted = 600,
  AuthorityMissing,
  AuthorityExpired,
  AuthorityClassMismatch,
  EmergencyAuthorityRequired,
  EmergencyAuthorityNotExplicit,
  TestAuthorityRequired,
  ServiceAuthorityRequired,
  TestOperationNotPermitted,

  // ---- evidence ----------------------------------------------------------
  EvidenceMissing = 700,
  EvidenceStale,
  EvidenceUnknown,
  EvidenceUnsupported,
  EvidenceContradictory,
  EvidenceDenied,
  EvidenceFutureDated,
  EvidenceProvenanceRejected,
  ReadinessNotSatisfied,
  InterlockEngaged,
  InterlockEvidenceUnavailable,

  // ---- resource / fuel ---------------------------------------------------
  ResourceEvidenceMissing = 800,
  ResourceEvidenceStale,
  ResourceInsufficient,
  ResourceUnitMismatch,
  ResourceRuntimeUnknown,
  ResourceCapacityUnknown,

  // ---- synchronization ---------------------------------------------------
  SynchronizationNotEligible = 900,
  SynchronizationEvidenceMissing,
  SynchronizationEvidenceStale,
  SynchronizationPreconditionFailed,
  SynchronizationAuthorityMissing,

  // ---- transfer / breaker ------------------------------------------------
  TransferNotEligible = 1000,
  TransferAuthorityMissing,
  TransferAuthorityStale,
  TransferPreconditionFailed,
  TransferTopologyReferenceInvalid,

  // ---- actuation / observation ------------------------------------------
  AdapterUnavailable = 1100,
  AdapterRejected,
  AdapterUnsupported,
  AdapterBusy,
  AdapterProtocolError,
  AttemptNotFound,
  AttemptAlreadyResolved,
  EffectNotObserved,
  EffectContradictory,
  EffectVerificationFailed,
  CommandNotAcknowledged,
  ObservationNotAuthoritative,

  // ---- persistence -------------------------------------------------------
  StoreNotFound = 1200,
  StoreOpenFailed,
  StoreClosed,
  StoreLocked,
  StoreReadOnly,
  StoreCorrupt,
  StoreTruncated,
  StoreOversize,
  StoreVersionUnsupported,
  StoreIntegrityFailure,
  StoreRollbackDetected,
  StorePublicationFailed,
  StoreFlushFailed,
  StoreGenerationMissing,
  StoreRecoveryRefused,
  PathInvalid,
  PathTraversal,
  PathDeviceName,
  PathReparsePoint,
  PathNotAbsolute,
  PathTooLong,
  PathEncodingInvalid,
  PathNotDirectory,
  PathIsDirectory,

  // ---- misc --------------------------------------------------------------
  NotImplemented = 1300,
  Internal,
  Cancelled,
};

// Broad classification used by operators and by CLI exit codes.
enum class ErrorCategory : std::uint8_t {
  Ok = 0,
  InvalidRequest,
  Identity,
  Idempotency,
  StaleAuthority,
  Lifecycle,
  Mode,
  Evidence,
  Resource,
  Synchronization,
  Transfer,
  Interlock,
  Adapter,
  Persistence,
  Internal,
};

// Documented validation precedence. Lower ordinal == earlier stage == wins.
enum class ValidationStage : std::uint8_t {
  None = 0,
  Format = 1,
  Identity = 2,
  IdempotencyReplay = 3,
  IdempotencyConflict = 4,
  Fencing = 5,
  Lifecycle = 6,
  Transition = 7,
  Mode = 8,
  Authority = 9,
  Interlock = 10,
  Readiness = 11,
  Resource = 12,
  Synchronization = 13,
  Transfer = 14,
  Reservation = 15,
  Actuation = 16,
  Observation = 17,
  Verification = 18,
  Persistence = 19,
  Internal = 20,
};

[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;
[[nodiscard]] std::string_view to_string(ErrorCategory category) noexcept;
[[nodiscard]] std::string_view to_string(ValidationStage stage) noexcept;

[[nodiscard]] ErrorCategory category_of(ErrorCode code) noexcept;
[[nodiscard]] ValidationStage default_stage(ErrorCode code) noexcept;

// True when retrying the identical request later could plausibly succeed without
// changing any input. Used only for operator guidance; the runtime never retries
// an actuation on its own.
[[nodiscard]] bool is_transient(ErrorCode code) noexcept;

// Zero when the code is Ok, otherwise a stable non-zero process exit code.
[[nodiscard]] int exit_code(ErrorCode code) noexcept;

class Status {
 public:
  Status() = default;
  Status(ErrorCode code, ValidationStage stage, std::string message)
      : code_(code), stage_(stage), message_(std::move(message)) {}

  [[nodiscard]] static Status success() { return Status{}; }
  // Convenience for tests and callers that already include a boolean context.
  [[nodiscard]] static Status ok_status() { return Status{}; }

  [[nodiscard]] ErrorCode code() const noexcept { return code_; }
  [[nodiscard]] ValidationStage stage() const noexcept { return stage_; }
  [[nodiscard]] ErrorCategory category() const noexcept { return category_of(code_); }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }
  [[nodiscard]] bool ok() const noexcept { return code_ == ErrorCode::Ok; }
  [[nodiscard]] bool failed() const noexcept { return code_ != ErrorCode::Ok; }
  [[nodiscard]] int exit_code() const noexcept { return genctl::exit_code(code_); }

  // "StaleControlEpoch [fencing]: ..." - stable, greppable, deterministic.
  [[nodiscard]] std::string to_string() const;

 private:
  ErrorCode code_{ErrorCode::Ok};
  ValidationStage stage_{ValidationStage::None};
  std::string message_{};
};

[[nodiscard]] Status make_status(ErrorCode code, std::string message);
[[nodiscard]] Status make_status(ErrorCode code, ValidationStage stage, std::string message);

// Result<T> is an explicit, non-throwing outcome carrier. T must be default
// constructible; the default value is never observable while !ok().
template <typename T>
class Result {
  static_assert(std::is_default_constructible_v<T>,
                "Result<T> requires a default constructible T");

 public:
  Result(T value) : value_(std::move(value)) {}          // NOLINT(google-explicit-constructor)
  Result(Status status) : status_(std::move(status)) {}  // NOLINT(google-explicit-constructor)

  [[nodiscard]] bool ok() const noexcept { return status_.ok(); }
  [[nodiscard]] const Status& status() const noexcept { return status_; }
  [[nodiscard]] ErrorCode code() const noexcept { return status_.code(); }

  [[nodiscard]] const T& value() const& noexcept { return value_; }
  [[nodiscard]] T& value() & noexcept { return value_; }
  [[nodiscard]] T&& value() && noexcept { return std::move(value_); }

  [[nodiscard]] const T* operator->() const noexcept { return &value_; }
  [[nodiscard]] T* operator->() noexcept { return &value_; }
  [[nodiscard]] const T& operator*() const& noexcept { return value_; }
  [[nodiscard]] T& operator*() & noexcept { return value_; }

  [[nodiscard]] T value_or(T fallback) const { return ok() ? value_ : std::move(fallback); }

 private:
  T value_{};
  Status status_{};
};

// Propagate a failing Status out of the current function.
#define GENCTL_TRY(expr)                    \
  do {                                      \
    const ::genctl::Status& _genctl_st_ = (expr); \
    if (!_genctl_st_.ok()) return _genctl_st_;    \
  } while (false)

// Propagate a failing Result<T> as its Status.
#define GENCTL_TRY_STATUS(expr)                   \
  do {                                            \
    const auto& _genctl_rs_ = (expr);             \
    if (!_genctl_rs_.ok()) return _genctl_rs_.status(); \
  } while (false)

// Bind a Result<T> value or return its Status.
#define GENCTL_TRY_ASSIGN(name, expr)             \
  auto&& _genctl_tmp_##name = (expr);             \
  if (!_genctl_tmp_##name.ok()) return _genctl_tmp_##name.status(); \
  auto&& name = _genctl_tmp_##name.value()

}  // namespace genctl
