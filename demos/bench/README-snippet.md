<!-- Paste this section into the main README.md, after "Demos". -->

## Benchmarks

`demos/bench/` compares OLSRT's primitives against equivalent workloads
on [libuv](https://libuv.org/), the event loop that powers Node.js.
Every benchmark is reproducible from the repo; no external harnesses
are required beyond `libuv-dev`.

**Full methodology and results:** [`demos/bench/BENCHMARK.md`](demos/bench/BENCHMARK.md)

### What is compared

16 demos, 16 libuv mirrors. Six are direct comparisons of the same
primitive (thread pool, timers, TCP, semaphores). Ten compare an
OLSRT built-in primitive against the closest libuv + pthread
equivalent, because libuv does not ship actors, channels, promises,
supervisors, dataflow graphs, or reactive streams.

The table below lists every row and labels each comparison as
`direct` or `emulated`. Emulated rows measure "cost of the same
workload in libuv's model", not "cost of the same primitive". That
is the honest framing; `BENCHMARK.md` says so in every paragraph.

### Headline results

*(Run `demos/bench/BENCHMARK.md` methodology to reproduce. Numbers
below are from the run dated in that file.)*

| Row | OLSRT | libuv | Fairness |
|-----|-------|-------|----------|
| Startup (process, ms) | TBD | TBD | OLSRT pays for arena + driver thread. Real cost. |
| 03 parallel (100 tasks, 4 workers) | TBD | TBD | **direct** |
| 04 timers (400 ms) | TBD | TBD | **direct** |
| 07 HTTP (100 req, `ab -c 50`) | TBD | TBD | **direct** |
| 12 semaphores (8 threads, cap 2) | TBD | TBD | **direct** |
| 02 channel (1M msgs) | TBD | TBD | emulated (pthread) |
| 16 full_stack (`ab -c 20`) | TBD | TBD | emulated |

### What this benchmark does *not* claim

- That OLSRT is "faster" than libuv. It is not. libuv is older,
  more portable, and proven at every scale.
- That the ten emulated rows are unfair to libuv. They are unfair
  to both sides: libuv users write more code to model the same
  thing, and OLSRT users carry an abstraction that is not free.
- That these numbers replace a serious performance study. They are
  a starting point: reproducible, honest, and pointing at demos
  anyone can run themselves.

### Reproduce it

```

make -C demos
make -C demos/bench/libuv

# then follow demos/bench/README.md

```

The methodology in `BENCHMARK.md` includes the machine spec, the
compiler, the libuv version, and the repeat count. Without those,
a benchmark is not reproducible.
