// Generator Control - internal canonical codecs for persisted records.
//
// Not installed: these specialisations exist only so the durable store and the
// engine can encode and decode the authoritative model. Every collection carries an
// explicit element count bounded by the reader before allocation, and every enum is
// range checked after decoding so a corrupt store cannot inject an out of range
// value into the state machine.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "genctl/engine.hpp"
#include "genctl/persistence.hpp"

namespace genctl {

#define GENCTL_PUT_U8(value) GENCTL_TRY(writer.put_u8(static_cast<std::uint8_t>(value)))
#define GENCTL_PUT_BOOL(value) GENCTL_TRY(writer.put_bool(value))
#define GENCTL_PUT_U16(value) GENCTL_TRY(writer.put_u16(static_cast<std::uint16_t>(value)))
#define GENCTL_PUT_U32(value) GENCTL_TRY(writer.put_u32(static_cast<std::uint32_t>(value)))
#define GENCTL_PUT_U64(value) GENCTL_TRY(writer.put_u64(static_cast<std::uint64_t>(value)))
#define GENCTL_PUT_I64(value) GENCTL_TRY(writer.put_i64(static_cast<std::int64_t>(value)))
#define GENCTL_PUT_STR(value) GENCTL_TRY(writer.put_string(value))
#define GENCTL_PUT_DIGEST(value) GENCTL_TRY(writer.put_digest(value))
#define GENCTL_PUT_VALUE(value) GENCTL_TRY(encode_value(writer, value))

#define GENCTL_GET_U8(target) GENCTL_TRY_ASSIGN(target, reader.get_u8())
#define GENCTL_GET_BOOL(target) GENCTL_TRY_ASSIGN(target, reader.get_bool())
#define GENCTL_GET_U16(target) GENCTL_TRY_ASSIGN(target, reader.get_u16())
#define GENCTL_GET_U32(target) GENCTL_TRY_ASSIGN(target, reader.get_u32())
#define GENCTL_GET_U64(target) GENCTL_TRY_ASSIGN(target, reader.get_u64())
#define GENCTL_GET_I64(target) GENCTL_TRY_ASSIGN(target, reader.get_i64())
#define GENCTL_GET_STR(target) GENCTL_TRY_ASSIGN(target, reader.get_string())
#define GENCTL_GET_DIGEST(target) GENCTL_TRY_ASSIGN(target, reader.get_digest())

template <>
struct ValueCodec<CheckKind> {
  static Status encode(CanonicalWriter& writer, CheckKind value) {
    return writer.put_u16(static_cast<std::uint16_t>(value));
  }
  static Result<CheckKind> decode(CanonicalReader& reader) {
    GENCTL_TRY_ASSIGN(raw, reader.get_u16());
    return static_cast<CheckKind>(raw);
  }
};

inline Status range_check(std::uint32_t raw, std::uint32_t maximum, const char* field) {
  if (raw > maximum) {
    return make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence,
                       std::string("decoded field '") + field + "' is out of range");
  }
  return Status::success();
}

// ---------------------------------------------------------------------------
// SwitchAuthorityToken
// ---------------------------------------------------------------------------
template <>
struct ValueCodec<SwitchAuthorityToken> {
  static Status encode(CanonicalWriter& writer, const SwitchAuthorityToken& value) {
    GENCTL_PUT_VALUE(value.authority);
    GENCTL_PUT_VALUE(value.subject_switch);
    // The subject identity is encoded as a plain string so that an unset token round
    // trips: an absent authority token is a legitimate state, not a corrupt record.
    GENCTL_PUT_STR(value.subject_generator.str());
    GENCTL_PUT_U32(value.subject_generation.hardware.value());
    GENCTL_PUT_U64(value.subject_generation.binding.value());
    GENCTL_PUT_U64(value.epoch.value());
    GENCTL_PUT_I64(value.issued_at);
    GENCTL_PUT_I64(value.valid_until);
    GENCTL_PUT_DIGEST(value.token_digest);
    return Status::success();
  }
  static Result<SwitchAuthorityToken> decode(CanonicalReader& reader) {
    SwitchAuthorityToken out{};
    GENCTL_TRY_ASSIGN(authority, decode_value<SwitchRef>(reader));
    GENCTL_TRY_ASSIGN(subject_switch, decode_value<SwitchRef>(reader));
    GENCTL_GET_STR(subject_generator_text);
    GeneratorId subject_generator{};
    if (!subject_generator_text.empty()) {
      GENCTL_TRY_ASSIGN(parsed_generator, GeneratorId::parse(subject_generator_text));
      subject_generator = parsed_generator;
    }
    GENCTL_GET_U32(hardware);
    GENCTL_GET_U64(binding);
    GENCTL_GET_U64(epoch);
    GENCTL_GET_I64(issued_at);
    GENCTL_GET_I64(valid_until);
    GENCTL_GET_DIGEST(token_digest);
    out.authority = authority;
    out.subject_switch = subject_switch;
    out.subject_generator = subject_generator;
    out.subject_generation.hardware = HardwareGeneration{hardware};
    out.subject_generation.binding = BindingEpoch{binding};
    out.epoch = ControlEpoch{epoch};
    out.issued_at = issued_at;
    out.valid_until = valid_until;
    out.token_digest = token_digest;
    return out;
  }
};

