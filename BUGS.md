# Bugs found during the `auto-detect` branch review

Each entry lists the affected file, the symptom/impact, the root cause, and the
fix applied on this branch.  Bugs are ordered roughly by severity.

---

## 1. Single-threaded resume was silently broken

**File:** `src/bs_driver_impl.cpp` (`ayc_bruteForceRange`)

**Impact:** A real single-threaded CPU search (`./aycwabtu -t file.ts -a …`,
the most common usage) never resumed from a previously written checkpoint.
Every restart began from the `-a` key, throwing away all progress.  This was a
**regression** introduced by the driver move: the original `main.cpp` ran the
single-threaded search with `tid = -1`, but the new entry point used `tid = 0`.

**Root cause:** `bfWriteResumeFile` writes the file as
`"resume-<tid>"` when `tid >= 0` and as plain `"resume"` when `tid < 0`.  The
reader in `main.cpp` (`bfReadResumeFile`) always opens `"resume"`.  With
`tid = 0` the writer produced `"resume-0"` while the reader looked for
`"resume"` — the two never matched, so resume was dead for single-threaded
mode.

**Fix:** `ayc_bruteForceRange` now calls `bruteForceRangeImpl` with
`tid = -1`, restoring the original single-threaded semantics (writes
`"resume"`, clean progress format without a `[T0]` prefix).  The self-test
path keeps `tid = 0` because it relies on `keyFound` becoming `1` via the
`compare_exchange_strong(expected, tid+1)` CAS to detect success (with
`tid = -1` the CAS target is `0` and the find is undetectable); the self-test
runs with `isBenchmark = true` so no resume file is written there anyway.

---

## 2. GPU chunk loop could hang or re-scan forever

**File:** `src/main.cpp` (`bruteForceGPU`)

**Impact:** Two distinct failure modes in the OpenCL search loop:

  a. **Infinite re-scan.** The loop cursor was a `uint32_t`:
     `for (uint32_t chunkStart = keyStart; chunkStart <= keyStop;
     chunkStart += chunkSize)`.  When `keyStop` is near `0xFFFFFFFF` (the
     default `-o` is `FFFFFFFFFFFF` → `0xFFFFFFFF`), `chunkStart += chunkSize`
     wraps past `0xFFFFFFFF` back to a small value, so `chunkStart <= keyStop`
     becomes true again and the search re-scans the key space from the
     beginning — forever, never terminating.

  b. **`chunkSize = 0` hang.** `AYCWABTU_GPU_CHUNK_SIZE` was read with
     `std::atoi` and cast straight to `uint32_t` with no validation.  Setting
     the env var to `0` (or any non-numeric/garbage value that `atoi` maps to
     `0`) made `chunkStart += 0` never advance — an instant infinite loop.

  c. **`nan%` progress + division by zero.** `totalOuterKeys = keyStop -
     keyStart + 1` is `uint32_t` and overflows to `0` for the default full
     range (`0 - 0xFFFFFFFF + 1`), producing `nan%` in the progress line and a
     potential divide-by-zero in `pctDone`/`mcwPerSec` (the latter also divided
     by `elapsed`, which is `0` on the very first iteration).

**Fix:** The loop now uses a 64-bit cursor (`uint64_t cur`) so it always
terminates once `cur > (uint64_t)keyStop`.  `chunkSize` is validated
(`std::atol`, reject `< 1` with a clear error and `exit(ERR_USAGE)`).
`totalOuterKeys` and `outerKeysDone` are `uint64_t`, and both the
`elapsed > 0` and `totalOuterKeys > 0` divisions are guarded.

---

## 3. `-S list` printed a bogus 5th `(null)` backend

**File:** `src/bs_dispatch.h` / `src/bs_dispatch.cpp`

**Impact:** `./aycwabtu -S list` showed five rows instead of four, the last
being `(null) (null)  batch 0 keys  [not built for this architecture]`, and
`bs_drivers()` reported a count of 5.  Any caller iterating the table with the
reported count would walk a zero-initialised phantom entry.

**Root cause:** the `BSimdB` enum was
`AUTO=0, SCALAR, SSE2, NEON, AVX2, __COUNT`.  Because `AUTO` participated in
the value sequence, `__COUNT` was 5, but the driver table only has 4 concrete
entries (scalar/sse2/neon/avx2) — the 5th slot was value-initialised to zero
(`name = nullptr`).

**Fix:** `AUTO` is now a sentry value (`= 99`) placed *after* `__COUNT`, so
`__COUNT` counts only the concrete drivers and equals 4.

---

## 4. `-s` (auto self-test) only ever tested the *first* backend

**File:** `src/bs_driver_impl.cpp` (`bruteForceRangeImpl`, `selfTestImpl`)

**Impact:** The headline feature of the branch — "run the self-test for every
available backend" — did not work.  `./aycwabtu -s` executed the self-test for
only the first backend and then terminated, so the other backends were never
validated.  (`-s -S <name>` for a single backend worked.)

