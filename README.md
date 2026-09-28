# Generator Control

**Generator Control 1.0.0** is a vendor-neutral control and lifecycle runtime for standby
generation. It answers one question, for one generator at a time:

> Is this generator eligible and authorized to start, run, synchronize, transfer, test or stop
> now, under which readiness/resource/safety evidence, and how do we prove the requested state
> transition without conflating command acknowledgement with electrical effect?

Apache License 2.0, Copyright 2026 Summon Software Labs. No telemetry transmission.

---

## Systems boundary

Generator Control owns the **generator-domain object**: its identity and generations, its
lifecycle and operating state, its readiness and safety interlocks, its fuel and consumable
evidence, its synchronization and transfer *eligibility*, its start/stop/test authority, the
command/acknowledgement/effect separation, fencing, durable persistence and audit.

It deliberately does **not** own, and does not implement:

| Not owned | Owned by |
| --- | --- |
| Power control plane policy and grants | Power Control Plane |
| Power topology (buses, feeders, switching arrangements) | Power Topology |
| Feed authority / source selection | Feed Authority |
| UPS and PDU control | UPS Control, PDU Control |
| Load shedding decisions | Load Shedding |
| Energy accounting and ledgers | Energy Ledger |
| Whole-facility black start orchestration | Black Start Manager |
| Synchronization algorithms, protection relay logic, governor/AVR control, utility anti-islanding | The actual devices and their controllers |

Those functions appear here only as **externally supplied evidence and permission**. Generator
Control holds *references* to breakers, buses and transfer paths; it never asserts topology and
never closes a breaker on its own authority. Where a real synchronizer, protection relay or
governor is required, the runtime models the *external permissive* it must produce and refuses to
act without it. It does not pretend to implement the device.

---

## Architecture

```
include/genctl/            public API (installed, Apache-2.0)
  result.hpp               error codes, categories and the documented validation precedence
  ids.hpp                  strongly typed identities, epochs, incarnations, revisions, attempts
  time.hpp                 injected clocks, freshness arithmetic, deterministic formatting
  units.hpp                checked integer arithmetic, unit-tagged quantities, TypedValue
  digest.hpp               CRC-32C, FNV-1a-64, SHA-256 (implemented locally, vector tested)
  canonical.hpp            deterministic canonical encoding and strict decoding
  evidence.hpp             evidence state, provenance, lifetime and usability assessment
  state.hpp                lifecycle, operating state, mode, the transition rule table
  readiness.hpp            readiness checks and safety interlocks
  resource.hpp             fuel and consumable evidence and sufficiency
  sync.hpp                 synchronization preconditions and eligibility
  transfer.hpp             breaker/bus/path references and transfer eligibility
  authority.hpp            authority classes, emergency grants, audit entries
  adapter.hpp              the narrow adapter boundary and the synthetic generating set
  attempt.hpp              operation requests, attempts, the bounded replay journal
  model.hpp                the authoritative persistable state image
  persistence.hpp          the versioned, integrity-checked durable store
  engine.hpp               the control engine
  platform.hpp             narrow native OS abstractions (locks, durable writes, processes)
  path_safety.hpp          path validation under the documented trust model
src/                       implementation and the internal canonical codecs
tools/main.cpp             the generatorctl administration and inspection CLI
examples/                  six runnable examples
benchmarks/bench_main.cpp  completed-operation benchmark
tests/                     the proof suite (107 tests)
```

### Layering

```
         CLI / examples / downstream consumers
                        |
                  GeneratorControlEngine        <- the only mutation authority
             /            |             \
      evaluation      durable store     adapter boundary
   (readiness,      (versioned, CRC +    (issue / observe;
    resource, sync,  SHA-256, staging,    synthetic or vendor)
    transfer, gates) atomic publish)
```

The engine is single-threaded internally for mutation. There is no background thread, no timer, no
watchdog and no autonomous external control anywhere in the runtime. Nothing is actuated that a
caller did not explicitly request through the adapter boundary.

---

## The central separation

```
planned      what the caller asked for, and the authority/version it was planned against
authorized   the evaluation outcome that permitted it
issued       a command object was handed to an adapter
acknowledged the adapter accepted the command
observed     the outside world reported a physical condition
verified     the observed condition satisfies the intended effect
```

These are separate fields with separate state machines. **No code path advances one from another.**
In particular:

* no transition rule can set the operating state to `synchronized` from a command; the transition
  table is asserted at runtime and in tests to contain no such rule, and the only writer of that
  state is the observation path;
