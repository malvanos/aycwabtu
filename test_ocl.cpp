/* Test program for OpenCL CSA brute-force kernel.
   Tests with known key: Testfile_CW_7FFAE9A02486.ts
   Build:
     g++ -std=c++17 -DPARALLEL_MODE=3 -I src/ -I src/libdvbcsa/dvbcsa \
         test_ocl.cpp src/ocl.cpp src/ts.c -o test_ocl -framework OpenCL
   Run:
     ./test_ocl
*/

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>

#include "ocl.hpp"
#include "ts.h"
#include "config.h"

/* Read kernel source from file */
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
    /* Read TS file */
    unsigned char probedata[48];  /* 3 x 16 bytes */
    memset(probedata, 0, sizeof(probedata));

    const char *tsfile = "test/Testfile_CW_7FFAE9A02486.ts";
    if (ayc_read_ts(tsfile, probedata) != 1) {
        std::cerr << "Failed to read TS file" << std::endl;
        return 1;
    }

    /* Load kernel source */
    std::string kernel_src = load_kernel_source("src/aycwabtu.cl");

    /* Initialize OpenCL */
    OclContext ocl;
    if (!ocl_init(ocl, kernel_src.c_str())) {
        std::cerr << "OpenCL init failed" << std::endl;
        return 1;
    }

    /* Known key from Testfile_CW_7FFAE9A02486:
       Parsed as 6 hex bytes: 7F FA E9 A0 24 86
       byte0=0x7F, byte1=0xFA, byte2=0xE9
       byte4=0xA0, byte5=0x24, byte6=0x86
       byte3=0x7F+0xFA+0xE9=0x62 (checksum)
       byte7=0xA0+0x24+0x86=0x4A (checksum)

       Outer key32 = (byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte4
                   = 0x7FFAE9A0
       Inner key   = (byte5 << 8) | byte6 = 0x2486

       Expected CW: 7F FA E9 62 A0 24 86 4A
    */

    uint32_t key_start   = 0x7FFAE9A0;
    uint32_t key_count   = 1;        /* one outer key */
    uint32_t inner_start = 0;        /* full inner range */
    uint32_t inner_count = 65536;   /* test all inner keys */

    uint8_t cw_out[8] = {0};

    std::cout << "Searching for key..." << std::endl;
    std::cout << "Outer key: 0x" << std::hex << key_start << std::dec << std::endl;
    std::cout << "Inner range: 0.." << inner_count << std::endl;

    bool found = ocl_search(ocl, probedata, key_start, key_count,
                            inner_start, inner_count, cw_out);

    if (found) {
        printf("KEY FOUND: %02X %02X %02X %02X %02X %02X %02X %02X\n",
               cw_out[0], cw_out[1], cw_out[2], cw_out[3],
               cw_out[4], cw_out[5], cw_out[6], cw_out[7]);

        /* Verify expected CW */
        uint8_t expected[8] = {0x7F, 0xFA, 0xE9, 0x62, 0xA0, 0x24, 0x86, 0x4A};
        if (memcmp(cw_out, expected, 8) == 0) {
            std::cout << "PASS: Found key matches expected value!" << std::endl;
        } else {
            std::cout << "NOTE: Found key differs from expected." << std::endl;
            printf("Expected: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                   expected[0], expected[1], expected[2], expected[3],
                   expected[4], expected[5], expected[6], expected[7]);
        }
    } else {
        std::cout << "Key not found in search range." << std::endl;
    }

    ocl_cleanup(ocl);
    return found ? 0 : 1;
}
