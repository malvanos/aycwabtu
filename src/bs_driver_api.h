#ifndef AYCW_BS_DRIVER_API_H_
#define AYCW_BS_DRIVER_API_H_

/*
   Entry points exported by bs_driver_impl.cpp (compiled once per SIMD
   backend).  When AYCW_BACKEND is defined (backend build), bs_rename.h
   renames these declarations to the suffixed symbols, so this file must be
   included AFTER bs_rename.h inside per-backend translation units.

   C linkage keeps the emitted symbol names unmangled so bs_dispatch.cpp can
   reference every backend's entry point (ayc_bruteForceRange_sse2, ...)
   directly without hard-coding C++ mangling.
*/

#include <cstdint>

extern "C" {

void ayc_bruteForceRange(uint32_t keyStart,
                         uint32_t keyStop,
                         const unsigned char probedata[3][16],
                         bool isBenchmark);

void ayc_bruteForceParallel(uint32_t keyStart,
                            uint32_t keyStop,
                            int numThreads,
                            bool isBenchmark,
                            const unsigned char probedata[3][16]);

void ayc_benchmark(int numThreads);

void ayc_selfTest(void);

} /* extern "C" */

#endif /* AYCW_BS_DRIVER_API_H_ */