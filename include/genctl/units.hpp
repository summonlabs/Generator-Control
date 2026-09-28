// Generator Control - checked arithmetic and explicit physical units.
//
// Every authoritative quantity carries its unit in the type. Fuel volume is never
// a bare double, an electrical angle is never a bare int: a value that cannot be
// interpreted cannot be compared against a safety window. Cross-unit arithmetic
// is refused instead of guessed, because converting litres to kilograms requires a
// density that this runtime does not own.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "genctl/canonical.hpp"
#include "genctl/result.hpp"

namespace genctl {

// ---------------------------------------------------------------------------
// Checked integer arithmetic. Authoritative calculations never wrap and never
// silently clamp: overflow is refused with ArithmeticOverflow.
// ---------------------------------------------------------------------------
[[nodiscard]] Result<std::int64_t> add_checked(std::int64_t a, std::int64_t b);
[[nodiscard]] Result<std::int64_t> sub_checked(std::int64_t a, std::int64_t b);
[[nodiscard]] Result<std::int64_t> mul_checked(std::int64_t a, std::int64_t b);
[[nodiscard]] Result<std::int64_t> div_checked(std::int64_t a, std::int64_t b);
[[nodiscard]] bool in_range(std::int64_t value, std::int64_t lo, std::int64_t hi) noexcept;

// ---------------------------------------------------------------------------
// Fuel and consumable units
// ---------------------------------------------------------------------------
enum class FuelUnit : std::uint8_t {
  Unknown = 0,
  // Liquid volume. Stored amount is millilitres.
  Millilitres = 1,
  // Mass. Stored amount is grams.
  Grams = 2,
  // Gaseous volume. Stored amount is litres (1e-3 m3).
  Litres = 3,
  // Fraction of nameplate capacity. Stored amount is basis points (0..10000).
  PercentOfCapacity = 4,
};

[[nodiscard]] std::string_view to_string(FuelUnit unit) noexcept;
[[nodiscard]] Result<FuelUnit> parse_fuel_unit(std::string_view text);
// SI-ish symbol used in reports, e.g. "mL", "g", "L", "bp".
[[nodiscard]] std::string_view fuel_unit_symbol(FuelUnit unit) noexcept;

// A quantity of fuel, expressed in the storage sub-unit of its unit.
struct FuelQuantity {
  FuelUnit unit{FuelUnit::Unknown};
  std::int64_t amount{0};

  friend bool operator==(const FuelQuantity&, const FuelQuantity&) noexcept = default;
};

// A rate of consumption, expressed per hour in the storage sub-unit.
struct FuelRate {
  FuelUnit unit{FuelUnit::Unknown};
  std::int64_t amount_per_hour{0};

  friend bool operator==(const FuelRate&, const FuelRate&) noexcept = default;
};

// Adds two fuel quantities. Refuses mismatched units rather than converting.
[[nodiscard]] Result<FuelQuantity> fuel_add(const FuelQuantity& a, const FuelQuantity& b);
[[nodiscard]] Result<FuelQuantity> fuel_sub(const FuelQuantity& a, const FuelQuantity& b);
[[nodiscard]] int compare_fuel(const FuelQuantity& a, const FuelQuantity& b);

// Runtime available from (level - reserve) at the given rate. Returns
// ResourceRuntimeUnknown when the rate is absent, unknown or non-positive rather
// than reporting an unbounded or zero runtime.
enum class RuntimeBasis : std::uint8_t { Unknown = 0, MeasuredRate, AttestedRate };
[[nodiscard]] std::string_view to_string(RuntimeBasis basis) noexcept;

struct RuntimeEstimate {
  bool known{false};
  std::int64_t seconds{0};
  RuntimeBasis basis{RuntimeBasis::Unknown};
  std::string detail{};
};

[[nodiscard]] Result<RuntimeEstimate> estimate_runtime(const FuelQuantity& level,
                                                       const FuelQuantity& reserve,
                                                       const FuelRate& rate,
                                                       RuntimeBasis basis);

// ---------------------------------------------------------------------------
// Electrical, thermal and mechanical quantities
// ---------------------------------------------------------------------------
struct Voltage {
  std::int64_t millivolts{0};
  friend bool operator==(const Voltage&, const Voltage&) noexcept = default;
};

struct Current {
  std::int64_t milliamps{0};
  friend bool operator==(const Current&, const Current&) noexcept = default;
};

struct Frequency {
  std::int64_t millihertz{0};
  friend bool operator==(const Frequency&, const Frequency&) noexcept = default;
};

struct PhaseAngle {
  std::int64_t millidegrees{0};
  friend bool operator==(const PhaseAngle&, const PhaseAngle&) noexcept = default;
};

struct ActivePower {
  std::int64_t watts{0};
  friend bool operator==(const ActivePower&, const ActivePower&) noexcept = default;
};

struct ReactivePower {
  std::int64_t vars{0};
  friend bool operator==(const ReactivePower&, const ReactivePower&) noexcept = default;
};

// Temperature in thousandths of a degree Celsius.
struct Temperature {
  std::int64_t millidegrees_celsius{0};
  friend bool operator==(const Temperature&, const Temperature&) noexcept = default;
};

struct Pressure {
  std::int64_t pascals{0};
  friend bool operator==(const Pressure&, const Pressure&) noexcept = default;
};

// A fraction in basis points (1/10000). 10000 bp == 100%.
struct Percent {
  std::int64_t basis_points{0};
  friend bool operator==(const Percent&, const Percent&) noexcept = default;
  [[nodiscard]] bool in_range_0_100() const noexcept {
    return basis_points >= 0 && basis_points <= 10000;
  }
};

struct Duration {
  std::int64_t seconds{0};
  friend bool operator==(const Duration&, const Duration&) noexcept = default;
};

// A closed numeric window. A window whose bounds are inverted is rejected at
// construction time by the callers that build it from configuration.
template <typename Q>
struct Window {
  Q minimum{};
  Q maximum{};

