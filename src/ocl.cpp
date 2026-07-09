#include "ocl.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

/* Embedded kernel source (loaded from file or embedded string) */
static const char* get_kernel_source();

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

bool ocl_search(OclContext& ocl,
                const uint8_t probedata[48],
                uint32_t key_start, uint32_t key_stop,
                uint32_t key_count,
                uint8_t cw_out[8]) {
    if (!ocl.ready) return false;

    cl_int err;
    const size_t wg_size = 128;  /* work-group size */
    size_t global_size = ((key_count + wg_size - 1) / wg_size) * wg_size;

    /* Clamp to reasonable max (avoid GPU timeout) */
    if (global_size > 1024 * 1024) global_size = 1024 * 1024;

    /* Buffers */
    cl_mem buf_probe = clCreateBuffer(ocl.context,
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        48, (void*)probedata, &err);
    if (err != CL_SUCCESS) return false;

    uint32_t found_init[3] = { 0, 0, 0 };
    cl_mem buf_found = clCreateBuffer(ocl.context,
        CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
        sizeof(found_init), found_init, &err);
    if (err != CL_SUCCESS) { clReleaseMemObject(buf_probe); return false; }

    /* Set kernel args */
    clSetKernelArg(ocl.kernel, 0, sizeof(cl_mem), &buf_probe);
    clSetKernelArg(ocl.kernel, 1, sizeof(cl_mem), &buf_found);
    clSetKernelArg(ocl.kernel, 2, sizeof(uint32_t), &key_start);
    clSetKernelArg(ocl.kernel, 3, sizeof(uint32_t), &key_stop);

    /* Launch kernel */
    err = clEnqueueNDRangeKernel(ocl.queue, ocl.kernel, 1, nullptr,
                                 &global_size, &wg_size, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        clReleaseMemObject(buf_probe);
        clReleaseMemObject(buf_found);
        return false;
    }

    /* Read result */
    uint32_t found_out[3];
    err = clEnqueueReadBuffer(ocl.queue, buf_found, CL_TRUE, 0,
                              sizeof(found_out), found_out,
                              0, nullptr, nullptr);
    clReleaseMemObject(buf_probe);
    clReleaseMemObject(buf_found);

    if (err != CL_SUCCESS) return false;

    if (found_out[0] != 0) {
        cw_out[0] = (found_out[1] >> 24) & 0xFF;
        cw_out[1] = (found_out[1] >> 16) & 0xFF;
        cw_out[2] = (found_out[1] >>  8) & 0xFF;
        cw_out[3] = (found_out[1]) & 0xFF;
        cw_out[4] = (found_out[2] >> 24) & 0xFF;
        cw_out[5] = (found_out[2] >> 16) & 0xFF;
        cw_out[6] = (found_out[2] >>  8) & 0xFF;
        cw_out[7] = (found_out[2]) & 0xFF;
        return true;
    }
    return false;
}

void ocl_cleanup(OclContext& ocl) {
    if (ocl.kernel)  clReleaseKernel(ocl.kernel);
    if (ocl.program) clReleaseProgram(ocl.program);
    if (ocl.queue)   clReleaseCommandQueue(ocl.queue);
    if (ocl.context) clReleaseContext(ocl.context);
    ocl.ready = false;
}
