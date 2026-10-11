# OpenCL GPU Implementation Plan

## Status: Phase 2 in progress (code-verified 2026-10-11) — 129 Mcw/s after Phase A1+A2+A4+A5

The OpenCL implementation is fully integrated and correct.
- Phase 1 (CSA algorithm port): ✅ Complete
- Phase 2 (Performance optimizations): 🔄 A1+A2+A4+A5 done (95 → 129 Mcw/s); A3 skipped; A6-A8 + Phase B remain
- Phase 3 (Integration into main.cpp): ✅ Complete (resume file for GPU deferred)
- Phase 4 (Productionize): ❌ Not started
- Bug fix (threadgroup-count corruption): ✅ Complete (2026-08-18)
- Bug fixes (PLAN2 — 12 bugs): ✅ Complete (commit `e873011`)
- Hybrid CPU+GPU mode: ✅ Complete (2026-10-11, beyond original plan)
- Host pipelining (A5): ✅ Complete (2026-10-11)
- 64-TG ceiling re-test (A4): ✅ Complete (2026-10-11) — ceiling unchanged, cap stays at 64

GPU search runs at **129.2 Mcw/s** on Apple M2 Pro (up from 95.1 baseline).
Target: portable performance on **any OpenCL machine** (Apple/NVIDIA/AMD/Intel GPU,
OpenCL CPU runtimes) via Phase A (incremental) + Phase B (bitsliced kernel).

## ⚠️ Bug Fix: Threadgroup-Count Corruption on M2 Pro (2026-08-18)

**Symptom**: probabilistic false positives.  The GPU reported a "found" CW with
impossible checksums, or an outer key **outside the launched range** — a
corrupted work-item fabricating a winner.  Reproducible whenever a single
dispach exceeded ~72 threadgroups; silent otherwise.

**Root cause** (found with a per-gid differential harness that has every
work-item write a checksum of its final state, then compares clean vs. corrupt
geometries): on Apple M2 Pro via cl2Metal, **a single dispatch of the CSA
kernel above ~72 threadgroups silently corrupts work-item state**.  Evidence:
- trivial scalar, private-array, 16 KB `__constant`-table, and 14-deep-call
  kernels are each **clean at 128 groups** → not grid size / arrays / tables / calls alone
- real kernel: clean ≤72 groups (wg=128), wholesale corruption ≥76 groups
- wg=256 @ 64 groups is clean, wg=64 @ 72 groups corrupts → the limit is
  **per-dispatch threadgroups, not work-items**
- nondeterministic: garbage accumulation state, whole threadgroups losing
  their final writes (all-zero readback), fabricated found-buffer winners

**Fix**:
- `src/ocl.cpp`: `ocl_search` splits any requested range into sequential
  dispatches of at most **64 threadgroups × 128 items = 8192 work-items**.
  The API accepts arbitrarily large ranges.
- `src/main.cpp`: every GPU winner is **CPU-verified** (libdvbcsa decrypt of
  all 3 probed packets must yield `0x000001` start codes) *before* the key
  file is written / `exit(OK)`.  A failed verification re-runs the chunk up
  to 3× (an honest relaunch usually finds the real key in the same range),
  then logs and continues to the next chunk.
- Verified: the previously-corrupting 16384-item geometry now finds and
  CPU-verifies the real key `7F FA E9 62 A0 24 86 4A`.

## ✅ Bug Fixes: PLAN2 (all 12 fixed, commit `e873011`)

Root-cause analysis and fixes for bugs found during the `auto-detect` branch
review.  All fixed and verified.

### Category A: auto-detect branch & search loop fixes

