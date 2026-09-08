# Implementation Plan: AYCWABTU Bug Fixes & Auto-Detect Consolidation (PLAN2)

This plan documents the root causes, proposed code changes, and verification steps for all bugs identified in the codebase, distinguishing between regressions tied to the `auto-detect` branch and general pre-existing issues.

---

## 1. Scope & Categorization

### Category A: `auto-detect` Branch Issues & Search Loop Fixes
These issues were either directly introduced on the `auto-detect` branch (e.g. in commit `9f88499`) or belong to the per-backend driver (`src/bs_driver_impl.cpp`) created during the SIMD auto-detection refactoring.
1. **GPU Search 32-bit Truncation Infinite Spin** (`src/main.cpp`)
2. **CPU Search Infinite Wrap at End-of-Keyspace (`0xFFFFFFFF`)** (`src/bs_driver_impl.cpp`)
3. **Multi-Threaded Underflow when `range < nThreads`** (`src/bs_driver_impl.cpp`)
4. **Data Race on `static int divider` in Multi-Threaded Search** (`src/bs_driver_impl.cpp`)
5. **Cosmetic `[T-1]` Progress Output & 32-bit Shift UB** (`src/bs_driver_impl.cpp`, `src/main.cpp`)

### Category B: Resume File & CLI State Handling
6. **Existing `resume` File Silently Overwrites User `-a` Parameter** (`src/main.cpp`)
7. **`fread` Buffer Under-Read / Unterminated Stack String in `bfReadResumeFile`** (`src/main.cpp`)

### Category C: OpenCL Boundary & Input Validation
8. **Missing GPU Work-Item Bounds Check for Non-Multiple Launch Sizes** (`src/aycwabtu.cl`, `src/ocl.cpp`)
9. **`AYCWABTU_OCL_NGROUPS_CAP=0` Infinite Loop** (`src/ocl.cpp`)

### Category D: Parser Safety & Typo Fixes
10. **Unterminated String / Stack Overrun in TS Probe Output** (`src/ts.c`)
11. **SSE2 Fallback Macro Typo Referencing `c`** (`src/bs_sse2.h`)
12. **Format String Typos** (`src/ts.c`)

---

## 2. Detailed Technical Fixes & Proposed Code Diffs

### Phase 1: `auto-detect` & Core Search Driver Fixes

#### 1.1 GPU Chunk Loop 32-bit Truncation (`src/main.cpp`)
* **Problem**: Commit `9f88499` changed the GPU loop cursor to `uint64_t cur`, but cast `(uint64_t)keyStop - cur + 1` into `uint32_t remaining`. For default range (`keyStart=0, keyStop=0xFFFFFFFF`), this truncates $2^{32}$ to `0`, setting `count = 0` and spinning indefinitely.
* **Proposed Diff**:
```diff
--- a/src/main.cpp
+++ b/src/main.cpp
@@ -388,8 +388,8 @@ static void bruteForceGPU(const Settings& settings,
     /* 64-bit cursor so the loop ALWAYS terminates even when keyStop is
        near 0xFFFFFFFF: a 32-bit `chunkStart += chunkSize` would wrap past
        the end and re-scan from the beginning forever. */
     for (uint64_t cur = keyStart; cur <= (uint64_t)keyStop; ) {
         uint32_t chunkStart = (uint32_t)cur;
-        uint32_t remaining  = (uint32_t)((uint64_t)keyStop - cur + 1);
-        uint32_t count = chunkSize < remaining ? chunkSize : remaining;
+        uint64_t remaining  = (uint64_t)keyStop - cur + 1;
+        uint32_t count = (remaining < (uint64_t)chunkSize) ? (uint32_t)remaining : chunkSize;
```

#### 1.2 CPU Search Infinite Wrap at `0xFFFFFFFF` (`src/bs_driver_impl.cpp`)
* **Problem**: `while (st.currentkey32 <= st.stopkey32)` never terminates when `stopkey32 == 0xFFFFFFFF` because `0xFFFFFFFF + 1` wraps to `0`, which is `<= 0xFFFFFFFF`.
* **Proposed Diff**:
```diff
--- a/src/bs_driver_impl.cpp
+++ b/src/bs_driver_impl.cpp
@@ -367,6 +367,9 @@ static void bruteForceRangeImpl(uint32_t keyStart, uint32_t keyStop,
         if (!st.benchmark) bfWriteResumeFile(st, tid);
 
+        if (st.currentkey32 == st.stopkey32) {
+            break;
+        }
         st.currentkey32++;
     }
```

