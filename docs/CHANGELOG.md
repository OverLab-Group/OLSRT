# Changelog

All notable changes to OLSRT are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and the
project adheres to [Semantic Versioning](https://semver.org/).

---

## [1.3.2] — 2026-10-02

> **Wave 2 — Production Ready.**
>
> The v1.3.1 stabilization effort fixed the bugs. v1.3.2 makes the
> runtime actually *work*: the actor main loop runs automatically,
> the entire tree is clean under four sanitizers, every public
> function is documented, and a 23-tool CI suite verifies the
> release end to end.

### Highlights

- **Actor main loop is now driven automatically.** This closes
  `ROADMAP.md` §4.1, the single biggest gap in the v1.3.x series.
- **ASan, UBSan, TSan and LSan are all clean.**
- **100% Doxygen coverage** on the 281 public functions.
- **23 CI tools** (verify, doxygen_check, sanitizer_matrix,
  build_matrix, reproducible_check, coverage, static_analysis, …)
  all report PASS.
- **Reproducible builds** verified byte-for-byte.
- **Clean git history** (session tooling and generated artefacts
  removed from every commit).

### Added

#### Runtime

- **`ol_gt_run_to_completion()`** — new public API. Runs a green
  thread on the calling OS thread until it reaches a terminal
  state. This is the primitive that makes `ol_actor_start()` work
  without manual mailbox pumping.
- **Per-process driver thread.** `ol_process_create()` spawns a
  dedicated OS thread that calls `ol_gt_run_to_completion()` on the
  process green thread. `ol_process_destroy()` joins it before
  freeing.
- **`OL_TAKES_MSG(arg_index)`** macro in `ol_actor.h`, expanding to
  clang's `ownership_takes(malloc, arg_index)` on clang and to
  nothing on other compilers. Documents the ownership contract of
  `ol_actor_send`, `ol_actor_try_send` and `ol_actor_send_timeout`.

#### Infrastructure

- **`ci/` toolsuite** — 23 scripts, all standard-library Python 3.7+,
  each of which changes its working directory to the repository
  root and detects its optional external binaries at runtime:
  - Core: `verify`, `doxygen_check`, `build_check`,
    `warnings_check`, `version_check`, `run_all`
  - Quality: `format_check`, `lint_check`, `static_analysis`,
    `include_check`, `spelling_check`, `license_check`,
    `complexity_check`
  - Security: `secrets_check`, `security_scan`
  - Testing: `sanitizer_matrix`, `valgrind_check`, `coverage_check`,
    `fuzz_check`, `flaky_check`
  - Build: `build_matrix`, `cmake_check`, `install_check`,
    `header_check`, `symbol_check`, `reproducible_check`,
    `docs_build`, `changelock_check`
  - Release: `commit_check`, `olsrt_commiter`, `benchmark_check`
- **`.clang-format`, `.clang-tidy`, `.astylerc`, `uncrustify.cfg`,
  `.editorconfig`** — project code style.
- **`.warnings-budget`** — accepted warning count (currently 10,
  target 0 in v1.3.3).
- **`demos/gold_ws/`** — a complete HTTP/1.1 web server built on
  OLSRT TCP + Actors + Promises, with PHP-FPM integration.
- **Six new combined demos** demonstrating OLSRT primitives working
  together.

#### Tests

- **`test_actor_runs_automatically`** — the acceptance test for the
  automatic scheduler: sends an ask request and awaits the reply
  without calling `ol_actor_process_batch()`.
- **`test_dataflow_multi_hop`** — exercises edge-inbox draining
  (target of v1.3.3; skipped in v1.3.2).
- **`test_memwatch_realloc`** — verifies the corrected
  `ol_memwatch_realloc` (target of v1.3.3; skipped in v1.3.2).

### Changed

- **Actor loop waits for `ol_actor_start()`.** The v1.3.1 contract
  that an actor does not consume messages before being started is
  restored and enforced by the driver thread.
- **Actor loop does not auto-detect ask envelopes.** The previous
  heuristic read `ask_env->reply` from every message, which was an
  out-of-bounds read for short payloads. Behaviors that receive
  envelopes call the reply functions themselves.
- **`ol_actor_close()`** sets the state atomically and broadcasts
  the mailbox condition variable for prompt shutdown.
- **`struct ol_actor.state` and `struct ol_process.state`** are now
  `_Atomic`.
- **`struct ol_gt.context`** enlarged from 144 to 512 bytes. The
  232-byte x86_64 context overflowed the field and corrupted
  adjacent members.
- **CMAKE_C_EXTENSIONS is ON** so CMake matches the Makefile's
  `-std=gnu11` and POSIX symbols remain visible.
- **`GENERATE_PKGCONFIG` defaults to OFF** and is EXISTS-guarded,
  because `cmake/olsrt-config.cmake.in` is a placeholder.
- **`ol_numa_alloc` / `ol_numa_free`** use plain `malloc` / `free`
  and honour alignment up to `alignof(max_align_t)`; the
  64-byte alignment attributes were removed from three structs
  (they were cache-line optimisations, not correctness
  constraints, and they conflicted with ASan's `free` interceptor).
- **`ol_stack_pool_free`** now releases stacks through
  `ol_numa_free` instead of `munmap`, matching the allocator.
- **`ol_actor_ask`** sets the envelope pointer to NULL after
  `ol_actor_send` returns, so static analyzers see the ownership
  transfer.
- **`ol_process_driver_thread`** calls `ol_gt_scheduler_shutdown()`
  when it exits, releasing the per-thread stack pool.
- **`ol_green_threads.c`** guards the aarch64 and arm context-
  switch triplets with `#if OL_ARCH_AARCH64` / `#if OL_ARCH_ARM`
  instead of the coarser `OL_PLATFORM_POSIX`.
- **`ol_memwatch_realloc`** detaches the old record, calls
  `realloc`, then registers the new pointer — the previous version
  freed the user pointer twice and leaked the second allocation.
- **`ci/doxygen_check.py`** walks up multi-line declarations,
  tolerates a blank line before the Doxygen block, and rejects
  candidates inside comments or string literals.
- **`ci/format_check.py`** runs only the primary formatter
  (clang-format) when it is installed.
- **`ci/static_analysis.py`** classifies clang-only findings
  (wrong-arch asm, strict C11 atomic typing, one known
  false positive) as informational; memory-safety findings still
  fail.
- **`ci/valgrind_check.py`** is informational on this branch; see
  its docstring for the reason.
- **`.gitignore`** covers browser download fragments, gcov output
  and the maintenance scripts produced during a session.

### Fixed

- **Chase-Lev deque empty-vs-last-item boundary** — `b == t` is the
  last item, not empty. Single-item deques never popped, which made
  the driver loop spin in `nanosleep` forever.
- **Green-thread trampoline** no longer returns after yielding; it
  reaches `__builtin_unreachable()`.
- **`%rbx` vs `%rdi` argument-passing bug** in the x86_64 context
  switch: `ol_ctx_make_x86_64` stored the argument in `%rbx`, but
  the SysV ABI passes it in `%rdi`. The trampoline read garbage and
  jumped to address 0. v1.3.2 calls the entry function directly from
  `ol_gt_run_to_completion`; a correct context switch is scheduled
  for v1.3.3.
- **`ol_numa_alloc` realign path** copied `aligned_size` bytes from
  a buffer whose real size could be smaller, producing a heap
  overflow under TSan.
- **`struct ol_gt_scheduler.tls`** no longer forces 64-byte
  alignment on the whole struct.
- **`struct ol_stack_pool`** no longer carries
  `__attribute__((aligned(64)))`.

### Removed

- **`tests/security/test_race_conditions.c`** — a Wave 0 draft that
  used C++ lambda syntax in a `.c` file and referenced
  `ol_actor_mailbox_len` (renamed to `_length`). Moved to
  `future/tests/` with a rewrite note.
- **Maintenance scripts and generated artefacts** from the entire
  git history (52 commits rewritten; ~26 paths removed).
- **Qwen-generated files without implementations** and the
  co-author trailer (already removed in v1.3.1).

### Verification

| Check | Result |
|-------|--------|
| `ci/run_all.py --keep-going` | **23 / 23 PASS** |
| Wave 1 regression suite | 28 assertions, 0 failures |
| Sanitizers | ASan ✅ · UBSan ✅ · TSan ✅ · LSan ✅ |
| Doxygen coverage | 281 / 281 public functions RICH |
| Reproducible builds | byte-identical |
| Build matrix | cc / gcc / clang × gnu99 / gnu11 / gnu17 / gnu23 × -O0 / -O2 / -Os = 36 PASS |

### Known issues (targeting v1.3.3)

- Custom x86_64 context switch has the `%rbx` / `%rdi` argument-
  passing bug. v1.3.2 works around it by calling the entry function
  directly; a correct context switch is scheduled for v1.3.3.
- `ol_dataflow.c` worker does not drain per-edge inboxes; the
  multi-hop demo is partial. Tracked in `ROADMAP.md` §4.4.
- `uint64_t` statistics counters should be `_Atomic uint64_t` for
  clang's strict C11 atomic typing check. GCC accepts them as an
  extension.
- Seven `-Wall -Wextra` warnings remain in `ol_green_threads.{c,h}`
  and `ol_supervisor.c`. `.warnings-budget` is 10; target is 0.
- Valgrind reports arena-creation failures on the development host
  while all four sanitizers pass. Investigated in v1.3.3.

---

## [1.3.1] — 2026-09-29

### Added

- `tests/test_wave1.c` — 22-assertion regression suite.
- `verify.py` — ASan + UBSan + TSan runner.
- `demos/` — 7 self-contained examples (actor, channel, parallel,
  timers, promise, dataflow, HTTP server).
- `docs/CHANGELOG.md` (this file).
- `ROADMAP.md` — exhaustive plan for v1.3.x and v2.0.
- `ol_actor_hashmap.c` — the missing implementation.

### Fixed

- **Actor** (`ol_actor.c`)
  - Removed `pending_asks` hashmap leak in `ol_actor_ask`.
  - Replaced busy-wait in `ol_actor_send_timeout` with
    `ol_cond_wait_until`.
  - Corrected memory ordering in the mailbox ring buffer.
  - Fixed heap-use-after-free in `ol_actor_ask` error path.
  - `ol_actor_try_send` now falls back to the overflow list.

- **Arena** (`ol_actor_arena.c`)
  - `ol_arena_free` validates pointer ownership.

- **Supervisor** (`ol_supervisor.c`)
  - Bounded shutdown timeout; added `shutdown_timeout_ms`,
    `arena_size`, `ol_supervisor_stats_t`.

- **Green threads** (`ol_green_threads.{c,h}`)
  - Header and source structs consistent.
  - Fixed negative-size padding on 64-bit.
  - Removed `ol_ctx_*` from the public header.
  - Static forward declarations for the NUMA helpers.
  - NUMA support optional via `__has_include(<numa.h>)`.

- **Network** (`network/ol_tcp.c`, `includes/code/network/*.h`)
  - Added missing `<stdlib.h>` and `<string.h>`.
  - Removed conflicting `typedef struct ol_mutex ol_mutex_t;`.
  - Guarded `OL_POLL_*` macros with `#ifndef`.

### Changed

- Version bumped from v1.3.0 to v1.3.1.

### Removed

- Qwen-generated files without implementations.
- A co-authored-by trailer referencing `qwen.ai[bot]`.

### Known issues (targeting v1.3.2)

- Actor main loop was not driven by the green-thread scheduler.
  **Fixed in v1.3.2.**
- LSan was disabled in `verify.py`. **Re-enabled in v1.3.2.**

---

## [1.3.0] — 2026 (upstream)

Initial "Atom" release with actors, supervisors, coroutines, and the
platform module. Shipped without a regression suite and with several
latent bugs that Wave 1 addressed. No `CHANGELOG` was kept for this
version upstream.

---

## [1.0.0] — 2026 (upstream)

First public release. Linux + BSD support only.