| # | Bug | File | Fix |
|---|-----|------|-----|
| 1 | GPU search 32-bit truncation → infinite spin at `keyStop=0xFFFFFFFF` | `src/hybrid.cpp` | Use `uint64_t remaining` instead of casting to `uint32_t` |
| 2 | CPU search infinite wrap at end-of-keyspace (`0xFFFFFFFF+1` wraps to 0) | `src/bs_driver_impl.cpp` | Break when `currentkey32 == stopkey32` before incrementing |
| 3 | Multi-threaded underflow when `range < nThreads` (thread 0 searches 4B keys) | `src/bs_driver_impl.cpp` | Clamp `nThreads` to `totalKeys`, distribute `chunk` + `remainder` |
| 4 | Data race on `static int divider` in multi-threaded resume writes | `src/bs_driver_impl.cpp` | Move `resumeDivider` into per-thread `BFState` |
| 5 | Cosmetic `[T-1]` progress output & 32-bit shift UB | `src/bs_driver_impl.cpp`, `src/main.cpp` | Suppress `[T-1]` in single-threaded; cast shifts to `uint32_t` |

### Category B: resume file & CLI state handling

| # | Bug | File | Fix |
|---|-----|------|-----|
| 6 | Resume file silently overwrites user `-a` parameter | `src/main.cpp` | Add `keystartSpecified` flag; skip resume when `-a` given |
| 7 | `fread` buffer under-read / unterminated stack string in `bfReadResumeFile` | `src/main.cpp` | Use `char buf[64] = {0}`, read with `fread(buf, 1, sizeof(buf)-1, f)`, null-terminate |

### Category C: OpenCL boundary & input validation

| # | Bug | File | Fix |
|---|-----|------|-----|
| 8 | Missing GPU work-item bounds check for non-multiple launch sizes | `src/aycwabtu.cl`, `src/ocl.cpp` | Add `key_count` param; early guard `if (gid >= key_count \|\| found[0] != 0) return;` |
| 9 | `AYCWABTU_OCL_NGROUPS_CAP=0` → infinite loop | `src/ocl.cpp` | Validate `ngroups_cap >= 1`, reject with `exit(ERR_USAGE)` |

### Category D: parser safety & typo fixes

| # | Bug | File | Fix |
|---|-----|------|-----|
| 10 | Unterminated string / stack overrun in TS probe output | `src/ts.c` | Use `snprintf` instead of `memcpy` without null-termination |
| 11 | SSE2 fallback macro typo referencing `c` instead of `a` | `src/bs_sse2.h` | Replace `c` with `(a)` in `BS_EXTRACT32` |
| 12 | Format string typos (unused var, `&d` instead of `%d`) | `src/ts.c` | Remove unused arg; fix `%d` |

### Additional hybrid-mode bugs fixed (commit `10d8c9d`)

8 bugs in CPU+GPU hybrid mode were found and fixed alongside the hybrid search
feature: GPU truncation, CPU end-of-keyspace wrap, MT underflow/race, resume/CLI
handling, OpenCL bounds, and parser typos — all verified by `test/test_cpu_gpu.sh`
(20/20 pass).

## Files

| File | Status | Description |
|------|--------|-------------|
| `src/aycwabtu.cl` | ✅ done (Phase 1) + A1+A2 | OpenCL kernel — exact CSA port, 65536 inner keys/work-item; trimmed quick-check (3 bytes), fused `block_perm_sbox` |
| `src/ocl.hpp` | ✅ done + A5+A6 partial | Host-side header; `verifyCw`, platform detection, env overrides, double-buffered `buf_found[2]` |
| `src/ocl.cpp` | ✅ done + A5+A6 partial | Host implementation; chunking, CPU-verify gate, **A5 pipelining ✅** (non-blocking + look-ahead) |
| `src/hybrid.cpp` / `src/hybrid.hpp` | ✅ done | Hybrid CPU+GPU search with WorkCoordinator; `-c` flag |
| `src/main.cpp` | ✅ done (Phase 3) | `-g`, `-c`, `-s`, `-b` flags, GPU/hybrid search with progress |
| `test_ocl.cpp` | ✅ done | Test program — verifies known key against GPU |
| `bench_ocl.cpp` / `ocl_info.cpp` | ✅ done | Standalone benchmark/info tools (no makefile targets — A8) |
| `tools/aycwabtu_diag.cl` | ✅ done + A4 re-run (2026-10-11) | Differential harness kernel; regenerated with A1+A2; ceiling re-tested |
| `tools/fused_tables.py` | ✅ done (A2) | Generates `block_perm_sbox`; `--verify-only` cross-checks |
| `test/test_cpu_gpu.sh` | ✅ done | Hybrid mode test suite |
| `test_ocl_dbg.cpp` | ⚠️ stale | Debug file — fix or delete (A8) |