// ---------------------------------------------------------------------------
// CheckRecord / PreconditionRecord / TransferPreconditionRecord
// ---------------------------------------------------------------------------
template <>
struct ValueCodec<CheckRecord> {
  static Status encode(CanonicalWriter& writer, const CheckRecord& value) {
    GENCTL_PUT_U16(value.kind);
    GENCTL_PUT_BOOL(value.required);
    GENCTL_PUT_U8(value.state);
    GENCTL_PUT_U8(value.source);
    GENCTL_PUT_U8(value.lifetime);
    GENCTL_PUT_I64(value.observed_at);
    GENCTL_PUT_I64(value.valid_until);
    GENCTL_PUT_I64(value.max_age_millis);
    GENCTL_PUT_U64(value.version.value());
    GENCTL_PUT_VALUE(value.value);
    GENCTL_PUT_BOOL(value.latching);
    GENCTL_PUT_STR(value.detail);
    return Status::success();
  }
  static Result<CheckRecord> decode(CanonicalReader& reader) {
    CheckRecord out{};
    GENCTL_GET_U16(kind);
    GENCTL_GET_BOOL(required);
    GENCTL_GET_U8(state);
    GENCTL_GET_U8(source);
    GENCTL_GET_U8(lifetime);
    GENCTL_GET_I64(observed_at);
    GENCTL_GET_I64(valid_until);
    GENCTL_GET_I64(max_age);
    GENCTL_GET_U64(version);
    GENCTL_TRY_ASSIGN(value, decode_value<TypedValue>(reader));
    GENCTL_GET_BOOL(latching);
    GENCTL_GET_STR(detail);
    GENCTL_TRY(range_check(state, static_cast<std::uint32_t>(EvidenceState::Denied), "check.state"));
    GENCTL_TRY(
        range_check(source, static_cast<std::uint32_t>(EvidenceSource::CommandAcknowledgement),
                    "check.source"));
    GENCTL_TRY(range_check(lifetime,
                          static_cast<std::uint32_t>(EvidenceLifetime::StaticConfiguration),
                          "check.lifetime"));
    out.kind = static_cast<CheckKind>(kind);
    out.required = required;
    out.state = static_cast<EvidenceState>(state);
    out.source = static_cast<EvidenceSource>(source);
    out.lifetime = static_cast<EvidenceLifetime>(lifetime);
    out.observed_at = observed_at;
    out.valid_until = valid_until;
    out.max_age_millis = max_age;
    out.version = EvidenceVersion{version};
    out.value = value;
    out.latching = latching;
    out.detail = std::move(detail);
    return out;
  }
};

template <>
struct ValueCodec<PreconditionRecord> {
  static Status encode(CanonicalWriter& writer, const PreconditionRecord& value) {
    GENCTL_PUT_U16(value.kind);
    GENCTL_PUT_BOOL(value.required);
    GENCTL_PUT_U8(value.state);
    GENCTL_PUT_U8(value.source);
    GENCTL_PUT_U8(value.lifetime);
    GENCTL_PUT_I64(value.observed_at);
    GENCTL_PUT_I64(value.valid_until);
    GENCTL_PUT_I64(value.max_age_millis);
    GENCTL_PUT_U64(value.version.value());
    GENCTL_PUT_VALUE(value.value);
    GENCTL_PUT_STR(value.detail);
    return Status::success();
  }
  static Result<PreconditionRecord> decode(CanonicalReader& reader) {
    PreconditionRecord out{};
    GENCTL_GET_U16(kind);
    GENCTL_GET_BOOL(required);
    GENCTL_GET_U8(state);
    GENCTL_GET_U8(source);
    GENCTL_GET_U8(lifetime);
    GENCTL_GET_I64(observed_at);
    GENCTL_GET_I64(valid_until);
    GENCTL_GET_I64(max_age);
    GENCTL_GET_U64(version);
    GENCTL_TRY_ASSIGN(value, decode_value<TypedValue>(reader));
    GENCTL_GET_STR(detail);
    GENCTL_TRY(range_check(state, static_cast<std::uint32_t>(EvidenceState::Denied), "pre.state"));
    GENCTL_TRY(range_check(source,
                          static_cast<std::uint32_t>(EvidenceSource::CommandAcknowledgement),
                          "pre.source"));
    GENCTL_TRY(range_check(lifetime,
                          static_cast<std::uint32_t>(EvidenceLifetime::StaticConfiguration),
                          "pre.lifetime"));
    out.kind = static_cast<SyncPreconditionKind>(kind);
    out.required = required;
    out.state = static_cast<EvidenceState>(state);
    out.source = static_cast<EvidenceSource>(source);
    out.lifetime = static_cast<EvidenceLifetime>(lifetime);
    out.observed_at = observed_at;
    out.valid_until = valid_until;
    out.max_age_millis = max_age;
    out.version = EvidenceVersion{version};
    out.value = value;
    out.detail = std::move(detail);
    return out;
  }
};

template <>
struct ValueCodec<TransferPreconditionRecord> {
  static Status encode(CanonicalWriter& writer, const TransferPreconditionRecord& value) {
    GENCTL_PUT_U16(value.kind);
    GENCTL_PUT_BOOL(value.required);
    GENCTL_PUT_U8(value.state);
    GENCTL_PUT_U8(value.source);
    GENCTL_PUT_U8(value.lifetime);
    GENCTL_PUT_I64(value.observed_at);
    GENCTL_PUT_I64(value.valid_until);
    GENCTL_PUT_I64(value.max_age_millis);
    GENCTL_PUT_U64(value.version.value());
    GENCTL_PUT_VALUE(value.value);
    GENCTL_PUT_STR(value.detail);
    return Status::success();
  }
  static Result<TransferPreconditionRecord> decode(CanonicalReader& reader) {
    TransferPreconditionRecord out{};
    GENCTL_GET_U16(kind);
    GENCTL_GET_BOOL(required);
    GENCTL_GET_U8(state);
    GENCTL_GET_U8(source);
    GENCTL_GET_U8(lifetime);
    GENCTL_GET_I64(observed_at);
    GENCTL_GET_I64(valid_until);
    GENCTL_GET_I64(max_age);
    GENCTL_GET_U64(version);
    GENCTL_TRY_ASSIGN(value, decode_value<TypedValue>(reader));
    GENCTL_GET_STR(detail);
    GENCTL_TRY(range_check(state, static_cast<std::uint32_t>(EvidenceState::Denied), "tpre.state"));
    GENCTL_TRY(range_check(source,
                          static_cast<std::uint32_t>(EvidenceSource::CommandAcknowledgement),
                          "tpre.source"));
    GENCTL_TRY(range_check(lifetime,
                          static_cast<std::uint32_t>(EvidenceLifetime::StaticConfiguration),
                          "tpre.lifetime"));
    out.kind = static_cast<TransferPreconditionKind>(kind);
    out.required = required;
    out.state = static_cast<EvidenceState>(state);
    out.source = static_cast<EvidenceSource>(source);
    out.lifetime = static_cast<EvidenceLifetime>(lifetime);
    out.observed_at = observed_at;
    out.valid_until = valid_until;
    out.max_age_millis = max_age;
    out.version = EvidenceVersion{version};
    out.value = value;
    out.detail = std::move(detail);
    return out;
  }
};

