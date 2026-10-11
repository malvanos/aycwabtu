#include "ocl.hpp"
#include "config.h"

#ifdef HAVE_OPENCL

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

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
    cl_platform_id platform = nullptr;
    cl_uint num_platforms = 0;

    err = clGetPlatformIDs(0, nullptr, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0) {
        std::cerr << "OpenCL: no platforms found" << std::endl;
        return false;
    }

    std::vector<cl_platform_id> platforms(num_platforms);
    err = clGetPlatformIDs(num_platforms, platforms.data(), nullptr);
    if (err != CL_SUCCESS) {
        std::cerr << "OpenCL: failed to enumerate platforms" << std::endl;
        return false;
    }

    /* Candidate platforms that may be overridden by the env var below. */
    std::vector<cl_platform_id> preferred;
    std::vector<cl_platform_id> fallback;
    cl_uint    gpu_devs  = 0;
    cl_uint    cpu_devs  = 0;
    bool       found_dev = false;

    /* First pass: remember which platforms expose a GPU device, so we can
       prefer one over the pure-CPU OpenCL runtimes (e.g. PoCL).  ROCm's
       "AMD Accelerated Parallel Processing" platform reports the installed
       Radeon as CL_DEVICE_TYPE_GPU. */
    for (cl_uint i = 0; i < num_platforms; ++i) {
        char pname[256] = {0};
        clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(pname),
                          pname, nullptr);
        std::cout << "OpenCL platform: " << pname << std::endl;

        cl_uint ng = 0;
        if (clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, 0, nullptr, &ng)
                == CL_SUCCESS && ng > 0) {
            preferred.push_back(platforms[i]);
            if (gpu_devs == 1) gpu_devs = ng; // any GPU platform present
        }
        cl_uint nc = 0;
        if (clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_CPU, 0, nullptr, &nc)
                == CL_SUCCESS && nc > 0) {
            fallback.push_back(platforms[i]);
        }
    }

    /* Optional: force a specific platform by name (here the ROCm one). */
    const char* env_plat = std::getenv("AYCWABTU_OCL_PLATFORM");
    if (env_plat && env_plat[0]) {
        for (cl_uint i = 0; i < num_platforms; ++i) {
            char pname[256] = {0};
            clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(pname),
                              pname, nullptr);
            if (strstr(pname, env_plat)) { platform = platforms[i]; break; }
        }
    }

    /* Otherwise: a GPU-bearing platform is preferred; else fall back to CPU. */
    cl_uint dev_type = CL_DEVICE_TYPE_GPU;
    if (!platform) {
        if (!preferred.empty()) platform = preferred[0];
        else if (!fallback.empty()) { platform = fallback[0]; dev_type = CL_DEVICE_TYPE_CPU; }
    }
    if (!platform) {
        std::cerr << "OpenCL: no usable platform" << std::endl;
        return false;
    }

    char platform_name[256];
    clGetPlatformInfo(platform, CL_PLATFORM_NAME, sizeof(platform_name),
                      platform_name, nullptr);
    std::cout << "OpenCL: using platform " << platform_name << std::endl;

    /* Get a device of the preferred type on the selected platform. */
    err = clGetDeviceIDs(platform, dev_type, 1, &ocl.device, nullptr);
    if (err != CL_SUCCESS) {
        /* Device type we wanted isn't available here; try the other one. */
        std::cout << "OpenCL: no "
                  << (dev_type == CL_DEVICE_TYPE_GPU ? "GPU" : "CPU")
                  << " device, trying the other type..." << std::endl;
        cl_uint other = (dev_type == CL_DEVICE_TYPE_GPU)
                            ? CL_DEVICE_TYPE_CPU : CL_DEVICE_TYPE_GPU;
        err = clGetDeviceIDs(platform, other, 1, &ocl.device, nullptr);
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
        char* end = nullptr;
        long cap = std::strtol(env_cap, &end, 10);
        if (end == env_cap || *end != '\0' || cap < 1) {
            std::cerr << "Error: AYCWABTU_OCL_NGROUPS_CAP must be a positive integer (got \""
                      << env_cap << "\")" << std::endl;
            exit(ERR_USAGE);
        }
        if ((uint64_t)cap * 128 > 0xFFFFFFFFULL) {
            std::cerr << "Error: AYCWABTU_OCL_NGROUPS_CAP is too large (max "
                      << (0xFFFFFFFFULL / 128) << " threadgroups)" << std::endl;
            exit(ERR_USAGE);
        }
        ngroups_cap = (uint32_t)cap;  /* explicit override */
        std::cout << "OpenCL: threadgroup cap overridden to " << ngroups_cap << " via env" << std::endl;
    } else if (is_apple) {
        std::cout << "OpenCL: Apple cl2Metal - sub-dispatch capped at 64 threadgroups" << std::endl;
    } else {
        ngroups_cap = 4096;
        std::cout << "OpenCL: relaxed dispatch cap (default " << ngroups_cap << " threadgroups)" << std::endl;
    }

    /* items-per-dispatch cap (wg_size=128). Apple stays at 64*128=8192 by default.
       No retry logic: each sub-dispatch runs once, and ocl_search CPU-verifies a
       reported winner before accepting it (rejects fabricated ones). */
    ocl.itemsCap = (uint32_t)(ngroups_cap * 128);

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
                uint8_t cw_out[8],
                const std::atomic<int>* abortFlag,
                uint32_t* keysProcessed) {
    if (!ocl.ready) return false;

    cl_int err;

    /* Buffers created lazily on first call, cached in OclContext and reused
       across calls (avoids per-chunk create/destroy overhead in hybrid mode).
       The probe data is constant for the lifetime of the search.
       buf_found is double-buffered for A5 host pipelining: while the host
       reads the result of sub-dispatch N from buf_found[N%2], the GPU can
       already be running sub-dispatch N+1 writing to buf_found[(N+1)%2]. */
    if (!ocl.buf_probe) {
        ocl.buf_probe = clCreateBuffer(ocl.context,
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            48, (void*)probedata, &err);
        if (err != CL_SUCCESS) {
            std::cerr << "OpenCL error: clCreateBuffer(probe) failed ("
                      << err << ")" << std::endl;
            ocl.error = true;
            if (keysProcessed) *keysProcessed = 0;
            return false;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (!ocl.buf_found[i]) {
            uint32_t found_init[3] = { 0, 0, 0 };
            ocl.buf_found[i] = clCreateBuffer(ocl.context,
                CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                sizeof(found_init), found_init, &err);
            if (err != CL_SUCCESS) {
                std::cerr << "OpenCL error: clCreateBuffer(found[" << i
                          << "]) failed (" << err << ")" << std::endl;
                ocl.error = true;
                if (keysProcessed) *keysProcessed = 0;
                return false;
            }
        }
    }

    /* Constant kernel args — set once, never change across sub-dispatches. */
    clSetKernelArg(ocl.kernel, 0, sizeof(cl_mem), &ocl.buf_probe);
    clSetKernelArg(ocl.kernel, 4, sizeof(uint32_t), &inner_start);
    clSetKernelArg(ocl.kernel, 5, sizeof(uint32_t), &inner_count);

    /* ---- A5: host pipelining with double-buffered found + look-ahead ----
       Each sub-dispatch runs EXACTLY ONCE (no retry).  The pipeline keeps
       one sub-dispatch queued on the GPU while the host checks the result
       of the previous one, eliminating the zero→kernel→read serialization.

       Pipeline structure (look-ahead depth = 1):
         prologue: enqueue sub-dispatch 0 (buf[0])
         loop i = 0..N-1:
           if i+1 < N: enqueue sub-dispatch i+1 (buf[(i+1)%2])
           wait for sub-dispatch i's read event
           check result; if found, return
           if aborted, break

       Non-blocking writes (CL_FALSE) and reads (CL_FALSE + event) keep the
       CPU from stalling on the GPU.  The in-order command queue guarantees
       correct execution order; double buffering ensures the write-zero of
       sub-dispatch i+1 does not depend on the read of sub-dispatch i. */
    uint32_t found_init[3] = { 0, 0, 0 };
    uint32_t found_out[2][3] = {};   /* host-side double buffer */
    cl_event read_events[2] = { nullptr, nullptr };

    /* Enqueue one sub-dispatch: non-blocking write-zero → set args → kernel
       → non-blocking read with event.  Returns false on OpenCL error. */
    auto enqueue_subdispatch = [&](uint32_t chunk_start, uint32_t chunk,
                                   int buf_idx) -> bool {
        /* Re-zero the found flag (non-blocking; in-order queue ensures this
           completes before the kernel reads/writes the buffer). */
        err = clEnqueueWriteBuffer(ocl.queue, ocl.buf_found[buf_idx], CL_FALSE, 0,
                                   sizeof(found_init), found_init,
                                   0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            std::cerr << "OpenCL error: clEnqueueWriteBuffer failed ("
                      << err << ")" << std::endl;
            ocl.error = true;
            return false;
        }

        /* Per-chunk kernel args (arg 1 = found buffer changes per buf_idx). */
        clSetKernelArg(ocl.kernel, 1, sizeof(cl_mem), &ocl.buf_found[buf_idx]);
        clSetKernelArg(ocl.kernel, 2, sizeof(uint32_t), &chunk_start);
        clSetKernelArg(ocl.kernel, 3, sizeof(uint32_t), &chunk);

        size_t global_size = ((chunk + wg_size - 1) / wg_size) * wg_size;
        err = clEnqueueNDRangeKernel(ocl.queue, ocl.kernel, 1, nullptr,
                                     &global_size, &wg_size, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            std::cerr << "OpenCL error: clEnqueueNDRangeKernel failed ("
                      << err << ")" << std::endl;
            ocl.error = true;
            return false;
        }

        /* Non-blocking read: returns immediately, event signals completion. */
        err = clEnqueueReadBuffer(ocl.queue, ocl.buf_found[buf_idx], CL_FALSE, 0,
                                  sizeof(found_out[buf_idx]), found_out[buf_idx],
                                  0, nullptr, &read_events[buf_idx]);
        if (err != CL_SUCCESS) {
            std::cerr << "OpenCL error: clEnqueueReadBuffer failed ("
                      << err << ")" << std::endl;
            ocl.error = true;
            return false;
        }
        return true;
    };

    /* Number of sub-dispatches needed to cover key_count. */
    uint32_t num_sd = (key_count + ocl.itemsCap - 1) / ocl.itemsCap;

    /* Prologue: enqueue the first sub-dispatch. */
    if (num_sd > 0) {
        uint32_t c = (key_count < ocl.itemsCap) ? key_count : ocl.itemsCap;
        if (!enqueue_subdispatch(key_start, c, 0)) {
            if (keysProcessed) *keysProcessed = 0;
            return false;
        }
    }

    /* Main pipeline loop (look-ahead depth = 1).
       At iteration sd_idx we have sub-dispatch sd_idx in flight (its read
       event pending) and we enqueue sub-dispatch sd_idx+1 before waiting
       for sd_idx's result.  Sub-dispatch n uses buffer n % 2. */
    uint32_t offset = 0;   /* total outer keys checked so far */
    uint32_t sd_idx = 0;
    while (sd_idx < num_sd) {
        if (abortFlag && abortFlag->load(std::memory_order_relaxed) != 0) {
            break;
        }

        int cur_buf = sd_idx % 2;

        /* Look-ahead: enqueue the next sub-dispatch before waiting. */
        if (sd_idx + 1 < num_sd) {
            uint32_t next_offset = (sd_idx + 1) * ocl.itemsCap;
            uint32_t remaining = key_count - next_offset;
            uint32_t next_chunk = (remaining < ocl.itemsCap) ? remaining : ocl.itemsCap;
            if (!enqueue_subdispatch(key_start + next_offset, next_chunk,
                                     (sd_idx + 1) % 2)) {
                if (keysProcessed) *keysProcessed = offset;
                clFinish(ocl.queue);
                return false;
            }
        }

        /* Wait for the current sub-dispatch's read to complete. */
        if (read_events[cur_buf]) {
            err = clWaitForEvents(1, &read_events[cur_buf]);
            if (err != CL_SUCCESS) {
                std::cerr << "OpenCL error: clWaitForEvents failed ("
                          << err << ")" << std::endl;
                ocl.error = true;
                if (keysProcessed) *keysProcessed = offset;
                return false;
            }
            clReleaseEvent(read_events[cur_buf]);
            read_events[cur_buf] = nullptr;
        }

        /* Compute how many keys this sub-dispatch covered. */
        uint32_t sd_offset = sd_idx * ocl.itemsCap;
        uint32_t sd_chunk = (key_count - sd_offset < ocl.itemsCap)
                                ? (key_count - sd_offset) : ocl.itemsCap;

        /* Check the result. */
        if (found_out[cur_buf][0] != 0) {
            uint8_t temp_cw[8];
            unpack_cw(found_out[cur_buf][1], found_out[cur_buf][2], temp_cw);
            /* CPU-verify: reject any fabricated winner (corrupt threadgroup). */
            if (verifyCw(probedata, temp_cw)) {
                memcpy(cw_out, temp_cw, 8);
                if (keysProcessed) *keysProcessed = sd_offset + sd_chunk;
                clFinish(ocl.queue);
                return true;
            }
            /* False positive: not accepted, no retry — move on. */
        }

        offset = sd_offset + sd_chunk;
        sd_idx++;
    }

    /* Drain any pending look-ahead sub-dispatch and release events. */
    clFinish(ocl.queue);
    for (int i = 0; i < 2; i++) {
        if (read_events[i]) { clReleaseEvent(read_events[i]); read_events[i] = nullptr; }
    }

    if (keysProcessed) *keysProcessed = offset;
    return false;
}

void ocl_cleanup(OclContext& ocl) {
    if (ocl.buf_probe) { clReleaseMemObject(ocl.buf_probe); ocl.buf_probe = nullptr; }
    for (int i = 0; i < 2; i++) {
        if (ocl.buf_found[i]) { clReleaseMemObject(ocl.buf_found[i]); ocl.buf_found[i] = nullptr; }
    }
    if (ocl.kernel)  clReleaseKernel(ocl.kernel);
    if (ocl.program) clReleaseProgram(ocl.program);
    if (ocl.queue)   clReleaseCommandQueue(ocl.queue);
    if (ocl.context) clReleaseContext(ocl.context);
    ocl.ready = false;
}

#else /* !HAVE_OPENCL */

/* OpenCL is compiled out.  The stub type/inline functions live in ocl.hpp;
   this translation unit contributes nothing when not building with OpenCL. */

#endif /* HAVE_OPENCL */