## Architecture

### Kernel: one key per work-item (non-bitsliced)

```
Global work-items: N_groups × 128
Each work-item → one key → CSA decrypt → PES check
Work-group size: 128 (maps to GPU SIMD width)
```

### Host: split key range into chunks

```
for each chunk of 1M keys:
    upload probedata to GPU
    launch kernel with chunk_size work-items
    read back found-key buffer
    if found: report and exit
```

Chunk sizes above **64 threadgroups** are split further inside `ocl_search`
into sequential sub-dispatches (≤ 8192 work-items at wg=128).  This is the
empirically safe ceiling for the kernel on the M2 Pro (see bug-fix section
above); larger single dispatches corrupt results.

### Why not bitsliced on GPU?

The bitsliced approach packs 128 keys into one 128-bit word, but
within a work-group, work-items would need to communicate for the
key schedule permutation and sbox lookups (which cross bit positions).
The non-bitsliced approach avoids this complexity at the cost of lower
SIMD efficiency — acceptable for a first implementation.

## Remaining Work

### Phase 1: Fix CSA algorithm ✅ COMPLETE (2026-07-10)

The kernel's CSA stream and block ciphers have been replaced with exact ports from libdvbcsa.

**Stream cipher** (from `libdvbcsa/dvbcsa_stream.c`):
- [x] 7 sbox lookup tables (sbox1 through sbox7)
- [x] Shift register A/B initialization from nibble-swapped control word (cws)
- [x] 32 init rounds with interleaved nibble-swapped IV data
- [x] Key stream generation loop (4 rounds per byte with stream_out table)

**Block cipher** (from `libdvbcsa/dvbcsa_block.c`):
- [x] 64-bit key → 448-bit schedule expansion (6 permutation rounds via kperm[8][256])
- [x] 56-round decrypt loop with register shuffling
- [x] Byte-level permutation via csa_block_perm[256]
- [x] Correct dvbcsa_block_sbox[256] lookup table

**Verification**:
- [x] Test with known key (Testfile_CW_7FFAE9A02486.ts)
- [x] Cross-check decrypt output against libdvbcsa C reference
- [x] Full inner key loop (65536 iterations per work-item)

**Bugs found and fixed during implementation**:
- Chain XOR direction was reversed (data[i-8] ^= data[i], not data[i] ^= data[i-8])
- Inner loop used u16 causing overflow when inner_count=65536
- Stream cipher is required for PES check (chain XOR combines stream output with block decrypt output)

### Phase 2: Performance 🔄 IN PROGRESS (code-verified 2026-10-11)

- [x] Tuned chunk size: 4096 outer keys per launch (was 65536 — launch overhead was bottleneck)
- [x] Work-group size tuned to 128 for M2 GPU
- [x] Hybrid optimization plan below (approved)
- [x] Hybrid CPU+GPU mode (`-c` flag) with WorkCoordinator dynamic chunk sharing
- [x] SIMD runtime auto-detection (`-S` flag)
- [x] Self-test (`-s`) and benchmark (`-b`) modes for GPU and hybrid

**Code-verified status (2026-10-11)**: A1+A2+A4+A5 done and verified in `src/aycwabtu.cl` + `src/ocl.cpp`.
A6 partially done (platform detection + env overrides). Remaining: A6 (rest), A7, A8, Phase B (B1–B6).

**Baseline results**: ~13 → **95.1 Mcw/s** (7.3× speedup) by increasing chunk size.
Still well below what the GPU should deliver; the kernel burns ~470 random
`__constant` lookups per candidate (~48 schedule + 112 block + ~310 stream), and
divergent constant reads serialize on GPUs.

#### Kernel cost model (per candidate, read from `src/aycwabtu.cl`)

