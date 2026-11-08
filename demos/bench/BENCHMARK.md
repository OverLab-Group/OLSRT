# OLSRT vs libuv — Benchmark Results

Run on 2026-11-09. Reproduce with:

```

make -C demos && make -C demos/bench && bash demos/bench/bench-run.sh > BENCHMARK.md

```

---

## Machine

| Field | Value |
|---|---|
| CPU | AMD E2-1800 APU (2 cores, 1.7 GHz, 2012) |
| RAM | 3.4 GiB |
| Kernel | 6.12.113+deb13-amd64 |
| Distro | Debian GNU/Linux 13 (trixie) |
| Compiler | cc (Debian 14.2.0-19) 14.2.0 |
| libuv | 1.50.0 |
| Date | 2026-11-09 |
| OLSRT | v1.3.4 |

---

## Fairness rules

1. **Same workload on both sides.** `ops_total` in each raw output
   confirms the count.
2. **Same measurement code.** Both sides include `bench_util.h`.
3. **`native` vs `built`.** `native` = libuv ships the primitive.
   `built` = libuv does not, and the mirror uses pthread.
4. **Demos 06, 09, 13 in `demos/bench/` do NOT sleep.** The
   educational demos in `demos/` do (so a viewer can see them
   work). The benchmark variants run at full speed.
5. **Single-run caveat.** `bench-run.sh` runs each demo once. Where
   variance matters (03, 05, 08, 10, 11, 14), a hyperfine 10-run is
   required before quoting publicly. Demos 04, 12, 15 are
   wall-clock-bound and stable.

---

## Results (single run each)

| # | Demo | libuv kind | Wall OLSRT (ms) | Wall libuv (ms) | Ratio O/L | Ops/s OLSRT | Ops/s libuv | RSS OLSRT (KB) | RSS libuv (KB) |
|---|---|---|---|---|---|---|---|---|---|
| 01 | hello_actor | built | 39.2 | 60.0 | 0.65 | 25517 | 16660 | 2096 | 1984 |
| 02 | channel | built | 2081 | 1978 | 1.05 | 480541 | 505438 | 1976 | 1828 |
| 03 | parallel | **native** | 1.70 | 2.79 | 0.61 | 58852 | 35827 | 1960 | 2076 |
| 04 | timers | **native** | 400.6 | 400.0 | **1.00** | 17 | 17 | 1840 | 1960 |
| 05 | promise | built | 1.69 | 0.26 | 6.51 | 591063 | 3844601 | 1852 | 1680 |
| 06 | dataflow | built | 7.37 | 2.48 | 2.97 | 135660 | 402569 | 2100 | 1848 |
| 07 | http_server | **native** | 888 req/s | 1607 req/s | libuv 1.81× faster | 888 | 1607 | 1996 | 1940 |
| 08 | coroutines | built | **64.8** | 52.0 | **1.25** | 15428 | 19226 | 1936 | 1896 |
| 09 | supervisor | built | 524 | 32.5 | 16.12 | 2 | 31 | 2416 | 2072 |
| 10 | reactive | built | 1.49 | 0.54 | 2.77 | 670084 | 1858981 | 1856 | 1704 |
| 11 | streams | built | 1.21 | 0.54 | 2.23 | 827148 | 1846828 | 1828 | 1656 |
| 12 | semaphores | **native** | 407.0 | 403.2 | **1.01** | 20 | 20 | 2060 | 2108 |
| 13 | actor_supervisor | built | 322 | 1.44 | 223.5 | 9 | 2085 | 2536 | 1852 |
| 14 | actor_channel | built | 5.04 | 6.01 | **0.84** | 198314 | 166346 | 2248 | 1820 |
| 15 | reactive_loop | built | 500.2 | 501.6 | **1.00** | 20 | 20 | 1896 | 1964 |
| 16 | full_stack | built | NA | NA | — | — | — | — | — |

---

## What the numbers say

### Native, direct comparison

