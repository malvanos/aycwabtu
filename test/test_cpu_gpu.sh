#!/usr/bin/env bash
# -------------------------------------------------------------------------
# Unit tests for simultaneous CPU + GPU hybrid execution.
#
# Run from the test/ directory:  cd test && ./test_cpu_gpu.sh
# (the makefile targets `make test` and `make check` run this automatically).
#
# What it verifies:
#   1. CLI options: -c, --cpu-gpu, --hybrid, and -g with -p <n> activate hybrid mode
#   2. Banner reports Hybrid mode (CPU + GPU simultaneously)
#   3. Algorithm self-test in hybrid mode (-s --cpu-gpu, -s -c, with -S scalar, -p 2)
#   4. Benchmark self check in hybrid mode (-b --cpu-gpu, -b -c, -b --cpu-gpu -p 2)
#   5. End-to-end key find on bundled TS file using hybrid CPU+GPU mode
#   6. Keyfound output file correctness and cross-validation
#   7. Stop key reached (negative range) handling without key find
#
# If the binary was built without OpenCL (CPU-only build - a supported
# configuration), the GPU-dependent sections 3-7 are skipped with a notice
# instead of failing.
# -------------------------------------------------------------------------
set -u

BIN=../aycwabtu
TS=Testfile_CW_7FFAE9A02486.ts

PASS=0
FAIL=0
SKIP=0

ok()   { PASS=$((PASS+1)); echo "  PASS: $1"; }
bad()  { FAIL=$((FAIL+1)); echo "  FAIL: $1"; }

expect_success() {
    local desc="$1"; shift
    if "$@"; then ok "$desc"; else bad "$desc"; fi
}

expect_failure() {
    local desc="$1"; shift
    if "$@"; then bad "$desc (expected non-zero exit)"; else ok "$desc"; fi
}

expect_failure_msg() {
    local desc="$1" pat="$2"; shift 2
    local out rc
    out=$("$@" 2>&1); rc=$?
    if [ $rc -ne 0 ] && echo "$out" | grep -Eq "$pat"; then
        ok "$desc"
    elif [ $rc -ne 0 ]; then
        bad "$desc (failed but no '$pat' in output)"
    else
        bad "$desc (unexpected success)"
    fi
}

cleanup() { rm -f keyfound resume resume-*; }
trap cleanup EXIT
cleanup

echo "== CPU + GPU Hybrid Unit Tests =="
echo

[ -x "$BIN" ] || { echo "FAIL: binary $BIN not found (build first)"; exit 1; }
[ -f "$TS" ]  || { echo "FAIL: test TS $TS not found"; exit 1; }

# Check if OpenCL is supported by this build
HELP=$("$BIN" -h 2>&1)
if ! echo "$HELP" | grep -q -- "-c, --cpu-gpu"; then
    echo "FAIL: -c, --cpu-gpu not in help text"
    exit 1
fi

# ------------------------------------------------------------------ 1. CLI options
echo "-- 1. CLI options and help"
ok "-h documents -c, --cpu-gpu"

expect_failure_msg "missing input without -b or -s fails" "Neither ts filename provided" \
    "$BIN" --cpu-gpu

# ------------------------------------------------------------------ 2. Banner detection
echo
echo "-- 2. Mode banner"
OUT=$("$BIN" --cpu-gpu -s 2>&1)
if echo "$OUT" | grep -q "Hybrid mode (CPU + GPU simultaneously)"; then
    ok "--cpu-gpu activates hybrid mode in banner"
else
    bad "--cpu-gpu banner output"
fi

OUT=$("$BIN" -c -s 2>&1)
if echo "$OUT" | grep -q "Hybrid mode (CPU + GPU simultaneously)"; then
    ok "-c activates hybrid mode in banner"
else
    bad "-c banner output"
fi

OUT=$("$BIN" -g -p 2 -s 2>&1)
if echo "$OUT" | grep -q "Hybrid mode (CPU + GPU simultaneously)"; then
    ok "-g -p 2 activates hybrid mode in banner"
else
    bad "-g -p 2 banner output"
fi

# ------------------------------------------------------------------ GPU availability
# A CPU-only build (no OpenCL toolchain at build time) is a supported
# configuration: the binary still runs, but hybrid/GPU runs abort with a
# clear error.  Detect that once here and skip the GPU-dependent sections
# (3-6) below instead of failing them.
echo
echo "-- GPU availability"
if "$BIN" --cpu-gpu -s 2>&1 | grep -qF "GPU (OpenCL) support was not compiled into this build"; then
    SKIP=$((SKIP+1))
    echo "  SKIP: binary built without OpenCL (CPU-only build);"
    echo "        hybrid/GPU sections 3-6 skipped.  Rebuild on a machine with an"
    echo "        OpenCL toolchain (WITH_OPENCL=1) to run them."
    echo
    echo "=========================================="
    echo "CPU+GPU hybrid unit tests: $PASS passed, $FAIL failed, $SKIP skipped (OpenCL not compiled in)"
    echo "=========================================="
    if [ "$FAIL" -eq 0 ]; then exit 0; else exit 1; fi
fi
ok "binary supports OpenCL hybrid mode"

# ------------------------------------------------------------------ 3. Self-test in hybrid mode
echo
echo "-- 3. Algorithm self-test in CPU + GPU hybrid mode"
OUT=$("$BIN" -s --cpu-gpu 2>&1)
RC=$?
if [ $RC -eq 0 ] && echo "$OUT" | grep -q "CPU+GPU hybrid self-test: PASSED" && echo "$OUT" | grep -q "Self-test PASSED"; then
    ok "-s --cpu-gpu self-test passed"