- `key_schedule_block` (line 946): 6 rounds × 8 byte-channels = 48 lookups.
  Initial cost-model assumed per-byte-position independence would let us precompute
  the 5 fixed CW bytes (0..4) once per work-item and use 2-round-composed tables
  `T2_j = kperm[j]∘kperm[j]` for the 3 varying positions. **Investigation proved
  the assumption wrong**: `kperm[j][x]` returns a 64-bit word whose bit positions
  span multiple byte channels, so the chain operates on full `u64` words, not
  independent byte streams. The chain is still linear-in-OR over the per-channel
  contributions, but the only hoistable piece is the fixed byte contribution to
  k[5] (≈ 5 lookups saved out of 470, ~1%). Not worth the complexity. **A3 skipped.**
- `block_decrypt` (line 964): 56 rounds × (`block_sbox` + `block_perm`) = 112 lookups.
  `W[6] ^= block_perm[block_sbox[kk[i]^W[6]]]` composes into **one 256-byte table**
  `block_perm_sbox` → 56 lookups. Index is the pre-sbox byte `kk[i]^W[6]`, not the
  sbox output. **A2 done.**
- `quick_check_pes` (line 1013): full 8-byte stream gen (32 rounds) though only
  `final[0..2] = BD[0..2] ^ S[8..10]` is observed → trim to **3 bytes / 12 rounds**.
  Chain XOR reduced from 8 to 3 iterations. **A1 done.**
- Stream sboxes (`stream_sboxes`, line 779): 7 LUTs/round × 44 rounds (32 init + 12 gen)
  ≈ 310 lookups — irreducible in scalar form; this is what the bitsliced kernel kills.

#### Phase A — incremental (scalar kernel + host), low risk

- **A1** ✅ trim quick-check stream output to 3 bytes (32 → 12 gen rounds/candidate);
  chain XOR reduced 8 → 3 iterations. Verified: `quick_check_pes` calls
  `stream_xor(..., 3)` in `aycwabtu.cl:1023`.
- **A2** ✅ fuse `block_perm∘block_sbox` into one generated `__constant` table (112 → 56);
  table generated by `tools/fused_tables.py`, index is pre-sbox byte `kk[i]^W[6]`.
  Verified: `block_perm_sbox[256]` at `aycwabtu.cl:631`, used at `aycwabtu.cl:971`.
- **A3** ⛔ skipped — `kperm[j][x]` returns a 64-bit word with cross-byte coupling; the
  chain operates on full `u64` words, not per-byte streams. Only ~5/470 lookups (~1%)
  can be hoisted — not worth the complexity.
- **A4** ✅ re-test the 64-threadgroup ceiling with the per-gid differential harness
  (`bench_diag` + `tools/aycwabtu_diag.cl`, regenerated with A1+A2 changes).
  **Result (2026-10-11, Apple M2 Pro, wg=128)**: ceiling unchanged at production
  workload. Two independent clean runs (no concurrent GPU load):
  - 8 reps, inner=65536 (full workload): G=72 PASS, G=76 FAIL → max clean = 72 groups (9216 items)
  - 16 reps, inner=8192: G=80 PASS → ceiling rises at lower inner counts (A1+A2 made kernel lighter)
  - wg=64, inner=8192: G=72 now clean (was corrupt before A1+A2)
  - wg=256, inner=8192: ceiling ~32-36 groups (bug is per-dispatch threadgroups, not work-items)
  **Decision: keep the Apple cap at 64 threadgroups (8192 items).** The 8-group
  safety margin below the observed failure point is appropriate given the
  nondeterministic nature of the bug. CPU-verify gate stays regardless.
- **A5** ✅ host pipelining: double-buffered `found` (`buf_found[2]`) + non-blocking
  writes/reads (`CL_FALSE`) + events + look-ahead pipeline (depth 1).  The next
  sub-dispatch is enqueued while the previous readback is in flight, removing the
  zero→kernel→read serialization.  Implemented in `ocl.cpp:248-460` (2026-10-11).
  Verified: GPU search, hybrid search, self-tests, clean-miss all pass.