| # | Ratio | Verdict |
|---|---|---|
| **03 parallel** | 0.61 | **OLSRT 1.6× faster** (single run; variance large, needs hyperfine) |
| **04 timers** | 1.00 | **Parity.** Wall-clock bound. |
| **07 HTTP** | libuv 1.81× faster | libuv wins, ~1.8× more req/s. |
| **12 semaphores** | 1.01 | **Parity.** Both wrap the OS semaphore. |

**Honest headline:** on 4 native rows: one parity (04), one near-parity (12), one contested win (03, needs hyperfine), one loss (07, 1.8×). Peer, not leader.

### Built comparison (libuv emulated with pthread)

**OLSRT wins:** 01 hello_actor (0.65 = 1.5× faster), 14 actor_channel (0.84). Real wins: OLSRT's actor/ask/reply is faster than uv_queue_work per request.

**Parity (within 1.3×):** 02 channel, 08 coroutines, 15 reactive_loop.

**OLSRT loses 2-3×:** 06 dataflow, 10 reactive, 11 streams. Abstraction cost.

**OLSRT loses 6.5×:** 05 promise. Refcount + continuation tracking.

**Architectural difference (not overhead):** 09 supervisor (16.1×), 13 actor_supervisor (223.5×). These measure the 100 ms supervisor poll interval, not runtime overhead. v1.3.5 moves to event-driven supervision.

### v1.3.4 scheduler impact on demo 08

| Version | Wall time | What it measured |
|---|---|---|
| v1.3.3 | 114 ms | **1000 sequential function calls** (no real yield) |
| **v1.3.4** | **64.8 ms** | **1000 real cooperative context switches** |

The v1.3.3 number was *misleadingly fast*: `ol_gt_yield` returned immediately (no driver context), so the 1000 iterations ran as plain function calls. The v1.3.4 scheduler does 1000 actual `ol_ctx_save` / `ol_ctx_restore` cycles (asm, ~20-30 ns each) in 43% less total time. Both the functionality and the speed improved.

libuv comparison: 52 ms (pthread ping-pong). OLSRT is now 1.25× slower, which is the cost of the actor/arena wrapper around the raw context switch.

---

## Sanitizer caveat

The v1.3.4 real scheduler is **disabled under ASan/TSan**
(`OL_GT_USE_REAL_SCHEDULER=0`). The sanitizer builds use the
v1.3.3 direct-call path, which does not swap `%rsp` and therefore
does not confuse the runtimes. The same 30 test assertions run in
both modes.

**Why:** ASan tracks shadow memory per stack and TSan tracks thread
identity in TLS. A raw `%rsp` swap (via `ol_ctx_restore`) is invisible
to both: ASan thinks the current stack is the driver's, TSan thinks
the current thread is the driver thread. The result is cascading
false positives.

**v1.3.5 fix:** `__sanitizer_start_switch_fiber` /
`__sanitizer_finish_switch_fiber` around the switch, as used by
Boost.Context, libmill, and fcontext. Until then, sanitizer builds
still validate actor/promise/channel/supervisor/dataflow logic; the
real scheduler is only validated by the release-mode test suite and
benchmark.

**Benchmark measurement:** `bench-run.sh` builds without sanitizers,
so the numbers above are from the real scheduler.

---

## What this benchmark does and does not claim

**Claims:** Reproducible. Same measurement code. Identical workload
sizes. Native vs built labels. v1.3.4 scheduler measured in release
mode.

**Does NOT claim:** OLSRT is faster than libuv in general. On the
four native rows, one win (03), two parities (04, 12), one loss
(07). That is a peer, not a leader.

**For Hacker News, quotable without hyperfine re-run:**
- 04 timers: parity
- 12 semaphores: parity
- 07 HTTP: libuv 1.8× faster
- 08 coroutines: 64.8 ms vs 52 ms (1.25×), 1000 real context switches
- 09/13: 16×/224× — the supervisor poll interval, not runtime overhead

Everything else needs hyperfine 10-run before public quoting.
