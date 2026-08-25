#include "ocl.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

/* Embedded kernel source (loaded from file or embedded string) */
static const char* get_kernel_source();

/* Detect an Apple platform/device by scanning platform and device names.
   Returns true for "Apple" or "AMD"/"Apple" GPUs (cl2Metal / Metal drivers).
   Used to decide whether to apply the provably-clean 64 thread-group dispatch
   cap. Non-Apple platforms (NVIDIA/AMD/Intel GPU/OpenCL-CPU) relax the cap. */
static bool is_apple_platform(const char* name) {
    if (!name || !name[0]) return false;
    /* The cl2Metal platform advertises itself with "Apple" in the name. */
    if (strstr(name, "Apple")) return true;
    /* Intel platforms are conservative: keep the safe cap there too. */
    if (strstr(name, "Intel")) return true;
    return false;
}

bool ocl_init(OclContext& ocl, const char* kernel_source) {
    ocl.ready = false;

    cl_int err;
    cl_platform_id platform;
    cl_uint num_platforms;

    err = clGetPlatformIDs(1, &platform, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0) {
        std::cerr << "OpenCL: no platforms found" << std::endl;
        return false;
    }

    /* Print platform name */
    char platform_name[256];
    clGetPlatformInfo(platform, CL_PLATFORM_NAME, sizeof(platform_name),
                      platform_name, nullptr);
    std::cout << "OpenCL platform: " << platform_name << std::endl;

    /* Get GPU device */
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1,
                         &ocl.device, nullptr);
    if (err != CL_SUCCESS) {
        std::cerr << "OpenCL: no GPU device found, trying CPU..." << std::endl;
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1,
                             &ocl.device, nullptr);
        if (err != CL_SUCCESS) {
            std::cerr << "OpenCL: no device found" << std::endl;
            return false;
        }
    }

    char device_name[256];
    clGetDeviceInfo(ocl.device, CL_DEVICE_NAME, sizeof(device_name),
                    device_name, nullptr);
    std::cout << "OpenCL device: " << device_name << std::endl;

    /* WHY we cap every sub-dispatch at 64 threadgroups on Apple:
     *
     *  The Apple cl2Metal driver silently corrupts work-item state when a single
     *  dispatch of the heavy CSA kernel exceeds ~72 threadgroups (wg=128).  The
     *  corruption is NONDETERMINISTIC: a given geometry can be clean one launch
     *  and produce wrong results the next, and it emits fabricated "winner" CWs
     *  as well as silently skipping real keys.  root cause is inside Apple's
     *  runtime (not this codebase), so it cannot be fixed here.
     *
     *  We therefore hard-cap each dispatch at 64 threadgroups (wg=128) on Apple
     *  = 8192 work-items, a provably-clean ceiling (verified with the differential
     *  harness tools/bench_diag.cpp — MAX CLEAN G = 64).  key_count larger than
     *  that is split into sequential 64-group sub-dispatches inside ocl_search.
     *
     *  Non-Apple platforms (NVIDIA/AMD/Intel GPU / OpenCL-CPU) do not exhibit the
     *  bug, so they may safely dispatch far more threadgroups at once (default
     *  4096), avoiding the serialization penalty.  Both caps are overridable via
     *  AYCWABTU_OCL_NGROUPS_CAP.  See BUG.md for the full write-up. */
    uint32_t ngroups_cap = 64;
    bool is_apple = is_apple_platform(platform_name);
    char* env_cap = std::getenv("AYCWABTU_OCL_NGROUPS_CAP");
    if (env_cap && env_cap[0]) {
        ngroups_cap = (uint32_t)std::atoi(env_cap);  /* explicit override */
        std::cout << "OpenCL: threadgroup cap overridden to " << ngroups_cap << " via env" << std::endl;
    } else if (is_apple) {
        std::cout << "OpenCL: Apple cl2Metal - sub-dispatch capped at 64 threadgroups" << std::endl;
    } else {
        ngroups_cap = 4096;
        std::cout << "OpenCL: relaxed dispatch cap (default " << ngroups_cap << " threadgroups)" << std::endl;
    }

    /* items-per-dispatch cap (wg_size=128). Apple stays at 64*128=8192 by default. */
    ocl.itemsCap = (uint32_t)(ngroups_cap * 128);

    /* Enable the hardened false-negative / false-positive logic whenever we are
       on Apple.  Even at the 64-group cap the corruption can (rarely) strike,
       because it is nondeterministic; re-running a "no-find" sub-dispatch and
       CPU-verifying every reported winner closes both the miss hole (§3.2) and
       the fabrication path (§3.1) from BUG.md.  Non-Apple platforms do not need
       it, so they keep max_attempts = 1 (no retry overhead). */
    ocl.corruptCapable = is_apple;
    if (ocl.corruptCapable) {
        std::cout << "OpenCL: hardened false-negative retries enabled (Apple, "
                  << "cap=" << ngroups_cap << " threadgroups)" << std::endl;
    }

    /* Create context */
    ocl.context = clCreateContext(nullptr, 1, &ocl.device,
                                  nullptr, nullptr, &err);
    if (err != CL_SUCCESS) {
        std::cerr << "OpenCL: failed to create context" << std::endl;
        return false;
    }

    /* Create command queue */
    ocl.queue = clCreateCommandQueue(ocl.context, ocl.device, 0, &err);
    if (err != CL_SUCCESS) {
        std::cerr << "OpenCL: failed to create command queue" << std::endl;
        return false;
    }

    /* Compile kernel */
    const char* sources[] = { kernel_source };
    size_t lengths[] = { strlen(kernel_source) };
    ocl.program = clCreateProgramWithSource(ocl.context, 1, sources,
                                             lengths, &err);
    if (err != CL_SUCCESS) {
        std::cerr << "OpenCL: failed to create program" << std::endl;
        return false;
    }

    err = clBuildProgram(ocl.program, 1, &ocl.device,
                         "-cl-std=CL1.2", nullptr, nullptr);
    if (err != CL_SUCCESS) {
        std::cerr << "OpenCL: build failed" << std::endl;
        size_t log_size;
        clGetProgramBuildInfo(ocl.program, ocl.device, CL_PROGRAM_BUILD_LOG,
                              0, nullptr, &log_size);
        char* log = new char[log_size + 1];
        clGetProgramBuildInfo(ocl.program, ocl.device, CL_PROGRAM_BUILD_LOG,
                              log_size, log, nullptr);
        log[log_size] = '\0';
        std::cerr << "Build log:\n" << log << std::endl;
        delete[] log;
        return false;
    }

    ocl.kernel = clCreateKernel(ocl.program, "aycwabtu_search", &err);
    if (err != CL_SUCCESS) {
        std::cerr << "OpenCL: failed to create kernel" << std::endl;
        return false;
    }

    ocl.ready = true;
    std::cout << "OpenCL: initialized successfully" << std::endl;
    return true;
}

