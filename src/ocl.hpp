#ifndef AYCW_OCL_HPP
#define AYCW_OCL_HPP

#include <cstdint>

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

struct OclContext {
    cl_device_id     device;
    cl_context       context;
    cl_command_queue queue;
    cl_program       program;
    cl_kernel        kernel;
    bool             ready;
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
   Returns true if a key was found. */
bool ocl_search(OclContext& ocl,
                const uint8_t probedata[48],
                uint32_t key_start,
                uint32_t key_count,
                uint32_t inner_start,
                uint32_t inner_count,
                uint8_t cw_out[8]);

void ocl_cleanup(OclContext& ocl);

#endif
