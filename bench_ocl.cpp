/* Benchmark harness for the OpenCL CSA brute-force kernel.
   Measures Mcw/s for various chunk sizes (outer keys per launch)
   over a key range that does NOT contain the solution.

   Build:
     g++ -std=c++17 -O2 -I src/ -I src/libdvbcsa/dvbcsa \
         bench_ocl.cpp src/ocl.cpp src/ts.c -o bench_ocl -framework OpenCL
*/

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <fstream>
#include <string>

#include "ocl.hpp"
#include "ts.h"

static std::string load_kernel_source(const char *path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open kernel file: " << path << std::endl;
        exit(1);
    }
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

int main(int argc, char **argv) {
    unsigned char probedata[48];
    memset(probedata, 0, sizeof(probedata));

    const char *tsfile = "test/Testfile_CW_7FFAE9A02486.ts";
    if (ayc_read_ts(tsfile, probedata) != 1) {
        std::cerr << "Failed to read TS file" << std::endl;
        return 1;
    }

    std::string kernel_src = load_kernel_source("src/aycwabtu.cl");

    OclContext ocl;
    if (!ocl_init(ocl, kernel_src.c_str())) {
        std::cerr << "OpenCL init failed" << std::endl;
        return 1;
    }

    /* Device info */
    cl_uint units = 0; size_t lmem = 0, wg = 0; cl_ulong gmem = 0;
    size_t wgm = 0;
    clGetDeviceInfo(ocl.device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(units), &units, nullptr);
    clGetDeviceInfo(ocl.device, CL_DEVICE_LOCAL_MEM_SIZE, sizeof(lmem), &lmem, nullptr);
    clGetDeviceInfo(ocl.device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(wg), &wg, nullptr);
    clGetDeviceInfo(ocl.device, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(gmem), &gmem, nullptr);
    clGetDeviceInfo(ocl.device, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(wgm), &wgm, nullptr);
    printf("Device: %u compute units, local mem %zu B, max wg %zu, global mem %llu MB\n",
           units, lmem, wg, (unsigned long long)(gmem >> 20));

    /* Benchmark: search ranges that will NOT find the key (key 0x7FFAE9A0
       is the solution, so stay far away from it). */
    uint32_t base = 0x00000000;
    uint32_t counts[] = { 256, 1024, 4096, 16384, 65536, 262144 };

    for (uint32_t count : counts) {
        if (argc > 1 && atoi(argv[1]) != (int)count) continue;
        uint8_t cw_out[8] = {0};

        /* warmup */
        ocl_search(ocl, probedata, base, count, 0, 65536, cw_out);

        int reps = (count < 4096) ? 5 : 2;
        auto t0 = std::chrono::steady_clock::now();
        for (int r = 0; r < reps; r++) {
            bool found = ocl_search(ocl, probedata, base + r * 0x01000000,
                                    count, 0, 65536, cw_out);
            if (found) { printf("unexpected find!\n"); return 1; }
        }
        auto t1 = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count() / reps;
        double cw_total = (double)count * 65536.0;
        printf("chunk %7u outer keys: %8.4f s/launch  -> %7.1f Mcw/s\n",
               count, secs, cw_total / secs / 1e6);
    }

    ocl_cleanup(ocl);
    return 0;
}
