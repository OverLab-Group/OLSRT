#!/usr/bin/env bash

# OLSRT Wave 1 verification: build, sanitize, test.

# Usage: ./verify.sh [OLS_ROOT]

set -euo pipefail

RED='\033[31m'; GREEN='\033[32m'; YELLOW='\033[33m'; RESET='\033[0m'
info() { echo "<span class="katex"><span class="katex-mathml"><math xmlns="http://www.w3.org/1998/Math/MathML"><semantics><mrow><mrow><mi>Y</mi><mi>E</mi><mi>L</mi><mi>L</mi><mi>O</mi><mi>W</mi></mrow><mo>=</mo><mo>=</mo><mo>&gt;</mo></mrow><annotation encoding="application/x-tex">{YELLOW}==&gt;</annotation></semantics></math></span><span class="katex-html" aria-hidden="true"><span class="base"><span class="strut" style="height:0.7224em;vertical-align:-0.0391em;"></span><span class="mord"><span class="mord mathnormal" style="margin-right:0.22222em;">Y</span><span class="mord mathnormal" style="margin-right:0.05764em;">E</span><span class="mord mathnormal">LL</span><span class="mord mathnormal" style="margin-right:0.02778em;">O</span><span class="mord mathnormal" style="margin-right:0.13889em;">W</span></span><span class="mspace" style="margin-right:0.2778em;"></span><span class="mrel">==&gt;</span></span></span></span>{RESET} *"; }
ok()   { echo "{GREEN}✓<span class="katex"><span class="katex-mathml"><math xmlns="http://www.w3.org/1998/Math/MathML"><semantics><mrow><mi>R</mi><mi>E</mi><mi>S</mi><mi>E</mi><mi>T</mi></mrow><annotation encoding="application/x-tex">{RESET} </annotation></semantics></math></span><span class="katex-html" aria-hidden="true"><span class="base"><span class="strut" style="height:0.6833em;"></span><span class="mord"><span class="mord mathnormal" style="margin-right:0.13889em;">RESET</span></span></span></span></span>*"; }
fail() { echo "<span class="katex"><span class="katex-mathml"><math xmlns="http://www.w3.org/1998/Math/MathML"><semantics><mrow><mrow><mi>R</mi><mi>E</mi><mi>D</mi></mrow><mtext>✗</mtext></mrow><annotation encoding="application/x-tex">{RED}✗</annotation></semantics></math></span><span class="katex-html" aria-hidden="true"><span class="base"><span class="strut" style="height:0.6833em;"></span><span class="mord"><span class="mord mathnormal" style="margin-right:0.05764em;">RE</span><span class="mord mathnormal" style="margin-right:0.02778em;">D</span></span><span class="mord">✗</span></span></span></span>{RESET} $*"; exit 1; }

# Locate OLSRT root

if [[ $# -ge 1 ]]; then
    ROOT="$1"
elif [[ -f "includes/olsrt.h" ]]; then
ROOT="<span class="katex"><span class="katex-mathml"><math xmlns="http://www.w3.org/1998/Math/MathML"><semantics><mrow><mo stretchy="false">(</mo><mi>p</mi><mi>w</mi><mi>d</mi><mo stretchy="false">)</mo><mi mathvariant="normal">"</mi><mi>e</mi><mi>l</mi><mi>i</mi><mi>f</mi><mo stretchy="false">[</mo><mo stretchy="false">[</mo><mo>−</mo><mi>f</mi><mi mathvariant="normal">"</mi><mi mathvariant="normal">.</mi><mi mathvariant="normal">.</mi><mi mathvariant="normal">/</mi><mi>i</mi><mi>n</mi><mi>c</mi><mi>l</mi><mi>u</mi><mi>d</mi><mi>e</mi><mi>s</mi><mi mathvariant="normal">/</mi><mi>o</mi><mi>l</mi><mi>s</mi><mi>r</mi><mi>t</mi><mi mathvariant="normal">.</mi><mi>h</mi><mi mathvariant="normal">"</mi><mo stretchy="false">]</mo><mo stretchy="false">]</mo><mo separator="true">;</mo><mi>t</mi><mi>h</mi><mi>e</mi><mi>n</mi><mi>R</mi><mi>O</mi><mi>O</mi><mi>T</mi><mo>=</mo><mi mathvariant="normal">"</mi></mrow><annotation encoding="application/x-tex">(pwd)"
elif [[ -f "../includes/olsrt.h" ]]; then
    ROOT="</annotation></semantics></math></span><span class="katex-html" aria-hidden="true"><span class="base"><span class="strut" style="height:1em;vertical-align:-0.25em;"></span><span class="mopen">(</span><span class="mord mathnormal" style="margin-right:0.02691em;">pw</span><span class="mord mathnormal">d</span><span class="mclose">)</span><span class="mord">"</span><span class="mord mathnormal">e</span><span class="mord mathnormal" style="margin-right:0.01968em;">l</span><span class="mord mathnormal">i</span><span class="mord mathnormal" style="margin-right:0.10764em;">f</span><span class="mopen">[[</span><span class="mord">−</span><span class="mord mathnormal" style="margin-right:0.10764em;">f</span><span class="mord">"../</span><span class="mord mathnormal">in</span><span class="mord mathnormal">c</span><span class="mord mathnormal" style="margin-right:0.01968em;">l</span><span class="mord mathnormal">u</span><span class="mord mathnormal">d</span><span class="mord mathnormal">es</span><span class="mord">/</span><span class="mord mathnormal">o</span><span class="mord mathnormal" style="margin-right:0.01968em;">l</span><span class="mord mathnormal" style="margin-right:0.02778em;">sr</span><span class="mord mathnormal">t</span><span class="mord">.</span><span class="mord mathnormal">h</span><span class="mord">"</span><span class="mclose">]]</span><span class="mpunct">;</span><span class="mspace" style="margin-right:0.1667em;"></span><span class="mord mathnormal">t</span><span class="mord mathnormal">h</span><span class="mord mathnormal">e</span><span class="mord mathnormal">n</span><span class="mord mathnormal" style="margin-right:0.13889em;">ROOT</span><span class="mspace" style="margin-right:0.2778em;"></span><span class="mrel">=</span><span class="mspace" style="margin-right:0.2778em;"></span></span><span class="base"><span class="strut" style="height:0.6944em;"></span><span class="mord">"</span></span></span></span>(cd .. && pwd)"
else
fail "Cannot find OLSRT root. Pass it as arg: ./verify.sh /path/to/OLSRT"
fi
info "OLS root: $ROOT"

HERE="<span class="katex"><span class="katex-mathml"><math xmlns="http://www.w3.org/1998/Math/MathML"><semantics><mrow><mo stretchy="false">(</mo><mi>c</mi><mi>d</mi><mi mathvariant="normal">"</mi></mrow><annotation encoding="application/x-tex">(cd "</annotation></semantics></math></span><span class="katex-html" aria-hidden="true"><span class="base"><span class="strut" style="height:1em;vertical-align:-0.25em;"></span><span class="mopen">(</span><span class="mord mathnormal">c</span><span class="mord mathnormal">d</span><span class="mord">"</span></span></span></span>(dirname "{BASH_SOURCE[0]}")" && pwd)"
cd "ROOT"

# ----------------------------------------------------------------------------

# 1. Plain build

# ----------------------------------------------------------------------------

info "Building (plain, default target)…"
make clean-all >/dev/null 2>&1 || true
if make -j"$(nproc 2>/dev/null || echo 4)" >/tmp/olsrt_build.log 2>&1; then
ok "Plain build succeeded"
else
tail -40 /tmp/olsrt_build.log
fail "Plain build failed"
fi

# ----------------------------------------------------------------------------

# 2. ASan + UBSan build

# ----------------------------------------------------------------------------

info "Building with AddressSanitizer + UndefinedBehaviorSanitizer…"
ASAN_DIR="<span class="katex"><span class="katex-mathml"><math xmlns="http://www.w3.org/1998/Math/MathML"><semantics><mrow><mo stretchy="false">(</mo><mi>m</mi><mi>k</mi><mi>t</mi><mi>e</mi><mi>m</mi><mi>p</mi><mo>−</mo><mi>d</mi><mo stretchy="false">)</mo><mi mathvariant="normal">"</mi><mi>c</mi><mi>a</mi><mi>t</mi><mo>&gt;</mo><mi mathvariant="normal">"</mi></mrow><annotation encoding="application/x-tex">(mktemp -d)"
cat &gt; "</annotation></semantics></math></span><span class="katex-html" aria-hidden="true"><span class="base"><span class="strut" style="height:1em;vertical-align:-0.25em;"></span><span class="mopen">(</span><span class="mord mathnormal" style="margin-right:0.03148em;">mk</span><span class="mord mathnormal">t</span><span class="mord mathnormal">e</span><span class="mord mathnormal">m</span><span class="mord mathnormal">p</span><span class="mspace" style="margin-right:0.2222em;"></span><span class="mbin">−</span><span class="mspace" style="margin-right:0.2222em;"></span></span><span class="base"><span class="strut" style="height:1em;vertical-align:-0.25em;"></span><span class="mord mathnormal">d</span><span class="mclose">)</span><span class="mord">"</span><span class="mord mathnormal">c</span><span class="mord mathnormal">a</span><span class="mord mathnormal">t</span><span class="mspace" style="margin-right:0.2778em;"></span><span class="mrel">&gt;</span><span class="mspace" style="margin-right:0.2778em;"></span></span><span class="base"><span class="strut" style="height:0.6944em;"></span><span class="mord">"</span></span></span></span>ASAN_DIR/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.12)
project(olsrt_asan C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer")
file(GLOB_RECURSE SRC "src/code/streams/*.c")
add_library(olsrt SHARED ${SRC})
target_include_directories(olsrt PUBLIC includes includes/code/streams includes/runtime)
CMAKE

mkdir -p "ASAN_DIR/build" && cd "ASAN_DIR/build"
cmake .. >/dev/null 2>&1 || fail "ASan cmake configure failed"
if cmake --build . -j"(nproc 2>/dev/null || echo 4)" >/tmp/olsrt_asan.log 2>&1; then
    ok "ASan/UBSan build succeeded"
else
    tail -40 /tmp/olsrt_asan.log
    fail "ASan/UBSan build failed"
fi
cd "ROOT"

# ----------------------------------------------------------------------------

# 3. ThreadSanitizer build

# ----------------------------------------------------------------------------

info "Building with ThreadSanitizer…"
TSAN_DIR="<span class="katex"><span class="katex-mathml"><math xmlns="http://www.w3.org/1998/Math/MathML"><semantics><mrow><mo stretchy="false">(</mo><mi>m</mi><mi>k</mi><mi>t</mi><mi>e</mi><mi>m</mi><mi>p</mi><mo>−</mo><mi>d</mi><mo stretchy="false">)</mo><mi mathvariant="normal">"</mi><mi>c</mi><mi>a</mi><mi>t</mi><mo>&gt;</mo><mi mathvariant="normal">"</mi></mrow><annotation encoding="application/x-tex">(mktemp -d)"
cat &gt; "</annotation></semantics></math></span><span class="katex-html" aria-hidden="true"><span class="base"><span class="strut" style="height:1em;vertical-align:-0.25em;"></span><span class="mopen">(</span><span class="mord mathnormal" style="margin-right:0.03148em;">mk</span><span class="mord mathnormal">t</span><span class="mord mathnormal">e</span><span class="mord mathnormal">m</span><span class="mord mathnormal">p</span><span class="mspace" style="margin-right:0.2222em;"></span><span class="mbin">−</span><span class="mspace" style="margin-right:0.2222em;"></span></span><span class="base"><span class="strut" style="height:1em;vertical-align:-0.25em;"></span><span class="mord mathnormal">d</span><span class="mclose">)</span><span class="mord">"</span><span class="mord mathnormal">c</span><span class="mord mathnormal">a</span><span class="mord mathnormal">t</span><span class="mspace" style="margin-right:0.2778em;"></span><span class="mrel">&gt;</span><span class="mspace" style="margin-right:0.2778em;"></span></span><span class="base"><span class="strut" style="height:0.6944em;"></span><span class="mord">"</span></span></span></span>TSAN_DIR/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.12)
project(olsrt_tsan C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "-O1 -g -fsanitize=thread -fno-omit-frame-pointer")
file(GLOB_RECURSE SRC "src/code/streams/*.c")
add_library(olsrt SHARED ${SRC})
target_include_directories(olsrt PUBLIC includes includes/code/streams includes/runtime)
CMAKE

mkdir -p "TSAN_DIR/build" && cd "TSAN_DIR/build"
cmake .. >/dev/null 2>&1 || fail "TSan cmake configure failed"
if cmake --build . -j"(nproc 2>/dev/null || echo 4)" >/tmp/olsrt_tsan.log 2>&1; then
    ok "TSan build succeeded"
else
    tail -60 /tmp/olsrt_tsan.log
    fail "TSan build failed (see /tmp/olsrt_tsan.log)"
fi
cd "ROOT"

# ----------------------------------------------------------------------------

# 4. Compile and run the Wave-1 regression tests

# ----------------------------------------------------------------------------

info "Compiling Wave-1 regression tests…"
TEST_SRC="$HERE/tests/test_wave1.c"
TEST_BIN="/tmp/olsrt_wave1_tests"

# Link against the freshly built lib from ASan dir

ASAN_LIB_DIR="<span class="katex"><span class="katex-mathml"><math xmlns="http://www.w3.org/1998/Math/MathML"><semantics><mrow><mo stretchy="false">(</mo><mi>f</mi><mi>i</mi><mi>n</mi><mi>d</mi><mi mathvariant="normal">"</mi></mrow><annotation encoding="application/x-tex">(find "</annotation></semantics></math></span><span class="katex-html" aria-hidden="true"><span class="base"><span class="strut" style="height:1em;vertical-align:-0.25em;"></span><span class="mopen">(</span><span class="mord mathnormal" style="margin-right:0.10764em;">f</span><span class="mord mathnormal">in</span><span class="mord mathnormal">d</span><span class="mord">"</span></span></span></span>ASAN_DIR/build" -name 'libolsrt*' -type f | head -1 | xargs -r dirname)"
if [[ -z "$ASAN_LIB_DIR" ]]; then
fail "Could not locate built libolsrt in ASan dir"
fi

if cc -std=c11 -O1 -g 
-fsanitize=address,undefined -fno-omit-frame-pointer 
-I"<span class="katex"><span class="katex-mathml"><math xmlns="http://www.w3.org/1998/Math/MathML"><semantics><mrow><mi>R</mi><mi>O</mi><mi>O</mi><mi>T</mi><mi mathvariant="normal">/</mi><mi>i</mi><mi>n</mi><mi>c</mi><mi>l</mi><mi>u</mi><mi>d</mi><mi>e</mi><mi>s</mi><mi mathvariant="normal">"</mi><mo>−</mo><mi>I</mi><mi mathvariant="normal">"</mi></mrow><annotation encoding="application/x-tex">ROOT/includes" -I"</annotation></semantics></math></span><span class="katex-html" aria-hidden="true"><span class="base"><span class="strut" style="height:1em;vertical-align:-0.25em;"></span><span class="mord mathnormal" style="margin-right:0.13889em;">ROOT</span><span class="mord">/</span><span class="mord mathnormal">in</span><span class="mord mathnormal">c</span><span class="mord mathnormal" style="margin-right:0.01968em;">l</span><span class="mord mathnormal">u</span><span class="mord mathnormal">d</span><span class="mord mathnormal">es</span><span class="mord">"</span><span class="mspace" style="margin-right:0.2222em;"></span><span class="mbin">−</span><span class="mspace" style="margin-right:0.2222em;"></span></span><span class="base"><span class="strut" style="height:0.6944em;"></span><span class="mord mathnormal" style="margin-right:0.07847em;">I</span><span class="mord">"</span></span></span></span>ROOT/includes/code/streams" -I"<span class="katex"><span class="katex-mathml"><math xmlns="http://www.w3.org/1998/Math/MathML"><semantics><mrow><mi>R</mi><mi>O</mi><mi>O</mi><mi>T</mi><mi mathvariant="normal">/</mi><mi>i</mi><mi>n</mi><mi>c</mi><mi>l</mi><mi>u</mi><mi>d</mi><mi>e</mi><mi>s</mi><mi mathvariant="normal">/</mi><mi>r</mi><mi>u</mi><mi>n</mi><mi>t</mi><mi>i</mi><mi>m</mi><mi>e</mi><mi mathvariant="normal">"</mi><mtext>&nbsp;</mtext><mi mathvariant="normal">"</mi></mrow><annotation encoding="application/x-tex">ROOT/includes/runtime" \
      "</annotation></semantics></math></span><span class="katex-html" aria-hidden="true"><span class="base"><span class="strut" style="height:1em;vertical-align:-0.25em;"></span><span class="mord mathnormal" style="margin-right:0.13889em;">ROOT</span><span class="mord">/</span><span class="mord mathnormal">in</span><span class="mord mathnormal">c</span><span class="mord mathnormal" style="margin-right:0.01968em;">l</span><span class="mord mathnormal">u</span><span class="mord mathnormal">d</span><span class="mord mathnormal">es</span><span class="mord">/</span><span class="mord mathnormal" style="margin-right:0.02778em;">r</span><span class="mord mathnormal">u</span><span class="mord mathnormal">n</span><span class="mord mathnormal">t</span><span class="mord mathnormal">im</span><span class="mord mathnormal">e</span><span class="mord">"</span><span class="mspace">&nbsp;</span><span class="mord">"</span></span></span></span>TEST_SRC" 
-L"<span class="katex"><span class="katex-mathml"><math xmlns="http://www.w3.org/1998/Math/MathML"><semantics><mrow><mi>A</mi><mi>S</mi><mi>A</mi><msub><mi>N</mi><mi>L</mi></msub><mi>I</mi><msub><mi>B</mi><mi>D</mi></msub><mi>I</mi><mi>R</mi><mi mathvariant="normal">"</mi><mo>−</mo><mi>l</mi><mi>o</mi><mi>l</mi><mi>s</mi><mi>r</mi><mi>t</mi><mo>−</mo><mi>l</mi><mi>p</mi><mi>t</mi><mi>h</mi><mi>r</mi><mi>e</mi><mi>a</mi><mi>d</mi><mo>−</mo><mi>l</mi><mi>r</mi><mi>t</mi><mo>−</mo><mi>l</mi><mi>d</mi><mi>l</mi><mtext>&nbsp;</mtext><mo>−</mo><mi>W</mi><mi>l</mi><mo separator="true">,</mo><mo>−</mo><mi>r</mi><mi>p</mi><mi>a</mi><mi>t</mi><mi>h</mi><mo separator="true">,</mo><mi mathvariant="normal">"</mi></mrow><annotation encoding="application/x-tex">ASAN_LIB_DIR" -lolsrt -lpthread -lrt -ldl \
      -Wl,-rpath,"</annotation></semantics></math></span><span class="katex-html" aria-hidden="true"><span class="base"><span class="strut" style="height:0.8444em;vertical-align:-0.15em;"></span><span class="mord mathnormal">A</span><span class="mord mathnormal" style="margin-right:0.05764em;">S</span><span class="mord mathnormal">A</span><span class="mord"><span class="mord mathnormal" style="margin-right:0.10903em;">N</span><span class="msupsub"><span class="vlist-t vlist-t2"><span class="vlist-r"><span class="vlist" style="height:0.3283em;"><span style="top:-2.55em;margin-left:-0.109em;margin-right:0.05em;"><span class="pstrut" style="height:2.7em;"></span><span class="sizing reset-size6 size3 mtight"><span class="mord mathnormal mtight">L</span></span></span></span><span class="vlist-s">​</span></span><span class="vlist-r"><span class="vlist" style="height:0.15em;"><span></span></span></span></span></span></span><span class="mord mathnormal" style="margin-right:0.07847em;">I</span><span class="mord"><span class="mord mathnormal" style="margin-right:0.05017em;">B</span><span class="msupsub"><span class="vlist-t vlist-t2"><span class="vlist-r"><span class="vlist" style="height:0.3283em;"><span style="top:-2.55em;margin-left:-0.0502em;margin-right:0.05em;"><span class="pstrut" style="height:2.7em;"></span><span class="sizing reset-size6 size3 mtight"><span class="mord mathnormal mtight" style="margin-right:0.02778em;">D</span></span></span></span><span class="vlist-s">​</span></span><span class="vlist-r"><span class="vlist" style="height:0.15em;"><span></span></span></span></span></span></span><span class="mord mathnormal" style="margin-right:0.07847em;">I</span><span class="mord mathnormal" style="margin-right:0.00773em;">R</span><span class="mord">"</span><span class="mspace" style="margin-right:0.2222em;"></span><span class="mbin">−</span><span class="mspace" style="margin-right:0.2222em;"></span></span><span class="base"><span class="strut" style="height:0.7778em;vertical-align:-0.0833em;"></span><span class="mord mathnormal" style="margin-right:0.01968em;">l</span><span class="mord mathnormal">o</span><span class="mord mathnormal" style="margin-right:0.01968em;">l</span><span class="mord mathnormal" style="margin-right:0.02778em;">sr</span><span class="mord mathnormal">t</span><span class="mspace" style="margin-right:0.2222em;"></span><span class="mbin">−</span><span class="mspace" style="margin-right:0.2222em;"></span></span><span class="base"><span class="strut" style="height:0.8889em;vertical-align:-0.1944em;"></span><span class="mord mathnormal">lpt</span><span class="mord mathnormal">h</span><span class="mord mathnormal">re</span><span class="mord mathnormal">a</span><span class="mord mathnormal">d</span><span class="mspace" style="margin-right:0.2222em;"></span><span class="mbin">−</span><span class="mspace" style="margin-right:0.2222em;"></span></span><span class="base"><span class="strut" style="height:0.7778em;vertical-align:-0.0833em;"></span><span class="mord mathnormal" style="margin-right:0.01968em;">l</span><span class="mord mathnormal" style="margin-right:0.02778em;">r</span><span class="mord mathnormal">t</span><span class="mspace" style="margin-right:0.2222em;"></span><span class="mbin">−</span><span class="mspace" style="margin-right:0.2222em;"></span></span><span class="base"><span class="strut" style="height:0.7778em;vertical-align:-0.0833em;"></span><span class="mord mathnormal" style="margin-right:0.01968em;">l</span><span class="mord mathnormal">d</span><span class="mord mathnormal" style="margin-right:0.01968em;">l</span><span class="mspace">&nbsp;</span><span class="mspace" style="margin-right:0.2222em;"></span><span class="mbin">−</span><span class="mspace" style="margin-right:0.2222em;"></span></span><span class="base"><span class="strut" style="height:0.8889em;vertical-align:-0.1944em;"></span><span class="mord mathnormal" style="margin-right:0.13889em;">W</span><span class="mord mathnormal" style="margin-right:0.01968em;">l</span><span class="mpunct">,</span><span class="mspace" style="margin-right:0.1667em;"></span><span class="mord">−</span><span class="mord mathnormal" style="margin-right:0.02778em;">r</span><span class="mord mathnormal">p</span><span class="mord mathnormal">a</span><span class="mord mathnormal">t</span><span class="mord mathnormal">h</span><span class="mpunct">,</span><span class="mspace" style="margin-right:0.1667em;"></span><span class="mord">"</span></span></span></span>ASAN_LIB_DIR" 
-o "$TEST_BIN" >/tmp/olsrt_test_build.log 2>&1; then
ok "Test binary compiled"
else
tail -40 /tmp/olsrt_test_build.log
fail "Test binary compilation failed"
fi

info "Running Wave-1 regression tests under ASan/UBSan…"
if ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 UBSAN_OPTIONS=print_stacktrace=1 "$TEST_BIN"; then
ok "All Wave-1 tests passed"
else
fail "Wave-1 tests failed"
fi

echo
ok "Wave 1 verification complete."
echo "  ASan+UBSan build: <span class="katex"><span class="katex-mathml"><math xmlns="http://www.w3.org/1998/Math/MathML"><semantics><mrow><mi>A</mi><mi>S</mi><mi>A</mi><msub><mi>N</mi><mi>D</mi></msub><mi>I</mi><mi>R</mi><mi mathvariant="normal">"</mi><mi>e</mi><mi>c</mi><mi>h</mi><mi>o</mi><mi mathvariant="normal">"</mi><mi>T</mi><mi>S</mi><mi>a</mi><mi>n</mi><mi>b</mi><mi>u</mi><mi>i</mi><mi>l</mi><mi>d</mi><mo>:</mo></mrow><annotation encoding="application/x-tex">ASAN_DIR"
echo "  TSan build:       </annotation></semantics></math></span><span class="katex-html" aria-hidden="true"><span class="base"><span class="strut" style="height:0.8444em;vertical-align:-0.15em;"></span><span class="mord mathnormal">A</span><span class="mord mathnormal" style="margin-right:0.05764em;">S</span><span class="mord mathnormal">A</span><span class="mord"><span class="mord mathnormal" style="margin-right:0.10903em;">N</span><span class="msupsub"><span class="vlist-t vlist-t2"><span class="vlist-r"><span class="vlist" style="height:0.3283em;"><span style="top:-2.55em;margin-left:-0.109em;margin-right:0.05em;"><span class="pstrut" style="height:2.7em;"></span><span class="sizing reset-size6 size3 mtight"><span class="mord mathnormal mtight" style="margin-right:0.02778em;">D</span></span></span></span><span class="vlist-s">​</span></span><span class="vlist-r"><span class="vlist" style="height:0.15em;"><span></span></span></span></span></span></span><span class="mord mathnormal" style="margin-right:0.07847em;">I</span><span class="mord mathnormal" style="margin-right:0.00773em;">R</span><span class="mord">"</span><span class="mord mathnormal">ec</span><span class="mord mathnormal">h</span><span class="mord mathnormal">o</span><span class="mord">"</span><span class="mord mathnormal" style="margin-right:0.05764em;">TS</span><span class="mord mathnormal">anb</span><span class="mord mathnormal">u</span><span class="mord mathnormal">i</span><span class="mord mathnormal" style="margin-right:0.01968em;">l</span><span class="mord mathnormal">d</span><span class="mspace" style="margin-right:0.2778em;"></span><span class="mrel">:</span></span></span></span>TSAN_DIR"
echo "  Test binary:      $TEST_BIN"