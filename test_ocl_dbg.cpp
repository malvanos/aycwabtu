/* Debug test for OpenCL kernel - tests block_decrypt on GPU */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include "ocl.hpp"
#include "ts.h"
#include "config.h"

static std::string load_kernel_source(const char *path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open kernel file: " << path << std::endl;
        exit(1);
    }
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

int main() {
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

    /* Create debug kernel */
    cl_int err;
    cl_kernel dbg_kernel = clCreateKernel(ocl.program, "debug_block_decrypt", &err);
    if (err != CL_SUCCESS) {
        std::cerr << "Failed to create debug kernel (err=" << err << ")" << std::endl;
        /* Check if kernel exists in program */
        size_t log_size;
        clGetProgramBuildInfo(ocl.program, ocl.device, CL_PROGRAM_BUILD_LOG,
                              0, nullptr, &log_size);
        if (log_size > 1) {
            char *log = new char[log_size + 1];
            clGetProgramBuildInfo(ocl.program, ocl.device, CL_PROGRAM_BUILD_LOG,
                                  log_size, log, nullptr);
            log[log_size] = '\0';
            std::cerr << "Build log:\n" << log << std::endl;
            delete[] log;
        }
        ocl_cleanup(ocl);
        return 1;
    }

    /* Set up buffers */
    cl_mem buf_probe = clCreateBuffer(ocl.context,
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        48, probedata, &err);

    uint32_t found_init[5] = { 0, 0, 0, 0, 0 };
    cl_mem buf_found = clCreateBuffer(ocl.context,
        CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
        sizeof(found_init), found_init, &err);

    uint32_t outer_key = 0x7FFAE9A0;
    uint32_t inner_val = 0x2486;

    clSetKernelArg(dbg_kernel, 0, sizeof(cl_mem), &buf_probe);
    clSetKernelArg(dbg_kernel, 1, sizeof(cl_mem), &buf_found);
    clSetKernelArg(dbg_kernel, 2, sizeof(uint32_t), &outer_key);
    clSetKernelArg(dbg_kernel, 3, sizeof(uint32_t), &inner_val);

    size_t global = 1;
    err = clEnqueueNDRangeKernel(ocl.queue, dbg_kernel, 1, nullptr,
                                 &global, &global, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        std::cerr << "Kernel launch failed: " << err << std::endl;
    }

    uint32_t found_out[5];
    err = clEnqueueReadBuffer(ocl.queue, buf_found, CL_TRUE, 0,
                              sizeof(found_out), found_out,
                              0, nullptr, nullptr);

    printf("Debug output:\n");
    printf("  found[0] = 0x%08X\n", found_out[0]);
    printf("  found[1] (dec[0..3]) = 0x%08X -> %02X %02X %02X %02X\n",
           found_out[1],
           (found_out[1] >> 24) & 0xFF,
           (found_out[1] >> 16) & 0xFF,
           (found_out[1] >> 8) & 0xFF,
           found_out[1] & 0xFF);
    printf("  found[2] (dec[4..7]) = 0x%08X -> %02X %02X %02X %02X\n",
           found_out[2],
           (found_out[2] >> 24) & 0xFF,
           (found_out[2] >> 16) & 0xFF,
           (found_out[2] >> 8) & 0xFF,
           found_out[2] & 0xFF);
    printf("  found[3] (kk[0..3]) = 0x%08X -> %02X %02X %02X %02X\n",
           found_out[3],
           (found_out[3] >> 24) & 0xFF,
           (found_out[3] >> 16) & 0xFF,
           (found_out[3] >> 8) & 0xFF,
           found_out[3] & 0xFF);
    printf("  found[4] (kk[4..7]) = 0x%08X -> %02X %02X %02X %02X\n",
           found_out[4],
           (found_out[4] >> 24) & 0xFF,
           (found_out[4] >> 16) & 0xFF,
           (found_out[4] >> 8) & 0xFF,
           found_out[4] & 0xFF);

    printf("\nExpected (from C reference):\n");
    printf("  dec[0..7] = 00 00 01 39 E4 37 A9 1B\n");
    printf("  kk[0..7] should match libdvbcsa key schedule\n");

    clReleaseMemObject(buf_probe);
    clReleaseMemObject(buf_found);
    clReleaseKernel(dbg_kernel);
    ocl_cleanup(ocl);

    return 0;
}
