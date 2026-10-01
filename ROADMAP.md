# OLSRT Roadmap — Full Technical Plan

> **Audience:** project maintainers, contributors, and AI agents picking
> up work between sessions. This document is intentionally exhaustive so
> that a fresh reader can understand the *why*, the *what*, and the *how*
> of every planned version without external context.

**Last updated:** 2026-09-29
**Current released version:** v1.3.1 (Wave 1 Stabilization)
**Current development branch:** `wave1-stabilization` (merged into `main`)
**Next major milestone:** NWP MVP standalone, then v1.3.2 (Actor Scheduler)

---

## Table of Contents

1. [Project Vision](#1-project-vision)
2. [Versioning Strategy](#2-versioning-strategy)
3. [Current State — What Works Today](#3-current-state--what-works-today)
4. [Known Issues & Technical Debt](#4-known-issues--technical-debt)
5. [Version Plan — v1.3.x Series](#5-version-plan--v13x-series)
6. [NWP — Non-Waiting Paradigm](#6-nwp--non-waiting-paradigm)
7. [Version Plan — v2.0 and Beyond](#7-version-plan--v20-and-beyond)
8. [Infrastructure, Testing, and Tooling](#8-infrastructure-testing-and-tooling)
9. [Instructions for Future Agents](#9-instructions-for-future-agents)
10. [Glossary](#10-glossary)

---

## 1. Project Vision

OLSRT (OverLab Streams Runtime) is a **universal concurrency substrate**
written in portable C11. The long-term goal is to power **any language**
by exposing a stable C ABI that higher-level languages (PHP, Python, Rust, Go,
Node.js, etc.) can bind to.

The design priorities, in order:

1. **Correctness under sanitizers** — every release must pass ASan, UBSan,
   TSan on Linux x86_64 and aarch64.
2. **Zero-cost abstractions where possible** — channels, promises, and
   green threads must not add measurable overhead over hand-written code.
3. **Feature parity with libuv, then exceed it** — actors, supervisors,
   reactive streams, dataflow graphs, and (from v1.3.6) NWP branches.
4. **Hackability** — code must be readable and modifiable by a single
   contributor, not a committee.

---

## 2. Versioning Strategy

OLSRT follows **strict Semantic Versioning 2.0**:

| Change kind | Version bump | Example |
|-------------|--------------|---------|
| Bug fix, no API change | Patch (`x.y.Z`) | v1.3.0 → v1.3.1 |
| New feature, backward compatible | Minor (`x.Y.0`) | v1.3.1 → v1.4.0 |
| Breaking API/ABI change | Major (`X.0.0`) | v1.3.x → v2.0.0 |

### Branch strategy

- `main` — always buildable and green. Never force-pushed except during
  history cleanups (last done: 2026-09-29 to remove a Qwen co-author).
- `v1.3.N-feature` — feature branches cut from `main`.
- Tags — annotated, signed with the maintainer's GPG key when available.

### Release checklist

Every release must pass:

[ ] python3 verify.py                           # ASan + UBSan + TSan
[ ] make TARGET=linux ARCH=x86_64               # standard build
[ ] All demos in demos/ produce expected output
[ ] docs/CHANGELOG.md updated
[ ] Version numbers bumped in:
includes/runtime/ol_globals.h
includes/code/streams/ol_green_threads.h  (OL_GT_VERSION_*)
[ ] Git tag `vX.Y.Z` created with release notes
[ ] GitHub Release published

### ABI stability

Public symbols are versioned via `.symver` (planned for v1.4.0). Until
then, `libolsrt.so` ships with `SOVERSION 1`.

---

## 3. Current State — What Works Today

### 3.1 Working subsystems (verified by `tests/test_wave1.c` and `demos/`)

| Subsystem | File | Status | Verified by |
|-----------|------|--------|-------------|
| **Channels** | `ol_channel.c` | ✅ | demo 02 (1M msg), test 6 |
| **Promises/Futures** | `ol_promise.c` | ✅ | demo 05, tests 1–2 |
| **Event loop** | `ol_event_loop.c` | ✅ | demo 04, test 5 |
| **Poller** | `ol_poller.c` | ✅ (epoll/kqueue/select) | demo 07 |
| **Parallel pool** | `ol_parallel.c` | ✅ | demo 03 |
| **Green threads** | `ol_green_threads.c` | ⚠️ compiles; scheduler not driven | compile-only |
| **Coroutines** | `ol_coroutines.c` | ⚠️ wrapper; relies on green threads | compile-only |
| **Actor (mailbox)** | `ol_actor.c` | ✅ mailbox, ask/reply | demo 01, tests 1–5 |
| **Actor (loop)** | `ol_actor.c` | ⚠️ requires manual `ol_actor_process_batch` | demo 01 |
| **Supervisor** | `ol_supervisor.c` | ✅ lifecycle, bounded shutdown | compile-only |
| **Arena allocator** | `ol_actor_arena.c` | ✅ | test 3 |
| **Hashmap** | `ol_actor_hashmap.c` | ✅ | implicitly via actor |
| **TCP sockets** | `network/ol_tcp.c` | ✅ | demo 07 |
| **UDP sockets** | `network/ol_udp.c` | ✅ (not yet demoed) | compile-only |
| **Dataflow** | `ol_dataflow.c` | ⚠️ see §4.4 | demo 06 (partial) |
| **Reactive** | `ol_reactive.c` | ✅ (not yet demoed) | compile-only |
| **Streams** | `ol_streams.c` | ✅ (not yet demoed) | compile-only |
| **Semaphores** | `ol_semaphores.c` | ✅ | compile-only |

### 3.2 Test suite

- `tests/test_wave1.c` — 22 assertions, all passing.
- Runs under **ASan + UBSan** and **TSan** via `verify.py`.
- LSan is currently **disabled** (see §4.1).

### 3.3 Demos

Seven demos in `demos/`, all working:
`01_hello_actor`, `02_channel`, `03_parallel`, `04_timers`,
`05_promise`, `06_dataflow` (partial), `07_http_server`.

### 3.4 Build matrix

- Linux x86_64 ✅
- Linux aarch64 ⚠️ untested in CI
- BSD ✅ (reported)
- Windows / macOS ❌ (falls back to `select`, limited to 64 FDs)

---

## 4. Known Issues & Technical Debt

Every issue below is *tracked* and assigned to a future version.

### 4.1 Actor main loop is not driven by the green-thread scheduler

**Severity:** High - **FIXED in v1.3.2.**

**Symptom:** `ol_actor_start()` sets a flag on the actor, but the actor
entry function runs inside a green thread spawned by `ol_process_create()`.
Nothing calls `ol_gt_resume()` on that green thread, so the actor never
runs its main loop. Demo 01 works only because it pumps the mailbox
manually with `ol_actor_process_batch()`.

**Fix (v1.3.2):** In `ol_process_create()`, after spawning the green
thread, register it with the scheduler. In `ol_actor_start()`, signal the
scheduler to begin resuming that green thread. Ensure the scheduler is
initialised (currently `ol_gt_scheduler_init()` is never called by the
actor layer).

**Acceptance criteria:**
- `demos/01_hello_actor.c` runs **without** `ol_actor_process_batch`.
- LSan re-enabled in `verify.py`; no leaks from the actor lifecycle.
- New test in `test_wave1.c`: `test_actor_runs_automatically`.

**Files:** `src/code/streams/ol_actor.c`,
`src/code/streams/ol_actor_process.c`,
`src/code/streams/ol_green_threads.c`.

---

### 4.2 `ol_memwatch.c` has a use-after-free

**Severity:** High — flagged by GCC's `-Wuse-after-free`.

**Location:** `src/code/streams/ol_memwatch.c:406`

```c
void *new_ptr = realloc(ptr, size);
if (!new_ptr) return NULL;

if (g_memwatch.initialized && g_memwatch.enabled && new_ptr != ptr) {
    ol_memwatch_track_free(ptr, NULL, 0);  // ← ptr may be dangling
    ol_memwatch_track_alloc(size, NULL, 0);
}
```

**Fix (v1.3.3):** Move the `ol_memwatch_track_free(ptr, ...)` call
**before** `realloc`, or use the return value `new_ptr` instead of `ptr`.

**Acceptance criteria:** Clean `-Wuse-after-free` build; ASan stays green.

---

### 4.3 Cosmetic compiler warnings

**Severity:** Low — noise only, but pollutes CI output.

Full list (all `-W` warnings in a clean build):

| File | Warning | Fix |
|---|---|---|
| `ol_actor.c` | `_GNU_SOURCE` redefined | Wrap with `#ifndef _GNU_SOURCE` |
| `ol_actor_arena.c` | `_GNU_SOURCE` redefined | Same |
| `ol_green_threads.h` | `OL_ALWAYS_INLINE` redefined | Guard with `#ifndef OL_ALWAYS_INLINE` |
| `ol_green_threads.c:1537` | address of packed member | Remove `__attribute__((packed))` from `struct ol_gt` or align atomics |
| `ol_green_threads.c:2105` | `%zu` with `int` argument | Cast to `(size_t)` |
| `ol_actor_serialize.c` | unused parameters `key`, `iv`, `auth_tag` | `(void)param;` |
| `ol_supervisor.c:466` | signed/unsigned compare | Cast to `uint64_t` |
| `ol_supervisor.c:556` | unused parameter `process` | `(void)process;` |
| `ol_streams.c:156` | unused parameter `fd` | `(void)fd;` |
| `ol_memwatch.c:334` | unused parameters `file`, `line` | `(void)file; (void)line;` |

**Fix (v1.3.3):** All in one commit titled `chore: silence -Wall -Wextra`.

**Acceptance criteria:** `make 2>&1 | grep warning:` returns nothing.

---

### 4.4 Dataflow worker does not drain edge inboxes

**Severity:** Medium — demo 06 runs but multi-hop graphs are broken.

**Symptom:** In `demos/06_dataflow.c`, only the `source` node's handler
runs. The `doubler` and `sink` handlers never fire because the worker
loop in `ol_dataflow.c` only polls each node's `self_inbox`, never the
inboxes attached to outgoing edges.

**Fix (v1.3.3):** In `df_worker()`:

1. For each node, before processing `self_inbox`, also drain every edge
whose `to` field points at this node.
2. Move each item from `edge->inbox.ch` into `node->self_inbox.ch`.
3. Replace the busy-poll with `nanosleep(1ms)` when nothing was drained.

**Acceptance criteria:** Demo 06 output shows `source → doubler → sink`
for each of the five items (10→20, 20→40, ...). New test:
`test_dataflow_multi_hop`.

---

### 4.5 `Makefile.mk` had a `TARGET`/`ARCH` bug

**Severity:** Low — file was already removed from git.

If anyone re-adds it, fix line 24: `ifeq ($(TARGET),x86_64)` must be
`ifeq ($(ARCH),x86_64)`.

---

### 4.6 Windows/macOS poller uses `select` (64 FD limit)

**Severity:** High for production use on those platforms.

**Fix (v2.0):** Implement `ol_poller_iocp.c` (Windows) and
`ol_poller_kqueue.c` (macOS) — see §7.2.

---

### 4.7 ABI versioning is not yet implemented

**Severity:** Low — only matters once third-party binaries link OLSRT.

**Fix (v1.4.0):** Add `.symver` annotations to every public function and
bump `SOVERSION` only on breaking changes. See §5.6.

---

## 5. Version Plan — v1.3.x Series

### v1.3.1 — Wave 1 Stabilization ✅ DONE

**Released:** 2026-09-29
**Tag:** `v1.3.1`
**Commits on `main`:** 8c5dce7 → 2c5100f → ddbb31c → f24250e → 2d86537

**Deliverables:**

- Fixed 11 real bugs in v1.3.0:

- Promise leak in `ol_actor_ask`
- Busy-wait in `ol_actor_send_timeout`
- Memory-ordering bug in mailbox ring buffer
- Heap-use-after-free in `ol_actor_ask` error path
- `ol_actor_try_send` ignored the overflow list
- `ol_arena_free` did not validate ownership
- Supervisor shutdown had no bound
- Missing `ol_supervisor_stats_t` and config fields
- Missing `ol_actor_hashmap.c`
- Header/source mismatch in `ol_green_threads.{c,h}`
- TCP/UDP deadline not enforced
- Added `tests/test_wave1.c` (22 assertions).
- Added `verify.py` (ASan + UBSan + TSan).
- Added 7 working demos.
- Fixed network headers: missing `<stdlib.h>`, conflicting `ol_mutex_t`, missing `#ifndef` guards on `OL_POLL_*`.

**Not in this release:** the actor scheduler (§4.1) — that is v1.3.2.

---

### v1.3.2 - Actor Scheduler ✅ DONE

**Goal:** Make actors run their main loop automatically, without manual
mailbox pumping.

**Dependencies:** none (NWP is developed in parallel, see §6).

**Deliverables:**

1. `ol_gt_scheduler_init()` called once on first actor creation.
2. `ol_process_create()` registers the spawned green thread with the
scheduler's run queue.
3. `ol_actor_start()` signals the scheduler (via
`ol_gt_resume` or an equivalent queue push).
4. `ol_actor_process_batch()` kept public but no longer needed for demos.
5. LSan re-enabled in `verify.py`.
6. New test: `test_actor_runs_automatically`.

**Acceptance criteria:**

- `demos/01_hello_actor.c` simplified to use `ol_actor_ask` +
`ol_future_await` **without** any manual pumping.
- `verify.py` output shows `[OK] asan_tests: PASS` with LSan enabled
(`detect_leaks=1`).
- ASan and TSan both clean.

**Files to modify:**

- `src/code/streams/ol_actor.c` (create, start)
- `src/code/streams/ol_actor_process.c` (create, trampoline)
- `src/code/streams/ol_green_threads.c` (scheduler loop)
- `src/code/streams/ol_actor_process_entry` (main loop)

**Estimated effort:** 1–2 days.
**Risks:** Deadlock if the scheduler is not thread-safe with actor
mailboxes. Mitigation: run a targeted TSan pass early.

---

### v1.3.3 — Dataflow Cleanup

**Goal:** Fix the dataflow edge-inbox bug, silence all warnings, and
close the LSan story.

**Dependencies:** v1.3.2 (LSan already re-enabled).

**Deliverables:**

1. Edge-inbox draining in `df_worker` (see §4.4).
2. `ol_memwatch.c` use-after-free fix (§4.2).
3. All cosmetic warnings from §4.3 gone.
4. New tests:

- `test_dataflow_multi_hop`
- `test_memwatch_realloc`
5. Demo 06 updated to show the full three-hop chain.

**Acceptance criteria:**

- `make 2>&1 | grep warning:` empty.
- `make 2>&1 | grep -i "use-after-free"` empty.
- Demo 06 output shows `source → doubler → sink` per item.

**Estimated effort:** 2–3 days.
**Risks:** The df_worker refactor touches the core scheduling path; run
both ASan and TSan on each iteration.

---

### v1.3.4 — ORoutines (Goroutine-like API)

**Goal:** Provide a Go-style `go func()` API on top of OLSRT's green
threads, with the "HCR" (Hot-Coding References) flavour described in
`nwp/Non-Waiting.txt`.

**Reference:** `nwp/mvp/includes/nwp_api.h` (the `nwp_go` macro is the
direct ancestor of ORoutines' API).

**Public API design (draft):**

```
/* oroutines.h */
typedef void (*oroutine_fn)(void* arg);

/* Spawn a coroutine. Returns a handle. */
oroutine_t* or_go(oroutine_fn fn, void* arg);

/* Wait for a coroutine's result. */
void* or_join(oroutine_t* co, int64_t deadline_ns);

/* Yield from inside a coroutine. */
void or_yield(void);

/* Cancel cooperatively. */
void or_cancel(oroutine_t* co);

/* Channel-like primitives specialised for oroutines. */
or_chan_t* or_chan_create(size_t elem_size, size_t capacity);
void       or_chan_send(or_chan_t* ch, const void* elem);
void       or_chan_recv(or_chan_t* ch, void* out);
void       or_chan_close(or_chan_t* ch);

/* Select over multiple channels (Go's `select`). */
int or_select(or_select_case_t* cases, size_t n);
```

**Implementation strategy:**

- `oroutine_t` is a thin wrapper over `ol_gt_t`.
- `or_chan_t` is a thin wrapper over `ol_channel_t` with a `msg_size`
field so that values are copied into the ring buffer, not pointers.
- `or_select` polls channels in random order until one is ready.

**Deliverables:**

1. `includes/code/streams/oroutines.h` — public API.
2. `src/code/streams/oroutines.c` — implementation.
3. `demos/08_oroutines.c` — worker-pool demo (100 goroutines, 10
channels, message passing).
4. New test file `tests/test_oroutines.c` with 10+ assertions.
5. Documentation section in `docs/`.

**Acceptance criteria:**

- Demo 08 runs 100,000 messages through 10 channels with zero loss.
- No new warnings, ASan/UBSan/TSan clean.
- API documented in `docs/index.html` under "ORoutines".

**Estimated effort:** 1 week.
**Risks:** The select implementation is tricky to make race-free. Plan
to use a channel-array + mutex approach first, then optimise.

---

### v1.3.5 — Supervisor 2.0

**Goal:** Bring the supervisor up to Erlang/OTP parity.

**Deliverables:**

1. **Restart strategies:** one-for-one, one-for-all, rest-for-one,
simple-one-for-one. (Current code has the first three; verify and
document.)
2. **Hierarchical supervision:** a supervisor can be a child of another.
3. **Escalation:** when the restart intensity is exceeded, the supervisor
itself terminates, propagating the exit up the tree.
4. **Hot code upgrade hooks:** before a restart, call an optional
`before_restart(old_pid, new_pid)` callback.
5. **`ol_supervisor_dump_tree()`** — emit an ASCII tree for debugging.
6. New demo `demos/09_supervisor.c` — three-level supervision tree with
simulated crashes.

**Acceptance criteria:**

- Demo 09 shows correct restart behaviour for each strategy.
- Supervisor tests cover escalation.

**Estimated effort:** 1 week.
**Risks:** Cross-process signal delivery is subtle. Use the existing
`ol_process_monitor` mechanism rather than rolling a new one.

---

### v1.3.6 — NWP MVP Integration

See §6 for the full plan. Summary:

- Bring the standalone `nwp/mvp/` build into OLSRT as `src/nwp/`.
- Expose `ol_nwp_*` API from OLSRT.
- Add demo `10_nwp_helloworld.c`.
- Benchmark NWP branch switch vs. green-thread switch.

**Estimated effort:** 2–3 weeks.

---

### v1.3.7 — Release Polish

**Goal:** Make v1.3.x series production-ready.

**Deliverables:**

1. **Benchmarks** in `bench/` — actor ping-pong, channel throughput,
timer accuracy, NWP branch switch.
2. **SDK binding** for one language (candidate: Python via ctypes).
3. **Full API reference** with examples for every public function.
4. **GitHub Actions CI:** build + test on Linux x86_64, aarch64, FreeBSD.
5. **Docker image** `overlab/olsrt:1.3.7` for reproducible builds.

**Estimated effort:** 1–2 weeks.

---

## 6. NWP — Non-Waiting Paradigm

### 6.1 What NWP is

NWP is a **paradigm**, not a library. Its core claim:

> *The main program never blocks. Every reference to a callable entity
> spawns a Branch Unit. The main program continues. At program end, all
> Branches commit their results atomically.*

This is distinct from OLSRT's futures (which can block via
`ol_future_await`). NWP's promise is **zero waiting, ever**, at the cost
of an explicit commit phase at shutdown.

---

## 7. Version Plan — v2.0 and Beyond

### 7.1 v2.0 — Apollo

**Codename meaning:** Apollo = mission to many destinations. Here, the
"destinations" are **platforms** and **network protocols**.

**Two major thrusts:**

#### A. Cross-platform parity (2–3 months)

| Deliverable | Detail |
|---|---|
| **Windows IOCP** | New `src/code/streams/ol_poller_iocp.c` using `CreateIoCompletionPort`, `GetQueuedCompletionStatus`. All socket I/O rewritten on `OVERLAPPED` structs. |
| **macOS kqueue** | New `src/code/streams/ol_poller_kqueue.c` using `kqueue`, `kevent`, `EVFILT_READ/WRITE`. |
| **io_uring (Linux 5.1+)** | Optional backend `ol_poller_io_uring.c` using `io_uring_setup`, `io_uring_enter`. |
| **Async file I/O** | `ol_file_async_read/write` on all three backends. |
| **Async DNS** | `ol_dns_resolve(hostname) -> future<address>`, uses threadpool fallback. |
| **Async process spawn** | `ol_process_spawn_async(cmd) -> future<exit_code>`. |
| **Signal handling** | `ol_signal_register(sig, cb)` with a self-pipe on POSIX, `SetConsoleCtrlHandler` on Windows. |

#### B. Network protocols (3–4 months)

The `includes/code/network/` directory already contains **~70 header
stubs** for network protocols. None have working implementations except
`ol_tcp.c` and `ol_udp.c`. v2.0 fills in the rest.

**Protocol tiers:**

**Tier 1 — Transport & core (~10 protocols)**

- TCP ✅ (already implemented)
- UDP ✅ (already implemented)
- QUIC (`ol_quic.h`) — over UDP, TLS 1.3 built in
- SCTP (`ol_sctp.h`) — multi-homed reliable transport
- TLS 1.3 (`ol_tls.h`) — needs a crypto backend
- SSL (legacy, `ol_ssl.h`) — for compatibility
- DTLS (part of `ol_tls.h`) — for UDP-based TLS
- Raw sockets (`ol_net.h`)
- Socket options helper (`ol_port.h`)
- System logger (`ol_syslog.h`)

**Tier 2 — Application layer (~20 protocols)**

- HTTP/1.1, HTTP/2, HTTP/3 (`ol_http.h`)
- WebSocket (`ol_ws.h`)
- gRPC (`ol_grpc.h`) — needs HTTP/2
- GraphQL (`ol_graphql.h`) — needs HTTP
- SMTP, POP3, IMAP (`ol_smtp.h`, `ol_pop3.h`, IMAP TBD)
- FTP, SFTP (`ol_ftp.h`, `ol_sftp.h`)
- Telnet, TFTP (`ol_telnet.h`, `ol_tftp.h`)
- DNS (`ol_dns.h`) — resolver + message format
- NTP (`ol_ntp.h`) — time synchronisation
- DHCP, DHCPv6 (`ol_dhcp.h`)
- MQTT (`ol_mqtt.h`) — for IoT
- CoAP (`ol_coap.h`) — lightweight IoT
- AMQP (`ol_amqp.h`) — message queues
- Syslog (`ol_syslog.h`)
- LDAP (`ol_ldap.h`) — directory services
- RADIUS, Diameter (`ol_radius.h`, `ol_diameter.h`) — AAA
- Kerberos (`ol_kerberos.h`)

**Tier 3 — Routing & control (~15 protocols)**

- OSPF, BGP, RIP, IS-IS, EIGRP (`ol_ospf.h`, `ol_bgp.h`, `ol_rip.h`, `ol_is_is.h`, `ol_eirgp.h`)
- OpenFlow (`ol_openflow.h`) — SDN
- STP, RSTP (`ol_stp.h`, `ol_rstp.h`)
- ARP, ICMP, IGMP, MLD (`ol_arp.h`, `ol_icmp.h`, `ol_igmp.h`, `ol_mld.h`)
- L2TP, PPP, PPPoE, L2F (`ol_l2tp.h`, `ol_ppp.h`, `ol_pppoe.h`, `ol_l2f.h`)
- NetFlow, IPFIX (`ol_netflow.h`, `ol_ipfix.h`)
- 802.1X (`ol_802.1x.h`) — port-based access control

**Tier 4 — Media & streaming (~8 protocols)**

- RTP, RTCP, RTSP, SDP (`ol_rtp.h`, `ol_rtcp.h`, `ol_rtsp.h`, `ol_sdp.h`)
- SRTP (`ol_srtp.h`) — secure RTP
- HLS, DASH (`ol_hls.h`, `ol_dash.h`) — HTTP streaming
- WebRTC (`ol_webrtc.h`) — real-time communication

**Tier 5 — Security & identity (~6 protocols)**

- IPsec (`ol_ipsec.h`)
- PGP (`ol_pgp.h`)
- OAuth 2.0 (`ol_oauth.h`)
- SAML (`ol_saml.h`)
- SCP (`ol_scp.h`)
- SSH (`ol_ssh.h`)

**Tier 6 — Miscellaneous**

- WebDAV (`ol_webdav.h`)

**OverLab Protocols**

- NOPO (`ol_nopo.h`)
- TOP (`ol_top.h`)

```
These protocols will not publish yet
```

**Estimated effort:** Each protocol takes 2–7 days for a minimal
implementation (RFC 2119 conformance, no extensions). Total for Tier 1

- Tier 2: ~3 months. Full 70-protocol coverage: ~6–9 months.

**Acceptance criteria for v2.0:**

- All Tier 1 + Tier 2 protocols pass interoperability tests against
`curl`, `openssl s_client`, `dig`, `mqttx`, and similar tools.
- Cross-platform CI matrix green on Linux, Windows, macOS, FreeBSD.
- `libolsrt.so` builds on all four platforms with no warnings.

### 7.2 v3.0 — Nova (Virtualization)

**Scope:**

- `ol_virtual.c` — VM abstraction
- `ol_gpu.c` — GPU compute orchestration
- Network namespace isolation
- Container runtime primitives

### 7.3 v4.0–v7.0 — Utilities

- **v4.0 Core:** `ol_filesystem`, `ol_timer`, `ol_db`, `ol_dsl`
- **v5.0 Spark:** `ol_compression`, `ol_crypto`
- **v6.0 Orion:** `ol_board`, `ol_fs_to_fs`
- **v7.0 Cosmos:** `ol_ai`, `ol_security`

### 7.4 v8.0–v12.0 — Compiler & OS support

- **v8.0 Hermes:** `ollc` (OverLab Language Compiler) era begins
- **v9.0 Kernel:** advanced runtime core
- **v10.0 Stream:** architectures added
- **v11.0 Flow:** full OS support
- **v12.0 Wave:** 30% language coverage (SDK bindings for 30% of
mainstream programming languages)

---

## 8. Infrastructure, Testing, and Tooling

### 8.1 Current tooling

| Tool | Purpose | Location |
|---|---|---|
| `Makefile` | Primary build system | repo root |
| `CMakeLists.txt` | IDE-friendly build | repo root |
| `Doxyfile` | API documentation | repo root |
| `verify.py` | Sanitizer test runner | repo root |
| `demos/Makefile` | Demo build | `demos/` |
| `source/conf.py` | Sphinx documentation | `source/` |

### 8.2 Recommended additions (v1.3.3+)

- **CI/CD** — `.github/workflows/ci.yml` with matrix builds.
- **Fuzzing** — `tests/fuzz/` using libFuzzer.
- **Benchmarks** — `bench/` with hyperfine scripts.
- **Coverage** — `gcov`/`lcov` reports in CI.

### 8.3 Code conventions

- 4-space indentation, no tabs.
- Public API: `ol_` prefix, snake_case.
- Macros: `OL_` prefix, UPPER_SNAKE_CASE.
- Opaque types: `typedef struct ol_foo ol_foo_t;`.
- Every public function has a Doxygen block.
- No `goto` except for error cleanup labels named `cleanup:`.

---

## 9. Instructions for Future Agents

If you are an AI agent (or a new contributor) picking up this project:

### 9.1 Read first

1. This file (`ROADMAP.md`), especially §4 and §9.
2. `README.md` — orientation.
3. `docs/CHANGELOG.md` — what has been tried.
4. `demos/README.md` — what already works end-to-end.

### 9.2 Environment setup

```
git clone git@github.com:OverLab-Group/OLSRT.git
cd OLSRT

# Verify SSH works
ssh -T git@github.com       # expect: Hi <username>!

# Build and test
python3 verify.py           # ~2 minutes on a modern laptop
```

If `verify.py` reports anything other than four `PASS` lines, **stop and
fix the build before writing any new code**.

### 9.3 Where to start

The next planned task is always described in the **highest-numbered
"DONE" section** of §5. For example, if the last done version is v1.3.2,
your next task is v1.3.3.

### 9.4 Commit conventions

```
<scope>: <imperative summary>

<body: why, what, and any caveats>

<footer: Signed-off-by: Name <email>>
```

Scopes: `wave1`, `actor`, `network`, `dataflow`, `green`, `supervisor`,
`oroutines`, `nwp`, `docs`, `chore`.

### 9.5 Test checklist before committing

```
[ ] python3 verify.py                         → 4 PASS
[ ] cd demos && make && (run each demo)       → expected output
[ ] git status --short                        → no stray files
[ ] grep -rn "TODO\|FIXME" src/ | wc -l       → not increasing
```

### 9.6 Never do

- Force-push to `main` without a maintainer's explicit approval.
- Add a dependency that is not available on Linux, BSD, macOS, and
Windows.
- Remove a public API without a major version bump.
- Silently swallow a compiler warning with `-w`.

---

## 10. Glossary

| Term | Meaning |
|---|---|
| **Actor** | Lightweight concurrency unit with a mailbox. Isolated in its own process (arena + green thread). |
| **Arena** | Region-based memory allocator used per actor for isolation. |
| **Branch Unit (BU)** | NWP's smallest execution unit. Lighter than a fiber. |
| **Commit Engine** | NWP component that injects branch results into main memory at program end. |
| **Green thread** | Cooperative user-space thread. |
| **HCR** | Hot-Coding References — paradigm where every callable reference spawns a branch. |
| **NWDC** | Non-Wait Data Channel — the pipe between branch and main. |
| **NWL** | Non-Waiting Loop — the shutdown loop that spins until all branches commit. |
| **NWP** | Non-Waiting Paradigm — the philosophy that main never blocks. |
| **ORoutine** | OLSRT's Goroutine-like primitive (v1.3.4). |
| **Poller** | Platform-agnostic I/O multiplexing (epoll/kqueue/IOCP/select). |
| **Supervisor** | Actor that monitors and restarts its children. |
| **Wave** | A named stabilization effort. Wave 1 produced v1.3.1. |

---

*End of ROADMAP.md — for questions, open an issue or contact
[@OverLab-Group](https://github.com/OverLab-Group).*
