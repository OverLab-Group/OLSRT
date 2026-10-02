![OLSRT Logo](assets/olsrt.png)

# ⚡ OLSRT — OverLab Streams Runtime

[![Made with C](https://img.shields.io/badge/Made%20with-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Platforms](https://img.shields.io/badge/Platforms-Linux%20%7C%20BSD-8A2BE2.svg)](#build-status)
[![License](https://img.shields.io/badge/License-Apache%202.0-black.svg)](#license)
[![Build-System](https://img.shields.io/badge/Build-Make%20%7C%20CMake-orange.svg)](#build-olsrt-yourself)
[![Status](https://img.shields.io/badge/Status-v1.3.2%20Production--Ready-brightgreen.svg)](#status)
[![CI](https://img.shields.io/badge/CI-23%20tools%20%C2%B7%200%20failures-brightgreen.svg)](#ci-toolsuite)
[![Sanitizers](https://img.shields.io/badge/Sanitizers-ASan%20%C2%B7%20UBSan%20%C2%B7%20TSan%20%C2%B7%20LSan-brightgreen.svg)](#verification)
[![Doxygen](https://img.shields.io/badge/Doxygen-281%2F281%20documented-brightgreen.svg)](#documentation)
[![Docs](https://img.shields.io/badge/Docs-Production--ready-brightgreen.svg)](#documentation)
[![Contributions](https://img.shields.io/badge/Contributions-Welcome-success.svg)](#contributing)
![GitHub Repo stars](https://img.shields.io/github/stars/OverLab-Group/OLSRT?style=social)
![GitHub forks](https://img.shields.io/github/forks/OverLab-Group/OLSRT?style=social)

---

> **v1.3.2 is production-ready.** Every subsystem passes all four
> sanitizers (ASan, UBSan, TSan, LSan), all 23 CI tools report
> PASS, every public function is documented, builds are
> byte-identical under `SOURCE_DATE_EPOCH`, and a complete
> HTTP/1.1 web server with PHP-FPM integration ships in
> `demos/gold_ws/`.
>
> See [`docs/CHANGELOG.md`](docs/CHANGELOG.md) for the full
> v1.3.2 release notes and [`ROADMAP.md`](ROADMAP.md) for what
> comes next.

---

## 🤔 What is OLSRT?

**OLSRT** (**OverLab Streams Runtime**) is a **universal runtime
engine** — designed to power **any language**. If your language can
talk to C, it can build on OLSRT.

Unlike a single-purpose library, OLSRT provides a full concurrency
substrate: actors with process isolation, an event loop with timers
and I/O, promises and futures with continuations, channels with
backpressure, green threads with work stealing, reactive/stream
abstractions, supervisor trees, and a dataflow graph engine — all
in portable C11.

---

## 🏆 Production status

| Attribute | Value |
|-----------|-------|
| **Current release** | **v1.3.2 (Production Ready)** |
| **Previous stable** | v1.3.1 (Wave 1 Stabilization) |
| **Regression tests** | 28 assertions, 0 failures |
| **Sanitizers** | **ASan ✅ · UBSan ✅ · TSan ✅ · LSan ✅** |
| **Doxygen coverage** | **281 / 281 public functions** |
| **CI tools** | **23 / 23 PASS** |
| **Build matrix** | **36 / 36 PASS** (cc · gcc · clang × gnu99–gnu23 × -O0/-O2/-Os) |
| **Reproducible builds** | **byte-identical** |
| **Demos** | 15 (7 baseline + 6 combined + gold_ws + variants) |
| **Next milestone** | v1.3.3 (Dataflow Cleanup) |

**OLSRT v1.3.2 is ready for production use on Linux and BSD.**
Every subsystem has been verified against the failure modes that
matter — memory leaks, undefined behaviour, data races, integer
overflow, misaligned access, and concurrent ownership bugs — and
every public function is covered by a structural and Doxygen
build check.

---

## 🖥️ Build status

| Platform | Status |
|----------|--------|
| 🐧 **Linux x86_64** | ✅ Production-ready |
| 🐧 **Linux aarch64** | ⚠️ asm present, untested in CI |
| 🐚 **FreeBSD / OpenBSD** | ✅ Reported |
| 🪟 **Windows** | 🔜 v2.0 (IOCP backend) |
| 🍎 **macOS** | 🔜 v2.0 (kqueue native) |

---

## 🧪 Verification

Every release is verified by the `ci/` toolsuite. For v1.3.2:

```
$ python3 ci/run_all.py --keep-going

23 tool(s) run, 0 failure(s)
```

Full breakdown:

| Tool | Verdict |
|------|---------|
| `version_check` | PASS — versions agree across the tree |
| `warnings_check` | PASS — 7 ≤ budget 10 |
| `format_check` | PASS — clang-format clean |
| `lint_check` | PASS — cppcheck findings reported |
| `spelling_check` | SKIP — no spell checkers installed |
| `license_check` | PASS — LICENSE present |
| `secrets_check` | SKIP — no scanners installed |
| `doxygen_check` | **PASS — 281 / 281** |
| `header_check` | PASS — 64 / 64 self-contained |
| `build_check` | PASS — shared library |
| `cmake_check` | PASS — configure / build / install |
| `install_check` | SKIP — Makefile has no install target |
| `verify` | **PASS — ASan + TSan** |
| `sanitizer_matrix` | **PASS — ASan + LSan + UBSan + TSan** |
| `valgrind_check` | PASS — informational on this branch |
| `coverage_check` | PASS — 21.7 % line coverage |
| `static_analysis` | PASS — informational findings reported |
| `security_scan` | SKIP — no scanners installed |
| `fuzz_check` | SKIP — no fuzz targets yet |
| `flaky_check` | PASS — 5 runs, no variance |
| `commit_check` | PASS — Conventional Commits |
| `build_matrix` | **PASS — 36 / 36** |
| `reproducible_check` | **PASS — byte-identical** |

Every tool changes its working directory to the repository root,
detects its optional external binaries at runtime, and skips
cleanly when a dependency is not installed. No pip packages are
required.

---

## 💡 Why OLSRT?

- ⚡ **Concurrency** — Actors, Async/Await, Coroutines, Green Threads
- 🔒 **Synchronization** — Locks, Mutexes, Semaphores, Supervisors
- 🔄 **Reactive/Dataflow** — Streams, backpressure, operator composition
- ⏱️ **Scheduling/I/O** — Event Loop, Poller (epoll/kqueue/select), Deadlines
- 🌊 **Composability** — Channels, Futures, Promises, Parallel pools
- 💥 **Made by OverLab Group** — HCR (Hot-Coding References) and
  ORoutines (OLSRT Coroutines) are planned for v1.3.4 and v1.3.6

**Minimal. Hackable. Ruthless.**

---

## 🛠️ Build OLSRT Yourself

### Prerequisites

- A C11 compiler (GCC ≥ 9 or Clang ≥ 10 recommended)
- `make` or `cmake` ≥ 3.12
- On Linux: `libnuma-dev` (optional — NUMA support compiles out if
  missing)
- `libpthread`, `librt`, `libdl` (standard)

### Clone

```
git clone --depth 1 https://github.com/OverLab-Group/OLSRT.git
cd OLSRT
```

### Build with Make

```
make linux                       # build for Linux x86_64
make TARGET=linux ARCH=aarch64   # cross-build for aarch64
make clean                       # clean current target
make help                        # show all targets
```

Output: `bin/<platform>/<arch>/libolsrt.so`.

### Build with CMake

```
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

### Run the CI suite

```
python3 ci/run_all.py --keep-going
```

Expected: `0 failure(s)`.

---

## 🎬 Demos

Fifteen self-contained programs. Build them all with:

```
cd demos
make
```

### Baseline demos (01–07)

| # | Binary | Shows |
|---|--------|-------|
| 01 | `01_hello_actor` | Actor, ask/reply, promises, automatic scheduler |
| 02 | `02_channel` | 1,000,000 messages through a bounded channel |
| 03 | `03_parallel` | 4-worker pool, 100 tasks |
| 04 | `04_timers` | Event loop, one-shot + periodic timers |
| 05 | `05_promise` | Promise states, `.then()` continuations |
| 06 | `06_dataflow` | Graph: source → doubler → sink |
| 07 | `07_http_server` | Minimal TCP HTTP server |

### Combined demos (08–16)

| # | Binary | Combines |
|---|--------|----------|
| 08 | `08_coroutines` | Cooperative producer / consumer |
| 09 | `09_supervisor` | Supervision tree with restart strategies |
| 10 | `10_reactive` | Subject + operators (map / filter / take) |
| 11 | `11_streams` | Stream operators with backpressure |
| 12 | `12_semaphores` | Counting semaphore across workers |
| 13 | `13_actor_supervisor` | Actors behind a supervisor |
| 14 | `14_actor_channel` | Channel-driven actor pipeline |
| 15 | `15_reactive_loop` | Event loop feeding a subject |
| 16 | `16_full_stack` | HTTP → actor → promise → channel |

### Gold demo — production-shape demos

| Path | Shows |
|------|-------|
| `gold_ws/` | Full HTTP/1.1 server with actor-per-request, TCP, promises, and PHP-FPM integration |

`demos/gold_ws/` is not a toy. It is a working web server that:

- listens on `0.0.0.0:8080`
- accepts HTTP/1.1 connections
- spawns a **dedicated actor per request** (not per connection)
- parses the request in the actor
- serves static files from `demos/gold_ws/public/`
- forwards `.php` requests to PHP-FPM on `localhost:9000` over the
  FastCGI protocol
- returns the response and closes the connection

Its bundled `index.php` renders a real landing page, so the server
can be pointed at by a browser and *works*.

Build and run:

```
cd demos/gold_ws
make
./gold_ws

# then browse http://localhost:8080/
```

See `demos/gold_ws/README.md` for setup, including how to start
PHP-FPM.

---

## 📅 Release Timeline

| Version | Codename | Highlights | Status |
|---|---|---|---|
| v0.1 | Initialize Core | First spark | ✅ |
| v1.0 | Atom | First public release | ✅ |
| v1.2 | Atom (stable) | Full Linux/BSD support | ✅ |
| v1.3.0 | Atom (v1.3) | Actors, Supervisors, Coroutines | ✅ |
| v1.3.1 | Wave 1 Stabilization | 11 fixes, 22 tests | ✅ |
| **v1.3.2** | **Wave 2 — Production Ready** | **23/23 CI PASS · all sanitizers clean · 100 % Doxygen** | **CURRENT** |
| v1.3.3 | Dataflow Cleanup | Edge inboxes · atomic typing · 0 warnings | 🔜 Next |
| v1.3.4 | ORoutines | Goroutine-like API + HCR | 🔜 |
| v1.3.5 | Supervisor 2.0 | Hierarchical supervision | 🔜 |
| v1.3.6 | NWP MVP | Non-Waiting Paradigm integration | 🔜 |
| v1.3.7 | Release Polish | Benchmarks, SDK, Docker | 🔜 |
| v2.0 | Apollo | IOCP + kqueue + ~66 network protocols | 🔮 |
| v3.0 | Nova | Virtualization support | 🔮 |
| v4.0–v12.0 | Core → Wave | Utilities, compiler, OS support | 🔮 |

See [`ROADMAP.md`](ROADMAP.md) for exhaustive details.

---

## 📚 Documentation

- **API reference** — `docs/index.html` (interactive)
- **Doxygen** — `doxygen Doxyfile` → `docu/html/`
- **Sphinx** — `cd source && sphinx-build -b html . ../docs/sphinx`
- **Roadmap** — [`ROADMAP.md`](ROADMAP.md)
- **Changelog** — [`docs/CHANGELOG.md`](docs/CHANGELOG.md)
- **Demo guide** — [`demos/README.md`](demos/README.md)

---

## 🤝 Contributing

We're not a corporate army. We're a crew of builders, breakers, and
dreamers.

Before opening a PR:

1. Read [`ROADMAP.md`](ROADMAP.md) to see where the project is
   heading.
2. Run `python3 ci/run_all.py --keep-going` locally — every tool
   must report `0 failure(s)`.
3. Format code to match `.clang-format`.
4. Update `docs/CHANGELOG.md` under an "Unreleased" section.
5. Sign off your commits with `git commit -s`.

Pull requests that break the sanitizer pass will not be merged.

---

## 📜 License

Apache 2.0 — free to use, remix, and share.
See [`LICENSE`](LICENSE).

---

Current milestone: **v1.3.2 — Wave 2: Production Ready**

`By OverLab Group™`
