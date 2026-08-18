# OpenCL GPU Implementation Plan

## Status: Phase 2 Partial — 95 Mcw/s, correctness bug fixed

The OpenCL implementation is fully integrated and performing well.
- Phase 1 (CSA algorithm port): ✅ Complete
- Phase 2 (Performance optimizations): 🔄 Partial — 95 Mcw/s achieved
- Phase 3 (Integration into main.cpp): ✅ Complete
- Bug fix (threadgroup-count corruption): ✅ Complete (2026-08-18)

GPU search runs at **95.1 Mcw/s** on Apple M2 Pro (up from 13 Mcw/s after tuning).
This is competitive with 8-thread CPU NEON (68 Mcw/s).

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

## Files

| File | Status | Description |
|------|--------|-------------|
| `src/aycwabtu.cl` | ✅ done (Phase 1) | OpenCL kernel — exact CSA port, 65536 inner keys/work-item |
| `src/ocl.hpp` | ✅ done | Host-side header (inner loop params added) |
| `src/ocl.cpp` | ✅ done | Host implementation (inner loop params added) |
| `test_ocl.cpp` | ✅ done | Test program — verifies known key against GPU |
| main.cpp integration | ✅ done (Phase 3) | `-g` flag, GPU search with chunking and progress |

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

### Phase 2: Performance 🔄 DEFERRED (2026-08-18)

- [x] Tuned chunk size: 4096 outer keys per launch (was 65536 — launch overhead was bottleneck)
- [x] Work-group size tuned to 128 for M2 GPU
- [ ] **Pending**: reduce kernel register/private-memory pressure so a single
      dispatch can exceed 64 threadgroups without corruption → unlocks larger
      launches and higher occupancy (currently the hard correctness ceiling)
- [ ] Work-group collaborative bitslicing (work-items share bit-slices via local memory)
- [ ] Local memory for expanded key schedule (avoids recomputing per work-item)
- [ ] Double-buffering: overlap GPU execution with host-side key range iteration
- [ ] Benchmark vs CPU NEON + multi-threading

**Results**: GPU performance improved from ~13 Mcw/s to **95.1 Mcw/s** (7.3× speedup)
by increasing chunk size to reduce kernel launch overhead.  Further optimization
was set aside pending the correctness bug fix (2026-08-18), which caps single
dispatches at 64 threadgroups.

### Phase 3: Integration ✅ COMPLETE (2026-08-18)

- [x] Add `-g` flag to main.cpp for GPU mode
- [x] Auto-detect OpenCL availability (falls back with error message)
- [x] Progress reporting from GPU searches (Mcw/s, percentage)
- [x] Key verification with CPU decrypt after GPU find (now a hard gate:
      rejects fabricated GPU winners, see bug-fix section)
- [ ] Resume file support for GPU mode (deferred)

### Phase 4: Productionize (~2-3 days)

- [ ] Larger ranges are already split into sub-chunks inside `ocl_search`
      (≤64 threadgroups per dispatch) — no GPU-timeout path needed on M2 Pro
- [ ] Multi-GPU support
- [ ] OpenCL on Linux (NVIDIA/AMD GPU)
- [ ] OpenCL on Windows
- [ ] Error recovery (GPU resets, memory allocation failures)

## Performance (Measured)

| Platform | CPU NEON | OpenCL GPU |
|----------|----------|------------|
| M2 Pro (1 thread) | 9.5 Mcw/s | — |
| M2 Pro (8 threads) | 68 Mcw/s | — |
| M2 Pro GPU | — | **95.1 Mcw/s** ✅ |
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
