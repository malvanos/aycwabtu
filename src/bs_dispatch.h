#ifndef AYCW_BS_DISPATCH_H_
#define AYCW_BS_DISPATCH_H_

/*
   Run-time SIMD backend dispatch (auto-detect or manual -S selection).

   One binary now contains every backend that the host architecture can
   compile (x86_64: scalar + sse2 + avx2, ARM64: scalar + neon).  At run
   time bs_detect_cpu() reads the CPU features and bs_select() returns the
   best *built and supported* backend, or the explicitly requested one.
*/

#include <cstdint>

enum BSimdB {
    BSIMD_SCALAR,
    BSIMD_SSE2,
    BSIMD_NEON,
    BSIMD_AVX2,
    BSIMD__COUNT,     /* number of concrete drivers (must stay last) */
    BSIMD_AUTO = 99   /* magic value: "pick the best" */
};

struct BSDriver {
    BSimdB      id;
    const char* name;          /* CLI name for -S */
    const char* desc;          /* human-readable description          */
    int         parallelMode;  /* PARALLEL_* constant                 */
    int         batchSize;     /* keys per SIMD batch                 */
    bool        built;         /* compiled into this binary           */
    bool        supported;     /* this CPU supports it (run time)     */

    /* per-backend driver entry points (bs_driver_impl.cpp) */
    void (*bruteForceRange)(uint32_t keyStart, uint32_t keyStop,
                            const unsigned char probedata[3][16],
                            bool isBenchmark);
    void (*bruteForceParallel)(uint32_t keyStart, uint32_t keyStop,
                               int numThreads, bool isBenchmark,
                               const unsigned char probedata[3][16]);
    void (*benchmark)(int numThreads);
    void (*selfTest)(void);
};

/* initialize the built/supported flags. Call once at startup. */
void bs_detect_cpu(void);

/* all known backends (built or not). count = number of entries. */
const BSDriver* bs_drivers(int* count);

/* select a backend:
     - "auto"          -> best built && supported backend
     - "scalar"/"sse2"/"avx2"/"neon" -> that backend (check .built/.supported
       to produce a user error)
   returns nullptr for an unknown name. */
const BSDriver* bs_select(const char* name);

/* print the backend table (one line per backend) for -S list / help. */
void bs_print_list(FILE* out);

#endif /* AYCW_BS_DISPATCH_H_ */