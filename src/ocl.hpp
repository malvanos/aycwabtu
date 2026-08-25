#ifndef AYCW_OCL_HPP
#define AYCW_OCL_HPP

#include <cstdint>
#include <cstring>
#include "dvbcsa.h"

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

/* CPU-side verification of a GPU-reported control word (CW): decrypt all three
   probed packets and require the 0x000001 PES start code in each.
   The GPU (Apple M2 Pro / cl2Metal) can FABRICATE winners when a single
   dispatch exceeds ~72 threadgroups (see BUG.md), so every GPU hit must pass
   this before being accepted.  Defined here as the ONE canonical implementation
   shared by ocl.cpp and main.cpp (deduplicated). */
static inline bool verifyCw(const uint8_t probedata[48], const uint8_t cw[8]) {
    dvbcsa_key_t key;
    dvbcsa_key_set(cw, &key);
    uint8_t data[16];
    for (int p = 0; p < 3; p++) {
        memcpy(data, probedata + p * 16, 16);
        dvbcsa_decrypt(&key, data, 16);
        if (data[0] != 0x00 || data[1] != 0x00 || data[2] != 0x01)
            return false;
    }
    return true;
}

struct OclContext {
    cl_device_id     device;
    cl_context       context;
    cl_command_queue queue;
    cl_program       program;
    cl_kernel        kernel;
    bool             ready;
    size_t           wg_size = 128;   /* work-group size (tuned for M2 GPU) */
    uint32_t         itemsCap = 0; /* per-platform max thread-groups/sub-dispatch */
};

/* Initialise OpenCL: pick first GPU, compile kernel from embedded source.
   Returns true on success. */
bool ocl_init(OclContext& ocl, const char* kernel_source);

/* Run the brute-force search on GPU.
   - probedata: 48 bytes (3 packets × 16 bytes)
   - key_start: start of outer 32-bit key range
   - key_count: number of outer keys to test (launch this many work-items)
   - inner_start: start of inner 16-bit key loop (usually 0)
   - inner_count: number of inner keys per work-item (usually 65536)
   - cw_out: output control word (8 bytes), valid if return == true
   Returns true if a key was found.
   Internally key_count is searched as sequential sub-dispatches of at most
   itemsCap threadgroups.  On Apple cl2Metal the cap is 64 threadgroups because
   this kernel corrupts work-item state when a single dispatch exceeds ~72
   threadgroups (provably clean at 64; verified by tools/bench_diag.cpp — see
   BUG.md).  Each sub-dispatch runs exactly once; code must CPU-verify a found
   winner before accepting it.  Callers may pass an arbitrarily large key_count. */
bool ocl_search(OclContext& ocl,
                const uint8_t probedata[48],
                uint32_t key_start,
                uint32_t key_count,
                uint32_t inner_start,
                uint32_t inner_count,
                uint8_t cw_out[8]);

void ocl_cleanup(OclContext& ocl);

#endif