  [[nodiscard]] bool contains(const Q& value) const noexcept {
    return !(value < minimum) && !(maximum < value);
  }
};

[[nodiscard]] bool operator<(const Voltage& a, const Voltage& b) noexcept;
[[nodiscard]] bool operator<(const Frequency& a, const Frequency& b) noexcept;
[[nodiscard]] bool operator<(const PhaseAngle& a, const PhaseAngle& b) noexcept;
[[nodiscard]] bool operator<(const Temperature& a, const Temperature& b) noexcept;
[[nodiscard]] bool operator<(const Pressure& a, const Pressure& b) noexcept;
[[nodiscard]] bool operator<(const ActivePower& a, const ActivePower& b) noexcept;

// Absolute difference with overflow refusal.
[[nodiscard]] Result<std::int64_t> abs_diff(std::int64_t a, std::int64_t b);

// Deterministic rendering helpers used by reports. Units are always printed.
[[nodiscard]] std::string format_fuel(const FuelQuantity& quantity);
[[nodiscard]] std::string format_fuel_rate(const FuelRate& rate);
[[nodiscard]] std::string format_voltage(const Voltage& value);
[[nodiscard]] std::string format_frequency(const Frequency& value);
[[nodiscard]] std::string format_phase_angle(const PhaseAngle& value);
[[nodiscard]] std::string format_temperature(const Temperature& value);
[[nodiscard]] std::string format_pressure(const Pressure& value);
[[nodiscard]] std::string format_power(const ActivePower& value);
[[nodiscard]] std::string format_percent(const Percent& value);

// ---------------------------------------------------------------------------
// TypedValue: a unit-tagged scalar carried inside recorded checks and
// preconditions. The tag travels with the number so a decoded value can never be
// reinterpreted in the wrong unit.
// ---------------------------------------------------------------------------
enum class TypedValueKind : std::uint8_t {
  None = 0,
  Boolean = 1,
  Millivolts = 2,
  Millihertz = 3,
  Millidegrees = 4,
  MilliAmps = 5,
  Watts = 6,
  Vars = 7,
  MilliDegreesCelsius = 8,
  Pascals = 9,
  BasisPoints = 10,
  Millilitres = 11,
  Grams = 12,
  Litres = 13,
  Seconds = 14,
  MillilitresPerHour = 15,
  GramsPerHour = 16,
  LitresPerHour = 17,
};

[[nodiscard]] std::string_view to_string(TypedValueKind kind) noexcept;

struct TypedValue {
  TypedValueKind kind{TypedValueKind::None};
  std::int64_t amount{0};