#### 1.3 Multi-Threaded Partition Underflow (`src/bs_driver_impl.cpp`)
* **Problem**: When `range < nThreads` (e.g. searching 1 key `-a 0 -o 0 -p 2`), `chunk = range / nThreads` evaluates to `0`. `start0 + chunk - 1` underflows to `0xFFFFFFFF`, causing thread 0 to search the entire 4-billion key space.
* **Proposed Diff**:
```diff
--- a/src/bs_driver_impl.cpp
+++ b/src/bs_driver_impl.cpp
@@ -382,6 +382,13 @@ static void bruteForceParallelImpl(uint32_t keystart, uint32_t keystop,
                                    int nThreads, bool isBenchmark,
                                    unsigned char probedata[3][16]) {
-    const uint32_t range = keystop - keystart;
-    const uint32_t chunk = range / nThreads;
+    const uint64_t totalKeys = (uint64_t)keystop - keystart + 1;
+    if (totalKeys < (uint64_t)nThreads) {
+        nThreads = (int)totalKeys;
+    }
+    const uint32_t chunk = (uint32_t)(totalKeys / nThreads);
+    const uint32_t remainder = (uint32_t)(totalKeys % nThreads);
```
Distribute `chunk` and `remainder` so all threads receive valid, non-overlapping `[start, stop]` slices without underflow.

#### 1.4 Data Race on `static int divider` in Multi-Threading (`src/bs_driver_impl.cpp`)
* **Problem**: `bfWriteResumeFile` uses `static int divider = 10;`, which is mutated concurrently by worker threads without synchronization.
* **Proposed Diff**:
Move `resumeDivider` into `struct BFState` (per-thread state) so each thread has independent divider counters:
```diff
--- a/src/bs_driver_impl.cpp
+++ b/src/bs_driver_impl.cpp
@@ -73,6 +73,7 @@ struct BFState {
     int      divider      = 0;
+    int      resumeDivider = 10;
     bool     benchmark;
 };
...
-    static int divider = 10;
-    divider++;
-    divider &= 0x1ff;
-    if (!divider) {
+    st.resumeDivider++;
+    st.resumeDivider &= 0x1ff;
+    if (!st.resumeDivider) {
```

#### 1.5 Finalize Unstaged Cosmetic `[T-1]` and Shift UB Changes
Commit the current working tree modifications in `src/bs_driver_impl.cpp` (suppressing `[T-1]` in single-threaded output) and `src/main.cpp` (casting `(uint32_t)tmp[0] << 24` to prevent signed shift overflow).

---

### Phase 2: Resume File & CLI State Handling

#### 2.1 Resume File Precedence Policy (`src/main.cpp`)
* **Problem**: `main()` unconditionally calls `bfReadResumeFile(&settings.keystart)` in single-threaded mode, silently overwriting any `-a <cw>` supplied on the CLI.
* **Proposed Diff**:
Add `bool keystartSpecified = false;` to `Settings`. Set it `true` when `-a` is encountered.
In `main()`:
```cpp
if (!settings.benchmark && settings.numThreads == 1 && !settings.keystartSpecified) {
    bfReadResumeFile(&settings.keystart);
}
```

#### 2.2 `fread` Buffer Under-Read & Null-Termination (`src/main.cpp`)
* **Problem**: `sizeof(buf)` is 27, but the resume file is 24 bytes. `fread(buf, sizeof(buf), 1, f)` requests 1 block of 27 bytes and returns `0`. `buf` remains un-terminated stack memory.
* **Proposed Diff**:
```diff
--- a/src/main.cpp
+++ b/src/main.cpp
@@ -58,16 +58,18 @@ static uint64_t getTicksMs() {
 static void bfReadResumeFile(uint32_t *key) {
     FILE *f = fopen(RESUMEFILENAME, "rb");
     if (f) {
-        char buf[8 * 3 + 2 + 1];
+        char buf[64] = {0};
         unsigned char tmp[8 + 3];
         fseek(f, 0, SEEK_SET);
-        fread(buf, sizeof(buf), 1, f);
+        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
         fclose(f);
-        if (8 == sscanf(buf, "%02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX\n",
+        if (n > 0) {
+            buf[n] = '\0';
+            if (8 == sscanf(buf, "%02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX\n",
                         &tmp[0], &tmp[1], &tmp[2], &tmp[3],
                         &tmp[4], &tmp[5], &tmp[6], &tmp[7])) {
                 *key = (uint32_t)tmp[0] << 24 | (uint32_t)tmp[1] << 16
                       | (uint32_t)tmp[2] << 8 | (uint32_t)tmp[4];
                 printf("resuming at key %08X\n", *key);
             }
+        }
     }
 }
```

