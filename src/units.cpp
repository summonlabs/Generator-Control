// Generator Control - checked arithmetic, units and deterministic formatting.
#include "genctl/units.hpp"

#include <cstdio>
#include <limits>

namespace genctl {
namespace {

constexpr std::int64_t kI64Max = (std::numeric_limits<std::int64_t>::max)();
constexpr std::int64_t kI64Min = (std::numeric_limits<std::int64_t>::min)();

Status overflow_status(const char* operation) {
  return make_status(ErrorCode::ArithmeticOverflow, ValidationStage::Format,
                     std::string("checked ") + operation + " overflowed a 64 bit integer");
}

std::string format_scaled(std::int64_t value, std::int64_t scale, int fraction_digits) {
  const bool negative = value < 0;
  const std::uint64_t magnitude =
      negative ? static_cast<std::uint64_t>(-(value + 1)) + 1ull : static_cast<std::uint64_t>(value);
  const std::uint64_t whole = magnitude / static_cast<std::uint64_t>(scale);
  const std::uint64_t fraction = magnitude % static_cast<std::uint64_t>(scale);
  std::string out;
  if (negative) out.push_back('-');
  out += std::to_string(whole);
  if (fraction_digits > 0) {
    std::string digits = std::to_string(fraction);
    digits.insert(digits.begin(), static_cast<std::size_t>(fraction_digits) - digits.size(), '0');
    out.push_back('.');
    out += digits;
  }
  return out;
}

}  // namespace

Result<std::int64_t> add_checked(std::int64_t a, std::int64_t b) {
  if (b > 0 && a > kI64Max - b) return overflow_status("addition");
  if (b < 0 && a < kI64Min - b) return overflow_status("addition");
  return a + b;
}

Result<std::int64_t> sub_checked(std::int64_t a, std::int64_t b) {
  if (b == kI64Min) return overflow_status("subtraction");
  return add_checked(a, -b);
}

Result<std::int64_t> mul_checked(std::int64_t a, std::int64_t b) {
  if (a == 0 || b == 0) return std::int64_t{0};
  if (a == -1 && b == kI64Min) return overflow_status("multiplication");
  if (b == -1 && a == kI64Min) return overflow_status("multiplication");
  if (a > 0) {
    if (b > 0) {
      if (a > kI64Max / b) return overflow_status("multiplication");
    } else {
      if (b < kI64Min / a) return overflow_status("multiplication");
    }
  } else {
    if (b > 0) {
      if (a < kI64Min / b) return overflow_status("multiplication");
    } else {
      if (a < kI64Max / b) return overflow_status("multiplication");
    }
  }
  return a * b;
}

Result<std::int64_t> div_checked(std::int64_t a, std::int64_t b) {
  if (b == 0) {
    return make_status(ErrorCode::DivisionByZero, ValidationStage::Format,
                       "checked division by zero");
  }
  if (a == kI64Min && b == -1) return overflow_status("division");
  return a / b;
}

bool in_range(std::int64_t value, std::int64_t lo, std::int64_t hi) noexcept {
  return value >= lo && value <= hi;
}

std::string_view to_string(FuelUnit unit) noexcept {
  switch (unit) {
    case FuelUnit::Unknown: return "unknown";
    case FuelUnit::Millilitres: return "millilitres";
    case FuelUnit::Grams: return "grams";
    case FuelUnit::Litres: return "litres";
    case FuelUnit::PercentOfCapacity: return "percent-of-capacity";
  }
  return "unknown";
}

Result<FuelUnit> parse_fuel_unit(std::string_view text) {
  if (text == "millilitres" || text == "ml" || text == "mL") return FuelUnit::Millilitres;
  if (text == "grams" || text == "g") return FuelUnit::Grams;
  if (text == "litres" || text == "l" || text == "L") return FuelUnit::Litres;
  if (text == "percent" || text == "bp" || text == "PercentOfCapacity") {
    return FuelUnit::PercentOfCapacity;
  }
  return make_status(ErrorCode::UnsupportedUnit, ValidationStage::Format,
                     std::string("unsupported fuel unit '") + std::string(text) + "'");
}

std::string_view fuel_unit_symbol(FuelUnit unit) noexcept {
  switch (unit) {
    case FuelUnit::Unknown: return "?";
    case FuelUnit::Millilitres: return "mL";
    case FuelUnit::Grams: return "g";
    case FuelUnit::Litres: return "L";
    case FuelUnit::PercentOfCapacity: return "bp";
  }
  return "?";
}

bool unit_compatible(FuelUnit a, FuelUnit b) noexcept {
  return a != FuelUnit::Unknown && a == b;
}

Result<FuelQuantity> fuel_add(const FuelQuantity& a, const FuelQuantity& b) {
  if (!unit_compatible(a.unit, b.unit)) {
    return make_status(ErrorCode::ResourceUnitMismatch, ValidationStage::Resource,
                       std::string("cannot add ") + std::string(to_string(a.unit)) + " to " +
                           std::string(to_string(b.unit)) + " without a conversion this runtime "
                           "does not own");
  }
  GENCTL_TRY_ASSIGN(sum, add_checked(a.amount, b.amount));
  return FuelQuantity{a.unit, sum};
}

Result<FuelQuantity> fuel_sub(const FuelQuantity& a, const FuelQuantity& b) {
  if (!unit_compatible(a.unit, b.unit)) {
    return make_status(ErrorCode::ResourceUnitMismatch, ValidationStage::Resource,
                       std::string("cannot subtract ") + std::string(to_string(b.unit)) + " from " +
                           std::string(to_string(a.unit)));
  }
  GENCTL_TRY_ASSIGN(difference, sub_checked(a.amount, b.amount));
  return FuelQuantity{a.unit, difference};
}

int compare_fuel(const FuelQuantity& a, const FuelQuantity& b) {
  if (a.unit != b.unit) return a.unit < b.unit ? -1 : 1;
  if (a.amount < b.amount) return -1;
  if (a.amount > b.amount) return 1;
  return 0;
}

std::string_view to_string(RuntimeBasis basis) noexcept {
  switch (basis) {
    case RuntimeBasis::Unknown: return "unknown";
    case RuntimeBasis::MeasuredRate: return "measured-rate";
    case RuntimeBasis::AttestedRate: return "attested-rate";
  }
  return "unknown";
}

Result<RuntimeEstimate> estimate_runtime(const FuelQuantity& level, const FuelQuantity& reserve,
                                         const FuelRate& rate, RuntimeBasis basis) {
  RuntimeEstimate out{};
  if (level.unit == FuelUnit::Unknown || rate.unit == FuelUnit::Unknown) {
    out.known = false;
    out.detail = "fuel unit is unknown; runtime cannot be established";
    return out;
  }
  if (level.unit != rate.unit) {
    out.known = false;
    out.detail = std::string("fuel level is reported in ") + std::string(to_string(level.unit)) +
                 " but consumption in " + std::string(to_string(rate.unit)) +
                 "; no conversion is applied because none is owned by this runtime";
    return out;
  }
  if (reserve.unit != FuelUnit::Unknown && reserve.unit != level.unit) {
    out.known = false;
    out.detail = "reserve unit does not match the fuel level unit";
    return out;
  }
  if (rate.amount_per_hour <= 0) {
    out.known = false;
    out.detail = "consumption rate is zero or negative; runtime is unbounded and therefore "
                 "cannot be treated as sufficient evidence";
    return out;
  }
  const std::int64_t reserve_amount = reserve.unit == FuelUnit::Unknown ? 0 : reserve.amount;
  GENCTL_TRY_ASSIGN(usable, sub_checked(level.amount, reserve_amount));
  if (usable <= 0) {
    out.known = true;
    out.seconds = 0;
    out.basis = basis;
    out.detail = "fuel at or below the reserve threshold";
    return out;
  }
  // seconds = usable * 3600 / rate, computed with checked intermediate arithmetic.
  GENCTL_TRY_ASSIGN(scaled, mul_checked(usable, 3600));
  GENCTL_TRY_ASSIGN(seconds, div_checked(scaled, rate.amount_per_hour));
  out.known = true;
  out.seconds = seconds;
  out.basis = basis;
  out.detail = "derived from a consumption rate of " + format_fuel_rate(rate);
  return out;
}

bool operator<(const Voltage& a, const Voltage& b) noexcept { return a.millivolts < b.millivolts; }
bool operator<(const Frequency& a, const Frequency& b) noexcept {
  return a.millihertz < b.millihertz;
}
bool operator<(const PhaseAngle& a, const PhaseAngle& b) noexcept {
  return a.millidegrees < b.millidegrees;
}
bool operator<(const Temperature& a, const Temperature& b) noexcept {
  return a.millidegrees_celsius < b.millidegrees_celsius;
}
bool operator<(const Pressure& a, const Pressure& b) noexcept { return a.pascals < b.pascals; }
bool operator<(const ActivePower& a, const ActivePower& b) noexcept { return a.watts < b.watts; }

Result<std::int64_t> abs_diff(std::int64_t a, std::int64_t b) {
  GENCTL_TRY_ASSIGN(difference, sub_checked(a, b));
  if (difference == kI64Min) return overflow_status("absolute difference");
  return difference < 0 ? -difference : difference;
}

std::string format_fuel(const FuelQuantity& quantity) {
  if (quantity.unit == FuelUnit::Unknown) return "unknown";
  if (quantity.unit == FuelUnit::PercentOfCapacity) {
    return format_scaled(quantity.amount, 100, 2) + "%";
  }
  return format_scaled(quantity.amount, 1000, 3) + " " + std::string(fuel_unit_symbol(quantity.unit));
}

std::string format_fuel_rate(const FuelRate& rate) {
  if (rate.unit == FuelUnit::Unknown) return "unknown";
  if (rate.unit == FuelUnit::PercentOfCapacity) {
    return format_scaled(rate.amount_per_hour, 100, 2) + "%/h";
  }
  return format_scaled(rate.amount_per_hour, 1000, 3) + " " +
         std::string(fuel_unit_symbol(rate.unit)) + "/h";
}

std::string format_voltage(const Voltage& value) {
  return format_scaled(value.millivolts, 1000, 3) + " V";
}

std::string format_frequency(const Frequency& value) {
  return format_scaled(value.millihertz, 1000, 3) + " Hz";
}

std::string format_phase_angle(const PhaseAngle& value) {
  return format_scaled(value.millidegrees, 1000, 3) + " deg";
}

std::string format_temperature(const Temperature& value) {
  return format_scaled(value.millidegrees_celsius, 1000, 3) + " C";
}

std::string format_pressure(const Pressure& value) {
  return format_scaled(value.pascals, 1000, 3) + " kPa";
}

std::string format_power(const ActivePower& value) {
  return format_scaled(value.watts, 1000, 3) + " kW";
}

std::string format_percent(const Percent& value) {
  return format_scaled(value.basis_points, 100, 2) + "%";
}

std::string_view to_string(TypedValueKind kind) noexcept {
  switch (kind) {
    case TypedValueKind::None: return "none";
    case TypedValueKind::Boolean: return "boolean";
    case TypedValueKind::Millivolts: return "millivolts";
    case TypedValueKind::Millihertz: return "millihertz";
    case TypedValueKind::Millidegrees: return "millidegrees";
    case TypedValueKind::MilliAmps: return "milliamps";
    case TypedValueKind::Watts: return "watts";
    case TypedValueKind::Vars: return "vars";
    case TypedValueKind::MilliDegreesCelsius: return "millidegrees-celsius";
    case TypedValueKind::Pascals: return "pascals";
    case TypedValueKind::BasisPoints: return "basis-points";
    case TypedValueKind::Millilitres: return "millilitres";
    case TypedValueKind::Grams: return "grams";
    case TypedValueKind::Litres: return "litres";
    case TypedValueKind::Seconds: return "seconds";
    case TypedValueKind::MillilitresPerHour: return "millilitres-per-hour";
    case TypedValueKind::GramsPerHour: return "grams-per-hour";
    case TypedValueKind::LitresPerHour: return "litres-per-hour";
  }
  return "none";
}

std::string format_typed_value(const TypedValue& value) {
  switch (value.kind) {
    case TypedValueKind::None: return "none";
    case TypedValueKind::Boolean: return value.amount != 0 ? "true" : "false";
    case TypedValueKind::Millivolts: return format_voltage(Voltage{value.amount});
    case TypedValueKind::Millihertz: return format_frequency(Frequency{value.amount});
    case TypedValueKind::Millidegrees: return format_phase_angle(PhaseAngle{value.amount});
    case TypedValueKind::MilliAmps: return format_scaled(value.amount, 1000, 3) + " A";
    case TypedValueKind::Watts: return format_power(ActivePower{value.amount});
    case TypedValueKind::Vars: return format_scaled(value.amount, 1000, 3) + " kvar";
    case TypedValueKind::MilliDegreesCelsius:
      return format_temperature(Temperature{value.amount});
    case TypedValueKind::Pascals: return format_pressure(Pressure{value.amount});
    case TypedValueKind::BasisPoints: return format_percent(Percent{value.amount});
    case TypedValueKind::Millilitres:
      return format_fuel(FuelQuantity{FuelUnit::Millilitres, value.amount});
    case TypedValueKind::Grams: return format_fuel(FuelQuantity{FuelUnit::Grams, value.amount});
    case TypedValueKind::Litres: return format_fuel(FuelQuantity{FuelUnit::Litres, value.amount});
    case TypedValueKind::Seconds: return std::to_string(value.amount) + "s";
    case TypedValueKind::MillilitresPerHour:
      return format_fuel_rate(FuelRate{FuelUnit::Millilitres, value.amount});
    case TypedValueKind::GramsPerHour:
      return format_fuel_rate(FuelRate{FuelUnit::Grams, value.amount});
    case TypedValueKind::LitresPerHour:
      return format_fuel_rate(FuelRate{FuelUnit::Litres, value.amount});
  }
  return "none";
}

TypedValue from_voltage(const Voltage& value) { return TypedValue{TypedValueKind::Millivolts, value.millivolts}; }
TypedValue from_frequency(const Frequency& value) {
  return TypedValue{TypedValueKind::Millihertz, value.millihertz};
}
TypedValue from_phase_angle(const PhaseAngle& value) {
  return TypedValue{TypedValueKind::Millidegrees, value.millidegrees};
}
TypedValue from_temperature(const Temperature& value) {
  return TypedValue{TypedValueKind::MilliDegreesCelsius, value.millidegrees_celsius};
}
TypedValue from_pressure(const Pressure& value) {
  return TypedValue{TypedValueKind::Pascals, value.pascals};
}
TypedValue from_percent(const Percent& value) {
  return TypedValue{TypedValueKind::BasisPoints, value.basis_points};
}
TypedValue from_fuel(const FuelQuantity& value) {
  switch (value.unit) {
    case FuelUnit::Millilitres: return TypedValue{TypedValueKind::Millilitres, value.amount};
    case FuelUnit::Grams: return TypedValue{TypedValueKind::Grams, value.amount};
    case FuelUnit::Litres: return TypedValue{TypedValueKind::Litres, value.amount};
    case FuelUnit::PercentOfCapacity:
      return TypedValue{TypedValueKind::BasisPoints, value.amount};
    case FuelUnit::Unknown: break;
  }
  return TypedValue{TypedValueKind::None, 0};
}
TypedValue from_fuel_rate(const FuelRate& value) {
  switch (value.unit) {
    case FuelUnit::Millilitres:
      return TypedValue{TypedValueKind::MillilitresPerHour, value.amount_per_hour};
    case FuelUnit::Grams: return TypedValue{TypedValueKind::GramsPerHour, value.amount_per_hour};
    case FuelUnit::Litres: return TypedValue{TypedValueKind::LitresPerHour, value.amount_per_hour};
    case FuelUnit::PercentOfCapacity:
      return TypedValue{TypedValueKind::BasisPoints, value.amount_per_hour};
    case FuelUnit::Unknown: break;
  }
  return TypedValue{TypedValueKind::None, 0};
}

}  // namespace genctl