* a command acknowledgement has `EvidenceSource::CommandAcknowledgement`, which
  `is_effect_authoritative` rejects as proof of any physical effect;
* a transfer command produces a *request*; the transfer effect exists only as an externally
  observed breaker position.

---

## State semantics

**Lifecycle** (administrative envelope): `unknown`, `commissioned`, `maintenance`, `isolated`,
`retired`.

**Operating state** (physical condition): `unknown`, `stopped`, `starting`, `warm-up`,
`ready-unsynchronized`, `synchronized`, `cooldown`, `degraded`, `faulted`.

**Operating mode**: `normal`, `test`, `emergency`, `service`.

**Synchronization state**: `unknown`, `not-synchronized`, `synchronizing`, `synchronized`,
`failed`.

The transition rule table in `include/genctl/state.hpp` is the single source of truth for every
operation: the states it may start from, the provisional state a command may set, the mode it
requires and establishes, the authority class it needs, and which evidence classes must be
satisfied. Fail-closed behaviour follows from the table:

* `retired` and `unknown` lifecycles permit nothing;
* `maintenance` permits only `exit-maintenance`, `isolate` and `retire`;
* `isolated` permits only `return-to-service` and `retire`;
* `faulted` permits only `fault-reset` — not even an emergency start;
* stop-type operations are accepted under any held authority class and are never gated on
  interlocks, because refusing to stop is not the fail-safe direction.

**Unknown, unavailable, unsupported, stale, denied and zero are distinct states.** A missing fuel
reading is never converted to zero fuel; an unknown consumption rate never becomes an unbounded
runtime; a present zero voltage is present evidence and is compared against its window like any
other value.

---

## Authority, generations and fencing

Six semantically distinct identifiers are distinct C++ types, so a control epoch cannot be
assigned to a state revision and an incarnation cannot be assigned to an attempt:

| Type | Meaning |
| --- | --- |
| `GeneratorId` | stable identity of the generating set |
| `HardwareGeneration` | vendor/hardware revision |
| `BindingEpoch` | epoch under which this controller is bound to the device |
| `ControlEpoch` | epoch granted by the external power control plane |
| `IncarnationId` | one writer acquisition of the store |
| `StateRevision` | mutable per-generator state revision |
| `AttemptId`, `CommandId`, `CommitSeq`, `ObservationSeq`, `JournalSeq` | monotonic sequences |

Every state-dependent mutation states the controller generation, generator generation and state
revision it was planned against, and refuses stale authority rather than merging it. Each counter
saturates rather than wrapping.

**Authority classes are disjoint.** A test grant never authorizes a production operation and a
normal grant never authorizes a test run; production operations are additionally refused while the
generator is in test mode, and test/transfer operations are refused while it is in normal mode.

**Emergency authority is explicit, bounded, attributable and auditable.** A grant must carry the
emergency class, an explicit-grant flag, a reason of at least eight characters, a non-empty
validity window no longer than four hours and a named granter. It is never inferred from an alarm
or a fault. It relaxes exactly one thing: checks classified as **Advisory** (for example a
maintenance window or an economic fuel reserve). Safety interlocks and protection permissives
remain mandatory in every mode, and every waiver is written to the authority audit trail.

### Deterministic validation precedence

When one request violates several rules, the earliest stage wins, so the same invalid request
always produces the same primary error:

```
format -> identity -> idempotency replay -> idempotency conflict -> fencing
  -> lifecycle -> transition -> mode -> authority -> interlock -> readiness
  -> resource -> synchronization -> transfer -> reservation -> actuation
  -> observation -> verification -> persistence
```

Idempotent replay sits deliberately **before** fencing: a retry of an already accepted attempt
returns the prior accepted result even though the revision it was planned against has moved on.
The request fingerprint covers the semantic identity of the request (identity, operation, generator
generation, authority class, epoch, key) and deliberately excludes the revision.

### Idempotency and its retention semantics

* Full attempt records are retained up to `JournalPolicy::max_attempts` (default 256).
* Compact replay entries — key, fingerprint, operation, command id and terminal states — are
  retained up to `JournalPolicy::idempotency_window` (default 1024).
* A retry whose key is inside the window with a matching fingerprint returns the prior accepted
  result and performs no new actuation.
* A retry with a matching key and a **different** fingerprint is refused with
  `IdempotencyKeyConflict`; a conflicting retry is never treated as a new actuation.
* A retry whose key is still in the window but whose full record has been retired is refused with
  `IdempotencyWindowExpired`.