// ---------------------------------------------------------------------------
// Resource and policy structures
// ---------------------------------------------------------------------------
template <>
struct ValueCodec<ResourceSnapshot> {
  static Status encode(CanonicalWriter& writer, const ResourceSnapshot& value) {
    GENCTL_PUT_VALUE(value.fuel_level);
    GENCTL_PUT_VALUE(value.fuel_capacity);
    GENCTL_PUT_VALUE(value.fuel_consumption);
    GENCTL_PUT_VALUE(value.fuel_temperature);
    GENCTL_PUT_VALUE(value.lube_oil_pressure);
    GENCTL_PUT_VALUE(value.lube_oil_temperature);
    GENCTL_PUT_VALUE(value.coolant_temperature);
    GENCTL_PUT_VALUE(value.coolant_level);
    GENCTL_PUT_VALUE(value.battery_voltage);
    GENCTL_PUT_VALUE(value.compressed_air_pressure);
    return Status::success();
  }
  static Result<ResourceSnapshot> decode(CanonicalReader& reader) {
    ResourceSnapshot out{};
    GENCTL_TRY_ASSIGN(fuel_level, decode_value<Evidence<FuelQuantity>>(reader));
    GENCTL_TRY_ASSIGN(fuel_capacity, decode_value<Evidence<FuelQuantity>>(reader));
    GENCTL_TRY_ASSIGN(fuel_consumption, decode_value<Evidence<FuelRate>>(reader));
    GENCTL_TRY_ASSIGN(fuel_temperature, decode_value<Evidence<Temperature>>(reader));
    GENCTL_TRY_ASSIGN(lube_oil_pressure, decode_value<Evidence<Pressure>>(reader));
    GENCTL_TRY_ASSIGN(lube_oil_temperature, decode_value<Evidence<Temperature>>(reader));
    GENCTL_TRY_ASSIGN(coolant_temperature, decode_value<Evidence<Temperature>>(reader));
    GENCTL_TRY_ASSIGN(coolant_level, decode_value<Evidence<Percent>>(reader));
    GENCTL_TRY_ASSIGN(battery_voltage, decode_value<Evidence<Voltage>>(reader));
    GENCTL_TRY_ASSIGN(compressed_air_pressure, decode_value<Evidence<Pressure>>(reader));
    out.fuel_level = fuel_level;
    out.fuel_capacity = fuel_capacity;
    out.fuel_consumption = fuel_consumption;
    out.fuel_temperature = fuel_temperature;
    out.lube_oil_pressure = lube_oil_pressure;
    out.lube_oil_temperature = lube_oil_temperature;
    out.coolant_temperature = coolant_temperature;
    out.coolant_level = coolant_level;
    out.battery_voltage = battery_voltage;
    out.compressed_air_pressure = compressed_air_pressure;
    return out;
  }
};

template <>
struct ValueCodec<ResourceRequirement> {
  static Status encode(CanonicalWriter& writer, const ResourceRequirement& value) {
    GENCTL_PUT_VALUE(value.reserve);
    GENCTL_PUT_I64(value.required_runtime_seconds);
    GENCTL_PUT_BOOL(value.require_lube_oil_pressure);
    GENCTL_PUT_BOOL(value.require_coolant_temperature);
    GENCTL_PUT_BOOL(value.require_battery_voltage);
    GENCTL_PUT_BOOL(value.require_compressed_air);
    GENCTL_PUT_I64(value.fuel_max_age_millis);
    GENCTL_PUT_I64(value.machinery_max_age_millis);
    return Status::success();
  }
  static Result<ResourceRequirement> decode(CanonicalReader& reader) {
    ResourceRequirement out{};
    GENCTL_TRY_ASSIGN(reserve, decode_value<FuelQuantity>(reader));
    GENCTL_GET_I64(runtime);
    GENCTL_GET_BOOL(lube);
    GENCTL_GET_BOOL(coolant);
    GENCTL_GET_BOOL(battery);
    GENCTL_GET_BOOL(air);
    GENCTL_GET_I64(fuel_age);
    GENCTL_GET_I64(machinery_age);
    out.reserve = reserve;
    out.required_runtime_seconds = runtime;
    out.require_lube_oil_pressure = lube;
    out.require_coolant_temperature = coolant;
    out.require_battery_voltage = battery;
    out.require_compressed_air = air;
    out.fuel_max_age_millis = fuel_age;
    out.machinery_max_age_millis = machinery_age;
    return out;
  }
};

template <>
struct ValueCodec<SyncPolicy> {
  static Status encode(CanonicalWriter& writer, const SyncPolicy& value) {
    GENCTL_PUT_VALUE(value.voltage_difference);
    GENCTL_PUT_VALUE(value.frequency_difference);
    GENCTL_PUT_VALUE(value.phase_angle_difference);
    GENCTL_PUT_I64(value.max_age_millis);
    GENCTL_PUT_BOOL(value.utility_parallel);
    GENCTL_PUT_BOOL(value.require_sync_relay);
    return Status::success();
  }
  static Result<SyncPolicy> decode(CanonicalReader& reader) {
    SyncPolicy out{};
    GENCTL_TRY_ASSIGN(voltage, decode_value<Window<Voltage>>(reader));
    GENCTL_TRY_ASSIGN(frequency, decode_value<Window<Frequency>>(reader));
    GENCTL_TRY_ASSIGN(phase, decode_value<Window<PhaseAngle>>(reader));
    GENCTL_GET_I64(max_age);
    GENCTL_GET_BOOL(parallel);
    GENCTL_GET_BOOL(relay);
    out.voltage_difference = voltage;
    out.frequency_difference = frequency;
    out.phase_angle_difference = phase;
    out.max_age_millis = max_age;
    out.utility_parallel = parallel;
    out.require_sync_relay = relay;
    return out;
  }
};

template <>
struct ValueCodec<TransferPolicy> {
  static Status encode(CanonicalWriter& writer, const TransferPolicy& value) {
    GENCTL_PUT_I64(value.max_token_age_millis);
    GENCTL_PUT_I64(value.max_position_age_millis);
    GENCTL_PUT_BOOL(value.require_protection_permissive);
    GENCTL_PUT_BOOL(value.require_generator_synchronized);
    return Status::success();
  }
  static Result<TransferPolicy> decode(CanonicalReader& reader) {
    TransferPolicy out{};
    GENCTL_GET_I64(token_age);
    GENCTL_GET_I64(position_age);
    GENCTL_GET_BOOL(protection);
    GENCTL_GET_BOOL(synchronized);
    out.max_token_age_millis = token_age;
    out.max_position_age_millis = position_age;
    out.require_protection_permissive = protection;
    out.require_generator_synchronized = synchronized;
    return out;
  }
};

