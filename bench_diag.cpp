/* Per-gid differential harness for the OpenCL CSA kernel (PLAN.MD Phase A4).
  
   Detects the cl2Metal per-dispatch threadgroup corruption on the Apple M2
   Pro: a single dispatch of the heavy kernel above ~72 threadgroups silently
   corrupts work-item state.  The harness is a *differential* test -- for the
   same total work-item count it runs the diagnostic kernel two ways:
     - reference: G sequential single-threadgroup dispatches (always clean)
     - test:      one dispatch of G threadgroups
   and compares the per-gid checksums.  Any mismatch => corruption at that
   geometry.  Because the bug is nondeterministic, each geometry is repeated
   several times; a single mismatch in any repetition is a FAIL.

   This is the gate used to justify raising the dispatch ceiling: the Apple
   cap may be raised only to the largest G that PASSES here across all reps.
   The CPU-verify gate in main.cpp stays regardless.

   Build:
     g++ -std=c++17 -O2 -DPARALLEL_MODE=3 -I src/ -I src/libdvbcsa/dvbcsa \
         bench_diag.cpp src/ts.c -o bench_diag -framework OpenCL
        (PARALLEL_MODE must match the platform: 3=NEON/arm64, 2=SSE2/x86,
         1=scalar. ts.c -> config.h selects the SIMD path and defaults to SSE2,
         which fails to build on arm64, so this flag is required here.)
   Usage:
     ./bench_diag [wg] [gmax] [reps] [inner_count]
       wg           work-group (work-items) per threadgroup   (default 128)
       gmax         largest threadgroup count to test          (default 128)
       reps         repetitions per geometry (nondet. coverage)(default  4)
       inner_count  inner keys per work-item                  (default 65536)
   */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "ts.h"