* Keys evicted from the window are counted. New actuating work is then refused with
  `IdempotencyWindowExpired` until an operator explicitly acknowledges the loss of replay
  detection (`generatorctl key-window --accept`), because a retry of an evicted key is otherwise
  indistinguishable from a fresh request. This is a deliberate fail-closed default; size the window
  for the retry horizon the installation needs.

An actuating command whose effect has not been observed or contradicted **blocks** the next
actuation for that generator (`AttemptUnresolved`). The runtime never re-issues, never retries and
never abandons autonomously; abandonment requires an observation that already proved the commanded
effect did not occur.

---

## Persistence and recovery

### Format

Store directory layout; every name is a fixed constant or derived from a monotonic integer:

```
genctl.lock                     OS writer lock (LockFileEx / flock)
genctl.lease                    writer lease: epoch, incarnation, holder token
genctl.fence                    monotonic fence: highest committed sequence
genctl.head                     committed head marker  == the commit point
generation-<20 digits>.gcs      immutable published state images
*.gcs.staging / *.head.tmp      residue from an interrupted publication
```

Record frame (format 1), little-endian, 72-byte header plus payload plus trailer:

| Offset | Field |
| --- | --- |
| 0 | magic `"GENCTLST"` |
| 8 | format version (u32) |
| 12 | file kind (u32): 1 state image, 2 head, 3 fence, 4 lease |
| 16 | commit sequence (u64) |
| 24 | payload length (u64) |
| 32 | payload SHA-256 (32 bytes) |
| 64 | header CRC-32C over bytes [0,64) |
| 68 | reserved, must be zero |
| 72 | payload |
| 72+len | payload CRC-32C |

A file is accepted only when its size is exactly `72 + payload_length + 4`, the magic and format
match, the payload length is within the configured bound, the header CRC, the payload CRC and the
payload SHA-256 all verify, and the payload decodes canonically with **no trailing bytes**. Records
are decoded with explicit length bounds before anything is allocated, so an absurd declared length
fails on the bound rather than on the allocator.

### Publication protocol and the commit point

```
1. reserve      revision/sequence under the mutation lock
2. stage        write generation-<seq>.gcs.staging
3. flush        FlushFileBuffers / fsync
4. read back    re-open the staging file; verify size, CRCs and SHA-256
5. publish      atomic replace staging -> generation-<seq>.gcs
6. COMMIT POINT atomic replace genctl.head.tmp -> genctl.head   (MOVEFILE_WRITE_THROUGH)
7. fence        advance genctl.fence to the committed sequence
8. retire       delete generations outside the retention window and any residue
```

Recovery adopts **exactly one whole verified state image**: the one named by the head marker.
Partial generations are never stitched together. When the head marker is missing or damaged the
store refuses to open unless an explicit recovery scan is requested, which is audited. A head whose
commit sequence is below the monotonic fence is refused as `StoreRollbackDetected` unless an
operator explicitly accepts the rollback.

### Recovery never makes an observation fresh

Opening a store creates a new controller incarnation. Every record whose lifetime is
`VolatileObservation` and whose state is `Present` is demoted to `Stale` with the reason that a
new incarnation must re-observe it. Synchronization and breaker position are cleared outright, and
a stored `synchronized` operating state is reset to `unknown` with a history entry, because a live
electrical condition cannot be carried across a restart. Evidence with an explicit validity window
(`AttestedWithValidity`, used for external grants) survives until that window expires.

---

## Concurrency model and lock order

* `mutation_mutex_` is the only lock that serialises state changes. It is held across the durable
  publication of a mutation, which is a deliberate invariant: two threads must never publish
  generations out of order. Inside that critical section there is no callback, no adapter call, no
  user code, no clock read, no sleep and no nested lock other than the leaf lock.
* `snapshot_mutex_` is a leaf. It is held only to publish or copy a
  `std::shared_ptr<const EngineView>`, never across a file operation, an adapter call or a clock
  read. The only lock order used is `mutation_mutex_` → `snapshot_mutex_`.
* The adapter is called with **no** engine lock held. The engine records durable write-ahead intent
  before the call and commits the acknowledgement after it.
* Readers copy an immutable snapshot pointer under the leaf lock and then work on the copy, so a
  reader never blocks a writer and a writer never blocks a reader.

Cross-process writer authority uses a real OS primitive (`LockFileEx` with
`LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY` on Windows). The lock is released by the
operating system when the owning process dies, so a killed writer never leaves the store locked.

---

## CLI

`generatorctl` is a thin shell over the library: it parses arguments, opens a store, drives the
engine and prints deterministic text. It contains no control policy of its own.