- **A6** 🟡 device-adaptive geometry — **partially done**:
  - ✅ vendor/platform queries in `ocl_init` (`is_apple_platform`)
  - ✅ 64-TG cap only on Apple/Intel cl2Metal; 4096 elsewhere
  - ✅ env overrides: `AYCWABTU_OCL_NGROUPS_CAP`, `AYCWABTU_OCL_PLATFORM`
  - ❌ missing: `AYCWABTU_OCL_LANES` override, inner-split parameter S
    (65536 inner = S banks → S× more work-items) for big GPUs,
    per-kernel WG size / private memory logging after build
- **A7** ❌ `hybrid.cpp`: chunk size from device info instead of hardcoded 262144;
  keep `verifyCw` + retry loop untouched
- **A8** ❌ cleanup:
  - dead `get_kernel_source` decl at `ocl.cpp:13` (declared, never defined/used)
  - duplicate `wg_size` field: `ocl.hpp:47` (in `OclContext`) and `ocl.cpp:233` (static)
  - stale `test_ocl_dbg.cpp` still present
  - no makefile targets for `bench_ocl` / `ocl_info` / `test_ocl*` (built manually)

**Expected vs measured (M2 Pro GPU)**:
- Baseline (pre-Phase-A scalar): **95.1 Mcw/s** (chunk 4096)
- After A1+A2: **129.2 Mcw/s** (+35.8%, chunk 4096)
- A3 was skipped (~1% estimated, complexity not justified)
- Stream sboxes still scalar (~310 LUTs/candidate); remaining scalar wins live in
  Phase B (bitsliced) or host-side (A5-A7).
- On NVIDIA/AMD, uncapping large dispatches (A4 + A6) alone removes today's
  launch-serialization bottleneck.

**bench_ocl sweep (after A1+A2, Apple M2 Pro, wg=128)**:
- chunk 256: 8.8 Mcw/s (launch overhead dominates)
- chunk 1024: 34.3 Mcw/s
- chunk 4096: **129.2 Mcw/s** (current sweet spot)

#### Phase B — bitsliced kernel (port of the proven CPU implementation), high impact — ❌ NOT STARTED

The CPU path is already a validated byte/bit-sliced CSA (`src/bs_block.c` +
`bs_block_ab.c` boolean sbox, `src/bs_stream.c` boolean sboxes + bit-sliced A_BS/B_BS,
`bs_algo.c` bitsliced increment + PES mask) behind a `dvbcsa_bs_word_t` word-API
(NEON 128-bit; portable fallback `bs_uint32.c`). **Port, don't re-invent:**

- **B1** ❌ new OpenCL word layer (op set of `bs_uint32.c`) with runtime-selectable lane
  widths: 32 (`uint`) / 64 (`ulong`) / 128 (`uint4`) → three kernels from one source
- **B2** ❌ block cipher: 56 rounds boolean-sbox (zero LUTs), byte-sliced 8-word state;
  key schedule = the 6 bit-permutation word-moves + NOTs recomputed per inner
  iteration (word reorderings, amortized over the lane batch)
- **B3** ❌ stream cipher: 32 init + 12 gen rounds with boolean sboxes — removes the last
  ~310 per-candidate LUTs
- **B4** ❌ kernel body: 1 outer key/work-item, inner loop = 65536/lanes iterations;
  PES mask on bitsliced result → nonzero mask = winning lane; extract lane → scalar CW
  → `atomic_cmpxchg` found write → host CPU-verifies as today
- **B5** ❌ register budget ~100–160 words/work-item: 32 lanes ≈ 400–650 B/thread;
  64 lanes ≈ 0.8–1.3 KB; 128 lanes ≈ 1.6–2.5 KB (CPU devices / wide-reg GPUs).
  Select per device + `AYCWABTU_OCL_LANES` override.
- **B6** ❌ gating: known-key + randomized differential vs CPU libdvbcsa
  **before enabling on any device**. If the heavy kernel trips the cl2Metal corruption
  on Apple (harness from A4), disable there and keep the scalar kernel as fallback.

**Expected**: eliminates divergent-LUT serialization entirely → several× over the tuned
scalar kernel on NVIDIA/AMD-class GPUs; smaller on M2.

