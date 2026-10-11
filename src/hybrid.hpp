#ifndef AYCW_HYBRID_HPP
#define AYCW_HYBRID_HPP

#include <cstdint>
#include <string>
#include "bs_dispatch.h"
#include "work_coordinator.h"
#include "settings.h"

/* Find kernel source file — searches src/aycwabtu.cl, ../src/aycwabtu.cl, aycwabtu.cl */
std::string findKernelPath();

/* Load kernel source from file */
std::string loadKernelSource(const char* path);

/* Run hybrid CPU + GPU search.
   Launches concurrent CPU worker threads (using the selected SIMD driver)
   and a GPU worker thread (via OpenCL), dynamically sharing the outer key
   space through WorkCoordinator.
   Returns true if the key was found, false otherwise. */
bool runHybridSearch(const Settings& settings,
                     const BSDriver* cpuDriver,
                     const unsigned char probedata[3][16],
                     uint32_t keyStart,
                     uint32_t keyStop,
                     bool isBenchmark,
                     bool isSelfTest,
                     uint8_t* foundCwOut = nullptr);

/* Run GPU-only search (used for GPU standalone search, benchmark, and self-test).
   Returns true if the key was found, false otherwise. */
bool runGpuSearch(const Settings& settings,
                  const unsigned char probedata[3][16],
                  uint32_t keyStart,
                  uint32_t keyStop,
                  bool isBenchmark,
                  bool isSelfTest,
                  uint8_t* foundCwOut = nullptr);

#endif /* AYCW_HYBRID_HPP */