```
Store and identity options
  --store <dir>            store directory (absolute)
  --generator <id>         generator identity
  --epoch <n>              control epoch this invocation holds
  --now / --at <millis>    evaluation instant / evidence instant
  --revision <n|auto>      state revision the request is planned against (default auto)
  --key <idempotency>      idempotency key for an actuating request
  --authority <class>      normal | test | service | emergency
  --authority-by/--reason/--authority-until/--authority-explicit
  --adapter-journal <f>    laboratory device journal (default <store>/lab-device/...)
  --seed-synthetic-evidence   read the synthetic installation and record what it reports
  --crash-at <point>       deterministic crash injection (validation tooling)

Administration   init, register, check, resource, sync-precondition, transfer-precondition,
                 switch-authority, transfer-path, authority, mode, lifecycle, key-window
Inspection       inspect, readiness, resource-report, synchronization, transfer, evaluate,
                 attempts, history, store-audit
Actuation        act (--operation <name>), observe, verify, abandon, revalidate
Diagnostics      self-test, version
```

Exit codes: 0 ok, 1 internal, 2 invalid request, 3 not permitted, 4 evidence unavailable,
5 stale authority, 6 adapter failure, 7 storage failure, 8 idempotency conflict.

```
generatorctl init --store C:\sites\alpha\genctl
generatorctl register --store C:\sites\alpha\genctl --generator gen-1 --key register-gen-1
generatorctl act --store C:\sites\alpha\genctl --generator gen-1 --operation start \
              --authority normal --key start-00000001 --seed-synthetic-evidence
generatorctl inspect --store C:\sites\alpha\genctl --generator gen-1
generatorctl store-audit --store C:\sites\alpha\genctl
```

Because each CLI invocation is a separate writer incarnation, dynamic evidence recorded by one
invocation is demoted by the next. That is the documented semantics, not a limitation to work
around: `--seed-synthetic-evidence` reads the installation and actuates inside a single
incarnation, exactly as a controller does, and C++ callers hold one engine open for their whole
session.

---

## Examples

| Example | Demonstrates |
| --- | --- |
| `example_start_lifecycle` | a complete synthetic start, provisional state, observation-verified effect |
| `example_stale_resource` | stale fuel evidence refusing a start, and a fresh reading restoring it |
| `example_test_mode_separation` | test mode and test authority never becoming production authority |
| `example_ack_without_sync` | an acknowledged synchronizing command that never becomes synchronized |
| `example_emergency_authority` | explicit bounded emergency grants; interlocks still mandatory |
| `example_crash_reopen` | a killed process, one device actuation, and a retry that replays |

---

## Package consumption

```cmake
find_package(GeneratorControl 1.0.0 REQUIRED)
target_link_libraries(your_target PRIVATE GeneratorControl::generatorcontrol)
```