else
    bad "-s --cpu-gpu self-test failed"
    echo "$OUT" | tail -10
fi

OUT=$("$BIN" -s -c 2>&1)
RC=$?
if [ $RC -eq 0 ] && echo "$OUT" | grep -q "CPU+GPU hybrid self-test: PASSED"; then
    ok "-s -c self-test passed"
else
    bad "-s -c self-test failed"
fi

OUT=$("$BIN" -s --cpu-gpu -S scalar 2>&1)
RC=$?
if [ $RC -eq 0 ] && echo "$OUT" | grep -q "CPU+GPU hybrid self-test (scalar + OpenCL)"; then
    ok "-s --cpu-gpu -S scalar passed"
else
    bad "-s --cpu-gpu -S scalar failed"
fi

OUT=$("$BIN" -s --cpu-gpu -p 2 2>&1)
RC=$?
if [ $RC -eq 0 ] && echo "$OUT" | grep -q "CPU+GPU hybrid self-test: PASSED"; then
    ok "-s --cpu-gpu -p 2 passed"
else
    bad "-s --cpu-gpu -p 2 failed"
fi

# ------------------------------------------------------------------ 4. Benchmark self-check in hybrid mode
echo
echo "-- 4. Benchmark self check in CPU + GPU hybrid mode"
cleanup
OUT=$("$BIN" -b --cpu-gpu 2>&1)
RC=$?
if [ $RC -eq 0 ] && echo "$OUT" | grep -q "KEY FOUND" && echo "$OUT" | grep -q "00 11 22 \[33\]  44 00 00 \[44\]"; then
    ok "-b --cpu-gpu benchmark self check found known demo key"
    if echo "$OUT" | grep -q "Benchmark complete"; then
        ok "  benchmark reports aggregate Mcw/s and breakdown"
    else
        bad "  benchmark report missing"
    fi
else
    bad "-b --cpu-gpu benchmark self check failed"
    echo "$OUT" | tail -10
fi

cleanup
OUT=$("$BIN" -b -c 2>&1)
RC=$?
if [ $RC -eq 0 ] && echo "$OUT" | grep -q "KEY FOUND"; then
    ok "-b -c benchmark self check passed"
else
    bad "-b -c benchmark self check failed"
fi

cleanup
OUT=$("$BIN" -b --cpu-gpu -p 2 2>&1)
RC=$?
if [ $RC -eq 0 ] && echo "$OUT" | grep -q "KEY FOUND"; then
    ok "-b --cpu-gpu -p 2 (multi-thread CPU + GPU) benchmark self check passed"
else
    bad "-b --cpu-gpu -p 2 benchmark self check failed"
fi

# ------------------------------------------------------------------ 5. End-to-end TS key search
echo
echo "-- 5. End-to-end TS key search with CPU + GPU hybrid mode"
cleanup
OUT=$("$BIN" --cpu-gpu -t "$TS" -a 7FFAE9A00000 2>&1)
RC=$?
if [ $RC -eq 0 ] && echo "$OUT" | grep -q "KEY FOUND" && echo "$OUT" | grep -q "7F FA E9 \[62\]  A0 24 86 \[4A\]"; then
    ok "e2e key find with --cpu-gpu"
    if [ -f keyfound ] && [ "$(cat keyfound)" = "7F FA E9 62 A0 24 86 4A" ]; then
        ok "  keyfound file verified: 7F FA E9 62 A0 24 86 4A"
    else
        bad "  keyfound file missing or invalid content"
    fi
else
    bad "e2e key find with --cpu-gpu failed"
    echo "$OUT" | tail -10
fi

cleanup
OUT=$("$BIN" -c -p 2 -t "$TS" -a 7FFAE9A00000 2>&1)
RC=$?
if [ $RC -eq 0 ] && echo "$OUT" | grep -q "KEY FOUND"; then
    ok "e2e key find with -c -p 2"
    if [ -f keyfound ] && [ "$(cat keyfound)" = "7F FA E9 62 A0 24 86 4A" ]; then
        ok "  keyfound file verified"
    else
        bad "  keyfound file missing or invalid"
    fi
else
    bad "e2e key find with -c -p 2 failed"
fi

# Test multi-chunk search where start key is before the target
cleanup
OUT=$("$BIN" --cpu-gpu -t "$TS" -a 7FFAE99F0000 -o 7FFAE9A10000 2>&1)
RC=$?
if [ $RC -eq 0 ] && echo "$OUT" | grep -q "KEY FOUND"; then
    ok "e2e multi-chunk search across key boundary in hybrid mode"
else
    bad "e2e multi-chunk search failed"
fi

# ------------------------------------------------------------------ 6. Stop key reached without find
echo
echo "-- 6. Stop key reached (negative range)"
cleanup
OUT=$("$BIN" --cpu-gpu -t "$TS" -a 000000000000 -o 000000000002 2>&1)
RC=$?
if [ $RC -eq 0 ] && echo "$OUT" | grep -q "Stop key reached. No key found"; then
    ok "range without key completes cleanly with stop key reached"
else
    bad "range without key failed or did not report stop key reached"
    echo "$OUT" | tail -5
fi

cleanup

echo
echo "=========================================="
echo "CPU+GPU hybrid unit tests: $PASS passed, $FAIL failed, $SKIP skipped"
echo "=========================================="

[ "$FAIL" -eq 0 ]