static std::string load_file(const char *path) {
    std::ifstream f(path);
    if (!f.is_open()) { std::cerr << "open fail: " << path << "\n"; exit(1); }
    return std::string((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
}
#define CK(x) do { cl_int e=(x); if (e!=CL_SUCCESS) { \
      std::cerr << "CL error " << e << " at " << #x << "\n"; return 1; } } while(0)

int main(int argc, char **argv) {
    size_t  wg          = (argc > 1) ? strtoul(argv[1], nullptr, 0) : 128;
    size_t  gmax        = (argc > 2) ? strtoul(argv[2], nullptr, 0) : 128;
    int     reps        = (argc > 3) ? atoi(argv[3])            :  4;
    uint32_t inner_count= (argc > 4) ? (uint32_t)strtoul(argv[4], nullptr, 0) : 65536;
    size_t  step        = (argc > 5) ? strtoul(argv[5], nullptr, 0) :    0;
    uint32_t key_start  = 0x00000000;   /* far from the 0x7FFAE9A0 solution */

    unsigned char probedata[48];
    memset(probedata, 0, sizeof(probedata));
    if (ayc_read_ts("test/Testfile_CW_7FFAE9A02486.ts", probedata) != 1)
        return 1;

    std::string src = load_file("tools/aycwabtu_diag.cl");

    cl_int err;
    cl_platform_id platform; cl_uint np;
    CK(clGetPlatformIDs(1, &platform, &np));
    cl_device_id dev;
    if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &dev, nullptr) != CL_SUCCESS)
        clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &dev, nullptr);

    char dname[128];
    clGetDeviceInfo(dev, CL_DEVICE_NAME, sizeof(dname), dname, nullptr);
    printf("device: %s  (wg=%zu, gmax=%zu, reps=%d, inner=%u, %s)\n",
           dname, wg, gmax, reps, inner_count,
           step ? "linear" : "doubling");

    cl_context ctx = clCreateContext(nullptr, 1, &dev, nullptr, nullptr, &err);
    CK(err);
    cl_command_queue q = clCreateCommandQueue(ctx, dev, 0, &err);
    CK(err);

    const char *sources[] = { src.c_str() };
    size_t lengths[] = { src.size() };
    cl_program prog = clCreateProgramWithSource(ctx, 1, sources, lengths, &err);
    CK(err);
    err = clBuildProgram(prog, 1, &dev, "-cl-std=CL1.2", nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t ls; clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG,
                                          0, nullptr, &ls);
        char *log = new char[ls + 1];
        clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, ls, log, nullptr);
        log[ls] = 0;
        std::cerr << "build failed:\n" << log << "\n";
        delete[] log;
        return 1;
     }
    cl_kernel k = clCreateKernel(prog, "aycwabtu_diag", &err);
    if (err != CL_SUCCESS) { std::cerr << "kernel 'aycwabtu_diag' not found\n"; return 1; }

    size_t kpriv = 0, kwg = 0;
    clGetKernelWorkGroupInfo(k, dev, CL_KERNEL_PRIVATE_MEM_SIZE,
                              sizeof(kpriv), &kpriv, nullptr);
    clGetKernelWorkGroupInfo(k, dev, CL_KERNEL_WORK_GROUP_SIZE,
                              sizeof(kwg), &kwg, nullptr);
    printf("diag kernel: max wg %zu, private mem %llu B/thread\n",
           kwg, (unsigned long long)kpriv);
    if (wg > kwg) { std::cerr << "requested wg " << wg << " > max " << kwg << "\n";
                    return 1; }

    /* Per-dispatch work-items buffer (checksum, one u32/work-item). */
    size_t buf_items = gmax * wg;
    cl_mem probe = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                  48, probedata, &err);
    CK(err);
    cl_mem refs = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY,
                                  buf_items * 4, nullptr, &err);
    CK(err);
    cl_mem test = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY,
                                  buf_items * 4, nullptr, &err);
    CK(err);

    uint32_t inner_start = 0;
    cl_int setarg = clSetKernelArg(k, 0, sizeof(cl_mem), &probe);
    (void)setarg;

    std::vector<uint32_t> ref_h(buf_items), tst_h(buf_items);

    auto launch = [&](cl_mem buf, uint32_t base, size_t groups) -> cl_int {
        clSetKernelArg(k, 1, sizeof(cl_mem), &buf);
        uint32_t ks = key_start;
        clSetKernelArg(k, 2, sizeof(uint32_t), &ks);
        clSetKernelArg(k, 3, sizeof(uint32_t), &base);
        clSetKernelArg(k, 4, sizeof(uint32_t), &inner_start);
        clSetKernelArg(k, 5, sizeof(uint32_t), &inner_count);
        size_t global = groups * wg;
        return clEnqueueNDRangeKernel(q, k, 1, nullptr,
                                       &global, &wg, 0, nullptr, nullptr);
    };

    /* Build the clean reference ONCE: single-group dispatches never corrupt,
       and the per-gid checksum depends only on gid + the inner loop, so this
       full-range prefix [0, gmax*wg) serves every candidate G. */
    for (size_t g = 0; g < gmax; g++)
        if (launch(refs, (uint32_t)(g * wg), 1) != CL_SUCCESS) { return 1; }
    if (clFinish(q) != CL_SUCCESS) return 1;
    if (clEnqueueReadBuffer(q, refs, CL_TRUE, 0, buf_items * 4,
                            ref_h.data(), 0, nullptr, nullptr) != CL_SUCCESS)
        return 1;

    /* Candidate G: powers of two (step==0, default) or a fine linear step so
       the ceiling can be pinpointed, not just bracketed to a power of two. */
    std::vector<size_t> Gs;
    if (step == 0) { for (size_t G = 1;    G <= gmax; G *= 2)   Gs.push_back(G); }
    else             { for (size_t G = step; G <= gmax; G += step) Gs.push_back(G); }

    size_t max_clean = 0;
    bool   hit_fail  = false;
    for (size_t G : Gs) {
        bool clean = true;
        for (int r = 0; r < reps; r++) {
              /* Test: one G-group dispatch vs the clean prefix. */
            if (launch(test, 0, G) != CL_SUCCESS) return 1;
            if (clFinish(q) != CL_SUCCESS) return 1;
            if (clEnqueueReadBuffer(q, test, CL_TRUE, 0, G * wg * 4,
                                    tst_h.data(), 0, nullptr, nullptr) != CL_SUCCESS)
                return 1;
            for (size_t i = 0; i < G * wg; i++)
                if (ref_h[i] != tst_h[i]) { clean = false; break; }
            if (!clean) break;
        }
        if (clean) {
            max_clean = G;
            printf("G=%4zu groups x %3zu wg = %7zu items : PASS\n", G, wg, G * wg);
        } else {
            hit_fail = true;
            printf("G=%4zu groups x %3zu wg = %7zu items : FAIL\n", G, wg, G * wg);
            break;         /* first failing G stops; larger cannot raise the cap */
        }
    }

    printf("MAX CLEAN G = %zu groups x %3zu wg = %7llu items%s\n", max_clean, wg,
           (unsigned long long)max_clean * wg,
           hit_fail ? " -- ceiling is below gmax"
                    : " -- clean through the tested ceiling");
    clReleaseMemObject(test); clReleaseMemObject(refs); clReleaseMemObject(probe);
    clReleaseKernel(k); clReleaseProgram(prog); clReleaseCommandQueue(q);
    clReleaseContext(ctx);
    return hit_fail ? 2 : 0;
}