Install with:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix <prefix>
```

An out-of-tree consumer using `find_package` against a clean install is built and executed as part
of release validation; see `docs/VALIDATION.md`.

---

## Build

C++20, CMake 3.25 or newer, no third-party dependency. The core uses only the C++ standard library
plus narrow native abstractions for file locking, durable writes, atomic replacement and process
spawning.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Options: `GC_BUILD_TESTS`, `GC_BUILD_EXAMPLES`, `GC_BUILD_BENCHMARKS`, `GC_BUILD_CLI`,
`GC_ENABLE_ASAN`.

---

## Validation performed

Full detail, including toolchain blockers, is in `docs/VALIDATION.md`. Summary of what was run on
this host:

* **Release** and **Debug** builds, both with `/W4 /WX /permissive-` on every first-party target
  and no globally suppressed warnings.
* **107 automated tests**, organised by proof obligation: identity and error precedence, checked
  units and time, evidence and freshness, readiness and interlocks, resources, synchronization,
  transfer, authority and fencing, the engine lifecycle, idempotency, persistence and recovery,
  adversarial store parsing, path safety, multiprocess authority and crash semantics, concurrency,
  and seeded randomized property tests.
* **Real multiprocess tests**: writer exclusion, release on abrupt process death, monotonic
  incarnation handoff and stale-writer fencing, using `CreateProcessW`, `TerminateProcess` and a
  file-based handshake. No pipe, no shell wrapper and no interactive input.
* **Crash injection at every durable stage** — before reserve, after reserve, after staging, after
  read-back, after publish, after head commit, after publish before actuation, after actuation
  before acknowledgement commit, after acknowledgement commit. Each run reopens the store, adopts
  one whole verified generation, and checks the independent device journal to prove the actuation
  count.
* **Adversarial store parsing**: truncation, single-byte tampering, header tampering, unsupported
  format versions, oversize files, missing generations, unknown record kinds, non-zero reserved
  fields and every declared-length boundary.
* **Path safety**: traversal, Windows device names, alternate data streams, trailing dot/space,
  control characters, overlong input, malformed UTF-8 (overlong forms, surrogates, out-of-range)
  and a real directory junction created with the operating system's own tool.
* **Seeded randomized state-machine exploration** with a fixed, printed seed, checked against an
  independent reference journal model after every step.

---

## Benchmark methodology and results

Every measurement times a **completed** operation: the timer starts before the work and stops after
the operation has actually finished, including validation, canonical encoding, staged writes,
required flushes, read-back verification, atomic publish and the head/fence commit. Nothing is
timed at submission and nothing is deferred, because the runtime has no background threads.
Scenario setup between iterations (resolving an outstanding effect by observation, returning the
set to a stopped condition, refreshing consumable readings) is deliberately outside the measured
region and is stated in the output.

Measured on this host (Windows, MSVC 19.44, Release, Ninja), one generator with the full 20-check
readiness binding:

| Operation | Label | n | mean | best | worst | ops/s |
| --- | --- | --- | --- | --- | --- | --- |
| `operation_planning` (all evaluation stages) | REAL | 20000 | 41.09 us | 27.50 us | 471.70 us | 24339.6 |
| `readiness_evaluation` (20 checks + binding digest) | REAL | 50000 | 10.26 us | 7.20 us | 472.30 us | 97482.6 |
| `durable_mutation` (full publication protocol) | REAL | 300 | 14008.91 us | 9613.40 us | 81915.70 us | 71.4 |
| `actuation_attempt` (write-ahead + issue + ack commit) | SYNTHETIC | 200 | 106180.60 us | 29157.20 us | 1103864.10 us | 9.4 |
| `idempotent_replay` (no actuation, no publication) | REAL | 50000 | 14.10 us | 11.00 us | 659.80 us | 70925.1 |
| `resource_assessment` | REAL | 50000 | 7.40 us | 5.90 us | 358.20 us | 135128.9 |
| `store_audit` (re-read and verify every generation) | REAL | 200 | 30415.79 us | 25783.60 us | 38225.20 us | 32.9 |
| `store_reopen_recovery` (close + reopen + adopt) | REAL | 30 | 9828.03 us | 5783.10 us | 67869.20 us | 101.7 |

Reading the numbers honestly:

* the durable paths are dominated by `FlushFileBuffers` and the atomic rename, which is the point:
  a completed operation here means the head marker has moved, not that bytes were handed to the
  operating system;
* `actuation_attempt` performs **two** complete publications per operation (write-ahead intent
  before the adapter call, acknowledgement commit after it) plus the adapter round trip, which is
  why it is slower per operation than a single durable mutation;
* the run above is a single series on an otherwise idle host. The same workload run while another
  build was in progress showed the durable numbers varying by roughly a factor of two (for example
  `durable_mutation` at 56.5 ops/s and `store_audit` at 12.8 ops/s), so treat the durable figures
  as host-load sensitive rather than as a fixed property of the implementation;
* no before/after pair is published, because no implementation change is being compared.

Benchmark state is verified before reporting (a full store audit reports the head sequence, the
fence and zero anomalies) and the benchmark store is removed afterwards; the run prints the cleanup
result.

---

## Genuine limitations

* **No hardware was validated.** Every generator-domain measurement in this release is produced by
  the deterministic synthetic adapter and is labelled SYNTHETIC. The control path, the authority
  model, the durability and the adapter boundary are real; the electrical behaviour is simulated.
  No claim is made about any physical generating set.
* No synchronization algorithm, protection relay logic, governor or AVR control, or utility
  anti-islanding behaviour is implemented. Those are modelled as required external evidence and
  permission, and the runtime refuses to act without them.
* POSIX branches exist behind the same narrow platform interface but were **not built or executed**
  in this environment; only the Windows implementations are verified.
* Publication writes a whole state image per mutation, so the cost of a durable mutation grows with
  the size of the retained state (generators, attempt records and history). The defaults are
  deliberately bounded and the retention windows are configurable.
* Temperature, pressure, coolant and battery readings are carried as evidence and compared against
  installation configuration; the runtime does not model thermal or hydraulic dynamics.
* Failure injection for the synthetic device (acknowledge-without-effect, stuck cranking, busy,
  contradictory observation, unavailable observation) is deterministic and manually selected; it is
  not a fuzzing campaign against a real device.
* The runtime is single-writer per store. Concurrent writers are excluded, not merged; there is no
  replication or multi-site coordination.

---

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
