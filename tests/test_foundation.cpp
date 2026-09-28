// Proof obligations: strongly typed identities, error precedence, time arithmetic,
// checked units and the local digest primitives.
#include <array>
#include <vector>
#include <limits>
#include <type_traits>

#include "genctl/canonical.hpp"
#include "genctl/digest.hpp"
#include "genctl/ids.hpp"
#include "genctl/time.hpp"
#include "genctl/units.hpp"
#include "support.hpp"
#include "test_framework.hpp"

namespace {
using namespace genctl;
using namespace gctest;

TEST(ids_reject_traversal_and_reserved_forms) {
  CHECK(GeneratorId::parse("gen-1").ok());
  CHECK(GeneratorId::parse("A").ok());
  CHECK(GeneratorId::parse("unit.a:1").ok());
  CHECK_RESULT_CODE(GeneratorId::parse(""), ErrorCode::InvalidIdentifier);
  CHECK_RESULT_CODE(GeneratorId::parse(".."), ErrorCode::InvalidIdentifier);
  CHECK_RESULT_CODE(GeneratorId::parse("a..b"), ErrorCode::InvalidIdentifier);
  CHECK_RESULT_CODE(GeneratorId::parse("-leading"), ErrorCode::InvalidIdentifier);
  CHECK_RESULT_CODE(GeneratorId::parse("trailing-"), ErrorCode::InvalidIdentifier);
  CHECK_RESULT_CODE(GeneratorId::parse("has space"), ErrorCode::InvalidIdentifier);
  CHECK_RESULT_CODE(GeneratorId::parse("path/sep"), ErrorCode::InvalidIdentifier);
  CHECK_RESULT_CODE(GeneratorId::parse("back\\slash"), ErrorCode::InvalidIdentifier);
  CHECK_RESULT_CODE(GeneratorId::parse(std::string(65, 'a')), ErrorCode::LengthLimitExceeded);
}

TEST(idempotency_keys_are_bounded) {
  CHECK(IdempotencyKey::parse("12345678").ok());
  CHECK_RESULT_CODE(IdempotencyKey::parse("short"), ErrorCode::InvalidIdentifier);
  CHECK_RESULT_CODE(IdempotencyKey::parse(""), ErrorCode::IdempotencyKeyMissing);
  CHECK_RESULT_CODE(IdempotencyKey::parse("has space here"), ErrorCode::InvalidIdentifier);
  CHECK_RESULT_CODE(IdempotencyKey::parse(std::string(65, 'k')), ErrorCode::LengthLimitExceeded);
}

TEST(distinct_counter_types_do_not_interchange) {
  // The point of the strong types is that this file does not compile if the types are
  // collapsed: each counter is only comparable with its own kind.
  static_assert(!std::is_convertible_v<ControlEpoch, StateRevision>);
  static_assert(!std::is_convertible_v<IncarnationId, AttemptId>);
  static_assert(!std::is_convertible_v<HardwareGeneration, CommitSeq>);
  const ControlEpoch epoch{7};
  CHECK_EQ(epoch.value(), 7u);
  CHECK(epoch.is_set());
  CHECK(ControlEpoch{}.is_unset());
  // Compile-time facts about the counter types are asserted at compile time, which is
  // stronger than a runtime check and keeps the runtime assertions free of constant
  // conditions.
  static_assert(ControlEpoch{7}.next().value() == 8u, "successor is monotonic");
  static_assert(ControlEpoch{7} < ControlEpoch{8}, "counter ordering");
  static_assert(ControlEpoch{}.is_unset(), "a default counter is unset");
  static_assert(!ControlEpoch{} .is_set(), "a default counter is not set");
}

TEST(strong_id_saturates_instead_of_wrapping) {
  const ControlEpoch maximum{(std::numeric_limits<std::uint64_t>::max)()};
  CHECK(maximum.at_max());
  CHECK_EQ(maximum.next().value(), maximum.value());
}

TEST(error_precedence_is_documented_and_stable) {
  CHECK(default_stage(ErrorCode::InvalidIdentifier) == ValidationStage::Format);
  CHECK(default_stage(ErrorCode::UnknownGenerator) == ValidationStage::Identity);
  CHECK(default_stage(ErrorCode::IdempotencyKeyConflict) ==
        ValidationStage::IdempotencyConflict);
  CHECK(default_stage(ErrorCode::StaleControlEpoch) == ValidationStage::Fencing);
  CHECK(default_stage(ErrorCode::InterlockEngaged) == ValidationStage::Interlock);
  CHECK(default_stage(ErrorCode::ResourceInsufficient) == ValidationStage::Resource);
  CHECK(default_stage(ErrorCode::SynchronizationNotEligible) ==
        ValidationStage::Synchronization);
  CHECK(default_stage(ErrorCode::TransferNotEligible) == ValidationStage::Transfer);
  CHECK(default_stage(ErrorCode::StoreCorrupt) == ValidationStage::Persistence);
  // The ordinals encode the documented validation precedence. They are read through
  // non-constant locals so the assertion is a runtime comparison.
  const std::vector<ValidationStage> order = {
      ValidationStage::Format,          ValidationStage::Identity,
      ValidationStage::IdempotencyReplay, ValidationStage::IdempotencyConflict,
      ValidationStage::Fencing,         ValidationStage::Lifecycle,
      ValidationStage::Transition,      ValidationStage::Mode,
      ValidationStage::Authority,       ValidationStage::Interlock,
      ValidationStage::Readiness,       ValidationStage::Resource,
      ValidationStage::Synchronization, ValidationStage::Transfer,
      ValidationStage::Reservation,     ValidationStage::Actuation,
      ValidationStage::Observation,     ValidationStage::Verification,
      ValidationStage::Persistence,     ValidationStage::Internal};
  for (std::size_t i = 1; i < order.size(); ++i) {
    const int previous = static_cast<int>(order[i - 1]);
    const int current = static_cast<int>(order[i]);
    CHECK(previous < current);
  }
}

TEST(exit_codes_are_stable_per_category) {
  CHECK_EQ(exit_code(ErrorCode::Ok), 0);
  CHECK_EQ(exit_code(ErrorCode::InvalidArgument), 2);
  CHECK_EQ(exit_code(ErrorCode::MaintenanceActive), 3);
  CHECK_EQ(exit_code(ErrorCode::EvidenceStale), 4);
  CHECK_EQ(exit_code(ErrorCode::StaleStateRevision), 5);
  CHECK_EQ(exit_code(ErrorCode::AdapterRejected), 6);
  CHECK_EQ(exit_code(ErrorCode::StoreCorrupt), 7);
  CHECK_EQ(exit_code(ErrorCode::IdempotencyKeyConflict), 8);
}

TEST(time_arithmetic_refuses_overflow) {
  CHECK_RESULT_CODE(add_millis((std::numeric_limits<Millis>::max)(), 1),
                    ErrorCode::ArithmeticOverflow);
  CHECK_RESULT_CODE(sub_millis((std::numeric_limits<Millis>::min)(), 1),
                    ErrorCode::ArithmeticOverflow);
  CHECK_RESULT_CODE(sub_millis(0, (std::numeric_limits<Millis>::min)()),
                    ErrorCode::ArithmeticOverflow);
  CHECK_EQ(add_millis(5, -3).value(), 2);
}

TEST(freshness_distinguishes_stale_future_and_unbounded) {
  const EpochMillis now = 1'000'000;
  CHECK(assess_freshness(now - 500, now, FreshnessPolicy::within(1000)).fresh());
  CHECK(assess_freshness(now - 5000, now, FreshnessPolicy::within(1000)).verdict ==
        FreshnessVerdict::Stale);
  CHECK(assess_freshness(now + 60000, now, FreshnessPolicy::within(1000)).verdict ==
        FreshnessVerdict::FutureDated);
  CHECK(assess_freshness(now - 999999, now, FreshnessPolicy::unlimited()).verdict ==
        FreshnessVerdict::Unbounded);
  // A small clock skew inside the tolerance is still fresh.
  CHECK(assess_freshness(now + 500, now, FreshnessPolicy::within(1000)).fresh());
}

TEST(epoch_formatting_is_deterministic) {
  CHECK_EQ(format_epoch_millis(0), std::string("1970-01-01T00:00:00.000Z"));
  CHECK_EQ(format_epoch_millis(1'700'000'000'000LL), std::string("2023-11-14T22:13:20.000Z"));
  CHECK_EQ(format_millis(1500), std::string("1.500s"));
  CHECK_EQ(format_millis(-250), std::string("-250ms"));
}

TEST(checked_arithmetic_refuses_wrap_and_zero_divisors) {
  CHECK_RESULT_CODE(add_checked((std::numeric_limits<std::int64_t>::max)(), 1),
                    ErrorCode::ArithmeticOverflow);
  CHECK_RESULT_CODE(sub_checked((std::numeric_limits<std::int64_t>::min)(), 1),
                    ErrorCode::ArithmeticOverflow);
  CHECK_RESULT_CODE(mul_checked((std::numeric_limits<std::int64_t>::max)(), 2),
                    ErrorCode::ArithmeticOverflow);
  CHECK_RESULT_CODE(mul_checked((std::numeric_limits<std::int64_t>::min)(), -1),
                    ErrorCode::ArithmeticOverflow);
  CHECK_RESULT_CODE(div_checked(1, 0), ErrorCode::DivisionByZero);
  CHECK_RESULT_CODE(div_checked((std::numeric_limits<std::int64_t>::min)(), -1),
                    ErrorCode::ArithmeticOverflow);
  CHECK_EQ(mul_checked(0, (std::numeric_limits<std::int64_t>::max)()).value(), 0);
  CHECK_EQ(add_checked(-5, 3).value(), -2);
}

TEST(fuel_arithmetic_refuses_cross_unit_guessing) {
  const FuelQuantity litres{FuelUnit::Millilitres, 1000};
  const FuelQuantity same{FuelUnit::Millilitres, 500};
  const FuelQuantity other{FuelUnit::Grams, 500};
  CHECK_EQ(fuel_add(litres, same).value().amount, 1500);
  CHECK_RESULT_CODE(fuel_add(litres, other), ErrorCode::ResourceUnitMismatch);
  CHECK_RESULT_CODE(fuel_sub(litres, other), ErrorCode::ResourceUnitMismatch);
  CHECK(compare_fuel(litres, same) > 0);
}

TEST(runtime_estimate_never_invents_a_runtime) {
  const FuelQuantity level{FuelUnit::Millilitres, 900'000};
  const FuelQuantity reserve{FuelUnit::Millilitres, 200'000};
  const FuelRate rate{FuelUnit::Millilitres, 60'000};
  const Result<RuntimeEstimate> estimate =
      estimate_runtime(level, reserve, rate, RuntimeBasis::MeasuredRate);
  CHECK(estimate.ok());
  CHECK(estimate.value().known);
  CHECK_EQ(estimate.value().seconds, 42000);  // (900000-200000) mL * 3600 / 60000 mL/h

  // Unknown units must not become a runtime.
  const Result<RuntimeEstimate> unknown =
      estimate_runtime(FuelQuantity{}, reserve, rate, RuntimeBasis::Unknown);
  CHECK(unknown.ok());
  CHECK(!unknown.value().known);

  // A mismatched unit is refused rather than converted using an assumed density.
  const Result<RuntimeEstimate> mismatch =
      estimate_runtime(FuelQuantity{FuelUnit::Grams, 900'000}, reserve, rate,
                       RuntimeBasis::MeasuredRate);
  CHECK(mismatch.ok());
  CHECK(!mismatch.value().known);

  // A zero rate never yields an unbounded runtime.
  const Result<RuntimeEstimate> zero_rate =
      estimate_runtime(level, reserve, FuelRate{FuelUnit::Millilitres, 0},
                       RuntimeBasis::MeasuredRate);
  CHECK(zero_rate.ok());
  CHECK(!zero_rate.value().known);
}

TEST(digest_primitives_match_published_vectors) {
  CHECK(digest_self_test().ok());
  CHECK_EQ(sha256(std::string_view("abc")).hex(),
           std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  CHECK_EQ(crc32c(std::string_view("123456789")), 0xE3069283u);
  CHECK_EQ(fnv1a64(std::string_view("")), 0xcbf29ce484222325ull);
  const Digest256 parsed = Digest256::parse_hex(sha256("abc").hex()).value();
  CHECK(parsed == sha256("abc"));
  CHECK_RESULT_CODE(Digest256::parse_hex("zz"), ErrorCode::InvalidArgument);
  CHECK_RESULT_CODE(Digest256::parse_hex(std::string(63, 'a')), ErrorCode::InvalidArgument);
}

TEST(canonical_encoding_is_little_endian_and_strict) {
  CanonicalWriter writer;
  CHECK(writer.put_u32(0x01020304u).ok());
  CHECK(writer.put_i64(-1).ok());
  CHECK_EQ(writer.bytes().size(), std::size_t{12});
  CHECK_EQ(writer.bytes()[0], 0x04u);
  CHECK_EQ(writer.bytes()[3], 0x01u);
  CHECK_EQ(writer.bytes()[4], 0xFFu);

  CanonicalReader reader(writer.bytes().data(), writer.bytes().size());
  CHECK_EQ(reader.get_u32().value(), 0x01020304u);
  CHECK_EQ(reader.get_i64().value(), -1);
  CHECK(reader.expect_end().ok());
}

TEST(canonical_reader_rejects_truncation_and_trailing_bytes) {
  CanonicalWriter writer;
  CHECK(writer.put_string("hello").ok());
  const ByteBuffer full = writer.bytes();

  CanonicalReader truncated(full.data(), full.size() - 1);
  CHECK_RESULT_CODE(truncated.get_string(), ErrorCode::StoreTruncated);

  CanonicalWriter extended;
  CHECK(extended.put_string("hello").ok());
  CHECK(extended.put_u8(1).ok());
  CanonicalReader trailing(extended.bytes().data(), extended.bytes().size());
  CHECK(trailing.get_string().ok());
  CHECK_CODE(trailing.expect_end(), ErrorCode::MalformedEncoding);
}

TEST(canonical_reader_refuses_absurd_declared_lengths_before_allocating) {
  // A string header that declares 4 GiB inside a 10 byte buffer must fail on the
  // bound, not on the allocation.
  ByteBuffer hostile;
  hostile.push_back(0xFFu);
  hostile.push_back(0xFFu);
  hostile.push_back(0xFFu);
  hostile.push_back(0x7Fu);
  hostile.push_back('a');
  CanonicalReader reader(hostile.data(), hostile.size());
  CHECK_RESULT_CODE(reader.get_string(), ErrorCode::LengthLimitExceeded);
}

TEST(canonical_writer_bounds_total_size) {
  CanonicalLimits limits{};
  limits.max_total_bytes = 16;
  CanonicalWriter writer(limits);
  CHECK(writer.put_u64(1).ok());
  CHECK(writer.put_u64(2).ok());
  CHECK_CODE(writer.put_u64(3), ErrorCode::LengthLimitExceeded);
}

TEST(boolean_encoding_refuses_non_boolean_bytes) {
  const std::uint8_t bytes[] = {0x02u};
  CanonicalReader reader(bytes, 1);
  CHECK_RESULT_CODE(reader.get_bool(), ErrorCode::MalformedEncoding);
}

TEST(equivalent_state_produces_identical_canonical_bytes) {
  // Two encodings of the same logical value, produced through different call paths,
  // must be byte identical.
  CanonicalWriter first;
  CHECK(first.put_string("unit-1").ok());
  CHECK(first.put_u64(4).ok());
  CHECK(first.put_blob(Digest256{}.bytes.data(), 32).ok());
  CanonicalWriter second;
  CHECK(second.put_string(std::string("unit-1")).ok());
  CHECK(second.put_u64(4).ok());
  const std::array<std::uint8_t, 32> zeros{};
  CHECK(second.put_blob(zeros.data(), zeros.size()).ok());
  CHECK(first.bytes() == second.bytes());
  CHECK(sha256(first.bytes()) == sha256(second.bytes()));
}

TEST(digest_chain_binds_previous_content) {
  const Digest256 zero = Digest256::zero();
  const ByteBuffer a{'a'};
  const ByteBuffer b{'b'};
  CHECK(digest_chain("genctl", zero, a) != digest_chain("genctl", zero, b));
  CHECK(digest_chain("genctl", zero, a) != digest_chain("other", zero, a));
  const Digest256 first = digest_chain("genctl", zero, a);
  CHECK(digest_chain("genctl", first, a) != first);
}

}  // namespace
