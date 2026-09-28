# OLSRT Demos

Small, self-contained programs that demonstrate OLSRT features.

## Build

    make

This produces `libolsrt.so` (built from `../src/code/streams/*.c`)
and one binary per demo.

## Run

    ./01_hello_actor

or

    make run

## Demos

| # | File | What it shows |
|---|------|---------------|
| 01 | `01_hello_actor.c` | Actor model, ask/reply, promises, manual mailbox drive |

## Notes

- Actor main loops are not yet driven by the green-thread scheduler
  (Wave 2 work). Demo 01 pumps the mailbox manually via
  `ol_actor_process_batch()`.
- `-DOL_DISABLE_NUMA=1` is set so the demo builds without `libnuma-dev`.