/* Max threadgroups per single dispatch.
   On the Apple M2 Pro (cl2Metal) this kernel silently corrupts work-item
   state when a dispatch exceeds ~72 threadgroups (wg=128: ~9200 items).
   Empirically 64 groups x 128 items is always clean (see diff_host sweep).
   Larger ranges are searched as sequential sub-dispatch of <= this many
   work-items. */
static const size_t MAX_GROUPS   = 64;
static const size_t wg_size      = 128;   /* work-group size (tuned for M2 GPU) */
static const size_t ITEMS_CHUNK  = MAX_GROUPS * wg_size;

/* Convert the kernel-found u32 pair into the 8-byte CW (same layout as
   aycwabtu.cl writes). */
static void unpack_cw(uint32_t hi, uint32_t lo, uint8_t cw[8]) {
    cw[0] = (hi >> 24) & 0xFF;
    cw[1] = (hi >> 16) & 0xFF;
    cw[2] = (hi >>  8) & 0xFF;
    cw[3] = (hi)       & 0xFF;
    cw[4] = (lo >> 24) & 0xFF;
    cw[5] = (lo >> 16) & 0xFF;
    cw[6] = (lo >>  8) & 0xFF;
    cw[7] = (lo)       & 0xFF;
}

bool ocl_search(OclContext& ocl,
                const uint8_t probedata[48],
                uint32_t key_start,
                uint32_t key_count,
                uint32_t inner_start,
                uint32_t inner_count,
                uint8_t cw_out[8]) {
    if (!ocl.ready) return false;

    cl_int err;

    /* Buffers (created once; reused across sub-dispatches) */
    cl_mem buf_probe = clCreateBuffer(ocl.context,
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        48, (void*)probedata, &err);
    if (err != CL_SUCCESS) return false;

    uint32_t found_init[3] = { 0, 0, 0 };
    cl_mem buf_found = clCreateBuffer(ocl.context,
        CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
        sizeof(found_init), found_init, &err);
    if (err != CL_SUCCESS) { clReleaseMemObject(buf_probe); return false; }

    clSetKernelArg(ocl.kernel, 0, sizeof(cl_mem), &buf_probe);
    clSetKernelArg(ocl.kernel, 1, sizeof(cl_mem), &buf_found);
    clSetKernelArg(ocl.kernel, 3, sizeof(uint32_t), &inner_start);
    clSetKernelArg(ocl.kernel, 4, sizeof(uint32_t), &inner_count);

    uint32_t offset = 0;
    while (offset < key_count) {
        uint32_t chunk = key_count - offset;
        if (chunk > ocl.itemsCap) chunk = ocl.itemsCap;

        bool sub_dispatch_done = false;
        int attempts = 0;
        /* One extra no-find/false-positive retry on corrupt-capable (Apple)
           geometry.  The 64-group cap is provably clean, so a miss/fabrication
           is rare; a single re-run is enough insurance.  More retries cost ~10x
           throughput on the common no-solution path, so we keep it to 2. */
        const int max_attempts = ocl.corruptCapable ? 2 : 1;

        while (!sub_dispatch_done && attempts < max_attempts) {
            attempts++;
            /* Re-zero the found flag so every sub-dispatch/retry runs all items */
            err = clEnqueueWriteBuffer(ocl.queue, buf_found, CL_TRUE, 0,
                                       sizeof(found_init), found_init,
                                       0, nullptr, nullptr);
            if (err != CL_SUCCESS) { clReleaseMemObject(buf_probe); clReleaseMemObject(buf_found); return false; }

            uint32_t chunk_start = key_start + offset;
            size_t global_size = ((chunk + wg_size - 1) / wg_size) * wg_size;

            clSetKernelArg(ocl.kernel, 2, sizeof(uint32_t), &chunk_start);

            err = clEnqueueNDRangeKernel(ocl.queue, ocl.kernel, 1, nullptr,
                                         &global_size, &wg_size, 0, nullptr, nullptr);
            if (err != CL_SUCCESS) { clReleaseMemObject(buf_probe); clReleaseMemObject(buf_found); return false; }

            uint32_t found_out[3];
            err = clEnqueueReadBuffer(ocl.queue, buf_found, CL_TRUE, 0,
                                      sizeof(found_out), found_out,
                                      0, nullptr, nullptr);
            if (err != CL_SUCCESS) { clReleaseMemObject(buf_probe); clReleaseMemObject(buf_found); return false; }

            if (found_out[0] != 0) {
                uint8_t temp_cw[8];
                unpack_cw(found_out[1], found_out[2], temp_cw);
                if (verifyCw(probedata, temp_cw)) {
                    // Verified real key found!
                    memcpy(cw_out, temp_cw, 8);
                    clReleaseMemObject(buf_probe);
                    clReleaseMemObject(buf_found);
                    return true;
                } else {
                    // False positive candidate (garbage reported by corrupt threadgroup)
                    if (ocl.corruptCapable) {
                        std::cout << "\n[ocl_search] False positive detected on corrupt-capable geometry (attempt "
                                  << attempts << "/" << max_attempts << "). Retrying sub-dispatch..." << std::endl;
                    } else {
                        // Even on clean geometry, we verify to be safe, but we don't retry.
                        sub_dispatch_done = true;
                    }
                }
            } else {
                // No candidate reported by GPU.
                if (ocl.corruptCapable && attempts < max_attempts) {
                    // On corrupt-capable geometry, retry in case it was a false negative (missed key)
                    std::cout << "\n[ocl_search] No find on corrupt-capable geometry (attempt "
                              << attempts << "/" << max_attempts << "). Retrying sub-dispatch to protect against false negatives..." << std::endl;
                } else {
                    sub_dispatch_done = true;
                }
            }
        }
        offset += chunk;
    }

    clReleaseMemObject(buf_probe);
    clReleaseMemObject(buf_found);
    return false;
}

void ocl_cleanup(OclContext& ocl) {
    if (ocl.kernel)  clReleaseKernel(ocl.kernel);
    if (ocl.program) clReleaseProgram(ocl.program);
    if (ocl.queue)   clReleaseCommandQueue(ocl.queue);
    if (ocl.context) clReleaseContext(ocl.context);
    ocl.ready = false;
}
