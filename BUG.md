# BUG.md — cl2Metal Threadgroup-Count Corruption (Apple M2 Pro)

**Status**: OPEN — no fix available at the source; mitigated with a hard dispatch
cap + CPU-verify gate.
**Severity**: high (silent data corruption → fabricated search results)
**Component**: OpenCL GPU path (`src/ocl.cpp`, `src/main.cpp`) running through
Apple's OpenCL→Metal translation (`cl2Metal`) on the Apple M2 Pro.

---

## 1. Symptom

The brute-force GPU search intermittently produces **wrong / impossible
results** that are indistinguishable from a valid find at the kernel level:

- A "found" control word (CW) with **impossible checksums**.
- A reported outer key **outside the launched search range**.
- **False negatives**: a real key in the searched range is *not* found
  (threadgroup reports all-zero / corrupt checksum).

On the M2 Pro this appears as soon as a **single kernel dispatch exceeds
~72 threadgroups** (wg=128 ⇒ ~9,200 work-items). Below that it is rare;
the transition is not a clean wall — it is **probabilistic and
nondeterministic** (a given geometry may be clean one invocation and
corrupt the next).

The corruption is **silent**: `clEnqueueNDRangeKernel` still returns
`CL_SUCCESS`, `clFinish` completes, and the buffers read back normally —
the corrupted data is just *wrong*.

---

## 2. Root Cause

Lives **inside Apple's `cl2Metal` translation layer**, not in this codebase.
A single dispatch of the *heavy* CSA kernel above ~72 threadgroups silently
corrupts work-item state on the M2 Pro.

Wide-ranging isolation experiments (per-gid differential harness, cf. §5)
rule out our kernel characteristics as the cause:

| Kernel variant                              | Clean at 128 groups? |
|---------------------------------------------|----------------------|
| trivial scalar kernel                       | ✅ clean |
| private-array kernel                        | ✅ clean |
| 16 KB `__constant` table + 14-deep-call kernel | ✅ clean |
| real CSA kernel (after A1+A2)               | ❌ corrupts ≥ ~76 groups |

- Grid size alone is **not** the trigger (trivial kernels are clean at large grids).
- `__constant` tables / deep call stacks are **not** the trigger.
- `wg=256 @ 64 groups` is clean but `wg=64 @ 72 groups` corrupts ⇒ the limit is
  **per-dispatch threadgroup count**, not work-item count.
- The **threshold is nondeterministic** across runs:
  - earlier run: G=256 clean, G=512 corrupt
  - later run: G=128 corrupt, clean ceiling 64–72
  - probability trials at fixed G (reps, inner=4096, 8 runs each):
    - `G=64`   — appear all clean (trial incomplete)
    - `G=80`   — clean 2/8, corrupt 6/8
    - `G=96`   — clean 2/8, corrupt 6/8
  - observed symptoms: garbage accumulation state, whole threadgroups losing
    their final writes (all-zero readback), fabricated found-buffer winners

**Implication**: there is **no fixed threadgroup count above ~64 that is
provably always-clean**. Raising the cap to *any* larger constant re-introduces
the corruption. This makes the bug impossible to fix by tuning the geometry
alone.

---

## 3. Impact

Two distinct failure signatures that matter to a brute-force search:

1. **False positive / fabrication** — a corrupt threadgroup reports a winner
   that a real search would never produce. *Catchable* with host-side CPU
   verification (decrypt + PES start-code check).
2. **False negative / miss** — a corrupt threadgroup fails to report the *real*
   key it was assigned. **Not self-detecting**: the search simply appears
   ("in-progress" / later chunks searched) and the key is silently skipped.
   Only mitigated by retries, because corruption is nondeterministic.

The severity in practice is bounded by the mitigations in §4, but the false
negative path remains a correctness risk that halving to a *probability* does
not eliminate.

---

## 4. Current Mitigations (in place)

| Mitigation                     | File                  | What it does                                                                                         |
|--------------------------------|-----------------------|------------------------------------------------------------------------------------------------------|
| Hard dispatch cap              | `src/ocl.cpp:105`     | `MAX_GROUPS = 64`; every sub-dispatch is ≤ 64 threadgroups × wg=128 = **8192 work-items**. Any larger requested range is split into sequential sub-dispatches. |
| CPU-verify gate                | `src/main.cpp:605`    | `verifyCw()` decrypts all 3 probed packets with the reported CW on the CPU; the key is accepted only if each yields an `0x000001` PES start code. Rejects fabricated winners. |
| Retry loop (false positives)   | `src/main.cpp:721`    | On verify failure, re-runs the chunk up to 3×; an honest relaunch usually finds the real key in the same range (corruption is nondeterministic). |

These make the *reported* results trustworthy (every accepted key is CPU-verified).
They do **not** close the **false-negative** hole (§3.2): a corrupted threadgroup
may silently skip an assigned key, and no retry is currently triggered when a
chunk simply returns "not found".

