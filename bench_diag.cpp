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
     g++ -std=c++17 -O2 -I src/ -I src/libdvbcsa/dvbcsa \
         bench_diag.cpp src/ts.c -o bench_diag -framework OpenCL
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
    printf("device: %s  (wg=%zu, gmax=%zu, reps=%d, inner=%u)\n",
           dname, wg, gmax, reps, inner_count);

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

    bool any_fail = false;
    for (size_t G = 1; G <= gmax; G *= 2) {
        /* Include the bug-report boundary points explicitly. */
        bool boundary = (G == 72 || G == 76) ? true : false;
        for (int r = 0; r < reps; r++) {
            /* Reference: G sequential 1-threadgroup dispatches (clean). */
            for (size_t g = 0; g < G; g++)
                if (launch(refs, (uint32_t)(g * wg), 1) != CL_SUCCESS) { return 1; }
            if (clFinish(q) != CL_SUCCESS) return 1;
            if (clEnqueueReadBuffer(q, refs, CL_TRUE, 0, buf_items * 4,
                                    ref_h.data(), 0, nullptr, nullptr) != CL_SUCCESS)
                return 1;
            /* Test: one dispatch of G threadgroups. */
            if (launch(test, 0, G) != CL_SUCCESS) return 1;
            if (clFinish(q) != CL_SUCCESS) return 1;
            if (clEnqueueReadBuffer(q, test, CL_TRUE, 0, buf_items * 4,
                                    tst_h.data(), 0, nullptr, nullptr) != CL_SUCCESS)
                return 1;

            size_t mism = 0; size_t first = (size_t)-1;
            for (size_t i = 0; i < G * wg; i++)
                if (ref_h[i] != tst_h[i]) {
                    if (mism == 0) first = i;
                    mism++;
                }
            const char *tag = boundary ? " (boundary)" : "";
            if (mism == 0) {
                printf("G=%3zu groups x %3zu wg = %7zu items : PASS (rep %d)%s\n",
                       G, wg, G * wg, r, tag);
            } else {
                any_fail = true;
                printf("G=%3zu groups x %3zu wg = %7zu items : FAIL %zu/%zu mismatch "
                       "first@%zu (rep %d)%s\n",
                       G, wg, G * wg, mism, G * wg, first, r, tag);
                break;   /* no point repeating a failed geometry */
            }
        }
        if (any_fail) break;   /* first failure stops the sweep */
    }

    printf("RESULT: %s\n", any_fail ? "UNCLEAN (ceiling unchanged)"
                                    : "CLEAN through tested ceiling");
    clReleaseMemObject(test); clReleaseMemObject(refs); clReleaseMemObject(probe);
    clReleaseKernel(k); clReleaseProgram(prog); clReleaseCommandQueue(q);
    clReleaseContext(ctx);
    return any_fail ? 2 : 0;
}
