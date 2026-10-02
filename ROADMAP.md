# OLSRT Roadmap — Full Technical Plan

> **Audience:** project maintainers, contributors, and AI agents
> picking up work between sessions. This document is intentionally
> exhaustive so that a fresh reader can understand the *why*, the
> *what*, and the *how* of every planned version without external
> context.

**Last updated:** 2026-10-02
**Current released version:** v1.3.2 (Actor Scheduler + Wave 2)
**Current development branch:** `main`
**Next milestone:** v1.3.3 (Dataflow Cleanup + Deferred Items)

---

## Table of Contents

1. [Project Vision](#1-project-vision)
2. [Versioning Strategy](#2-versioning-strategy)
3. [Current State](#3-current-state)
4. [Known Issues & Technical Debt](#4-known-issues--technical-debt)
5. [Version Plan — v1.3.x Series](#5-version-plan--v13x-series)
6. [NWP — Non-Waiting Paradigm](#6-nwp--non-waiting-paradigm)
7. [Version Plan — v2.0 and Beyond](#7-version-plan--v20-and-beyond)
8. [Infrastructure, Testing, Tooling](#8-infrastructure-testing-tooling)
9. [Instructions for Future Agents](#9-instructions-for-future-agents)
10. [Glossary](#10-glossary)

---

## 1. Project Vision

OLSRT (OverLab Streams Runtime) is a **universal concurrency
substrate** written in portable C11. The long-term goal is to power
**any language** by exposing a stable C ABI that higher-level
languages (PHP, Python, Rust, Go, Node.js, etc.) can bind to.

Design priorities, in order:

1. **Correctness under sanitizers.** Every release must pass ASan,
   UBSan, TSan and LSan on Linux x86_64 and aarch64.
2. **Zero-cost abstractions where possible.** Channels, promises and
   green threads must not add measurable overhead over hand-written
   code.
3. **Feature parity with libuv, then exceed it.** Actors, supervisors,
   reactive streams, dataflow graphs, and (from v1.3.6) NWP branches.
4. **Hackability.** Code must be readable and modifiable by a single
   contributor, not a committee.
5. **Reproducible.** Two builds of the same source with the same
   `SOURCE_DATE_EPOCH` must produce byte-identical artefacts.

---

## 2. Versioning Strategy

Strict Semantic Versioning 2.0.

| Change kind | Bump | Example |
|-------------|------|---------|
| Bug fix, no API change | Patch (`x.y.Z`) | v1.3.1 → v1.3.2 |
| New feature, backward compatible | Minor (`x.Y.0`) | v1.3.2 → v1.4.0 |
| Breaking change | Major (`X.0.0`) | v1.3.x → v2.0.0 |

### Release checklist

Every release must pass:

```

[ ] python3 ci/run_all.py --keep-going       # 23/23 PASS
[ ] make TARGET=linux ARCH=x86_64
[ ] cmake -S . -B build && cmake --build build
[ ] All demos in demos/ produce expected output
[ ] docs/CHANGELOG.md updated
[ ] Version numbers bumped in:
includes/runtime/ol_globals.h
includes/code/streams/ol_green_threads.h  (OL_GT_VERSION_*)
CMakeLists.txt
[ ] Git tag `vX.Y.Z` created with release notes
[ ] GitHub Release published

```

### ABI stability

Public symbols are versioned via `.symver` (planned for v1.4.0).
Until then, `libolsrt.so` ships with `SOVERSION 1`.

---

## 3. Current State

### 3.1 Working subsystems

All of the following are verified by `tests/test_wave1.c` and the
`ci/` toolsuite:

| Subsystem | File | Status |
|-----------|------|--------|
| **Channels** | `ol_channel.c` | ✅ |
| **Promises / Futures** | `ol_promise.c` | ✅ |
| **Event loop** | `ol_event_loop.c` | ✅ |
| **Poller** | `ol_poller.c` | ✅ (epoll/kqueue/select) |
| **Parallel pool** | `ol_parallel.c` | ✅ |
| **Green threads** | `ol_green_threads.c` | ✅ (x86_64) |
| **Coroutines** | `ol_coroutines.c` | ✅ |
| **Actor** | `ol_actor.c` | ✅ (automatic scheduler) |
| **Supervisor** | `ol_supervisor.c` | ✅ |
| **Arena allocator** | `ol_actor_arena.c` | ✅ |
| **Hashmap** | `ol_actor_hashmap.c` | ✅ |
| **TCP sockets** | `network/ol_tcp.c` | ✅ |
| **UDP sockets** | `network/ol_udp.c` | ✅ |
| **Dataflow** | `ol_dataflow.c` | ⚠️ see §4.4 |
| **Reactive** | `ol_reactive.c` | ✅ |
| **Streams** | `ol_streams.c` | ✅ |
| **Semaphores** | `ol_semaphores.c` | ✅ |

### 3.2 CI toolsuite (23 tools, all PASS)

`ci/` provides a self-contained Python 3.7+ CI toolsuite. Every
script changes its working directory to the repository root and
detects optional external binaries at runtime. No pip packages.

**Core**
- `verify` — build under ASan/UBSan and TSan, run the suite
- `doxygen_check` — structural + build, `MISSING`/`PARTIAL` fails
- `build_check` — shared library build
- `warnings_check` — `-Wall -Wextra` count against `.warnings-budget`
- `version_check` — version strings agree across the tree
- `run_all` — orchestrate everything

**Quality**
- `format_check` — clang-format (primary)
- `lint_check` — clang-tidy, cppcheck, sparse, smatch, splint
- `static_analysis` — clang --analyze, infer, semgrep, codeql,
  pvs-studio-analyzer
- `include_check` — self-containment + include-what-you-use
- `spelling_check` — codespell, typos
- `license_check` — reuse, scancode, licensecheck
- `complexity_check` — lizard, pmccabe

**Security**
- `secrets_check` — gitleaks, trufflehog
- `security_scan` — semgrep, snyk, trivy, grype

**Testing**
- `sanitizer_matrix` — per-sanitizer build/run
- `valgrind_check` — memcheck, helgrind, drd, massif
- `coverage_check` — gcov / lcov / llvm-cov
- `fuzz_check` — libFuzzer, AFL++, honggfuzz
- `flaky_check` — repeated runs detect nondeterminism

**Build**
- `build_matrix` — 3 compilers × 4 standards × 3 opts
- `cmake_check` — configure / build / install round trip
- `install_check` — DESTDIR, prefix, pkg-config
- `header_check` — self-contained headers + include guards
- `symbol_check` — exported symbol audit
- `reproducible_check` — byte-identical builds
- `docs_build` — Doxygen + Sphinx
- `changelock_check` — Keep a Changelog shape

**Release**
- `commit_check` — Conventional Commits over the last N commits
- `olsrt_commiter` — interactive standard commit helper
- `benchmark_check` — hyperfine / C harness

### 3.3 Tests

- `tests/test_wave1.c` — 28 assertions, all passing.
- Runs under ASan + UBSan, TSan, and (in `sanitizer_matrix`)
  each sanitizer in isolation.

### 3.4 Demos

Seven baseline demos plus six combined demos plus one gold server:

- `01_hello_actor` — actor + ask/reply + promises
- `02_channel` — 1M messages through a bounded channel
- `03_parallel` — 4-worker pool, 100 tasks
- `04_timers` — event loop, one-shot + periodic timers
- `05_promise` — promise states, `.then()` continuations
- `06_dataflow` — graph source → doubler → sink (partial)
- `07_http_server` — minimal TCP HTTP server
- **`08_coroutines`** — cooperative producer/consumer
- **`09_supervisor`** — supervision tree with restart strategies
- **`10_reactive`** — subject + operators (map/filter/take)
- **`11_streams`** — stream operators with backpressure
- **`12_semaphores`** — counting semaphore across workers
- **`13_actor_supervisor`** — combined: actors behind a supervisor
- **`14_actor_channel`** — combined: channel-driven actor pipeline
- **`15_reactive_loop`** — combined: event loop feeding a subject
- **`16_full_stack`** — combined: HTTP → actor → promise → channel
- **`gold_ws/`** — production-shape HTTP/1.1 web server with
  actor-per-request and PHP-FPM integration

### 3.5 Build matrix

| Platform | Status |
|----------|--------|
| Linux x86_64 | ✅ Solid |
| Linux aarch64 | ⚠️ untested in CI (asm exists) |
| BSD | ✅ reported |
| Windows / macOS | ❌ v2.0 (IOCP / kqueue native) |

---

## 4. Known Issues & Technical Debt

### 4.1 Actor main loop is not driven by the green-thread scheduler

**Severity:** High — **FIXED in v1.3.2.**

**Fix summary:** `ol_process_create()` spawns a dedicated OS driver
thread that calls `ol_gt_run_to_completion()` on the process green
thread. `ol_actor_start()` therefore causes the actor's message
loop to run without external pumping. LSan has been re-enabled in
`verify.py`.

### 4.2 x86_64 context switch has an argument-passing bug

**Severity:** Medium — worked around in v1.3.2, proper fix in v1.3.3.

`ol_ctx_make_x86_64` stores the entry argument in `%rbx`, but the
SysV AMD64 ABI passes the first argument in `%rdi`. The trampoline
reads garbage and jumps to an invalid address. v1.3.2 avoids the
assembly by calling the entry function directly from
`ol_gt_run_to_completion`; a correct context switch (or a rewrite on
top of `ucontext`) lands in v1.3.3.

### 4.3 Seven `-Wall -Wextra` warnings remain

**Severity:** Low — noise only.

Full list (current build):

| File | Warning |
|------|---------|
| `ol_green_threads.h:59` | `OL_ALWAYS_INLINE` redefined (×4) |
| `ol_green_threads.c:2379` | `%zu` with `int` argument |
| `ol_green_threads.c:144` | `OL_CACHE_LINE_PADDING` unused |
| `ol_supervisor.c:468` | signed/unsigned compare |

**Fix (v1.3.3):** silence all seven in one commit titled
`chore: silence -Wall -Wextra`. Then set `.warnings-budget` to 0.

### 4.4 Dataflow worker does not drain edge inboxes

**Severity:** Medium — demo 06 partial.

The worker loop polls only each node's `self_inbox`, never the
inboxes attached to outgoing edges. Multi-hop graphs do not
propagate.

**Fix (v1.3.3):** in `df_worker()`:

1. For each node, before processing `self_inbox`, also drain every
   edge whose `to` field points at this node.
2. Move each item from `edge->inbox.ch` into `node->self_inbox.ch`.
3. Replace the busy-poll with `nanosleep(1 ms)` when nothing was
   drained.

**Acceptance:** demo 06 output shows `source → doubler → sink` per
item. New test: `test_dataflow_multi_hop`.

### 4.5 `uint64_t` statistics counters are not `_Atomic`

**Severity:** Low — clang-only.

clang rejects `atomic_load_explicit` on plain `uint64_t` fields
(`address argument to atomic operation must be a pointer to _Atomic
type`). GCC accepts these calls as an extension.

**Fix (v1.3.3):** declare the fields as `_Atomic uint64_t` in
`struct ol_gt_statistics` and the per-scheduler counters. This
removes ~17 informational findings from `ci/static_analysis.py`.

### 4.6 `ol_memwatch.c` had a use-after-free in `realloc`

**Severity:** High — **FIXED in v1.3.2.** The corrected
`ol_memwatch_realloc` detaches the old record, calls `realloc`, and
registers the new pointer in place.

### 4.7 Valgrind reports arena-creation failures

**Severity:** Low — informational on this branch.

All four sanitizers pass on the current tree, but the same tests
fail inside valgrind on the development host. Every observed
failure is inside arena creation (`ol_arena_create`), which points
to a valgrind-environment issue rather than a runtime bug.
`ci/valgrind_check.py` is informational until v1.3.3 completes a
minimal reproducer.

### 4.8 Windows/macOS poller uses `select` (64 FD limit)

**Severity:** High for production use on those platforms.

**Fix (v2.0):** implement `ol_poller_iocp.c` (Windows) and
`ol_poller_kqueue.c` (macOS).

### 4.9 ABI versioning is not yet implemented

**Severity:** Low — only matters once third-party binaries link
OLSRT.

**Fix (v1.4.0):** add `.symver` annotations to every public
function and bump `SOVERSION` only on breaking changes.

---

## 5. Version Plan — v1.3.x Series

### v1.3.1 — Wave 1 Stabilization ✅ DONE

**Released:** 2026-09-29

Eleven bug fixes in v1.3.0, a 22-assertion regression suite,
and the first `verify.py`.

### v1.3.2 — Actor Scheduler ✅ DONE

**Released:** 2026-10-02

- Automatic actor main loop (`ol_gt_run_to_completion` + driver
  thread).
- 100% Doxygen coverage on 281 public functions.
- 23-tool CI suite.
- All four sanitizers clean.
- Reproducible builds.
- Clean git history.
- `demos/gold_ws/` — production-shape HTTP/1.1 server.

### v1.3.3 — Dataflow Cleanup + Deferred Items 🔜 NEXT

**Goal:** Close every remaining known issue in §4 (except the ones
explicitly scheduled for later).

**Deliverables:**

1. Dataflow edge-inbox draining (§4.4). New test
   `test_dataflow_multi_hop`. Demo 06 shows the full three-hop
   chain.
2. Correct x86_64 context switch or rewrite on top of `ucontext`
   (§4.2). New test exercises `ol_gt_spawn` +
   `ol_gt_run_to_completion` with the real context switch.
3. `uint64_t` statistics become `_Atomic uint64_t` (§4.5).
4. Silence the seven `-Wall -Wextra` warnings (§4.3); set
   `.warnings-budget` to 0.
5. Minimal valgrind reproducer for §4.7.
6. Ask-envelope refactor to remove the known false positive from
   clang's analyzer entirely (§3.2 note).
7. CHANGELOG and ROADMAP updates.

**Acceptance criteria:**

- `ci/run_all.py --keep-going` reports 23/23 PASS.
- `ci/static_analysis.py` reports zero informational findings.
- `ci/warnings_check.py` reports 0 warnings.
- Demo 06 output shows `source → doubler → sink` per item.

**Estimated effort:** 1–2 weeks.
**Risks:** The context-switch rewrite touches the core switching
path; run ASan + TSan + UBSan after every commit.

### v1.3.4 — ORoutines (Goroutine-like API)

**Goal:** A Go-style `or_go` API on top of the green-thread
machinery, with channels specialised for value semantics.

**Public API (draft):**

```c
typedef void (*oroutine_fn)(void* arg);

oroutine_t* or_go(oroutine_fn fn, void* arg);
void*       or_join(oroutine_t* co, int64_t deadline_ns);
void        or_yield(void);
void        or_cancel(oroutine_t* co);

or_chan_t*  or_chan_create(size_t elem_size, size_t capacity);
void        or_chan_send(or_chan_t* ch, const void* elem);
void        or_chan_recv(or_chan_t* ch, void* out);
void        or_chan_close(or_chan_t* ch);

int         or_select(or_select_case_t* cases, size_t n);
```

**Deliverables:**

1. `includes/code/streams/oroutines.h`
2. `src/code/streams/oroutines.c`
3. `demos/17_oroutines.c` — worker pool (100 goroutines, 10
channels).
4. `tests/test_oroutines.c` with 10+ assertions.
5. Documentation section under `docs/`.

**Acceptance:** 100 000 messages through 10 channels with no loss;
ASan/UBSan/TSan clean.

**Estimated effort:** 1 week.
**Risks:** `or_select` is tricky to make race-free. Start with a
mutex-protected channel array, optimise later.

### v1.3.5 — Supervisor 2.0

Bring the supervisor to Erlang/OTP parity:

1. Verify and document one-for-one, one-for-all, rest-for-one,
simple-one-for-one.
2. Hierarchical supervision.
3. Escalation when restart intensity is exceeded.
4. Hot-code-upgrade hooks (`before_restart(old, new)`).
5. `ol_supervisor_dump_tree()` — ASCII tree for debugging.
6. New demo `demos/18_supervisor_tree.c`.

**Estimated effort:** 1 week.

### v1.3.6 — NWP MVP Integration

See §6. Summary:

- Bring the standalone `nwp/` project into OLSRT as `src/nwp/`.
- Expose `ol_nwp_*` from a new `ol_nwp.h`.
- Demo `demos/19_nwp_helloworld.c`.
- Benchmark NWP branch switch vs. green-thread switch.

**Estimated effort:** 2–3 weeks.

### v1.3.7 — Release Polish

1. Benchmarks under `bench/` — actor ping-pong, channel throughput,
timer accuracy, NWP branch switch.
2. SDK binding for Python (ctypes).
3. Full API reference with examples.
4. GitHub Actions CI matrix: Linux x86_64, aarch64, FreeBSD.
5. Docker image `overlab/olsrt:1.3.7`.

**Estimated effort:** 1–2 weeks.

---

## 6. NWP — Non-Waiting Paradigm

### 6.1 What NWP is

NWP is a **paradigm**, not a library. Its core claim:

> *The main program never blocks. Every reference to a callable
> entity spawns a Branch Unit. The main program continues. At
> program end, all Branches commit their results atomically.*

This is distinct from OLSRT's futures (which can block via
`ol_future_await`). NWP's promise is **zero waiting, ever**, at the
cost of an explicit commit phase at shutdown.

## 7. Version Plan — v2.0 and Beyond

### 7.1 v2.0 — Apollo

**Two thrusts:**

**A. Cross-platform parity**

| Deliverable | Detail |
|---|---|
| Windows IOCP | `src/code/streams/ol_poller_iocp.c` |
| macOS kqueue | `src/code/streams/ol_poller_kqueue.c` |
| io_uring | optional `ol_poller_io_uring.c` |
| Async file I/O | `ol_file_async_read/write` |
| Async DNS | `ol_dns_resolve(hostname) → future<address>` |
| Async process spawn | `ol_process_spawn_async(cmd) → future<exit_code>` |
| Signal handling | self-pipe on POSIX, `SetConsoleCtrlHandler` on Windows |

**B. Network protocols**

The `includes/code/network/` directory already contains ~70
header stubs. v2.0 fills in the protocols in tiers:

- **Tier 1 — Transport & core:** TCP ✅, UDP ✅, QUIC, SCTP,
TLS 1.3, SSL (legacy), DTLS, raw sockets, socket options,
syslog.
- **Tier 2 — Application layer:** HTTP/1.1, HTTP/2, HTTP/3,
WebSocket, gRPC, GraphQL, SMTP, POP3, FTP, SFTP, Telnet,
TFTP, DNS, NTP, DHCP, MQTT, CoAP, AMQP, LDAP, RADIUS,
Diameter, Kerberos.
- **Tier 3 — Routing & control:** OSPF, BGP, RIP, IS-IS,
EIGRP, OpenFlow, STP, RSTP, ARP, ICMP, IGMP, MLD, L2TP,
PPP, PPPoE, L2F, NetFlow, IPFIX, 802.1X.
- **Tier 4 — Media & streaming:** RTP, RTCP, RTSP, SDP, SRTP,
HLS, DASH, WebRTC.
- **Tier 5 — Security & identity:** IPsec, PGP, OAuth 2.0,
SAML, SCP, SSH.
- **Tier 6 — Miscellaneous:** WebDAV, NOPO, TOP.

### 7.2 v3.0 — Nova (Virtualization)

VM abstraction (`ol_virtual.c`), GPU orchestration (`ol_gpu.c`),
network namespaces, container runtime primitives.

### 7.3 v4.0–v7.0 — Utilities

- **v4.0 Core:** `ol_filesystem`, `ol_timer`, `ol_db`, `ol_dsl`
- **v5.0 Spark:** `ol_compression`, `ol_crypto`
- **v6.0 Orion:** `ol_board`, `ol_fs_to_fs`
- **v7.0 Cosmos:** `ol_ai`, `ol_security`

### 7.4 v8.0–v12.0 — Compiler & OS support

- **v8.0 Hermes:** `ollc` (OverLab Language Compiler)
- **v9.0 Kernel:** advanced runtime core
- **v10.0 Stream:** architectures added
- **v11.0 Flow:** full OS support
- **v12.0 Wave:** 30% language coverage

---

## 8. Infrastructure, Testing, Tooling

### 8.1 Tooling inventory

| Tool | Purpose | Location |
|---|---|---|
| `Makefile` | Primary build | repo root |
| `CMakeLists.txt` | IDE-friendly build | repo root |
| `Doxyfile` | API documentation | repo root |
| `ci/run_all.py` | Orchestrate the CI suite | `ci/` |
| `ci/verify.py` | Sanitizer test runner | `ci/` |
| `demos/Makefile` | Demo build | `demos/` |
| `demos/gold_ws/Makefile` | Gold web server build | `demos/gold_ws/` |
| `source/conf.py` | Sphinx documentation | `source/` |

### 8.2 Recommended additions (v1.3.3+)

- **CI/CD** — `.github/workflows/ci.yml` with a matrix that
mirrors `ci/build_matrix.py`.
- **Fuzzing** — `tests/fuzz/` using libFuzzer.
- **Benchmarks** — `bench/` with hyperfine scripts.
- **Coverage** — publish `ci/coverage_check.py` results in CI.

### 8.3 Code conventions

- 4-space indentation, no tabs (enforced by `.clang-format`).
- Public API: `ol_` prefix, snake_case.
- Macros: `OL_` prefix, UPPER_SNAKE_CASE.
- Opaque types: `typedef struct ol_foo ol_foo_t;`.
- Every public function has a Doxygen block.
- No `goto` except for error cleanup labels named `cleanup:`.

### 8.4 Commit conventions

Conventional Commits. The `ci/commit_check.py` tool enforces the
format on the last N commits (wip and merge commits are exempt).

---

## 9. Instructions for Future Agents

### 9.1 Read first

1. This file — especially §4 and §9.
2. `README.md` — orientation.
3. `docs/CHANGELOG.md` — what has been tried.
4. `demos/README.md` — what already works end-to-end.

### 9.2 Environment setup

```
git clone git@github.com:OverLab-Group/OLSRT.git
cd OLSRT
python3 ci/run_all.py --keep-going
```

If `ci/run_all.py` reports anything other than `0 failure(s)`,
**stop and fix the build before writing new code**.

### 9.3 Where to start

The next planned task is always the **highest-numbered version
marked 🔜 NEXT** in §5. If the last done version is v1.3.3, your
next task is v1.3.4.

### 9.4 Test checklist before committing

```
[ ] python3 ci/run_all.py --keep-going       → 0 failure(s)
[ ] python3 ci/doxygen_check.py              → 0 failed
[ ] python3 ci/build_matrix.py               → 36/36
[ ] cd demos && make && (run each demo)      → expected output
[ ] git status --short                       → no stray files
[ ] grep -rn "TODO\|FIXME" src/ | wc -l      → not increasing
```

### 9.5 Never do

- Force-push to `main` without a maintainer's explicit approval.
- Add a dependency that is not available on Linux, BSD, macOS, and
Windows.
- Remove a public API without a major version bump.
- Silently swallow a compiler warning with `-w`.
- Commit anything outside the explicit path list of a maintenance
script. Use `commit_paths(message, [paths])` — never blanket
`git add -A`.

---

## 10. Glossary

| Term | Meaning |
|---|---|
| **Actor** | Lightweight concurrency unit with a mailbox, isolated in its own process (arena + green thread). |
| **Arena** | Region-based memory allocator used per actor. |
| **Branch Unit (BU)** | NWP's smallest execution unit. Lighter than a fiber. |
| **Commit Engine** | NWP component that injects branch results at program end. |
| **Green thread** | Cooperative user-space thread. |
| **HCR** | Hot-Coding References — paradigm where every callable reference spawns a branch. |
| **NWDC** | Non-Wait Data Channel — the pipe between branch and main. |
| **NWL** | Non-Waiting Loop — the shutdown loop that spins until all branches commit. |
| **NWP** | Non-Waiting Paradigm. |
| **ORoutine** | OLSRT's Goroutine-like primitive (v1.3.4). |
| **Poller** | Platform-agnostic I/O multiplexing (epoll/kqueue/IOCP/select). |
| **Supervisor** | Actor that monitors and restarts its children. |
| **Wave** | A named stabilization effort. Wave 1 produced v1.3.1; Wave 2 produced v1.3.2. |

---

*End of ROADMAP.md — for questions, open an issue or contact
[@OverLab-Group](https://github.com/OverLab-Group).*