---

## 5. How to Reproduce

### 5.1 Prerequisites / hardware

- **Apple M2 Pro** (any Apple Silicon with an integrated GPU reproduces this).
- macOS + Apple OpenCL (`cl2Metal`). The bug is platform-specific; it is *not*
  expected on NVIDIA/AMD/Intel OpenCL runtimes.
- Build toolchain: `g++` / `clang++`, `-framework OpenCL`.

### 5.2 Build the differential harness

```bash
cd /Users/michailalvanos/work/sat/biss-c++

g++ -std=c++17 -O2 -DPARALLEL_MODE=3 -I src/ -I src/libdvbcsa/dvbcsa \
    bench_diag.cpp src/ts.c -o bench_diag -framework OpenCL
```

> `PARALLEL_MODE` must match the platform (3 = NEON/arm64). `ts.c` selects the
> SIMD path via `config.h` and defaults to SSE2, which fails to build on arm64,
> so the flag is required.

The harness kernel is `tools/aycwabtu_diag.cl` (generated from the production
kernel by `tools/make_diag.py`; do not edit by hand). It mirrors the production
per-dispatch load closely enough to reproduce the bug while giving the host a
**per-gid observable** (a non-zero, deterministic checksum per work-item).

### 5.3 Differential test (deterministic corruption at large G)

The harness runs each geometry two ways for the **same total work-item count**:

- **reference**: `G` sequential single-threadgroup dispatches (never corrupt);
- **test**: one dispatch of `G` threadgroups;

and compares per-gid checksums. Any mismatch ⇒ corruption at that geometry.

**Usage**: `./bench_diag [wg] [gmax] [reps] [inner_count] [step]`
- `wg`           work-group size (default 128)
- `gmax`         largest threadgroup count to test (default 128)
- `reps`         repetitions per geometry (nondeterminism coverage; default 4)
- `inner_count`  inner keys per work-item (default 65536)
- `step`         0 = doubling sweep, else linear step (default 0)

**Pinpoint the corrupt region (256 chosen as it is reliably above the ceiling):**

```bash
./bench_diag 128 256 2 4096 8     # linear sweep G = 8..256 step 8
```

Sample output (nondeterministic — expect run-to-run variance):

```
device: Apple M2 Pro  (wg=128, gmax=256, reps=2, inner=4096, linear)
diag kernel: max wg 256, private mem 0 B/thread
MAX CLEAN G = 72 groups x 128 wg =    9216 items -- ceiling is below gmax
```

**Show a fully clean ceiling (small G) for contrast:**

```bash
./bench_diag 128 64 2 4096 8
# → "MAX CLEAN G = 64 ... clean through the tested ceiling"
```

### 5.4 Measure the nondeterminism (pass/fail probability at fixed G)

Repeat a *single* geometry many times. Because `step == gmax` yields exactly
one candidate (`Gs = {gmax}`), a fixed `G` can be isolated:

```bash
for G in 64 80 96 112 128; do
    pass=0; fail=0
    for i in $(seq 1 8); do
        out=$(./bench_diag 128 $G 1 4096 $G 2>/dev/null | grep "MAX CLEAN G")
        if echo "$out" | grep -q "clean through the tested ceiling"; then
            pass=$((pass+1)); else fail=$((fail+1)); fi
    done
    echo "G=$G : clean_runs=$pass / fail_runs=$fail (of 8)"
done
```

> Each `$G` variant takes ~30–60 s at `inner=4096`; a full 5×8 matrix can run
> several minutes — budget a per-command timeout (`timeout 300s ...` per G).

Observed so far: `G=64` all-clean; `G=80` and `G=96` ≈ 2/8 clean — confirming
the ceiling is **not stable** above 64.

### 5.5 Reproduce the production symptom (false positive / miss)

Run the integrated binary against the known-key test vector, then inspect for
CPU-verify retries (these appear whenever a corrupt chunk fabricated a winner):

```bash
make && ./aycwabtu -t test/Testfile_CW_7FFAE9A02486.ts -g
```

Watch the log for:

```
CPU verify FAILED (false positive). Re-running chunk %08X ...
Re-run verify OK: 02 02 ...  → eventually accepts the *real* key
```

Temporarily raising `MAX_GROUPS` in `src/ocl.cpp` (e.g. to 256) and re-running
increases the frequency of fabricated winners dramatically — direct confirmation
that the corruption scales with the single-dispatch threadgroup count.

---

## 6. Plans to Solve