---

### Phase 3: OpenCL Boundary & Input Validation

#### 3.1 Kernel Bounds Check for Padding Work-Items (`src/aycwabtu.cl`, `src/ocl.cpp`)
* **Problem**: `global_size` is rounded up to multiples of 128 (`wg_size`). Extra work-items beyond `chunk` run CSA searches on keys outside the user's requested range.
* **Fix**:
  1. Add `u32 key_count` parameter to `aycwabtu_search` in `src/aycwabtu.cl`.
  2. Add early guard: `if (gid >= key_count || found[0] != 0) return;`.
  3. In `src/ocl.cpp`, pass `chunk` as kernel argument 3 (and adjust subsequent argument indices).

#### 3.2 Validate `AYCWABTU_OCL_NGROUPS_CAP` (`src/ocl.cpp`)
* **Problem**: Setting `AYCWABTU_OCL_NGROUPS_CAP=0` or garbage text leads to `itemsCap = 0`, causing an immediate infinite loop in `ocl_search`.
* **Fix**: Validate that `ngroups_cap >= 1`, rejecting invalid values with an error message and `exit(ERR_USAGE)`.

---

### Phase 4: Parser Safety & Typo Fixes

#### 4.1 Null-Terminate `probetsfilename` in TS Probe Generation (`src/ts.c`)
* **Problem**: `memcpy(probetsfilename, tsfile, strlen(tsfile))` does not append `\0`. `strrchr` searches past the string into uninitialized stack memory.
* **Fix**: Use `snprintf(probetsfilename, sizeof(probetsfilename), "%s", tsfile);`.

#### 4.2 SSE2 Fallback Macro Typo Fix (`src/bs_sse2.h`)
* **Problem**: Line 108: `#define BS_EXTRACT32(a,n) BS_EXTLS32(BS_SHR8(c, (n*4)))` uses `c` instead of `a`.
* **Fix**: Replace `c` with `(a)`.

#### 4.3 Format String Fixes (`src/ts.c`)
* Remove unused `tsfile` in `printf("searching for encrypted packets...\n", tsfile);`.
* Fix `&d` typo in `msgDbg(2, "... pid &d\n", lockpid, pid);` -> `%d`.

---

## 3. Verification Plan

### Automated Regression Suite
1. `make clean && make -j` (build clean binary with all backends).
2. `make test` (runs `test/test_simd.sh`: all backend self-tests, runtime auto-detect, and end-to-end key find).
3. `make check` (runs all SIMD tests, TS corruption suite, and adaptation field test).

### Edge-Case Verification Scenarios
| Test Case | Command | Expected Result |
|---|---|---|
| **GPU Default Range** | `./aycwabtu -t test/Testfile_CW_7FFAE9A02486.ts -g` | Begins search immediately, progress advances past 0.0%, non-zero Mcw/s (no infinite 0-spin). |
| **CPU End-of-Keyspace** | `./aycwabtu -t test/Testfile_CW_7FFAE9A02486.ts -a FFFFFFFF0000 -o FFFFFFFFFFFF` | Searches key `FFFFFFFF`, terminates cleanly with `"Stop key reached. No key found"`, exits code 0 (no wrap to 0). |
| **Narrow Range Multi-Threaded** | `./aycwabtu -t test/Testfile_CW_7FFAE9A02486.ts -a 000000000000 -o 000000000000 -p 4` | Clamps active threads to 1, tests 1 key, exits cleanly (no 4-billion key underflow). |
| **Resume Precedence** | Run with existing `resume`, then execute `./aycwabtu -t test/Testfile_CW_7FFAE9A02486.ts -a 7FFAE9A00000` | Searches start key `7FFAE9A0` directly without being overridden by `resume`. |
