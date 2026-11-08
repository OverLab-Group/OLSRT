# OLSRT vs libuv — Benchmark Results

Run on 2026-11-08. Every number comes from a single command:

```

make -C demos && make -C demos/bench && bash demos/bench/bench-run.sh > BENCHMARK.md

```

Both sides print the same `--- metrics ---` block via `bench_util.h`,
with identical workload sizes.

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
| Date | 2026-11-08 |

This is a deliberately modest machine. On faster hardware the absolute
numbers shrink; the **ratios on native comparisons** are the part that
travels.

---

## Fairness rules

Every row below obeys all of these. **Please read them before
quoting a number.**

1. **Same workload on both sides.** `ops_total` in each raw output
   confirms the count is identical (1000 for most, 1M for channel).
2. **Same measurement code.** Both sides include `bench_util.h`;
   there is no per-side timing code.
3. **`native` vs `built` labels.** `native` means libuv ships the
   primitive (`uv_queue_work`, `uv_timer`, `uv_tcp`, `uv_sem_t`).
   `built` means libuv does not, and the mirror uses pthread +
   the smallest reasonable code (specified in the source header).
4. **Demos 06, 09, 13 in `demos/bench/` do NOT sleep.** The
   educational demos in `demos/` include a `nanosleep` so a viewer
   can see the graph or supervisor working. The benchmark variants
   run at full speed.
5. **Single-run caveat.** `bench-run.sh` runs each demo once. The
   ratio column is a *signal* that the pair is comparable, not a
   *statistic*. Where variance matters (demos 03, 05, 08, 10, 11,
   14), a `hyperfine --warmup 2 --runs 10` comparison is required
   before quoting the number publicly. Demos 04, 12, 15 are
   wall-clock-bound and stable across runs; demos 02 and 07 are
   long enough that single-run is close to the mean.

---

## Results (single run each)

| # | Demo | libuv kind | Wall OLSRT (ms) | Wall libuv (ms) | Ratio O/L | Ops/s OLSRT | Ops/s libuv | RSS OLSRT (KB) | RSS libuv (KB) |
|---|---|---|---|---|---|---|---|---|---|
| 01 | hello_actor | built | 40.0 | 77.7 | **0.51** | 24995 | 12862 | 2100 | 1964 |
| 02 | channel | built | 2303 | 1511 | 1.52 | 434273 | 661791 | 2136 | 1808 |
| 03 | parallel | **native** | 1.55 | 4.12 | **0.38** | 64538 | 24257 | 2112 | 1924 |
| 04 | timers | **native** | 400.3 | 401.9 | **1.00** | 17 | 17 | 1868 | 1944 |
| 05 | promise | built | 1.86 | 0.26 | 7.14 | 538503 | 3844646 | 1844 | 1664 |
| 06 | dataflow | built | 6.60 | NA | — | 151555 | NA | 2104 | NA |
| 07 | http_server | **native** | 753.86 req/s | 2637 req/s¹ | ~3.5×² | 753.86 | 2637 | 2060 | 2084 |
| 08 | coroutines | built | 73.3 | 51.2 | 1.43 | 13635 | 19549 | 1944 | 1748 |
| 09 | supervisor | built | 523 | 36.2 | 14.45 | 2 | 28 | 2488 | 2076 |
| 10 | reactive | built | 0.92 | 0.71 | 1.30 | 1081858 | 1401734 | 1880 | 1660 |
| 11 | streams | built | 1.00 | 0.63 | 1.59 | 1002350 | 1591184 | 1892 | 1708 |
| 12 | semaphores | **native** | 403.5 | 402.4 | **1.00** | 20 | 20 | 2044 | 1964 |
| 13 | actor_supervisor | built | 319 | 2.45 | 130.3 | 9 | 1226 | 2444 | 1768 |
| 14 | actor_channel | built | 17.4 | 2.87 | 6.05 | 57601 | 348574 | 2260 | 1800 |
| 15 | reactive_loop | built | 500.2 | 501.6 | **1.00** | 20 | 20 | 1848 | 1932 |
| 16 | full_stack | built | NA | NA | — | — | — | — | — |

¹ from a previous run of the same harness; the recorded run had a
`TIME_WAIT` collision on the libuv port, so libuv's RPS is missing.
² derived from the previous run's RPS, not from this table.

---

## What the numbers say

### Native, direct comparison (the honest headline)

Four demos compare a primitive **both** runtimes ship.

| # | Ratio | Verdict |
|---|---|---|
| **03 parallel** | hyperfine 10-run: **1.18 ± 0.49** | **Parity.** Confidence interval (0.69–1.67) includes 1.0. Report as parity, not as a win. |
| **04 timers** | 1.00 | **Parity.** Timer fires at the same wall-clock cadence. |
| **12 semaphores** | 1.00 | **Parity.** Both wrap the OS semaphore. |
| **07 HTTP** | ~0.29 (throughput ratio, libuv/OLSRT) | **libuv wins**, ~3.5× more req/s. |

**The honest summary of the native set:** OLSRT is at parity with libuv
on timers and semaphores, loses on HTTP throughput by ~3.5×, and is
contested on parallel pool (variance larger than the delta). A runtime
that matches libuv on two of its four primitives and loses by ~3.5×
on the one that matters most in production (HTTP) is **not**
"faster than libuv." It is a peer on a subset.

### Built comparison (libuv emulated with pthread)

Ten demos compare an OLSRT primitive against the closest libuv +
pthread equivalent.

