# Future Modules — Design Placeholders

**This directory contains design placeholders for modules planned in
later OLSRT releases. None of the code here compiles into the library.**

## Why this directory exists

OLSRT's [`ROADMAP.md`](../ROADMAP.md) describes a long-term vision that
includes a compiler, network protocol stack, virtualization primitives,
and utility modules. The files in this directory are **stubs** that
document the intended API surface for those modules, so that:

1. Contributors can see *where* future work will live.
2. The directory layout matches the roadmap.
3. The main `src/` and `includes/` trees contain only **real, tested,
   working code**.

## What is real vs. placeholder

| Path | Status |
|------|--------|
| `../src/code/streams/` | Real — 22 files, all tested |
| `../src/code/network/ol_tcp.c`, `ol_udp.c` | Real |
| `../src/runtime/` | Real |
| `cli/compiler/` | Placeholder — planned v8.0 (Hermes) |
| `cli/virtualization/` | Placeholder — planned v3.0 (Nova) |
| `cli/utils/` | Placeholder — planned v5.0 (Spark) |
| `network/` | Placeholder — planned v2.0 (Apollo, ~66 protocols) |
| `utils/` | Placeholder — planned v4.0 (Core) |
| `panel/` | Placeholder — planned v3.x |
| `olsrt_placeholder.c` | Original stub for the entry point |

## How to add a new placeholder

1. Create the file under the appropriate `future/` subdirectory.
2. Put a single-line comment at the top:

   /* PLACEHOLDER — see ROADMAP.md section X.Y for details */

3. Declare the intended public API signatures as comments or in a
   `#if 0` block. Do **not** include compilable code.
4. Link to the roadmap section that describes when this will be built.

## How to graduate a placeholder to real code

When the roadmap reaches the version that implements a placeholder:

1. Move the file from `future/` back to `src/` or `includes/`.
2. Replace the placeholder body with a real implementation.
3. Add tests under `tests/`.
4. Add a demo under `demos/`.
5. Update this README's status table.

---

*This directory is intentionally excluded from the Makefile, CMake,
Doxyfile, and Sphinx builds. Only `src/` and `includes/` are compiled.*