template <>
struct ValueCodec<TransferPath> {
  static Status encode(CanonicalWriter& writer, const TransferPath& value) {
    GENCTL_PUT_VALUE(value.breaker);
    GENCTL_PUT_VALUE(value.source_bus);
    GENCTL_PUT_VALUE(value.target_bus);
    GENCTL_PUT_VALUE(value.feeder);
    GENCTL_PUT_VALUE(value.transfer_path);
    return Status::success();
  }
  static Result<TransferPath> decode(CanonicalReader& reader) {
    TransferPath out{};
    GENCTL_TRY_ASSIGN(breaker, decode_value<SwitchRef>(reader));
    GENCTL_TRY_ASSIGN(source_bus, decode_value<SwitchRef>(reader));
    GENCTL_TRY_ASSIGN(target_bus, decode_value<SwitchRef>(reader));
    GENCTL_TRY_ASSIGN(feeder, decode_value<SwitchRef>(reader));
    GENCTL_TRY_ASSIGN(path, decode_value<SwitchRef>(reader));
    out.breaker = breaker;
    out.source_bus = source_bus;
    out.target_bus = target_bus;
    out.feeder = feeder;
    out.transfer_path = path;
    return out;
  }
};

template <>
struct ValueCodec<AuthorityGrant> {
  static Status encode(CanonicalWriter& writer, const AuthorityGrant& value) {
    GENCTL_PUT_U8(value.cls);
    GENCTL_PUT_U64(value.epoch.value());
    GENCTL_PUT_STR(value.granted_by);
    GENCTL_PUT_I64(value.granted_at);
    GENCTL_PUT_I64(value.valid_until);
    GENCTL_PUT_STR(value.reason);
    GENCTL_TRY(writer.put_count(static_cast<std::uint32_t>(value.scope.size())));
    for (const OperationKind operation : value.scope) GENCTL_PUT_U8(operation);
    GENCTL_PUT_BOOL(value.explicit_grant);
    GENCTL_PUT_DIGEST(value.token_digest);
    return Status::success();
  }
  static Result<AuthorityGrant> decode(CanonicalReader& reader) {
    AuthorityGrant out{};
    GENCTL_GET_U8(cls);
    GENCTL_GET_U64(epoch);
    GENCTL_GET_STR(granted_by);
    GENCTL_GET_I64(granted_at);
    GENCTL_GET_I64(valid_until);
    GENCTL_GET_STR(reason);
    GENCTL_TRY_ASSIGN(scope_count, reader.get_count());
    std::vector<OperationKind> scope;
    scope.reserve(scope_count);
    for (std::uint32_t i = 0; i < scope_count; ++i) {
      GENCTL_GET_U8(operation);
      GENCTL_TRY(range_check(operation,
                            static_cast<std::uint32_t>(OperationKind::TransferToUtility),
                            "grant.scope"));
      scope.push_back(static_cast<OperationKind>(operation));
    }
    GENCTL_GET_BOOL(explicit_grant);
    GENCTL_GET_DIGEST(token_digest);
    GENCTL_TRY(range_check(cls, static_cast<std::uint32_t>(AuthorityClass::Emergency), "grant.cls"));
    out.cls = static_cast<AuthorityClass>(cls);
    out.epoch = ControlEpoch{epoch};
    out.granted_by = std::move(granted_by);
    out.granted_at = granted_at;
    out.valid_until = valid_until;
    out.reason = std::move(reason);
    out.scope = std::move(scope);
    out.explicit_grant = explicit_grant;
    out.token_digest = token_digest;
    return out;
  }
};

template <>
struct ValueCodec<HistoryEntry> {
  static Status encode(CanonicalWriter& writer, const HistoryEntry& value) {
    GENCTL_PUT_U64(value.seq.value());
    GENCTL_PUT_I64(value.at);
    GENCTL_PUT_U8(value.operation);
    GENCTL_PUT_U64(value.attempt.value());
    GENCTL_PUT_U8(value.from_lifecycle);
    GENCTL_PUT_U8(value.to_lifecycle);
    GENCTL_PUT_U8(value.from_operating);
    GENCTL_PUT_U8(value.to_operating);
    GENCTL_PUT_U8(value.mode);
    GENCTL_PUT_BOOL(value.provisional);
    GENCTL_PUT_STR(value.detail);
    GENCTL_PUT_DIGEST(value.digest);
    return Status::success();
  }
  static Result<HistoryEntry> decode(CanonicalReader& reader) {
    HistoryEntry out{};
    GENCTL_GET_U64(seq);
    GENCTL_GET_I64(at);
    GENCTL_GET_U8(operation);
    GENCTL_GET_U64(attempt);
    GENCTL_GET_U8(from_lifecycle);
    GENCTL_GET_U8(to_lifecycle);
    GENCTL_GET_U8(from_operating);
    GENCTL_GET_U8(to_operating);
    GENCTL_GET_U8(mode);
    GENCTL_GET_BOOL(provisional);
    GENCTL_GET_STR(detail);
    GENCTL_GET_DIGEST(digest);
    GENCTL_TRY(range_check(operation,
                          static_cast<std::uint32_t>(OperationKind::TransferToUtility),
                          "history.operation"));
    GENCTL_TRY(range_check(from_lifecycle, static_cast<std::uint32_t>(LifecycleState::Retired),
                          "history.from_lifecycle"));
    GENCTL_TRY(range_check(to_lifecycle, static_cast<std::uint32_t>(LifecycleState::Retired),
                          "history.to_lifecycle"));
    GENCTL_TRY(range_check(from_operating, static_cast<std::uint32_t>(OperatingState::Faulted),
                          "history.from_operating"));
    GENCTL_TRY(range_check(to_operating, static_cast<std::uint32_t>(OperatingState::Faulted),
                          "history.to_operating"));
    GENCTL_TRY(range_check(mode, static_cast<std::uint32_t>(OperatingMode::Service), "history.mode"));
    out.seq = JournalSeq{seq};
    out.at = at;
    out.operation = static_cast<OperationKind>(operation);
    out.attempt = AttemptId{attempt};
    out.from_lifecycle = static_cast<LifecycleState>(from_lifecycle);
    out.to_lifecycle = static_cast<LifecycleState>(to_lifecycle);
    out.from_operating = static_cast<OperatingState>(from_operating);
    out.to_operating = static_cast<OperatingState>(to_operating);
    out.mode = static_cast<OperatingMode>(mode);
    out.provisional = provisional;
    out.detail = std::move(detail);
    out.digest = digest;
    return out;
  }
};

