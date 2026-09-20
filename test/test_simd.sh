#!/usr/bin/env bash
# -------------------------------------------------------------------------
# Unit tests for run-time SIMD backend auto-detection / manual selection.
#
# Run from the test/ directory:  cd test && ./test_simd.sh
# (the makefile target `make test` does exactly that).
#
# What it verifies:
#   1. -S list prints the backend table (and drives the rest of the script)
#   2. -s (auto) runs the algorithm self-test for EVERY built+supported
#      backend and reports overall PASS
#   3. -s -S <name> runs the self-test for a single chosen backend
#   4. end-to-end key find on the bundled TS for each built+supported
#      backend (the known key must be found, exactly the same in every
#      backend)
#   5. negative: an unknown backend name must fail
#   6. negative: a backend that is not built / not supported must fail with
#      a clear error instead of silently running another backend
# -------------------------------------------------------------------------
set -u

BIN=../aycwabtu
TS=Testfile_CW_7FFAE9A02486.ts

PASS=0
FAIL=0

ok()   { PASS=$((PASS+1)); echo "  PASS: $1"; }
bad()  { FAIL=$((FAIL+1)); echo "  FAIL: $1"; }

# helper: run a command, expect success (exit 0) or failure (non-zero)
expect_success() { # $1=description, rest = command
    local desc="$1"; shift
    if "$@"; then ok "$desc"; else bad "$desc"; fi
}
expect_failure() { # $1=description, rest = command
    local desc="$1"; shift
    if "$@"; then bad "$desc (expected non-zero exit)"; else ok "$desc"; fi
}
# helper: expect the command to fail AND print <pattern> on stderr/stdout
expect_failure_msg() { # $1=description, $2=pattern, rest = command
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
cleanup   # also at start: a stale resume file from a prior run would perturb the e2e searches

echo "== SIMD backend unit tests =="
echo

[ -x "$BIN" ] || { echo "FAIL: binary $BIN not found (build first)"; exit 1; }
[ -f "$TS" ]  || { echo "FAIL: test TS $TS not found"; exit 1; }

# ------------------------------------------------------------------ 1. list
echo "-- 1. -S list table"
LIST=$("$BIN" -S list)
echo "$LIST" | head -3 >/dev/null
if echo "$LIST" | grep -q "SIMD backends:"; then
    ok "-S list prints the backend table"
else
    bad "-S list prints the backend table"
fi

# determine which backends are built+supported on this machine.
# lines look like:
#   scalar  32-bit scalar (portable) batch  32 keys  [available]
#   sse2    SSE2 (128-bit)           batch 128 keys  [not built for this architecture]
AVAILABLE=()
UNAVAILABLE=()
while read -r name rest; do
    [ -n "${name:-}" ] || continue
    case "$name" in
        scal*|sse2|avx2|neon)
            if echo "$rest" | grep -q "\[available\]"; then
                AVAILABLE+=("$name")
            else
                UNAVAILABLE+=("$name")
            fi
            ;;
    esac
done <<< "$LIST"

[ "${#AVAILABLE[@]}" -ge 1 ] || { echo "FAIL: no available backend detected"; exit 1; }
ok "-S list reports ${#AVAILABLE[@]} available backend(s): ${AVAILABLE[*]}"

# ------------------------------------------------------------------ 2. auto
echo
echo "-- 2. -s auto self-test over all backends"
SELF_AUTO=$("$BIN" -s 2>&1)
if echo "$SELF_AUTO" | grep -q "Self-test PASSED"; then
    ok "-s auto self-test passed"
else
    bad "-s auto self-test passed"
    echo "$SELF_AUTO" | tail -5
fi

n_runs=0
for b in "${AVAILABLE[@]}"; do
    if echo "$SELF_AUTO" | grep -q "not built for this architecture" ; then
        bad "auto self-test tried an unavailable backend"
    fi
    echo "$SELF_AUTO" | grep -q "backend: $b " && n_runs=$((n_runs+1))
done
if [ "$n_runs" -eq "${#AVAILABLE[@]}" ]; then
    ok "-s auto covered every available backend ($n_runs/${#AVAILABLE[@]})"
else
    bad "-s auto covered every available backend ($n_runs/${#AVAILABLE[@]})"
fi

# ------------------------------------------------------------------ 3. per
echo
echo "-- 3. per-backend self-test (-s -S <name>)"
for b in "${AVAILABLE[@]}"; do
    out=$("$BIN" -s -S "$b" 2>&1)
    if echo "$out" | grep -q "backend $b: PASSED"; then
        ok "self-test -s -S $b"
    else
        bad "self-test -s -S $b"
        echo "$out" | tail -3
    fi
done

# ------------------------------------------------------------------ 4. e2e
echo
echo "-- 4. end-to-end key find for each backend (-t $TS -a 7FFAE9A00000)"
for b in "${AVAILABLE[@]}"; do
    out=$("$BIN" -t "$TS" -a 7FFAE9A00000 -S "$b" 2>&1)
    rc=$?
    if [ $rc -eq 0 ] && echo "$out" | grep -q "KEY FOUND"; then
        key=$(echo "$out" | grep "KEY FOUND" | head -1)
        ok "e2e key find with -S $b ($key)"
        if [ -f keyfound ]; then
            found_in_file=$(cat keyfound)
            rm -f keyfound
            # outer 6 bytes must match the known key 7F FA E9 62 A0 24
            if [ "$found_in_file" = "7F FA E9 62 A0 24 86 4A" ]; then
                ok "  keyfound file content matches (all backends agree)"
            else
                bad "  keyfound file content unexpected: [$found_in_file]"
            fi
        else
            bad "e2e did not write keyfound file"
        fi
    else
        bad "e2e key find with -S $b (rc=$rc)"
        echo "$out" | tail -3
    fi
done

# ------------------------------------------------------------------ 5. neg
echo
echo "-- 5. negative: unknown backend name"
expect_failure "-S bogusbackend fails" "$BIN" -S bogusbackend -t "$TS"
expect_failure_msg "-S bogusbackend error message" "unknown SIMD backend" \
    "$BIN" -S bogusbackend -t "$TS"

# ------------------------------------------------------------------ 6. neg
echo
echo "-- 6. negative: backend not built / not supported"
if [ "${#UNAVAILABLE[@]}" -gt 0 ]; then
    for b in "${UNAVAILABLE[@]}"; do
        expect_failure "-S $b fails on this machine" "$BIN" -S "$b" -t "$TS"
        expect_failure_msg "-S $b error message is explicit" \
            "not compiled into this binary|not supported" "$BIN" -S "$b" -t "$TS"
    done
else
    ok "(no unavailable backends to test on this machine)"
fi

echo
echo "=========================================="
echo "SIMD unit tests: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
exit 0