  friend bool operator==(const TypedValue&, const TypedValue&) noexcept = default;
};

[[nodiscard]] std::string format_typed_value(const TypedValue& value);
[[nodiscard]] TypedValue from_voltage(const Voltage& value);
[[nodiscard]] TypedValue from_frequency(const Frequency& value);
[[nodiscard]] TypedValue from_phase_angle(const PhaseAngle& value);
[[nodiscard]] TypedValue from_temperature(const Temperature& value);
[[nodiscard]] TypedValue from_pressure(const Pressure& value);
[[nodiscard]] TypedValue from_percent(const Percent& value);
[[nodiscard]] TypedValue from_fuel(const FuelQuantity& value);
[[nodiscard]] TypedValue from_fuel_rate(const FuelRate& value);

// ---------------------------------------------------------------------------
// Canonical codecs for physical quantities. Every quantity encodes its unit
// alongside its amount so that a decode can never reinterpret the number.
// ---------------------------------------------------------------------------
#define GENCTL_DEFINE_UNIT_CODEC(TypeName, MemberName)                       \
  template <>                                                                \
  struct ValueCodec<TypeName> {                                              \
    static Status encode(CanonicalWriter& writer, const TypeName& value) {   \
      return writer.put_i64(value.MemberName);                               \
    }                                                                        \
    static Result<TypeName> decode(CanonicalReader& reader) {                \
      TypeName out{};                                                        \
      GENCTL_TRY_ASSIGN(raw, reader.get_i64());                              \
      out.MemberName = raw;                                                  \
      return out;                                                            \
    }                                                                        \
  }

GENCTL_DEFINE_UNIT_CODEC(Voltage, millivolts);
GENCTL_DEFINE_UNIT_CODEC(Current, milliamps);
GENCTL_DEFINE_UNIT_CODEC(Frequency, millihertz);
GENCTL_DEFINE_UNIT_CODEC(PhaseAngle, millidegrees);
GENCTL_DEFINE_UNIT_CODEC(ActivePower, watts);
GENCTL_DEFINE_UNIT_CODEC(ReactivePower, vars);
GENCTL_DEFINE_UNIT_CODEC(Temperature, millidegrees_celsius);
GENCTL_DEFINE_UNIT_CODEC(Pressure, pascals);
GENCTL_DEFINE_UNIT_CODEC(Percent, basis_points);
GENCTL_DEFINE_UNIT_CODEC(Duration, seconds);

#undef GENCTL_DEFINE_UNIT_CODEC

template <>
struct ValueCodec<TypedValue> {
  static Status encode(CanonicalWriter& writer, const TypedValue& value) {
    GENCTL_TRY(writer.put_u8(static_cast<std::uint8_t>(value.kind)));
    return writer.put_i64(value.amount);
  }
  static Result<TypedValue> decode(CanonicalReader& reader) {
    TypedValue out{};
    GENCTL_TRY_ASSIGN(kind, reader.get_u8());
    if (kind > static_cast<std::uint8_t>(TypedValueKind::LitresPerHour)) {
      return make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence,
                         "typed value kind out of range");
    }
    GENCTL_TRY_ASSIGN(amount, reader.get_i64());
    out.kind = static_cast<TypedValueKind>(kind);
    out.amount = amount;
    return out;
  }
};

template <>
struct ValueCodec<FuelQuantity> {
  static Status encode(CanonicalWriter& writer, const FuelQuantity& value) {
    GENCTL_TRY(writer.put_u8(static_cast<std::uint8_t>(value.unit)));
    return writer.put_i64(value.amount);
  }
  static Result<FuelQuantity> decode(CanonicalReader& reader) {
    FuelQuantity out{};
    GENCTL_TRY_ASSIGN(unit, reader.get_u8());
    if (unit > static_cast<std::uint8_t>(FuelUnit::PercentOfCapacity)) {
      return make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence,
                         "fuel unit out of range");
    }
    GENCTL_TRY_ASSIGN(amount, reader.get_i64());
    out.unit = static_cast<FuelUnit>(unit);
    out.amount = amount;
    return out;
  }
};

template <>
struct ValueCodec<FuelRate> {
  static Status encode(CanonicalWriter& writer, const FuelRate& value) {
    GENCTL_TRY(writer.put_u8(static_cast<std::uint8_t>(value.unit)));
    return writer.put_i64(value.amount_per_hour);
  }
  static Result<FuelRate> decode(CanonicalReader& reader) {
    FuelRate out{};
    GENCTL_TRY_ASSIGN(unit, reader.get_u8());
    if (unit > static_cast<std::uint8_t>(FuelUnit::PercentOfCapacity)) {
      return make_status(ErrorCode::MalformedEncoding, ValidationStage::Persistence,
                         "fuel rate unit out of range");
    }
    GENCTL_TRY_ASSIGN(amount, reader.get_i64());
    out.unit = static_cast<FuelUnit>(unit);
    out.amount_per_hour = amount;
    return out;
  }
};

}  // namespace genctl