template <>
struct ValueCodec<AuthorityAuditEntry> {
  static Status encode(CanonicalWriter& writer, const AuthorityAuditEntry& value) {
    GENCTL_PUT_U64(value.seq.value());
    GENCTL_PUT_I64(value.at);
    GENCTL_PUT_VALUE(value.generator);
    GENCTL_PUT_U8(value.operation);
    GENCTL_PUT_U8(value.cls);
    GENCTL_PUT_U64(value.epoch.value());
    GENCTL_PUT_STR(value.granted_by);
    GENCTL_PUT_STR(value.reason);
    GENCTL_TRY(writer.put_count(static_cast<std::uint32_t>(value.waived_checks.size())));
    for (const CheckKind kind : value.waived_checks) GENCTL_PUT_U16(kind);
    GENCTL_PUT_DIGEST(value.decision_digest);
    return Status::success();
  }
  static Result<AuthorityAuditEntry> decode(CanonicalReader& reader) {
    AuthorityAuditEntry out{};
    GENCTL_GET_U64(seq);
    GENCTL_GET_I64(at);
    GENCTL_TRY_ASSIGN(generator, decode_value<GeneratorId>(reader));
    GENCTL_GET_U8(operation);
    GENCTL_GET_U8(cls);
    GENCTL_GET_U64(epoch);
    GENCTL_GET_STR(granted_by);
    GENCTL_GET_STR(reason);
    GENCTL_TRY_ASSIGN(count, reader.get_count());
    std::vector<CheckKind> waived;
    waived.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      GENCTL_GET_U16(kind);
      waived.push_back(static_cast<CheckKind>(kind));
    }
    GENCTL_GET_DIGEST(decision_digest);
    GENCTL_TRY(range_check(operation,
                          static_cast<std::uint32_t>(OperationKind::TransferToUtility),
                          "audit.operation"));
    GENCTL_TRY(range_check(cls, static_cast<std::uint32_t>(AuthorityClass::Emergency), "audit.cls"));
    out.seq = JournalSeq{seq};
    out.at = at;
    out.generator = generator;
    out.operation = static_cast<OperationKind>(operation);
    out.cls = static_cast<AuthorityClass>(cls);
    out.epoch = ControlEpoch{epoch};
    out.granted_by = std::move(granted_by);
    out.reason = std::move(reason);
    out.waived_checks = std::move(waived);
    out.decision_digest = decision_digest;
    return out;
  }
};

// ---------------------------------------------------------------------------
// PlannedAgainst / AttemptRecord / ReplayEntry
// ---------------------------------------------------------------------------
template <>
struct ValueCodec<PlannedAgainst> {
  static Status encode(CanonicalWriter& writer, const PlannedAgainst& value) {
    GENCTL_PUT_U64(value.controller.epoch.value());
    GENCTL_PUT_U64(value.controller.incarnation.value());
    GENCTL_PUT_U32(value.generation.hardware.value());
    GENCTL_PUT_U64(value.generation.binding.value());
    GENCTL_PUT_U64(value.revision.value());
    GENCTL_PUT_DIGEST(value.readiness_binding);
    GENCTL_PUT_DIGEST(value.resource_binding);
    GENCTL_PUT_DIGEST(value.sync_binding);
    GENCTL_PUT_DIGEST(value.transfer_binding);
    return Status::success();
  }
  static Result<PlannedAgainst> decode(CanonicalReader& reader) {
    PlannedAgainst out{};
    GENCTL_GET_U64(epoch);
    GENCTL_GET_U64(incarnation);
    GENCTL_GET_U32(hardware);
    GENCTL_GET_U64(binding);
    GENCTL_GET_U64(revision);
    GENCTL_GET_DIGEST(readiness);
    GENCTL_GET_DIGEST(resource);
    GENCTL_GET_DIGEST(sync);
    GENCTL_GET_DIGEST(transfer);
    out.controller.epoch = ControlEpoch{epoch};
    out.controller.incarnation = IncarnationId{incarnation};
    out.generation.hardware = HardwareGeneration{hardware};
    out.generation.binding = BindingEpoch{binding};
    out.revision = StateRevision{revision};
    out.readiness_binding = readiness;
    out.resource_binding = resource;
    out.sync_binding = sync;
    out.transfer_binding = transfer;
    return out;
  }
};

