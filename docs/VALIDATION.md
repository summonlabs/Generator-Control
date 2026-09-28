# Validation record

This document records what was actually built, executed and observed for
Generator Control 1.0.0 on the validation host. It describes product behaviour and
evidence, not process. Where something could not be established, it is stated as a
limitation rather than claimed.

## Host and toolchain

| | |
| --- | --- |
| Operating system | Windows (x64) |
| Compiler | MSVC 19.44.35207 (Visual Studio 2022 Build Tools) |
| Generator | Ninja |
| CMake | 4.3.2 |
| Language standard | C++20 (`/std:c++20`, `/permissive-`) |
| Third-party dependencies | none |

## Build matrix

| Configuration | Command summary | Result |
| --- | --- | --- |
| Release | `-DCMAKE_BUILD_TYPE=Release`, all targets | builds clean |
| Debug | `-DCMAKE_BUILD_TYPE=Debug`, all targets | builds clean |
| Strict warnings | `/W4 /WX /permissive- /utf-8` on every first-party target | no warning, no suppression |
| AddressSanitizer | `-DGC_ENABLE_ASAN=ON`, `/fsanitize=address`, RelWithDebInfo | builds clean, suite passes |

Warnings are enabled per first-party target by a single helper function. No warning is
disabled globally; the only `#pragma warning` in the tree is a narrow push/pop around the
system `<windows.h>`/`<winioctl.h>` include in one test translation unit that creates a real
directory junction to prove the reparse-point refusal.

### AddressSanitizer

The installed toolchain supports `/fsanitize=address`; the MSVC ASan runtime
(`clang_rt.asan_dynamic-x86_64.dll`) is present in the toolchain `bin` directory and is placed on
`PATH` when the sanitized suite runs. **The complete 107-test suite passes under
AddressSanitizer with no error report.** There is no blocked sanitizer result to report.

## Test suite

`generatorcontrol_tests` — 107 tests, organised by proof obligation rather than by an arbitrary
count. Release, Debug and AddressSanitizer runs each report `passed=107 failed=0`.

| File | Proof obligations |
| --- | --- |
| `test_foundation.cpp` | strongly typed identities, identifier and key validation, error categories and the documented validation precedence, exit codes, checked time and unit arithmetic, runtime estimation refusing to invent a value, digest vectors, canonical encoding rules and strictness, deterministic bytes for equivalent state |
| `test_evidence.cpp` | present-vs-missing distinction, every non-present evidence state refused with its own code, stale and future-dated evidence, command acknowledgement never proving an effect, external authority requirements, synthetic evidence policy, explicit validity windows |
| `test_state_machine.cpp` | no rule can assert synchronization from a command, every actuating rule requires interlocks and observation, retired/unknown lifecycles permit nothing, maintenance/isolated/faulted fail closed except listed recovery, the full start/stop/synchronize transition table, unknown is never stopped, authority class disjointness, test mode never drives production, emergency mode is explicit, stop accepts any authority |
| `test_readiness.cpp` | missing safety evidence fails closed, complete binding satisfies, a tripped interlock is an engaged condition, stale interlock evidence, emergency waives advisory checks only, waiver classification, optional findings, command acknowledgement cannot satisfy a check, deterministic primary error, binding digest determinism |
| `test_resources_sync_transfer.cpp` | fuel sufficiency, missing evidence is not zero, unknown consumption never becomes an adequate runtime, unit mismatch refused, stale resource evidence, insufficient runtime reported with both numbers, every named synchronization precondition, indeterminate versus ineligible, acknowledgement cannot satisfy a precondition, stale synchronization evidence, transfer requires the external switch authority, incomplete topology references, strict switch reference validation |
| `test_engine_lifecycle.cpp` | complete synthetic start lifecycle, acknowledgement without synchronization, acknowledge-without-effect and observation-only abandonment, test-mode separation, lifecycle fail-closed through the engine, emergency authority and mandatory interlocks, idempotent replay before staleness, fencing at every generation, deterministic primary error, revalidation, concurrent single-device ordering, reader/writer coexistence |
| `test_persistence_recovery.cpp` | close/reopen round trip, identical commit digest across reopen, retention and residue retirement, rollback detection and explicit acceptance, recovery scan for a missing head, recovery never makes volatile evidence fresh, synchronized state not carried across a restart, process death at eight distinct durable stages |
| `test_adversarial_store.cpp` | truncated, tampered, header-tampered, version-bumped, oversized, missing and empty-store inputs; record frame boundary checks; trailing-byte rejection; structural validation of the state image; traversal/device-name/UTF-8/path rules; a real directory junction |
| `test_multiprocess.cpp` | second-writer refusal, abrupt death release, monotonic incarnation handoff, stale-writer fencing, control-epoch fencing and the epoch ceiling |
| `test_property.cpp` | seeded randomized state-machine exploration against an independent reference journal model, randomized canonical round trips, exhaustive single-byte mutation detection, randomized idempotency keys never actuating twice |

### Reproducibility

Randomized and property tests use a fixed seed that the runner prints
(`fixed seed = 0x5EED1234`), so any failure is reproducible from the seed alone. There is no
timeout mechanism of any kind in the suite: a hang is a defect to diagnose, not a condition to
paper over.

### Real multiprocess evidence

Cross-process authority is proven with real, independent processes launched through
`CreateProcessW`, coordinated through the filesystem (never through a pipe or a shell wrapper),
and terminated abruptly with `TerminateProcess` — which cannot raise Windows Error Reporting and
leaves no modal window. The scenarios are:

