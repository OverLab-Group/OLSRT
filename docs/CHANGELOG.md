# Changelog

All notable changes to OLSRT are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/), and the
project adheres to [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Planned for v1.3.2 — Actor Scheduler
- Drive the green-thread scheduler from `ol_process_create`.
- Re-enable LSan in `verify.py` once the actor lifecycle is closed.
- New test: `test_actor_runs_automatically`.

See `ROADMAP.md` §5 for full details.

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
  - Corrected memory ordering in the mailbox ring buffer
    (`__ATOMIC_ACQUIRE` / `__ATOMIC_RELEASE` instead of plain reads).
  - Fixed heap-use-after-free in `ol_actor_ask` error path (the
    envelope is freed by `ol_actor_send` on failure, so we must not
    touch it afterwards).
  - `ol_actor_try_send` now falls back to the overflow list; previously
    it returned "would block" as soon as the ring buffer filled,
    misreporting a 3/4-full mailbox as full.

- **Arena** (`ol_actor_arena.c`)
  - `ol_arena_free` validates pointer ownership before reading the
    allocation header.

- **Supervisor** (`ol_supervisor.c`)
  - Bounded shutdown timeout; stuck children no longer block teardown.
  - Added `shutdown_timeout_ms` to config, `arena_size` to child spec,
    and the `ol_supervisor_stats_t` type.

- **Green threads** (`ol_green_threads.{c,h}`)
  - Header and source structs are now consistent
    (`struct ol_work_stealing_queue` field renamed to `array`).
  - Fixed negative-size padding bug on 64-bit (`struct ol_stack_pool`).
  - Removed `ol_ctx_*` declarations from the public header (they are
    static in the source).
  - Added static forward declarations for NUMA helpers.
  - NUMA support is optional via `__has_include(<numa.h>)`.

- **Network** (`network/ol_tcp.c`, `includes/code/network/*.h`)
  - Added missing `<stdlib.h>` and `<string.h>`.
  - Removed conflicting `typedef struct ol_mutex ol_mutex_t;` from
    `ol_tcp.h` and `ol_udp.h`.
  - Guarded `OL_POLL_*` macros in `ol_poller.h` with `#ifndef`.

### Changed
- Version bumped from v1.3.0 to v1.3.1 in `v1.3.1` and
  `ol_green_threads.h`.

### Removed
- Qwen-generated files without implementations:
  `ol_actor_isolation.{c,h}`, `ol_actor_serialization.{c,h}`.
- A co-authored-by trailer referencing `qwen.ai[bot]` was scrubbed from
  history via `git-filter-repo`.

### Known issues (targeting v1.3.2 / v1.3.3)
- The actor main loop is not driven by the green-thread scheduler;
  demo 01 pumps the mailbox manually.
- LSan is disabled in `verify.py` because of the above.
- `ol_memwatch.c:406` has a use-after-free flagged by GCC.
- `ol_dataflow.c`'s worker does not drain edge inboxes (demo 06 partial).
- Cosmetic warnings remain in several files (see `ROADMAP.md` §4.3).

---

## [1.3.0] — 2026 (upstream)

Initial "Atom" release with actors, supervisors, coroutines, and the
platform module. Shipped without a regression suite and with several
latent bugs that Wave 1 addressed. No `CHANGELOG` was kept for this
version upstream.

---

## [1.0.0] — 2026 (upstream)

First public release. Linux + BSD support only.
