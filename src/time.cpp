// Generator Control - clock and freshness implementation.
#include "genctl/time.hpp"

#include <chrono>
#include <cstdio>

namespace genctl {

Clock::~Clock() = default;

EpochMillis SystemClock::now_millis() const {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

Result<Millis> add_millis(Millis a, Millis b) {
  constexpr Millis kMax = (std::numeric_limits<Millis>::max)();
  constexpr Millis kMin = (std::numeric_limits<Millis>::min)();
  if (b > 0 && a > kMax - b) {
    return make_status(ErrorCode::ArithmeticOverflow, ValidationStage::Format,
                       "millisecond addition overflows");
  }
  if (b < 0 && a < kMin - b) {
    return make_status(ErrorCode::ArithmeticOverflow, ValidationStage::Format,
                       "millisecond addition underflows");
  }
  return a + b;
}

Result<Millis> sub_millis(Millis a, Millis b) {
  constexpr Millis kMin = (std::numeric_limits<Millis>::min)();
  if (b == kMin) {
    return make_status(ErrorCode::ArithmeticOverflow, ValidationStage::Format,
                       "millisecond subtraction overflows");
  }
  return add_millis(a, -b);
}

std::string_view to_string(FreshnessVerdict verdict) noexcept {
  switch (verdict) {
    case FreshnessVerdict::Unknown: return "unknown";
    case FreshnessVerdict::Fresh: return "fresh";
    case FreshnessVerdict::Stale: return "stale";
    case FreshnessVerdict::FutureDated: return "future-dated";
    case FreshnessVerdict::Unbounded: return "unbounded";
  }
  return "unknown";
}

FreshnessAssessment assess_freshness(EpochMillis observed_at, EpochMillis now,
                                     const FreshnessPolicy& policy) {
  FreshnessAssessment out{};
  const Result<Millis> age = sub_millis(now, observed_at);
  if (!age.ok()) {
    out.verdict = FreshnessVerdict::FutureDated;
    out.detail = "observation instant cannot be compared with the current instant";
    return out;
  }
  out.age_millis = age.value();
  if (out.age_millis < -policy.future_tolerance_millis) {
    out.verdict = FreshnessVerdict::FutureDated;
    out.detail = "observation is " + format_millis(-out.age_millis) +
                 " ahead of the current instant, beyond the tolerance of " +
                 format_millis(policy.future_tolerance_millis);
    return out;
  }
  if (policy.max_age_millis < 0) {
    out.verdict = FreshnessVerdict::Unbounded;
    out.detail = "freshness policy does not bound the age of this evidence";
    return out;
  }
  if (out.age_millis > policy.max_age_millis) {
    out.verdict = FreshnessVerdict::Stale;
    out.detail = "observation is " + format_millis(out.age_millis) + " old, beyond the limit of " +
                 format_millis(policy.max_age_millis);
    return out;
  }
  out.verdict = FreshnessVerdict::Fresh;
  out.detail = "observation is " + format_millis(out.age_millis) + " old";
  return out;
}

std::string format_epoch_millis(EpochMillis value) {
  // Deterministic proleptic Gregorian rendering without locale involvement.
  const bool negative = value < 0;
  const std::int64_t magnitude = negative ? -value : value;
  const std::int64_t seconds = magnitude / 1000;
  const std::int64_t millis = magnitude % 1000;
  std::int64_t days = seconds / 86400;
  std::int64_t remainder = seconds % 86400;
  if (negative) {
    // Only used for pre-1970 instants; kept simple and still reversible.
    days = -days;
    remainder = -remainder;
  }
  const std::int64_t hour = remainder / 3600;
  const std::int64_t minute = (remainder % 3600) / 60;
  const std::int64_t second = remainder % 60;

  // civil_from_days (Howard Hinnant's algorithm), valid for the full int64 range
  // once the epoch day count is reduced.
  const std::int64_t z = days + 719468;
  const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const std::int64_t doe = z - era * 146097;
  const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const std::int64_t y = yoe + era * 400;
  const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const std::int64_t mp = (5 * doy + 2) / 153;
  const std::int64_t d = doy - (153 * mp + 2) / 5 + 1;
  const std::int64_t m = mp < 10 ? mp + 3 : mp - 9;
  const std::int64_t year = m <= 2 ? y + 1 : y;

  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld.%03lldZ",
                static_cast<long long>(year), static_cast<long long>(m),
                static_cast<long long>(d), static_cast<long long>(hour),
                static_cast<long long>(minute), static_cast<long long>(second),
                static_cast<long long>(millis));
  std::string out{buffer};
  if (negative) out.insert(0, "-");
  return out;
}

std::string format_millis(Millis value) {
  const bool negative = value < 0;
  const std::int64_t magnitude = negative ? -value : value;
  std::string out;
  if (negative) out.push_back('-');
  if (magnitude >= kMillisPerHour) {
    const std::int64_t hours = magnitude / kMillisPerHour;
    const std::int64_t rem = magnitude % kMillisPerHour;
    out += std::to_string(hours) + "h" + std::to_string(rem / kMillisPerMinute) + "m";
    return out;
  }
  if (magnitude >= kMillisPerSecond) {
    out += std::to_string(magnitude / kMillisPerSecond) + ".";
    const std::int64_t rem = magnitude % kMillisPerSecond;
    std::string fraction = std::to_string(rem);
    fraction.insert(fraction.begin(), 3 - fraction.size(), '0');
    out += fraction + "s";
    return out;
  }
  out += std::to_string(magnitude) + "ms";
  return out;
}

}  // namespace genctl