template <>
struct ValueCodec<AttemptRecord> {
  static Status encode(CanonicalWriter& writer, const AttemptRecord& value) {
    GENCTL_PUT_U64(value.id.value());
    GENCTL_PUT_U64(value.seq.value());
    GENCTL_PUT_VALUE(value.generator);
    GENCTL_PUT_U8(value.operation);
    GENCTL_PUT_VALUE(value.planned_against);
    GENCTL_PUT_VALUE(value.key);
    GENCTL_PUT_U64(value.fingerprint.value());
    GENCTL_PUT_U8(value.authority);
    GENCTL_PUT_U64(value.authority_epoch.value());
    GENCTL_PUT_STR(value.authority_reason);
    GENCTL_PUT_U8(value.command_state);
    GENCTL_PUT_U8(value.ack_status);
    GENCTL_PUT_U64(value.command_id.value());
    GENCTL_PUT_I64(value.created_at);
    GENCTL_PUT_I64(value.updated_at);
    GENCTL_PUT_I64(value.acknowledged_at);
    GENCTL_PUT_STR(value.ack_detail);
    GENCTL_PUT_U8(value.effect_state);
    GENCTL_PUT_U64(value.effect_observation_seq.value());
    GENCTL_PUT_I64(value.effect_observed_at);
    GENCTL_PUT_U8(value.observed_state);
    GENCTL_PUT_U8(value.observed_synchronization);
    GENCTL_PUT_U8(value.observed_breaker);
    GENCTL_PUT_U8(value.effect_source);
    GENCTL_PUT_DIGEST(value.effect_digest);
    GENCTL_PUT_STR(value.effect_detail);
    GENCTL_PUT_STR(value.note);
    return Status::success();
  }
  static Result<AttemptRecord> decode(CanonicalReader& reader) {
    AttemptRecord out{};
    GENCTL_GET_U64(id);
    GENCTL_GET_U64(seq);
    GENCTL_TRY_ASSIGN(generator, decode_value<GeneratorId>(reader));
    GENCTL_GET_U8(operation);
    GENCTL_TRY_ASSIGN(planned, decode_value<PlannedAgainst>(reader));
    GENCTL_TRY_ASSIGN(key, decode_value<IdempotencyKey>(reader));
    GENCTL_GET_U64(fingerprint);
    GENCTL_GET_U8(authority);
    GENCTL_GET_U64(authority_epoch);
    GENCTL_GET_STR(authority_reason);
    GENCTL_GET_U8(command_state);
    GENCTL_GET_U8(ack_status);
    GENCTL_GET_U64(command_id);
    GENCTL_GET_I64(created_at);
    GENCTL_GET_I64(updated_at);
    GENCTL_GET_I64(acknowledged_at);
    GENCTL_GET_STR(ack_detail);
    GENCTL_GET_U8(effect_state);
    GENCTL_GET_U64(effect_seq);
    GENCTL_GET_I64(effect_at);
    GENCTL_GET_U8(observed_state);
    GENCTL_GET_U8(observed_sync);
    GENCTL_GET_U8(observed_breaker);
    GENCTL_GET_U8(effect_source);
    GENCTL_GET_DIGEST(effect_digest);
    GENCTL_GET_STR(effect_detail);
    GENCTL_GET_STR(note);
    GENCTL_TRY(range_check(operation,
                          static_cast<std::uint32_t>(OperationKind::TransferToUtility),
                          "attempt.operation"));
    GENCTL_TRY(range_check(authority, static_cast<std::uint32_t>(AuthorityClass::Emergency),
                          "attempt.authority"));
    GENCTL_TRY(range_check(command_state,
                          static_cast<std::uint32_t>(CommandState::Abandoned),
                          "attempt.command_state"));
    GENCTL_TRY(range_check(ack_status, static_cast<std::uint32_t>(AdapterAckStatus::Invalid),
                          "attempt.ack_status"));
    GENCTL_TRY(range_check(effect_state,
                          static_cast<std::uint32_t>(EffectState::VerificationFailed),
                          "attempt.effect_state"));
    GENCTL_TRY(range_check(observed_state, static_cast<std::uint32_t>(OperatingState::Faulted),
                          "attempt.observed_state"));
    GENCTL_TRY(range_check(observed_sync,
                          static_cast<std::uint32_t>(SynchronizationState::Failed),
                          "attempt.observed_sync"));
    GENCTL_TRY(range_check(observed_breaker, static_cast<std::uint32_t>(BreakerPosition::Faulted),
                          "attempt.observed_breaker"));
    GENCTL_TRY(range_check(effect_source,
                          static_cast<std::uint32_t>(EvidenceSource::CommandAcknowledgement),
                          "attempt.effect_source"));
    out.id = AttemptId{id};
    out.seq = JournalSeq{seq};
    out.generator = generator;
    out.operation = static_cast<OperationKind>(operation);
    out.planned_against = planned;
    out.key = key;
    out.fingerprint = RequestFingerprint{fingerprint};
    out.authority = static_cast<AuthorityClass>(authority);
    out.authority_epoch = ControlEpoch{authority_epoch};
    out.authority_reason = std::move(authority_reason);
    out.command_state = static_cast<CommandState>(command_state);
    out.ack_status = static_cast<AdapterAckStatus>(ack_status);
    out.command_id = CommandId{command_id};
    out.created_at = created_at;
    out.updated_at = updated_at;
    out.acknowledged_at = acknowledged_at;
    out.ack_detail = std::move(ack_detail);
    out.effect_state = static_cast<EffectState>(effect_state);
    out.effect_observation_seq = ObservationSeq{effect_seq};
    out.effect_observed_at = effect_at;
    out.observed_state = static_cast<OperatingState>(observed_state);
    out.observed_synchronization = static_cast<SynchronizationState>(observed_sync);
    out.observed_breaker = static_cast<BreakerPosition>(observed_breaker);
    out.effect_source = static_cast<EvidenceSource>(effect_source);
    out.effect_digest = effect_digest;
    out.effect_detail = std::move(effect_detail);
    out.note = std::move(note);
    out.replayed = false;
    return out;
  }
};

template <>
struct ValueCodec<ReplayEntry> {
  static Status encode(CanonicalWriter& writer, const ReplayEntry& value) {
    GENCTL_PUT_VALUE(value.key);
    GENCTL_PUT_U64(value.attempt.value());
    GENCTL_PUT_U64(value.fingerprint.value());
    GENCTL_PUT_U8(value.operation);
    GENCTL_PUT_U8(value.command_state);
    GENCTL_PUT_U8(value.effect_state);
    GENCTL_PUT_U64(value.command_id.value());
    GENCTL_PUT_I64(value.recorded_at);
    return Status::success();
  }
  static Result<ReplayEntry> decode(CanonicalReader& reader) {
    ReplayEntry out{};
    GENCTL_TRY_ASSIGN(key, decode_value<IdempotencyKey>(reader));
    GENCTL_GET_U64(attempt);
    GENCTL_GET_U64(fingerprint);
    GENCTL_GET_U8(operation);
    GENCTL_GET_U8(command_state);
    GENCTL_GET_U8(effect_state);
    GENCTL_GET_U64(command_id);
    GENCTL_GET_I64(recorded_at);
    GENCTL_TRY(range_check(operation,
                          static_cast<std::uint32_t>(OperationKind::TransferToUtility),
                          "replay.operation"));
    GENCTL_TRY(range_check(command_state, static_cast<std::uint32_t>(CommandState::Abandoned),
                          "replay.command_state"));
    GENCTL_TRY(range_check(effect_state,
                          static_cast<std::uint32_t>(EffectState::VerificationFailed),
                          "replay.effect_state"));
    out.key = key;
    out.attempt = AttemptId{attempt};
    out.fingerprint = RequestFingerprint{fingerprint};
    out.operation = static_cast<OperationKind>(operation);
    out.command_state = static_cast<CommandState>(command_state);
    out.effect_state = static_cast<EffectState>(effect_state);
    out.command_id = CommandId{command_id};
    out.recorded_at = recorded_at;
    return out;
  }
};

