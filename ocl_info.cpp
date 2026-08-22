/* Query kernel resource usage (private memory, wg size limits). */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <string>
#include "ocl.hpp"

int main() {
    std::ifstream f("src/aycwabtu.cl");
    std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    OclContext ocl;
    if (!ocl_init(ocl, src.c_str())) return 1;

    size_t wg = 0; cl_ulong priv = 0, lmem = 0;
    clGetKernelWorkGroupInfo(ocl.kernel, ocl.device, CL_KERNEL_WORK_GROUP_SIZE,
                             sizeof(wg), &wg, nullptr);
    clGetKernelWorkGroupInfo(ocl.kernel, ocl.device, CL_KERNEL_PRIVATE_MEM_SIZE,
                             sizeof(priv), &priv, nullptr);
    clGetKernelWorkGroupInfo(ocl.kernel, ocl.device, CL_KERNEL_LOCAL_MEM_SIZE,
                             sizeof(lmem), &lmem, nullptr);
    printf("kernel: max wg %zu, private mem %llu B/thread, local mem %llu B\n",
           wg, (unsigned long long)priv, (unsigned long long)lmem);

    size_t lmemdev = 0;
    clGetDeviceInfo(ocl.device, CL_DEVICE_LOCAL_MEM_SIZE, sizeof(lmemdev), &lmemdev, nullptr);
    printf("device local mem: %zu B -> max groups/CU (if private lives there): %zu at 128 thr, %zu at 256 thr\n",
           lmemdev, priv ? lmemdev / (priv * 128) : 0, priv ? lmemdev / (priv * 256) : 0);
    ocl_cleanup(ocl);
    return 0;
}