**Current kernel state**: `src/aycwabtu.cl` has only the scalar `aycwabtu_search` kernel
(1119 lines). No bitsliced entry point, no `AYCWABTU_OCL_LANES`, no boolean sboxes.

#### Portability policy

- CL 1.2 only, no vendor extensions; per-device behavior behind queries + documented
  env overrides (`AYCWABTU_OCL_NGROUPS_CAP`, `AYCWABTU_OCL_LANES`)
- Apple cl2Metal: conservative dispatch cap until A4 proves otherwise + CPU-verify gate
- Windows/Linux desktop GPUs: sub-dispatch runtime cap (watchdog/TDR safety)
- OpenCL CPU devices: 128-lane kernel (maps to host SIMD width)

#### Verification (code-verified 2026-10-11)

1. ✅ `make && ./aycwabtu -t test/Testfile_CW_7FFAE9A02486.ts -g` → finds
   `7F FA E9 62 A0 24 86 4A`, CPU-verify passes; also key-at-chunk-boundary + clean-miss ranges
2. ✅ Fused tables cross-checked: `tools/fused_tables.py --verify-only` compares kernel table vs generated
3. ✅ Per-gid differential harness re-run after A1+A2 (2026-10-11): `bench_diag` with regenerated `tools/aycwabtu_diag.cl`. Result: ceiling unchanged at production workload (inner=65536, wg=128): G=72 PASS (8 reps), G=76 FAIL. Cap stays at 64.
4. ❌ Bitsliced kernel: known-key test + randomized differential vs CPU over many
   random (outer, inner) pairs (Phase B)
5. 🟡 `bench_ocl` geometry sweep: tool exists (`bench_ocl.cpp`), no makefile target (A8)
6. 🟡 `ocl_info` logs per-kernel private memory: tool exists (`ocl_info.cpp`), no makefile target (A6/A8)

#### Files

| File | Change |
|------|--------|
| `src/aycwabtu.cl` | A1+A2 done ✅; A4; Phase B kernel (same program, second entry point) ❌ |
| `tools/fused_tables.py` | A2 ✅: generates `block_perm_sbox`; `--verify-only` cross-checks the kernel |
| `tools/aycwabtu_diag.cl` | A4 ✅: differential-harness kernel (regenerated + re-run 2026-10-11) |
| `src/ocl.cpp` / `src/ocl.hpp` | A5 ✅ (2026-10-11), A6 🟡 (partial), multi-kernel selection + geometry ❌ |
| `src/hybrid.cpp` | A7 ❌ (device-driven chunking); hybrid mode ✅ done |
| `src/main.cpp` | A7 ❌; `-g`, `-c`, `-s`, `-b` flags ✅ done |
| `makefile` | A8 ❌: no targets for `bench_ocl` / `ocl_info` / `test_ocl*` |
| `test_ocl_dbg.cpp` | A8 ❌: fix or delete (stale) |
| `bench_ocl.cpp` / `ocl_info.cpp` | ✅ exist as standalone tools (no makefile targets) |
| `test/test_cpu_gpu.sh` | ✅ hybrid mode test suite |

#### Sequence

A1–A3 + table generator → A4 harness re-run → A5–A7 host rework → re-tune ceiling →
measure Phase A → B1–B5 port → B6 gating → auto-selection + overrides →
final bench matrix + docs.

#### Phase 2 — What Remains (code-verified 2026-10-11)

| # | Item | Effort | Impact |
|---|------|--------|--------|
| A4 | ~~Re-test 64-TG ceiling~~ ✅ **DONE** (2026-10-11) — ceiling unchanged, cap stays at 64 | — | — |
| A5 | ~~Host pipelining~~ ✅ **DONE** (2026-10-11) | — | — |
| A6 | Complete device-adaptive geometry: `AYCWABTU_OCL_LANES`, inner-split S, per-kernel logging | Medium | Big win on NVIDIA/AMD (uncapped dispatches) |
| A7 | Device-driven chunk size in `hybrid.cpp` (not hardcoded 262144) | Low | Auto-tune per device |
| A8 | Cleanup: dead `get_kernel_source` decl, duplicate `wg_size`, stale `test_ocl_dbg.cpp`, makefile targets | Low | Code hygiene |
| B1–B6 | **Bitsliced kernel** — port CPU bitsliced CSA to OpenCL (boolean sboxes, lane widths 32/64/128) | **High** | **Several× over scalar on NVIDIA/AMD** |

