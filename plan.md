# OpenCL GPU Implementation Plan

## Status: Proof of Concept Working

The OpenCL infrastructure is fully functional on Apple M2 Pro GPU.
Kernel compiles and executes correctly.  The CSA cipher algorithm needs
exact porting from the C reference implementation.

## Files

| File | Status | Description |
|------|--------|-------------|
| `src/aycwabtu.cl` | ✅ done (Phase 1) | OpenCL kernel — exact CSA port, 65536 inner keys/work-item |
| `src/ocl.hpp` | ✅ done | Host-side header (inner loop params added) |
| `src/ocl.cpp` | ✅ done | Host implementation (inner loop params added) |
| `test_ocl.cpp` | ✅ done | Test program — verifies known key against GPU |
| main.cpp integration | 🔲 todo | `--opencl` flag, OpenCL fallback path |

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

### Phase 2: Performance (~1 week)

- [ ] Work-group collaborative bitslicing (work-items share bit-slices via local memory)
- [ ] Local memory for expanded key schedule (avoids recomputing per work-item)
- [ ] Tune work-group size for M2 GPU (128, 256, 512)
- [ ] Double-buffering: overlap GPU execution with host-side key range iteration
- [ ] Benchmark vs CPU NEON + multi-threading

### Phase 3: Integration (~1 day)

- [ ] Add `--opencl` flag to main.cpp
- [ ] Auto-detect OpenCL availability
- [ ] Fall back to CPU if OpenCL unavailable
- [ ] Progress reporting from GPU searches
- [ ] Resume file support for GPU mode

### Phase 4: Productionize (~2-3 days)

- [ ] Handle GPU timeouts (split large ranges into sub-chunks)
- [ ] Multi-GPU support
- [ ] OpenCL on Linux (NVIDIA/AMD GPU)
- [ ] OpenCL on Windows
- [ ] Error recovery (GPU resets, memory allocation failures)

## Expected Performance

| Platform | Current (CPU NEON) | OpenCL GPU (est.) |
|----------|-------------------|-------------------|
| M2 Pro (1 thread) | 9.5 Mcw/s | 20-50 Mcw/s |
| M2 Pro (8 threads) | 68 Mcw/s | 160-400 Mcw/s |
| AMD Radeon discrete | N/A | 100-500 Mcw/s |
| NVIDIA RTX | N/A | 200-1000 Mcw/s |

## Build & Test

```bash
# Build OpenCL test
g++ -std=c++17 -DPARALLEL_MODE=3 -I src/ -I src/libdvbcsa/dvbcsa \
    test_ocl.cpp src/ocl.cpp src/ts.c -o test_ocl -framework OpenCL

# Run
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
