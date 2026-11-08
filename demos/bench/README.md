# OLSRT Benchmark Suite

This directory contains **reproducible benchmarks** comparing OLSRT's
primitives against equivalent workloads on [libuv](https://libuv.org/),
the event loop that powers Node.js.

**Read this first.** The comparisons are *fair but not symmetric*.
libuv is an event loop and thread pool. OLSRT is a concurrency
substrate with actors, channels, promises, supervisors, dataflow
graphs, and reactive streams. For primitives libuv has (event loop,
timers, TCP, work queue, semaphores), the comparison is direct. For
primitives libuv does **not** have, the libuv demo is the closest
equivalent built from libuv primitives + pthread, and the numbers are
labelled as such.

## Directory layout

```

demos/bench/
├── README.md              (this file)
├── BENCHMARK.md           (results table + analysis; filled after a run)
├── README-snippet.md      (paste into the main README.md)
└── libuv/
├── README.md          (equivalence notes per demo)
├── Makefile
├── bench_util.h       (timing / RSS / CPU helpers)
├── bench_channel.h    (thread-safe FIFO for the demos that need one)
├── 01_hello_actor.c
├── ... 16_full_stack.c

```

The OLSRT side lives in `demos/` at the project root (`01_hello_actor`,
`02_channel`, ...). No files there change for the benchmark.

## Requirements

- **libuv**: `sudo apt install libuv1-dev` (Debian/Ubuntu),
  `brew install libuv` (macOS), `pkg install libuv` (FreeBSD).
- **OLSRT**: `make -C demos` from the project root.
- **Benchmark harness**: `hyperfine` (recommended),
  `/usr/bin/time -v`, or your own shell loop.

Verify libuv is discoverable:

```

pkg-config --modversion libuv

```

## Build

```

# OLSRT side

cd demos && make            # produces libolsrt.so + 16 demo binaries

# libuv side

cd bench/libuv && make      # produces 16 equivalent binaries

```

## Run one pair

```

# OLSRT

./demos/01_hello_actor

# libuv

./demos/bench/libuv/01_hello_actor

```

Each demo prints its own metrics at the end:

```

--- metrics ---
wall_time_ms:      <internal elapsed>
cpu_user_ms:       <user CPU>
cpu_sys_ms:        <system CPU>
rss_kb:            <VmRSS from /proc/self/status>

```

For a full comparison including process startup and peak RSS:

```

/usr/bin/time -v ./demos/01_hello_actor 2>&1 | grep -E "Elapsed|Maximum resident"

```

## Run all 16 pairs

```

# 01–06, 08–15 (exit on their own)

for n in 01_hello_actor 02_channel 03_parallel 04_timers 05_promise 
06_dataflow 08_coroutines 09_supervisor 10_reactive 
11_streams 12_semaphores 13_actor_supervisor 
14_actor_channel 15_reactive_loop; do
echo "=== OLSRT <span class="katex"><span class="katex-mathml"><math xmlns="http://www.w3.org/1998/Math/MathML"><semantics><mrow><mi>n</mi><mo>=</mo><mo>=</mo><mo>=</mo><mi mathvariant="normal">"</mi><mi mathvariant="normal">/</mi><mi>u</mi><mi>s</mi><mi>r</mi><mi mathvariant="normal">/</mi><mi>b</mi><mi>i</mi><mi>n</mi><mi mathvariant="normal">/</mi><mi>t</mi><mi>i</mi><mi>m</mi><mi>e</mi><mo>−</mo><mi>v</mi><mi mathvariant="normal">.</mi><mi mathvariant="normal">/</mi><mi>d</mi><mi>e</mi><mi>m</mi><mi>o</mi><mi>s</mi><mi mathvariant="normal">/</mi></mrow><annotation encoding="application/x-tex">n ==="
    /usr/bin/time -v ./demos/</annotation></semantics></math></span><span class="katex-html" aria-hidden="true"><span class="base"><span class="strut" style="height:0.4306em;"></span><span class="mord mathnormal">n</span><span class="mspace" style="margin-right:0.2778em;"></span><span class="mrel">===</span><span class="mspace" style="margin-right:0.2778em;"></span></span><span class="base"><span class="strut" style="height:1em;vertical-align:-0.25em;"></span><span class="mord">"/</span><span class="mord mathnormal">u</span><span class="mord mathnormal" style="margin-right:0.02778em;">sr</span><span class="mord">/</span><span class="mord mathnormal">bin</span><span class="mord">/</span><span class="mord mathnormal">t</span><span class="mord mathnormal">im</span><span class="mord mathnormal">e</span><span class="mspace" style="margin-right:0.2222em;"></span><span class="mbin">−</span><span class="mspace" style="margin-right:0.2222em;"></span></span><span class="base"><span class="strut" style="height:1em;vertical-align:-0.25em;"></span><span class="mord mathnormal" style="margin-right:0.03588em;">v</span><span class="mord">./</span><span class="mord mathnormal">d</span><span class="mord mathnormal">e</span><span class="mord mathnormal">m</span><span class="mord mathnormal">os</span><span class="mord">/</span></span></span></span>n 2>&1 | tail -8
echo "=== libuv <span class="katex"><span class="katex-mathml"><math xmlns="http://www.w3.org/1998/Math/MathML"><semantics><mrow><mi>n</mi><mo>=</mo><mo>=</mo><mo>=</mo><mi mathvariant="normal">"</mi><mi mathvariant="normal">/</mi><mi>u</mi><mi>s</mi><mi>r</mi><mi mathvariant="normal">/</mi><mi>b</mi><mi>i</mi><mi>n</mi><mi mathvariant="normal">/</mi><mi>t</mi><mi>i</mi><mi>m</mi><mi>e</mi><mo>−</mo><mi>v</mi><mi mathvariant="normal">.</mi><mi mathvariant="normal">/</mi><mi>d</mi><mi>e</mi><mi>m</mi><mi>o</mi><mi>s</mi><mi mathvariant="normal">/</mi><mi>b</mi><mi>e</mi><mi>n</mi><mi>c</mi><mi>h</mi><mi mathvariant="normal">/</mi><mi>l</mi><mi>i</mi><mi>b</mi><mi>u</mi><mi>v</mi><mi mathvariant="normal">/</mi></mrow><annotation encoding="application/x-tex">n ==="
    /usr/bin/time -v ./demos/bench/libuv/</annotation></semantics></math></span><span class="katex-html" aria-hidden="true"><span class="base"><span class="strut" style="height:0.4306em;"></span><span class="mord mathnormal">n</span><span class="mspace" style="margin-right:0.2778em;"></span><span class="mrel">===</span><span class="mspace" style="margin-right:0.2778em;"></span></span><span class="base"><span class="strut" style="height:1em;vertical-align:-0.25em;"></span><span class="mord">"/</span><span class="mord mathnormal">u</span><span class="mord mathnormal" style="margin-right:0.02778em;">sr</span><span class="mord">/</span><span class="mord mathnormal">bin</span><span class="mord">/</span><span class="mord mathnormal">t</span><span class="mord mathnormal">im</span><span class="mord mathnormal">e</span><span class="mspace" style="margin-right:0.2222em;"></span><span class="mbin">−</span><span class="mspace" style="margin-right:0.2222em;"></span></span><span class="base"><span class="strut" style="height:1em;vertical-align:-0.25em;"></span><span class="mord mathnormal" style="margin-right:0.03588em;">v</span><span class="mord">./</span><span class="mord mathnormal">d</span><span class="mord mathnormal">e</span><span class="mord mathnormal">m</span><span class="mord mathnormal">os</span><span class="mord">/</span><span class="mord mathnormal">b</span><span class="mord mathnormal">e</span><span class="mord mathnormal">n</span><span class="mord mathnormal">c</span><span class="mord mathnormal">h</span><span class="mord">/</span><span class="mord mathnormal" style="margin-right:0.01968em;">l</span><span class="mord mathnormal">ib</span><span class="mord mathnormal" style="margin-right:0.03588em;">uv</span><span class="mord">/</span></span></span></span>n 2>&1 | tail -8
done

```

`07_http_server` and `16_full_stack` start a server and wait for
requests. Use `ab`, `wrk`, or `hey` in a second terminal:

```

# Terminal 1 — start whichever server you are measuring

./demos/07_http_server           # or ./demos/bench/libuv/07_http_server

# Terminal 2 — issue N requests

ab -n 10000 -c 50 http://127.0.0.1:8080/

```

## Methodology

- **Repeats**: each demo is expected to be run **5 times**; use the
  median for the table in `BENCHMARK.md`. The first run may include
  filesystem cache misses; discard it.
- **CPU pinning** (optional but recommended):
  `taskset -c 0,1 ./<demo>`. Document if you pin.
- **Environment**: do not run other builds or tests during a
  measurement. On laptops, disable the CPU frequency governor
  (`sudo cpupower frequency-set -g performance` on Linux).
- **Peak RSS** for short-lived demos is measured by
  `/usr/bin/time -v`'s `Maximum resident set size`. For long-running
  HTTP servers, `ps -o rss= -p <pid>` after a warmup.
- **Startup**: measured as the time from process `exec` to the demo's
  first `bench_now_ns()` sample minus the demo's internal elapsed.
  `/usr/bin/time -v`'s `Elapsed (wall clock) time` includes process
  startup, `libolsrt.so` dynamic linking, and OLSRT's arena
  allocations. This is a *real cost* and is reported as such.

## How to fill `BENCHMARK.md`

1. Run each pair 5 times (as above) on a quiet machine.
2. Paste the median row into the table in `BENCHMARK.md`.
3. Include the machine spec printed at the top of `BENCHMARK.md`.
4. Do **not** delete rows where OLSRT lost. The honesty is the point.

## What the numbers mean

| Demo | What it measures | Fairness |
|---|---|---|
| 01 hello_actor | Per-request async round-trip through a mailbox | Partial — libuv has no actor; equivalent is `uv_queue_work` per request |
| 02 channel | 1M-message FIFO throughput between two threads | Partial — libuv has no channel; equivalent is pthread mutex+condvar |
| 03 parallel | 100 CPU-bound tasks on a 4-worker pool | Direct — both use a thread pool |
| 04 timers | One-shot + periodic accuracy over 400 ms | Direct |
| 05 promise | Resolution / rejection / continuation plumbing | Partial — libuv has no promise; equivalent is callback+condvar |
| 06 dataflow | Source → doubler → sink, 5 items | Partial — libuv has no dataflow; equivalent is chained pthread queues |
| 07 http_server | HTTP/1.1 request latency and throughput | Direct (measure with `ab` / `wrk`) |
| 08 coroutines | Cooperative producer ↔ consumer, 8 items | Partial — libuv has no coroutines; equivalent is pthread ping-pong |
| 09 supervisor | Crash detection + restart of 3 children | Partial — libuv has `uv_spawn` but no restart policy |
| 10 reactive | Timer-driven subject, 5 emissions, 1 subscriber | Partial |
| 11 streams | Same with backpressure (demand = 1) | Partial |
| 12 semaphores | 8 threads, concurrency limit 2 | **Direct** — both have a semaphore |
| 13 actor_supervisor | 3 workers, one crashes, restart | Partial |
| 14 actor_channel | Producer → channel → consumer | Partial |
| 15 reactive_loop | Timer feeds subject, 5 ticks | Partial |
| 16 full_stack | TCP server → worker → response | Partial |

Six of the sixteen are direct equivalents (03, 04, 07, 12, plus the
timing portion of any demo). The other ten are honest emulations and
are labelled that way in both the demo source and the results table.

## Reporting

After filling `BENCHMARK.md`, copy the results section (without the
methodology preamble) into the main `README.md` via the
`README-snippet.md` file in this directory. Include the machine spec
and the date — benchmarks without those are not reproducible.