**Recommended order**: A8 (quick cleanup) → A6+A7 (device-adaptive) → B1–B6 (bitsliced kernel, the big win).

### Phase 3: Integration ✅ COMPLETE (2026-10-11, code-verified)

- [x] Add `-g` flag to main.cpp for GPU mode
- [x] Add `-c`/`--cpu-gpu` flag for hybrid CPU+GPU mode (beyond original plan)
- [x] Auto-detect OpenCL availability (makefile probe, falls back to CPU-only stubs)
- [x] Progress reporting from GPU searches (Mcw/s, percentage) — both GPU-only and hybrid
- [x] Key verification with CPU decrypt after GPU find (hard gate: `verifyCw` in `ocl.hpp`)
- [x] Self-test mode (`-s`) including GPU and hybrid self-tests
- [x] Benchmark mode (`-b`) for GPU and hybrid
- [x] SIMD runtime auto-detection (`-S` flag)
- [ ] Resume file support for GPU mode (deferred — `bfReadResumeFile` only called for single-threaded CPU)

### Phase 4: Productionize ❌ NOT STARTED (code-verified 2026-10-11)

- [ ] Multi-GPU support (fan out sub-ranges; OclContext + host thread per device)
- [ ] OpenCL on Linux (NVIDIA/AMD GPU) — makefile has Linux link flags + CI with PoCL; needs real GPU validation
- [ ] OpenCL on Windows — CI workflow exists; makefile has no Windows-specific OpenCL handling
- [ ] Error recovery (GPU resets, memory allocation failures) — `ocl.error` stops worker but no recovery
- [ ] Resume file support for GPU mode

## Performance (Measured)

| Platform | CPU NEON | OpenCL GPU |
|----------|----------|------------|
| M2 Pro (1 thread) | 9.5 Mcw/s | — |
| M2 Pro (8 threads) | 68 Mcw/s | — |
| M2 Pro GPU | — | **129.2 Mcw/s** ✅ (after A1+A2) |
| AMD Radeon discrete | N/A | 100-500 Mcw/s (est.) |
| NVIDIA RTX | N/A | 200-1000 Mcw/s (est.) |

## Build & Test

```bash
# Build main program (includes OpenCL support)
make

# Run with GPU
./aycwabtu -t test/Testfile_CW_7FFAE9A02486.ts -g

# Run OpenCL test directly
g++ -std=c++17 -DPARALLEL_MODE=3 -I src/ -I src/libdvbcsa/dvbcsa \
    test_ocl.cpp src/ocl.cpp src/ts.c -o test_ocl -framework OpenCL
./test_ocl
```

## Key Design Decisions

1. **Non-bitsliced per-work-item**: simpler to implement correctly, lower SIMD
   efficiency but easier to debug.  Can be upgraded to bitsliced later.

2. **Apple OpenCL → Metal**: macOS translates OpenCL to Metal via cl2Metal.
   The translation is mostly transparent but imposes some limits:
   - Max work-group size: 1024
   - No OpenCL 2.0 features (CL1.2 only)
   - `printf` from kernel goes to system log

3. **Atomic key-found**: `atomic_cmpxchg` ensures only one work-item
   records the found key, avoiding race conditions.

4. **No local memory yet**: Phase 2 will add local-memory key schedule
   caching to reduce per-work-item computation.

5. **64-threadgroup dispatch ceiling (M2 Pro)**: the kernel corrupts
   work-item state when a single dispatch exceeds ~72 threadgroups.
   `ocl_search` therefore never launches more than 64 groups and `main.cpp`
   CPU-verifies every GPU "find" before accepting it.  Any future kernel
   that raises the ceiling (lower register pressure) should re-validate with
   the differential harness (per-gid checksum, clean vs. large geometry).
