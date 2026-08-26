/* Repro/diagnostic for large-chunk searches.
   Normal mode:   ./test_ocl_big <base_hex> <count>            (uses src/aycwabtu.cl)
   Diagnostic:    ./test_ocl_big <base_hex> <count> diag       (uses /tmp/aycwabtu_diag.cl,
                   which reports (outer_key, inner) of the winning work-item)
   Any GPU-reported key is verified with libdvbcsa on the CPU. */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>

#include "ocl.hpp"
#include "ts.h"
#include "dvbcsa.h"

static std::string load_kernel_source(const char *path) {
    std::ifstream file(path);
    if (!file.is_open()) { std::cerr << "open fail: " << path << "\n"; exit(1); }
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

static int verify_cpu(const uint8_t *probedata, const uint8_t *cw) {
    dvbcsa_key_t key;
    dvbcsa_key_set(cw, &key);
    int good = 0;
    for (int p = 0; p < 3; p++) {
        uint8_t data[16];
        memcpy(data, probedata + p * 16, 16);
        dvbcsa_decrypt(&key, data, 16);
        if (data[0] == 0 && data[1] == 0 && data[2] == 1) good++;
    }
    return good;
}

int main(int argc, char **argv) {
    uint32_t base  = (argc > 1) ? (uint32_t)strtoul(argv[1], nullptr, 16) : 0;
    uint32_t count = (argc > 2) ? (uint32_t)strtoul(argv[2], nullptr, 0) : 16384;
    bool diag      = (argc > 3) && std::string(argv[3]) == "diag";
    uint32_t inner_count = (argc > 4) ? (uint32_t)strtoul(argv[4], nullptr, 0) : 65536;
    const char *kpath = (argc > 5) ? argv[5]
                       : (diag ? "/tmp/aycwabtu_diag.cl" : "src/aycwabtu.cl");

    unsigned char probedata[48];
    memset(probedata, 0, sizeof(probedata));
    if (ayc_read_ts("test/Testfile_CW_7FFAE9A02486.ts", probedata) != 1) return 1;

    std::cout << "kernel: " << kpath << std::endl;
    std::string kernel_src = load_kernel_source(kpath);
    OclContext ocl;
    if (!ocl_init(ocl, kernel_src.c_str())) return 1;

    uint8_t cw_out[8] = {0};
    bool found = ocl_search(ocl, probedata, base, count, 0, inner_count, cw_out);
    if (!found) { printf("no key found\n"); return 0; }

    uint8_t cw[8];
    if (diag) {
        uint32_t outer = ((uint32_t)cw_out[0] << 24) | ((uint32_t)cw_out[1] << 16) |
                         ((uint32_t)cw_out[2] << 8) | cw_out[3];
        uint32_t inner = ((uint32_t)cw_out[6] << 8) | cw_out[7];
        printf("GPU winner: outer=%08X inner=%04X\n", outer, inner);
        if (outer < base || outer >= base + count)
            printf("WARNING: outer key OUTSIDE launched range!\n");
        cw[0] = outer >> 24; cw[1] = outer >> 16; cw[2] = outer >> 8;
        cw[3] = cw[0] + cw[1] + cw[2];
        cw[4] = outer & 0xFF; cw[5] = inner >> 8; cw[6] = inner & 0xFF;
        cw[7] = cw[4] + cw[5] + cw[6];
    } else {
        memcpy(cw, cw_out, 8);
    }

    printf("CW: %02X %02X %02X %02X %02X %02X %02X %02X\n",
           cw[0], cw[1], cw[2], cw[3], cw[4], cw[5], cw[6], cw[7]);
    int good = verify_cpu(probedata, cw);
    printf(good == 3 ? "VERIFIED REAL KEY\n" : "FALSE POSITIVE (%d/3 packets)\n", good);
    ocl_cleanup(ocl);
    return 0;
}
