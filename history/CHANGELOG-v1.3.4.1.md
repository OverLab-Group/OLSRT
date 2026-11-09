## [1.3.4.1] — 2026-11-09

> **Feature completion for the 1.3.4 series.** Adds goroutine-like
> oroutines and channels on top of the v1.3.4 real scheduler. The
> v1.3.4 release fixed the scheduler first because it was a
> prerequisite; v1.3.4.1 delivers the ORoutines API that the
> original v1.3.4 plan promised.

### Added

#### Oroutines (`oroutines.h` / `oroutines.c`)

- **`or_go(fn, arg)`** — spawn a goroutine-like oroutine. Returns
  immediately; the scheduler runs `fn(arg)` when it is ready.
- **`or_yield()`** — cooperative yield. Marks the current oroutine
  RUNNABLE and switches to the next ready one.
- **`or_run()`** — drive the scheduler until no oroutine can make
  progress. Returns the number of oroutines completed.
- **`or_join(r, deadline)`** — wait for a specific oroutine.
- **`or_cancel(r)`** — cooperative cancellation.
- **`or_self()`** — current oroutine, or NULL from the main thread.

#### Channels (`or_chan_*`)

- **`or_chan_create(elem_size, capacity)`** — create a typed
  channel. Capacity 0 = unbuffered.
- **`or_chan_send(ch, elem)`** — send. Blocks (yields) on a full
  channel.
- **`or_chan_recv(ch, out)`** — receive. Blocks on an empty
  channel.
- **`or_chan_close(ch)`** — unblocks all senders/receivers.
- **`or_chan_destroy(ch)`**, **`or_chan_len`**, **`or_chan_cap`**,
  **`or_chan_is_closed`**.

#### Scheduler primitives (`ol_green_threads.h` / `.c`)

Three new public functions that oroutines builds on:

- **`ol_gt_scheduler_run()`** — multi-gt cooperative driver. Pops
  READY threads from the work-stealing queues and runs each until
  it yields, blocks, or finishes. Returns count of terminal threads.
- **`ol_gt_block(gt)`** — mark WAITING and yield. Called by channel
  send/recv when the operation cannot complete immediately.
- **`ol_gt_wake(gt)`** — CAS WAITING→READY and requeue. Called by
  channel send/recv when a handoff succeeds.

### Changed

- **`OL_VERSION_TWEAK`** added to `includes/runtime/ol_globals.h`
  (currently `1`). `ci/_common.py:read_version` now emits `X.Y.Z.W`
  when TWEAK is non-zero, so `version_check` recognises 4-part
  versions.
- **`ci/version_check.py`** CHANGELOG regex accepts `X.Y.Z.W`.
- **`ROADMAP.md`** — v1.3.4 marked Scheduler-only, v1.3.4.1 marked
  ORoutines. v1.3.5 remains Supervisor 2.0.

### Verification

| Check | Result |
|-------|--------|
| `tests/test_oroutines.c` | 10+ assertions, 6 tests |
| `demos/17_oroutines.c` | 100,000 messages through 11 channels |
| `ci/verify.py` | ASan · UBSan · LSan · TSan clean |
| `ci/version_check.py` | 1.3.4.1 consistent across tree |

### Known limitations

- **Single-threaded.** All oroutine and channel calls must happen
  on the same OS thread. For cross-thread communication, use
  `ol_channel` or `ol_actor`.
- **No `or_select`.** Multi-channel select is deferred to v1.3.4.2.
  For now, use one goroutine per channel and merge via a shared
  output channel (see `demos/17_oroutines.c` relay pattern).
- **Sanitizer path.** The real scheduler is disabled under
  ASan/TSan (see v1.3.4 notes). Proper fiber hooks land in v1.3.5.
