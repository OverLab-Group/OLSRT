# libuv mirrors of the 16 OLSRT demos

These are the closest equivalent to each OLSRT demo, built with
libuv + pthread. They are **not** an attempt to make libuv look slow
or to make OLSRT look fast. They are an attempt to answer the
question: *"If I had to build this in libuv, how much code would it
take, and how fast would it run?"*

## Equivalence notes

| # | OLSRT demo | libuv equivalent | Fairness |
|---|---|---|---|
| 01 | `hello_actor` | `uv_queue_work` per request; results collected on the loop thread | Partial — libuv has no actor model |
| 02 | `channel` | pthread mutex + condvar + linked list | Partial — libuv has no channel |
| 03 | `parallel` | `uv_queue_work` with the default UV thread pool | **Direct** |
| 04 | `timers` | `uv_timer_t` | **Direct** |
| 05 | `promise` | custom struct + pthread condvar | Partial — libuv has no promise |
| 06 | `dataflow` | two `bench_channel_t`s and one worker thread | Partial — libuv has no dataflow |
| 07 | `http_server` | `uv_tcp_t` accept loop | **Direct** |
| 08 | `coroutines` | two pthreads, ping-pong via condvar | Partial — libuv has no coroutines |
| 09 | `supervisor` | `uv_spawn` + custom restart on exit | Partial — libuv has no supervisor |
| 10 | `reactive` | `uv_timer_t` + user callback | Partial |
| 11 | `streams` | `uv_timer_t` + demand counter | Partial |
| 12 | `semaphores` | `uv_sem_t` | **Direct** |
| 13 | `actor_supervisor` | pthreads + restart loop | Partial |
| 14 | `actor_channel` | `bench_channel_t` + worker thread | Partial |
| 15 | `reactive_loop` | `uv_timer_t` | Partial |
| 16 | `full_stack` | `uv_tcp_t` + worker thread + queue | Partial |

## Build

```

make            # builds all 16
make clean

```

Requires `libuv-dev` (or equivalent). Override the compiler:

```

make CC=clang

```

## Runtime dependencies

libuv's default thread pool is `UV_THREADPOOL_SIZE`, default 4. Demo 03
uses it directly; demos 01 and 06 use it as well. To match OLSRT's
4-worker default, no override is needed. To measure a different pool
size:

```

UV_THREADPOOL_SIZE=8 ./03_parallel

```

## What each demo prints

Every demo ends with a `--- metrics ---` block:

```

wall_time_ms:   the demo's own internal elapsed time
cpu_user_ms:    user-mode CPU consumed by this process
cpu_sys_ms:     kernel-mode CPU consumed by this process
rss_kb:         current VmRSS from /proc/self/status

```

`/usr/bin/time -v` adds process-level numbers
(`Elapsed (wall clock)`, `Maximum resident set size`). Use both
together: the internal numbers exclude process startup and dynamic
linking; the `/usr/bin/time -v` numbers include them.

## Notes on honesty

- **Startup:** OLSRT will be slower. It loads `libolsrt.so`, allocates
  per-actor arenas, and spawns a driver thread per process. libuv
  does `uv_loop_init`. This is a real cost, printed in
  `BENCHMARK.md`.
- **Peak RSS:** OLSRT will be higher per "actor" because the arena
  exists. libuv has no unit, so "per-handle" is not comparable.
- **Throughput on direct comparisons** (03, 04, 07, 12): this is
  where the numbers should be close. If OLSRT loses on 03 or 12,
  report the delta.