// ---------------------------------------------------------------------------
// GeneratorState / StateImage
// ---------------------------------------------------------------------------
template <>
struct ValueCodec<GeneratorState> {
  static Status encode(CanonicalWriter& writer, const GeneratorState& value) {
    GENCTL_PUT_VALUE(value.id);
    GENCTL_PUT_U32(value.generation.hardware.value());
    GENCTL_PUT_U64(value.generation.binding.value());
    GENCTL_PUT_BOOL(value.commissioned);
    GENCTL_PUT_U64(value.revision.value());
    GENCTL_PUT_U8(value.lifecycle);
    GENCTL_PUT_U8(value.operating);
    GENCTL_PUT_U8(value.mode);
    GENCTL_PUT_U8(value.synchronization);
    GENCTL_PUT_U8(value.breaker);
    GENCTL_PUT_I64(value.breaker_observed_at);
    GENCTL_PUT_I64(value.last_observed_at);
    GENCTL_PUT_U64(value.last_observation_seq.value());
    GENCTL_PUT_VALUE(value.resources);
    GENCTL_PUT_VALUE(value.requirement);
    GENCTL_TRY(writer.put_count(static_cast<std::uint32_t>(value.required_checks.size())));
    for (const CheckKind kind : value.required_checks) GENCTL_PUT_U16(kind);
    GENCTL_TRY(writer.put_count(static_cast<std::uint32_t>(value.checks.size())));
    for (const auto& check : value.checks) GENCTL_PUT_VALUE(check);
    GENCTL_TRY(writer.put_count(static_cast<std::uint32_t>(value.sync_preconditions.size())));
    for (const auto& precondition : value.sync_preconditions) GENCTL_PUT_VALUE(precondition);
    GENCTL_TRY(writer.put_count(static_cast<std::uint32_t>(value.transfer_preconditions.size())));
    for (const auto& precondition : value.transfer_preconditions) GENCTL_PUT_VALUE(precondition);
    GENCTL_PUT_VALUE(value.transfer_path);
    GENCTL_PUT_VALUE(value.switch_authority);
    GENCTL_PUT_VALUE(value.sync_policy);
    GENCTL_PUT_VALUE(value.transfer_policy);
    GENCTL_PUT_VALUE(value.authority);
    GENCTL_TRY(writer.put_count(static_cast<std::uint32_t>(value.history.size())));
    for (const auto& entry : value.history) GENCTL_PUT_VALUE(entry);
    GENCTL_TRY(writer.put_count(static_cast<std::uint32_t>(value.audit.size())));
    for (const auto& entry : value.audit) GENCTL_PUT_VALUE(entry);
    GENCTL_PUT_U64(value.next_history_seq.value());
    return Status::success();
  }

  template <typename Element>
  static Result<std::vector<Element>> decode_list(CanonicalReader& reader) {
    GENCTL_TRY_ASSIGN(count, reader.get_count());
    std::vector<Element> out;
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      GENCTL_TRY_ASSIGN(element, decode_value<Element>(reader));
      out.push_back(std::move(element));
    }
    return out;
  }

  static Result<GeneratorState> decode(CanonicalReader& reader) {
    GeneratorState out{};
    GENCTL_TRY_ASSIGN(id, decode_value<GeneratorId>(reader));
    GENCTL_GET_U32(hardware);
    GENCTL_GET_U64(binding);
    GENCTL_GET_BOOL(commissioned);
    GENCTL_GET_U64(revision);
    GENCTL_GET_U8(lifecycle);
    GENCTL_GET_U8(operating);
    GENCTL_GET_U8(mode);
    GENCTL_GET_U8(synchronization);
    GENCTL_GET_U8(breaker);
    GENCTL_GET_I64(breaker_at);
    GENCTL_GET_I64(observed_at);
    GENCTL_GET_U64(observation_seq);
    GENCTL_TRY_ASSIGN(resources, decode_value<ResourceSnapshot>(reader));
    GENCTL_TRY_ASSIGN(requirement, decode_value<ResourceRequirement>(reader));
    GENCTL_TRY_ASSIGN(required_checks, decode_list<CheckKind>(reader));
    GENCTL_TRY_ASSIGN(checks, decode_list<CheckRecord>(reader));
    GENCTL_TRY_ASSIGN(sync_preconditions, decode_list<PreconditionRecord>(reader));
    GENCTL_TRY_ASSIGN(transfer_preconditions, decode_list<TransferPreconditionRecord>(reader));
    GENCTL_TRY_ASSIGN(path, decode_value<TransferPath>(reader));
    GENCTL_TRY_ASSIGN(switch_authority, decode_value<Evidence<SwitchAuthorityToken>>(reader));
    GENCTL_TRY_ASSIGN(sync_policy, decode_value<SyncPolicy>(reader));
    GENCTL_TRY_ASSIGN(transfer_policy, decode_value<TransferPolicy>(reader));
    GENCTL_TRY_ASSIGN(authority, decode_value<AuthorityGrant>(reader));
    GENCTL_TRY_ASSIGN(history, decode_list<HistoryEntry>(reader));
    GENCTL_TRY_ASSIGN(audit, decode_list<AuthorityAuditEntry>(reader));
    GENCTL_GET_U64(next_history_seq);
    GENCTL_TRY(range_check(lifecycle, static_cast<std::uint32_t>(LifecycleState::Retired),
                          "generator.lifecycle"));
    GENCTL_TRY(range_check(operating, static_cast<std::uint32_t>(OperatingState::Faulted),
                          "generator.operating"));
    GENCTL_TRY(range_check(mode, static_cast<std::uint32_t>(OperatingMode::Service),
                          "generator.mode"));
    GENCTL_TRY(range_check(synchronization,
                          static_cast<std::uint32_t>(SynchronizationState::Failed),
                          "generator.synchronization"));
    GENCTL_TRY(range_check(breaker, static_cast<std::uint32_t>(BreakerPosition::Faulted),
                          "generator.breaker"));
    out.id = id;
    out.generation.hardware = HardwareGeneration{hardware};
    out.generation.binding = BindingEpoch{binding};
    out.commissioned = commissioned;
    out.revision = StateRevision{revision};
    out.lifecycle = static_cast<LifecycleState>(lifecycle);
    out.operating = static_cast<OperatingState>(operating);
    out.mode = static_cast<OperatingMode>(mode);
    out.synchronization = static_cast<SynchronizationState>(synchronization);
    out.breaker = static_cast<BreakerPosition>(breaker);
    out.breaker_observed_at = breaker_at;
    out.last_observed_at = observed_at;
    out.last_observation_seq = ObservationSeq{observation_seq};
    out.resources = resources;
    out.requirement = requirement;
    out.required_checks = std::move(required_checks);
    out.checks = std::move(checks);
    out.sync_preconditions = std::move(sync_preconditions);
    out.transfer_preconditions = std::move(transfer_preconditions);
    out.transfer_path = path;
    out.switch_authority = switch_authority;
    out.sync_policy = sync_policy;
    out.transfer_policy = transfer_policy;
    out.authority = authority;
    out.history = std::move(history);
    out.audit = std::move(audit);
    out.next_history_seq = JournalSeq{next_history_seq};
    return out;
  }
};