The corruption cannot be fixed in this codebase (it is inside Apple's runtime).
The goal is to **either avoid the corrupt geometry entirely or make both the
false-positive *and* false-negative paths safe**, and to **not carry the
performance penalty on platforms that don't need it**.

### 6.1 Short term — hardened false-negative handling (recommended next)

Close the one remaining correctness hole (§3.2) that the current gate does not
cover. Today a chunk that *would* contain the real key but returns "not found"
is never retried; only *reported* finds are.

- Detect and re-run "no find" chunks that **should contain the key**:
  - keep the known-key search as a periodic self-test while idle;
  - for the live search, before declaring a range empty, re-run the chunk once
    more (corruption is nondeterministic, so the real key is usually recovered).
- This is cheap (retries only trigger on the not-found path) and closes the miss
  hole without any performance change on the common path.

**Done when**: a key deliberately placed inside a *corrupt-capable* geometry is
always found (0 missed keys over a stress run), and throughput is unchanged.

### 6.2 Mid term — make the dispatch cap platform-conditional (A6)

Currently the 64-TG cap is applied on **all** platforms, during the Apple GPU
money even though only Apple's cl2Metal corrupts.

- Gate the cap on `CL_DEVICE_NAME` (or platform vendor == Apple) rather than a
  bare constant. Add a documented env override, e.g.
  `AYCWABTU_OCL_NGROUPS_CAP=<N>` (0 = unlimited).
- Non-Apple GPUs (NVIDIA / AMD / Intel) and OpenCL CPU runtimes then get large,
  fast single dispatches; only Apple device hosts pay the serialization cost of
  8192-work-item sub-dispatches.

**Done when**: `bench_ocl` shows the large-dispatch sweet spot restored on a
non-Apple GPU, Apple still honors the cap, and behavior is reproducible via the
env override.

### 6.3 Mid term — device-adaptive dispatch sizing (A6 remainder)

- Query vendor/model once at `ocl_init`; choose sub-dispatch size per device:
  - Apple cl2Metal ⇒ conservative cap (default 64, override-able);
  - elsewhere ⇒ size sub-dispatches to ~1–3 s (TDR-safe) and ≥ several× the
    compute-unit count;
- Add an inner-split parameter `S` (65536 inner keys = `S` banks ⇒ `S×` more
  work-items) so big GPUs can use even larger effective ranges per launch.
- Always log the resolved WG size / per-kernel private memory after build so the
  chosen geometry is auditable.

### 6.4 Longer term — stop paying the corruption entirely (Phase B)

Even on Apple, the *reason* a heavy kernel trips the corruption threshold is its
large register/`__constant` footprint per work-item. Reduce it:

- Port the CPA to a **bitsliced** kernel (registered plan Phase B): boolean-sbox
  block/stream ciphers eliminate the ~470 divergent `__constant` LUT lookups per
  candidate and shrink per-thread register pressure.
- Re-validate the fresh kernel with the §5 harness *before* enabling it on any
  device (plan B6). If it no longer trips the threshold, raise the Apple cap and
  drop the CPU-verify-fabrication path back to a lightweight sanity gate (keep
  the miss-retry from §6.1).
- Keep CPU-verify as the ultimate gate regardless.

### Priority

| Item | Priority | Effort | Removes |
|------|----------|--------|---------|
| 6.1 false-negative retry | **High** (correctness hole) | S | missed-key risk |
| 6.2 platform-conditional cap | Medium | S–M | Apple-only perf penalty |
| 6.3 device-adaptive sizing | Medium | M | hand-tuned constants |
| 6.4 bitsliced kernel | Low (high impact, big effort) | XL | the root trigger |

**Recommended order**: 6.1 → 6.2 → 6.3, then 6.4 when Phase B is scheduled.

---

## 7. Verification Checklist

- [ ] `make` builds clean; `./aycwabtu -t test/Testfile_CW_7FFAE9A02486.ts -g`
      finds `7F FA E9 62 A0 24 86 4A`, CPU-verify passes.
- [ ] `bench_diag` (build + §5.3) shows a clean ceiling at small G and a corrupt
      ceiling at large G on an Apple device; results nondeterministic across runs.
- [ ] §5.4 probability matrix recorded (G=64 all-clean; ≥80 mostly corrupt).
- [ ] §6.1: a key forced inside a corrupt-capable geometry is always eventually
      found (0 misses over a stress run).
- [ ] §6.2: `AYCWABTU_OCL_NGROUPS_CAP` override changes geometry; non-Apple GPUs
      use large dispatches; Apple stays capped.
- [ ] CPU-verify gate still rejects any fabricated winner (inject a corrupt
      buffer and confirm rejection + retry).

---

## 8. References

- Plan & full-architecture context: `plan.md` (bug-fix section, Phase A4/A6,
  Phase B1–B6).
- Differential harness kernel: `tools/aycwabtu_diag.cl`
- Harness host: `bench_diag.cpp`
- Production cap: `src/ocl.cpp` (`MAX_GROUPS` / `wg_size` / `ITEMS_CHUNK`)
- CPU-verify + retry: `src/main.cpp` (`verifyCw`, re-run loop)
- Production kernel: `src/aycwabtu.cl`