* a child holds the store and exits cleanly while the parent is refused with `StoreLocked`;
* a child holds the store and is terminated abruptly; the operating system releases the lock and
  the parent then opens the store, finds a valid committed state and observes a strictly greater
  incarnation;
* three sequential child handovers; the parent observes a strictly greater incarnation again;
* a requested control epoch below the stored epoch is refused, an equal or higher one is accepted,
  and an epoch above the accepted ceiling is refused rather than permanently fencing every writer.

### Crash injection evidence

For each of `after-reserve-before-staging`, `after-staging-before-readback`,
`after-readback-before-publish`, `after-publish-before-head-commit`,
`after-head-commit-before-fence`, `after-publish-before-actuation`,
`after-actuation-before-ack-commit` and `after-ack-commit`, a child process is started with the
crash point configured, dies with a non-zero exit code, and the parent then reopens the store and
asserts that:

* exactly one whole verified generation is adopted and a full store audit reports zero anomalies;
* the independent device journal shows the expected number of accepted start commands — zero for
  every stage strictly before actuation, exactly one from `after-actuation-before-ack-commit`
  onwards;
* where the attempt was committed, a retry with the same idempotency key replays it
  (`replayed == true`) and the device actuation count does not change;
* where the command never reached the device, the unresolved attempt blocks a new key with
  `AttemptUnresolved`, and an observation followed by an explicit abandon is the only way out.

## Sanitizer substitute statement

Not applicable: AddressSanitizer is available and was run on the full suite. No substitute was
required and none is claimed.

## Install, export and downstream consumption

1. `cmake --install build --prefix <clean prefix>` installs the static library, every public
   header, `GeneratorControlConfig.cmake`, `GeneratorControlConfigVersion.cmake`,
   `GeneratorControlTargets.cmake`, the LICENSE and the NOTICE.
2. `tests/downstream` is an independent CMake project that is not part of the main build. It is
   configured with `-DCMAKE_PREFIX_PATH=<prefix>` and consumes only
   `GeneratorControl::generatorcontrol`.
3. The consumer registers a generator, binds readiness and consumable evidence, executes a
   synthetic start, observes the effect to `Verified`, retries the same key and asserts that the
   retry replayed rather than actuated, then audits the store. It prints:

```
downstream consumer built against Generator Control 1.0.0 (store format 1)
downstream: start acknowledged, effect verified by observation, retry replayed, device actuations = 1, store head=20 fence=20 generations=4/4 anomalies=0
downstream: OK
```

## CLI and examples

Every CLI command and every example was executed on the validation host. The examples cover the
core lifecycle and the required failure and stale-authority paths:

| Executable | Observed result |
| --- | --- |
| `example_start_lifecycle` | planning permitted, command acknowledged, effect not observed, then verified as `ready-unsynchronized` by observation |
| `example_stale_resource` | stale evidence refused with `ResourceEvidenceStale` and `InterlockEvidenceUnavailable`, zero commands issued, permission restored only after a full re-read |
| `example_test_mode_separation` | production start under test authority refused, test mode entered, production start in test mode refused with `TestOperationNotPermitted`, transfer under test authority refused with `TestOperationNotPermitted` |
| `example_ack_without_sync` | full synchronization eligibility, acknowledged synchronizing command reported as `synchronizing`, contradiction observed, synchronized established only after the external breaker closed and was observed |
| `example_emergency_authority` | implicit and over-long emergency grants refused, bounded explicit grant accepted, interlocks still refusing the command with `InterlockEngaged` |
| `example_crash_reopen` | child killed with exit code 78, one accepted start in the device journal, one unresolved attempt after reopen, retry replayed, actuation count still one |

## Benchmark

Methodology and the measured table are in the README. The run verifies state before reporting (a
full store audit printing the head sequence, the fence and the anomaly count) and removes the
benchmark store afterwards, printing the cleanup result and a post-cleanup existence check.

Labels used: **REAL** for measurements that exercise real code and real durability on this host,
**SYNTHETIC** for the one measurement whose device is the deterministic simulator. No
**UNSUPPORTED** entries were produced.

## Fresh-clone closure

The final committed state is validated from a fresh clone of the repository, not from the working
tree that produced it:

1. `git clone <repository> <empty directory>` at the closure commit;
2. configure with Ninja, Release, examples and CLI enabled;
3. build with `/W4 /WX /permissive-` — no warning, no error;
4. run the full suite — `passed=107 failed=0`;
5. install to a clean prefix;
6. configure and build the independent downstream consumer against that prefix and run it —
   `downstream: OK`.

The clone and every prefix, build directory and scratch store it created are removed afterwards;
only the repository's own committed files remain.

## Not validated

* **No physical generating set was used.** Every electrical observation in this release comes from
  the synthetic adapter and is labelled SYNTHETIC. No hardware behaviour is claimed.
* **POSIX branches are unverified.** `platform.hpp` declares a narrow interface with a Windows
  implementation; a POSIX implementation is described in the header comments but does not exist in
  this release, and no POSIX build was attempted.
* **No anti-islanding, protection, governor or AVR behaviour is implemented or tested**, because
  implementing it would require the real device and its settings. Those functions appear only as
  required external permissive evidence.
* **Multi-writer coordination is not validated**, because the design is single-writer: concurrent
  writers are excluded by an operating-system lock, not merged.
