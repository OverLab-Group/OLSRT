# OLSRT CI Toolsuite

Every script in this directory is a self-contained Python 3.7+ program
that changes its working directory to the repository root before doing
anything else. This means `python3 ci/<tool>.py` works from any
location.

## Design rules

1. **Standard library only.** No pip packages. A tool that needs an
   external binary (compiler, analyzer, formatter) detects it with
   `shutil.which()` and, if absent, records a skip. It never fails
   simply because an optional tool is missing.
2. **Plain text output.** CI logs are read by machines first; colors
   are disabled unconditionally.
3. **Meaningful exit codes.** `0` on success, `1` on any failure, `2`
   on usage error.
4. **Per-tool summary.** Each tool prints a short summary at the end
   (`passed`, `failed`, `skipped`).
5. **No side effects on failure.** A tool that changes files must
   restore them; none of the current tools do this because none
   modify source.

## Tools

### Core

| Script | Purpose |
|--------|---------|
| `verify.py` | Build the library under ASan + UBSan and TSan, then run the full test suite in each configuration. |
| `doxygen_check.py` | Report the Doxygen coverage of every public API. Fails on MISSING or PARTIAL. |
| `build_check.py` | Build the shared library with the default toolchain. |
| `warnings_check.py` | Compile with `-Wall -Wextra` and count warnings. Fails if the count exceeds the budget. |
| `version_check.py` | Assert that every version string in the tree agrees. |
| `run_all.py` | Run every applicable tool in order. |

### Code quality

| Script | External tools |
|--------|----------------|
| `format_check.py` | `clang-format`, `uncrustify`, `astyle` |
| `lint_check.py` | `clang-tidy`, `cppcheck`, `sparse`, `smatch`, `splint` |
| `static_analysis.py` | `clang --analyze`, `infer`, `semgrep`, `codeql`, `pvs-studio-analyzer` |
| `include_check.py` | `include-what-you-use` plus a header self-containment check |
| `spelling_check.py` | `codespell`, `typos` |
| `license_check.py` | `reuse`, `scancode-toolkit`, `licensecheck` |
| `complexity_check.py` | `lizard`, `pmccabe` |

### Security

| Script | External tools |
|--------|----------------|
| `security_scan.py` | `semgrep`, `snyk`, `trivy`, `grype` |
| `secrets_check.py` | `gitleaks`, `trufflehog` |

### Testing

| Script | External tools |
|--------|----------------|
| `sanitizer_matrix.py` | Per-sanitizer builds: ASan, UBSan, MSan, TSan, LSan |
| `valgrind_check.py` | `valgrind` (memcheck, helgrind, drd, massif) |
| `coverage_check.py` | `gcov`, `lcov`, `llvm-cov` |
| `fuzz_check.py` | `libFuzzer`, `afl-fuzz`, `honggfuzz` |
| `flaky_check.py` | Runs the test suite N times and reports variance |

### Build

| Script | Purpose |
|--------|---------|
| `build_matrix.py` | Every available compiler × C standard × optimisation level |
| `cmake_check.py` | `cmake` configure / build / install / uninstall round trip |
| `install_check.py` | `DESTDIR`, prefix, pkg-config and CMake package files |
| `header_check.py` | Self-containment of every header in `includes/` |
| `symbol_check.py` | Exported symbol count and visibility |
| `reproducible_check.py` | Two builds with `SOURCE_DATE_EPOCH` must produce identical bytes |

### Documentation

| Script | Purpose |
|--------|---------|
| `docs_build.py` | Doxygen and Sphinx build check |
| `changelock_check.py` | `CHANGELOG.md` follows Keep a Changelog |

### Release

| Script | Purpose |
|--------|---------|
| `commit_check.py` | Last N commits follow Conventional Commits |
| `olsrt_commiter.py` | Interactive standard commit helper |
| `benchmark_check.py` | Run the benchmark harness if present |

## Running everything

```

python3 ci/run_all.py
python3 ci/run_all.py --keep-going      # do not stop on first failure
python3 ci/run_all.py --only verify.py,build_check.py
python3 ci/run_all.py --list            # list tools, do not run

```

## Adding a tool

1. Drop the file in `ci/`, prefix its docstring with a one-line summary.
2. Import `_common` and call `chdir_to_root()` first.
3. Use the `Reporter` class for pass / fail / skip accounting.
4. Exit with `0` or `1`.

See any existing tool for the pattern.
