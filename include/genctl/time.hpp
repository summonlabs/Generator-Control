// Generator Control - time, clocks and freshness arithmetic.
//
// Time enters the runtime only through an injected Clock. All persisted instants
// are UTC milliseconds since the Unix epoch, because a monotonic counter is only
// comparable inside the process that created it and persisted evidence must be
// comparable across incarnations and processes.
//
// Freshness is always evaluated against an explicit "now" and an explicit policy;
// there is no implicit global clock and no hidden default freshness.
#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <string>

#include "genctl/result.hpp"

namespace genctl {

// UTC milliseconds since the Unix epoch.
using EpochMillis = std::int64_t;
// Signed duration in milliseconds.
using Millis = std::int64_t;

inline constexpr Millis kMillisPerSecond = 1000;
inline constexpr Millis kMillisPerMinute = 60 * kMillisPerSecond;
inline constexpr Millis kMillisPerHour = 60 * kMillisPerMinute;

// Checked arithmetic on authoritative time quantities.
[[nodiscard]] Result<Millis> add_millis(Millis a, Millis b);
[[nodiscard]] Result<Millis> sub_millis(Millis a, Millis b);

class Clock {
 public:
  virtual ~Clock();
  Clock(const Clock&) = delete;
  Clock& operator=(const Clock&) = delete;

  // UTC milliseconds since the Unix epoch.
  [[nodiscard]] virtual EpochMillis now_millis() const = 0;
  [[nodiscard]] virtual const char* name() const noexcept = 0;

 protected:
  Clock() = default;
};

// Production clock: system UTC time.
class SystemClock final : public Clock {
 public:
  [[nodiscard]] EpochMillis now_millis() const override;
  [[nodiscard]] const char* name() const noexcept override { return "system"; }
};

// Deterministic clock for tests, examples and benchmarks. Time only moves when
// the caller moves it, so canonical bytes are reproducible.
class ManualClock final : public Clock {
 public:
  explicit ManualClock(EpochMillis start = 1'700'000'000'000LL) : now_(start) {}
  [[nodiscard]] EpochMillis now_millis() const override { return now_; }
  [[nodiscard]] const char* name() const noexcept override { return "manual"; }
  void set(EpochMillis value) noexcept { now_ = value; }
  void advance(Millis delta) noexcept { now_ += delta; }

 private:
  EpochMillis now_{0};
};

// Freshness policy for one piece of evidence.
struct FreshnessPolicy {
  // Maximum accepted age. A negative value means "age is not checked"; this is
  // only appropriate for static configuration and explicitly attested grants
  // that carry their own validity window.
  Millis max_age_millis{-1};
  // Tolerance for an observation that appears to come from the future. Beyond
  // this, the evidence is contradictory rather than fresh: clock skew must not
  // be silently rounded into validity.
  Millis future_tolerance_millis{1000};

  [[nodiscard]] static FreshnessPolicy unlimited() noexcept { return FreshnessPolicy{-1, 1000}; }
  [[nodiscard]] static FreshnessPolicy within(Millis age) noexcept { return FreshnessPolicy{age, 1000}; }
};

enum class FreshnessVerdict : std::uint8_t {
  Unknown = 0,
  Fresh,
  Stale,
  FutureDated,   // observation is ahead of now beyond tolerance
  Unbounded,     // policy does not bound age
};

[[nodiscard]] std::string_view to_string(FreshnessVerdict verdict) noexcept;

struct FreshnessAssessment {
  FreshnessVerdict verdict{FreshnessVerdict::Unknown};
  Millis age_millis{0};
  std::string detail{};

  [[nodiscard]] bool fresh() const noexcept { return verdict == FreshnessVerdict::Fresh; }
};

[[nodiscard]] FreshnessAssessment assess_freshness(EpochMillis observed_at,
                                                   EpochMillis now,
                                                   const FreshnessPolicy& policy);

// Formats an instant deterministically as "1970-01-01T00:00:00.000Z".
[[nodiscard]] std::string format_epoch_millis(EpochMillis value);
// Formats a duration as e.g. "12.345s", "-3ms", "2.500h" (deterministic, no locale).
[[nodiscard]] std::string format_millis(Millis value);

}  // namespace genctl