**Root cause:** the brute-force core called `exit(OK)` the moment a key was
found and verified.  That is correct for a real search (stop ASAP), but in
self-test mode the end-to-end sub-test finds the known key on the first outer
iteration, so `exit(OK)` killed the whole process before the per-backend loop
in `runSelfTest` could advance to the next backend.

**Fix:** a per-backend `static std::atomic<bool> bf_selftest_mode` was added.
`selfTestImpl` sets it `true` around the end-to-end call; in that mode the
core sets `keyFound`, prints the result, and **returns** instead of calling
`exit(OK)`.  `selfTestImpl` then checks `keyFound` and reports PASS/FAIL.  A
real search leaves the flag `false`, so the original `exit(OK)` ASAP behaviour
is unchanged.

---

## 5. Duplicate symbol `probedata` across backends

**File:** `src/bs_testcases.c`

**Impact:** The link failed with `duplicate symbol '_probedata'` as soon as
two backends were compiled into one binary (e.g. scalar + neon on ARM64,
scalar + sse2 + avx2 on x86_64).

**Root cause:** `bs_testcases.c` defined a file-scope global
`unsigned char probedata[3][16]` with external linkage.  Because it was not
covered by `bs_rename.h` (it is a plain demo-data array, not an `aycw_*`
algorithm symbol), each backend's `bs_testcases.o` exported an identically
named `_probedata`, colliding at link time.

**Fix:** the global is now `static` (internal linkage).  It is only referenced
inside `bs_testcases.c` itself (confirmed by grep; `bs_driver_impl.cpp` uses a
parameter/local named `probedata`, not the global).

---

## 6. `make check` was broken on macOS (no `timeout`)

**File:** `makefile`

**Impact:** `make check` hard-coded `/usr/bin/timeout`, which does not exist on
macOS (nor on a default Windows setup).  The very first recipe line failed with
`command not found` (exit 127) *before* `test_simd.sh` or `testframe.sh` ever
ran, so the SIMD unit tests were never executed by `make check` on macOS.

**Fix:** a portable `TIMEOUT` make variable is resolved once
(`command -v timeout || command -v gtimeout`), and recipes use
`$(if $(TIMEOUT),$(TIMEOUT) 5) …` — bounded on Linux/macOS-with-coreutils,
and simply unbounded on systems without a `timeout` binary rather than
erroring out.

---

## 7. `test_simd.sh` negative-message checks gave false failures

**File:** `test/test_simd.sh`

**Impact:** Two "error message is explicit" assertions reported FAIL even
though the program both failed *and* printed the expected message.

**Root cause:** those checks ran
`bash -c "$BIN … 2>&1 | grep -q 'pattern'"`.  The exit status of a pipeline is
the exit status of its *last* command (`grep`), which is `0` when the pattern
matched — so the `expect_failure` helper saw success and reported a failure.

**Fix:** a new `expect_failure_msg` helper captures the program's output and
exit code directly (`out=$( "$@" 2>&1); rc=$?`) and asserts `rc != 0` **and**
the pattern present, using `grep -Eq` (extended regex, so `|` alternation
works — the old `grep -q` BRE treated `|` literally).

---

## 8. Cosmetic: confusing self-test sub-test labels

**File:** `src/bs_driver_impl.cpp` (`selfTestImpl`)

**Impact:** the round-trip and decrypt-vs-libdvbcsa sub-tests printed
`[1a/2b]` and `[1b/2b]` — there is no "2b" sub-test, so the labels were
misleading.

**Fix:** relabelled to `[1a]` and `[1b]`.

---

## False alarms (investigated, not bugs)

For the record, these were checked and are **not** problems:

* **`aycw_selftest` / `aycw_testpattern` in `bs_algo.c`** are inside `#if 0`
  (dead code), so their absence from `bs_rename.h` causes no duplicate
  symbol.  Likewise `aycw_bit2byteslice` in `bs_algo.c` is guarded by
  `#ifdef USE_SLOW_BIT2BYTESLICE` (never defined); the real definition comes
  from each per-backend `bs_<backend>.c`.  Every actually-compiled non-static
  extern in the backend `.c` files is covered by `bs_rename.h`, so there are
  no further duplicate-symbol collisions lurking for x86_64 (scalar+sse2+avx2).

* **`bs_dispatch.cpp` declaring `extern "C"` symbols for backends not built on
  the current arch** (e.g. `neon` on x86, `sse2`/`avx2` on ARM) is harmless:
  those declarations are never referenced (the table stores `nullptr` for
  unbuilt backends), so the linker does not require a definition.

* **`bfPerfShow`'s `#define DIVIDER` / `#undef DIVIDER`** is properly scoped
  inside that one function and does not leak into `bfPerfAggregate`.