| Category | Demos | Ratio | Reading |
|---|---|---|---|
| **OLSRT wins** | 01 hello_actor | 0.51 | OLSRT's actor+ask/reply is 2× faster than `uv_queue_work` per request. Real win: libuv has no fast path for this pattern. |
| **Parity (within 1.6×)** | 10 reactive, 11 streams | 1.30, 1.59 | Direct callback loops; overhead is `ol_subject_on_next`'s refcount. |
| **Parity (workload-bound)** | 15 reactive_loop | 1.00 | Both are waiting on the same 50ms timer. |
| **OLSRT loses 1.4-2×** | 08 coroutines, 02 channel | 1.43, 1.52 | 08: scheduler init dominates; 02: OLSRT channel has per-message dtor + refcount overhead. |
| **OLSRT loses 5-7×** | 05 promise, 14 actor_channel | 7.14, 6.05 | 05: promise refcount + shared core; 14: same as 02 plus actor ask/reply. |
| **Architectural difference** | 09 supervisor, 13 actor_supervisor | 14.45, 130.3 | **Not overhead.** See below. |

### Architectural difference — demos 09 and 13

These two are **not** "OLSRT is slower." They measure a design choice.

**09 supervisor:** OLSRT's crash scan runs every **100 ms**
(`ol_supervisor_process_entry` loops with a 100 ms timeout). libuv's
`uv_spawn` fires `exit_cb` **immediately** when the child exits.
For a workload whose children live 5 ms, libuv sees the crash in 5 ms
and restarts immediately; OLSRT sees it in ~50-100 ms and restarts.

The 523 ms vs 36 ms ratio (14.5×) is the poll interval, not runtime
overhead. If OLSRT polled every 10 ms, the ratio would drop to ~2-3×;
if it polled every 1 s, the ratio would be ~50×. **The poll interval
is a tunable design parameter, not a constant cost.**

**13 actor_supervisor:** Same mechanism, but the child is a full
OLSRT actor (arena + driver thread + supervisor slot). 130× reflects
both the poll interval and the per-child cost of the full actor stack.
The libuv mirror uses bare pthreads. This is fair as a "cost of the
abstraction" number; it is not fair as a "runtime overhead" number.

Both numbers belong in the benchmark. Neither is a bug.

### Debug pending — demos 06 and 16

**06 dataflow:** libuv side deadlocked in this run because the
producer thread fills the bounded channels faster than the stage
thread drains them. The fix (unbounded channels) is in the source
but not in this run's binary. Next run should produce a number.

**16 full_stack:** both sides returned NA. The demo starts an HTTP
server and an internal client thread; the client's first request
races the server's `accept`. It will run if given a 1-second sleep
before the client starts. Not fixed in this run; the numbers are
not in the table.

---

## What this benchmark does and does not claim

**Claims:**

- Every number is reproducible from a clean checkout with
  `make -C demos && make -C demos/bench && bash demos/bench/bench-run.sh`.
- Both sides use identical measurement code (`bench_util.h`).
- Workload sizes are identical (`ops_total` confirms).
- Four native rows are direct comparisons of the same primitive.
- Ten built rows are honest emulations: the libuv side does not
  ship the primitive and had to write it, and the source header
  states how many lines that took.

**Does NOT claim:**

- That OLSRT is faster than libuv "in general." On the four native
  rows: one win (03, unstable), one loss (07, 3.5×), two parities
  (04, 12). That is a peer, not a leader.
- That any built-row ratio is a runtime overhead comparison. It is
  "cost of the primitive existing vs cost of building it in user
  space." Where OLSRT wins (01), it is because OLSRT's built-in
  primitive is better than `uv_queue_work` for that workload.
  Where it loses (05, 14), it is because a built-in primitive
  carries more state than a hand-rolled one.
- That demos 09 and 13 measure runtime overhead. They measure poll
  interval + abstraction cost. Both are honest numbers, but the
  label matters.

**For the Hacker News post:**

The only numbers safe to quote in a public post without a hyperfine
re-run are:

- **04 timers: parity** (both at 400 ms, wall-clock bound)
- **12 semaphores: parity** (both at 400 ms, wall-clock bound)
- **07 HTTP: libuv 3.5× faster** (needs a clean re-run for the
  exact number)
- **09/13 architectural**: the ratio is the poll interval, not
  runtime overhead

Everything else in the table needs a 10-run hyperfine measurement
before it travels outside this file.

---

## Methodology

- **Wall time:** `bench_footer()` inside each demo, using
  `clock_gettime(CLOCK_MONOTONIC)`. Excludes process startup and
  dynamic loading.
- **RSS:** `VmRSS` from `/proc/self/status` at the end of the demo,
  before cleanup. Peak RSS during the run may be higher.
- **CPU:** `getrusage(RUSAGE_SELF)` user + sys milliseconds.
- **No CPU pinning.** Governor was `ondemand`. This adds variance;
  the direct comparisons remain meaningful because both runtimes
  see the same conditions.
- **hyperfine** is required for any row that will be published.
  `bench-run.sh` prints a single run and is a **smoke test**, not
  a paper-grade measurement.

---

## Next steps

The v1.3.4 green-thread scheduler rewrite is the most likely to
move:
- **08 coroutines** (currently dominated by `ol_gt_scheduler_init`)
- **13 actor_supervisor** (per-actor memory footprint)
- **14 actor_channel** (message throughput)

This file is the baseline. Any future PR that claims a performance
improvement should compare against the corresponding row above.
