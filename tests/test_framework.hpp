// Generator Control - deterministic test harness.
//
// Tests are registered with TEST(name) and run by gctest::run_all. There is no
// timeout mechanism of any kind: a hang is a defect to diagnose, not a condition to
// paper over. Randomized tests use a fixed, printed seed so every failure is
// reproducible from the seed alone.
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace gctest {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> cases;
  return cases;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) {
    registry().push_back(TestCase{name, std::move(fn)});
  }
};

// Fixed, printed seed for every randomized or property test.
inline std::uint32_t test_seed() { return 0x5EED1234u; }

[[noreturn]] inline void fail(const std::string& message) { throw std::runtime_error(message); }

#define TEST(name)                                                       \
  static void gctest_##name();                                           \
  static ::gctest::Registrar gctest_reg_##name(#name, &gctest_##name);   \
  static void gctest_##name()

#define CHECK(condition)                                                              \
  do {                                                                                \
    if (!(condition)) {                                                               \
      ::gctest::fail(std::string("CHECK failed: ") + #condition + " at " + __FILE__ + \
                     ":" + std::to_string(__LINE__));                                 \
    }                                                                                 \
  } while (false)

#define CHECK_EQ(a, b)                                                                     \
  do {                                                                                     \
    /* Copied, not bound by reference: a lifetime-extended temporary in an assertion is    \
       exactly the kind of subtlety a test harness must not contain. */                    \
    const auto _a_ = (a);                                                                  \
    const auto _b_ = (b);                                                                  \
    if (!(_a_ == _b_)) {                                                                   \
      ::gctest::fail(std::string("CHECK_EQ failed: ") + #a + " == " + #b + " at " +        \
                     __FILE__ + ":" + std::to_string(__LINE__));                           \
    }                                                                                      \
  } while (false)

#define CHECK_CODE(expr, expected)                                                       \
  do {                                                                                   \
    auto&& _status_ = (expr);                                                        \
    if (_status_.code() != (expected)) {                                                 \
      ::gctest::fail(std::string("CHECK_CODE failed: ") + #expr + " produced " +         \
                     std::string(::genctl::to_string(_status_.code())) + " (" +          \
                     _status_.message() + ") expected " +                                \
                     std::string(::genctl::to_string(expected)) + " at " + __FILE__ +    \
                     ":" + std::to_string(__LINE__));                                    \
    }                                                                                    \
  } while (false)

#define CHECK_RESULT_CODE(expr, expected)                                                \
  do {                                                                                   \
    auto&& _result_ = (expr);                                                        \
    if (_result_.code() != (expected)) {                                                 \
      ::gctest::fail(std::string("CHECK_RESULT_CODE failed: ") + #expr + " produced " +  \
                     std::string(::genctl::to_string(_result_.code())) + " (" +          \
                     _result_.status().message() + ") expected " +                       \
                     std::string(::genctl::to_string(expected)) + " at " + __FILE__ +    \
                     ":" + std::to_string(__LINE__));                                    \
    }                                                                                    \
  } while (false)

inline std::mt19937 make_rng(std::uint64_t salt) {
  return std::mt19937(static_cast<std::uint32_t>(test_seed() + salt));
}

// Implemented by test_main.cpp so that the runner can also act as a child process
// for the multiprocess and crash tests.
int run_all(int argc, char** argv);
int run_scenario(int argc, char** argv);

}  // namespace gctest
