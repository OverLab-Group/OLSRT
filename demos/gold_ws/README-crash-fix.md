# gold_ws crash fix — v1.3.2 patch

## Symptom

`ab -n 1000 -c 20 http://localhost:8080/style.css` against `gold_ws`
produces a nondeterministic `Segmentation fault (core dumped)` after
a few hundred requests. The number of successful requests varies from
run to run (752, 322, ...) and the client reports
`Connection reset by peer (104)`.

## Root cause

Every request creates a fresh actor. Every actor creates a fresh
process. Every process spawns a fresh driver thread. Every driver
thread inserts its scheduler into a global singly-linked list on
startup and removes it on shutdown.

The removal was not thread-safe:

```
Thread 1 (shutting down A):
    prev = NULL, curr = A
    CAS Head  from A to B
    free(A)

Thread 2 (shutting down B, walking from old Head):
    prev = A
    curr = B          <- B == g_thread_scheduler, so we stop here
    A->next = B->next <- WRITE TO FREED MEMORY
```

Under load, this write lands in freed memory and corrupts the heap.
The corruption is nondeterministic; sometimes it SEGVs immediately,
sometimes it survives a few hundred more requests.

The list itself was only ever read by `ol_gt_work_steal()`, and the
v1.3.2 driver loop never calls it. So there is nothing to lose by
leaving the list empty.

## Fix

1. `patch_scheduler_list.py` disables the insert and remove in
`src/code/streams/ol_green_threads.c`. The list is left empty and
work stealing finds nothing, exactly as in the current driver
architecture.
2. `router.c` replaces the process-wide `static char file_buf[16MB]`
with a per-call `malloc`. The static buffer was shared by every
worker thread and raced under concurrent static-file requests.

## How to apply

```
cd ~/Projects/OverLab/Z__ECOSYSTEMS/"OverLab Tech"/olsrt
python3 patch_scheduler_list.py
cp <this directory>/router.c demos/gold_ws/router.c
cd demos/gold_ws
make clean && make
./gold_ws
```

In another terminal:

```
ab -n 5000 -c 50 http://localhost:8080/style.css
```

Expect `Complete requests: 5000, Failed requests: 0`.

## If it still crashes

Run under gdb and capture a backtrace:

```
cd demos/gold_ws
gdb -batch -ex run -ex "thread apply all bt full" ./gold_ws \
    2>&1 | tee /tmp/gdb.log
```

Then trigger load from another terminal. Send `/tmp/gdb.log`.
The `thread apply all bt full` shows which thread crashed and where.

For a faster reproducer, lower the concurrency and enable ASan:

```
cd ../..
make CC="gcc -fsanitize=address -g -O1" linux
cd demos/gold_ws
make CC="gcc -fsanitize=address -g -O1"
ASAN_OPTIONS=abort_on_error=1:halt_on_error=1 ./gold_ws
```

ASan will name the exact line that reads or writes freed memory.

## What is still scheduled for v1.3.3

- Correct x86_64 context switch (the `%rbx`/`%rdi` argument bug).
v1.3.2 runs the green-thread entry on the driver thread's stack
directly, which is correct but loses the ability to multiplex
multiple green threads onto one OS thread.
- Proper work-stealing list with refcounts or a global mutex.
- `_Atomic uint64_t` for the statistics counters.