template <>
struct ValueCodec<StateImage> {
  static Status encode(CanonicalWriter& writer, const StateImage& value) {
    GENCTL_PUT_U32(value.format_version);
    GENCTL_PUT_U64(value.commit_seq.value());
    GENCTL_PUT_U64(value.epoch.value());
    GENCTL_PUT_U64(value.incarnation.value());
    GENCTL_PUT_DIGEST(value.previous_digest);
    GENCTL_PUT_I64(value.created_at);
    GENCTL_TRY(writer.put_count(static_cast<std::uint32_t>(value.generators.size())));
    for (const auto& entry : value.generators) {
      GENCTL_PUT_VALUE(entry.first);
      GENCTL_PUT_VALUE(entry.second);
    }
    GENCTL_TRY(writer.put_count(static_cast<std::uint32_t>(value.attempts.size())));
    for (const auto& attempt : value.attempts) GENCTL_PUT_VALUE(attempt);
    GENCTL_TRY(writer.put_count(static_cast<std::uint32_t>(value.replay.size())));
    for (const auto& entry : value.replay) GENCTL_PUT_VALUE(entry);
    GENCTL_PUT_U64(value.next_attempt_id.value());
    GENCTL_PUT_U64(value.next_journal_seq.value());
    GENCTL_PUT_U64(value.retained_floor.value());
    GENCTL_PUT_U64(value.evicted_keys);
    GENCTL_PUT_U32(value.journal_policy.max_attempts);
    GENCTL_PUT_U32(value.journal_policy.idempotency_window);
    GENCTL_PUT_U64(value.key_window.acknowledged_at_seq.value());
    GENCTL_PUT_I64(value.key_window.acknowledged_at);
    GENCTL_PUT_STR(value.key_window.acknowledged_by);
    GENCTL_PUT_STR(value.key_window.reason);
    GENCTL_PUT_U64(value.key_window.evicted_keys);
    return Status::success();
  }
  static Result<StateImage> decode(CanonicalReader& reader) {
    StateImage out{};
    GENCTL_GET_U32(format_version);
    GENCTL_GET_U64(commit_seq);
    GENCTL_GET_U64(epoch);
    GENCTL_GET_U64(incarnation);
    GENCTL_GET_DIGEST(previous_digest);
    GENCTL_GET_I64(created_at);
    if (format_version != kStoreFormatVersion) {
      return make_status(ErrorCode::StoreVersionUnsupported, ValidationStage::Persistence,
                         "state image format " + std::to_string(format_version) +
                             " is not supported by this build (expected " +
                             std::to_string(kStoreFormatVersion) + ")");
    }
    out.format_version = format_version;
    out.commit_seq = CommitSeq{commit_seq};
    out.epoch = ControlEpoch{epoch};
    out.incarnation = IncarnationId{incarnation};
    out.previous_digest = previous_digest;
    out.created_at = created_at;

    GENCTL_TRY_ASSIGN(generator_count, reader.get_count());
    for (std::uint32_t i = 0; i < generator_count; ++i) {
      GENCTL_TRY_ASSIGN(id, decode_value<GeneratorId>(reader));
      GENCTL_TRY_ASSIGN(state, decode_value<GeneratorState>(reader));
      if (state.id != id) {
        return make_status(ErrorCode::StoreCorrupt, ValidationStage::Persistence,
                           "generator map key does not match the encoded identity");
      }
      if (!out.generators.emplace(id, std::move(state)).second) {
        return make_status(ErrorCode::DuplicateIdentity, ValidationStage::Persistence,
                           "state image contains a duplicate generator identity");
      }
    }

    GENCTL_TRY_ASSIGN(attempt_count, reader.get_count());
    out.attempts.reserve(attempt_count);
    for (std::uint32_t i = 0; i < attempt_count; ++i) {
      GENCTL_TRY_ASSIGN(attempt, decode_value<AttemptRecord>(reader));
      out.attempts.push_back(std::move(attempt));
    }

    GENCTL_TRY_ASSIGN(replay_count, reader.get_count());
    out.replay.reserve(replay_count);
    for (std::uint32_t i = 0; i < replay_count; ++i) {
      GENCTL_TRY_ASSIGN(entry, decode_value<ReplayEntry>(reader));
      out.replay.push_back(std::move(entry));
    }

    GENCTL_GET_U64(next_attempt_id);
    GENCTL_GET_U64(next_journal_seq);
    GENCTL_GET_U64(retained_floor);
    GENCTL_GET_U64(evicted_keys);
    GENCTL_GET_U32(max_attempts);
    GENCTL_GET_U32(idempotency_window);
    GENCTL_GET_U64(key_seq);
    GENCTL_GET_I64(key_at);
    GENCTL_GET_STR(key_by);
    GENCTL_GET_STR(key_reason);
    GENCTL_GET_U64(key_evicted);
    out.next_attempt_id = AttemptId{next_attempt_id};
    out.next_journal_seq = JournalSeq{next_journal_seq};
    out.retained_floor = JournalSeq{retained_floor};
    out.evicted_keys = evicted_keys;
    out.journal_policy.max_attempts = max_attempts;
    out.journal_policy.idempotency_window = idempotency_window;
    out.key_window.acknowledged_at_seq = CommitSeq{key_seq};
    out.key_window.acknowledged_at = key_at;
    out.key_window.acknowledged_by = std::move(key_by);
    out.key_window.reason = std::move(key_reason);
    out.key_window.evicted_keys = key_evicted;
    return out;
  }
};

}  // namespace genctl

#undef GENCTL_PUT_U8
#undef GENCTL_PUT_BOOL
#undef GENCTL_PUT_U16
#undef GENCTL_PUT_U32
#undef GENCTL_PUT_U64
#undef GENCTL_PUT_I64
#undef GENCTL_PUT_STR
#undef GENCTL_PUT_DIGEST
#undef GENCTL_PUT_VALUE
#undef GENCTL_GET_U8
#undef GENCTL_GET_BOOL
#undef GENCTL_GET_U16
#undef GENCTL_GET_U32
#undef GENCTL_GET_U64
#undef GENCTL_GET_I64
#undef GENCTL_GET_STR
#undef GENCTL_GET_DIGEST
