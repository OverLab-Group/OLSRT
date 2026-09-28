# Changelog — Wave 1 Patches

## v1.3.1 — Stabilization Release

### Fixed

- **Actor (`ol_actor.c`)**

- Removed `pending_asks` hashmap operations in `ol_actor_ask` that
leaked one `ol_ask_envelope_t` + `ol_promise_t` per request.
- `ol_actor_send_timeout` now uses `ol_cond_wait_until` on the mailbox
`not_full` condition; returns `-3` on timeout, no CPU spinning.
- Fixed memory ordering in `actor_mailbox_try_send_fast` and
`actor_mailbox_batch_recv`: shared ring-buffer head/tail are now
accessed through `__atomic_load_n(..., __ATOMIC_ACQUIRE/RELAXED)`.
- `ol_actor_send_timeout` correctly releases the message to `msg_dtor`
on timeout or actor-closed paths.
- **Arena (`ol_actor_arena.c`)**

- `ol_arena_free` now validates that `ptr` falls within the arena’s
pool before touching the header. Foreign pointers are ignored
(preventing UB in multi-actor / multi-arena scenarios).
- **TCP (`ol_tcp.c`)**

- Every pending operation (`connect`, `accept`, `send`, `recv`)
registers a `deadline_ns`-based timer on the event loop.
- Timers are cancelled in `fulfill_and_reset` when the operation
completes; late rejections are prevented by state reset.
- Rejected promises use code `-3` (OL_TIMEOUT).
- **UDP (`ol_udp.c`)**

- Same deadline-timer mechanism as TCP for `sendto` and `recvfrom`.
- **Supervisor (`ol_supervisor.c`)**

- `ol_supervisor_stop` now enforces a hard timeout; unresponsive
children are killed rather than waited on indefinitely.

### Not changed (by design)

- Public API signatures.
- ABI version (still `libolsrt.so.1`).
- Wire format of serialized messages.
- Actor behavior semantics.
- Green-thread / coroutine scheduler.

### Tested

- `tests/test_wave1.c` — 6 focused regression tests.
- Build matrix: Linux x86_64, Linux aarch64.
- Sanitizers: ASan, UBSan, TSan (all clean).