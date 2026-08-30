/*
   Run-time SIMD backend dispatch.

   Detects the CPU instruction sets (SSE2 / AVX2 on x86, NEON on ARM64,
   scalar fallback) and exposes the compiled backends as a function-pointer
   table.  `auto` picks the fastest built && supported backend; -S picks a
   specific one.

   CPU detection uses the compiler's __builtin_cpu_supports() (GCC and
   Clang on x86), which also takes OS support (XSAVE/AVX state) into
   account.  On ARM64 the NEON backend is always valid because AArch64
   requires Advanced SIMD.
*/

#include <cstdio>
#include <cstring>

#include "bs_dispatch.h"

/* --------------------------------------------------------------------------
   Per-backend driver entry points.  Each backend object file exports the
   suffixed symbols (see bs_rename.h / makefile).
   -------------------------------------------------------------------------- */
#define AYCW_BS_DECL_DRIVER(b)                                          \
    extern "C" void ayc_bruteForceRange_##b(uint32_t, uint32_t,         \
                                            const unsigned char (*)[16], \
                                            bool);                       \
    extern "C" void ayc_bruteForceParallel_##b(uint32_t, uint32_t,       \
                                               int, bool,                \
                                               const unsigned char (*)[16]); \
    extern "C" void ayc_benchmark_##b(int);                              \
    extern "C" void ayc_selfTest_##b(void);

AYCW_BS_DECL_DRIVER(scalar)
AYCW_BS_DECL_DRIVER(sse2)
AYCW_BS_DECL_DRIVER(avx2)
AYCW_BS_DECL_DRIVER(neon)
#undef AYCW_BS_DECL_DRIVER

/* --------------------------------------------------------------------------
   CPU feature detection
   -------------------------------------------------------------------------- */
static bool cpuHas(const char* feature) {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_cpu_init();
    return __builtin_cpu_supports(feature) != 0;
#else
    (void)feature;
    return false;
#endif
}

static bool cpuHasNeon(void) {
#if defined(__aarch64__) || defined(__ARM_NEON) || defined(_M_ARM64)
    return true;      /* Advanced SIMD is mandatory in AArch64 */
#else
    return false;
#endif
}

/* --------------------------------------------------------------------------
   Backend table.  Entries for backends that can't be compiled on this
   architecture keep built=false and null function pointers.
   -------------------------------------------------------------------------- */
#if defined(__x86_64__) || defined(__i386__)
#define AYCW_X86 1
#else
#define AYCW_X86 0
#endif
#if defined(__aarch64__) || defined(__ARM_NEON) || defined(_M_ARM64)
#define AYCW_ARM64 1
#else
#define AYCW_ARM64 0
#endif

static BSDriver g_drivers[BSIMD__COUNT] = {
    {
        BSIMD_SCALAR, "scalar", "32-bit scalar (portable)",
        1, 32,
        /*built=*/true, /*supported=*/true,
        ayc_bruteForceRange_scalar, ayc_bruteForceParallel_scalar,
        ayc_benchmark_scalar, ayc_selfTest_scalar,
    },
#if AYCW_X86
    {
        BSIMD_SSE2, "sse2", "SSE2 (128-bit)",
        2, 128,
        /*built=*/true, /*supported=*/false,
        ayc_bruteForceRange_sse2, ayc_bruteForceParallel_sse2,
        ayc_benchmark_sse2, ayc_selfTest_sse2,
    },
    {
        BSIMD_AVX2, "avx2", "AVX2 (256-bit)",
        4, 256,
        /*built=*/true, /*supported=*/false,
        ayc_bruteForceRange_avx2, ayc_bruteForceParallel_avx2,
        ayc_benchmark_avx2, ayc_selfTest_avx2,
    },
#else
    {
        BSIMD_SSE2, "sse2", "SSE2 (128-bit)",
        2, 128,
        /*built=*/false, /*supported=*/false,
        nullptr, nullptr, nullptr, nullptr,
    },
    {
        BSIMD_AVX2, "avx2", "AVX2 (256-bit)",
        4, 256,
        /*built=*/false, /*supported=*/false,
        nullptr, nullptr, nullptr, nullptr,
    },
#endif
#if AYCW_ARM64
    {
        BSIMD_NEON, "neon", "NEON (128-bit)",
        3, 128,
        /*built=*/true, /*supported=*/true,
        ayc_bruteForceRange_neon, ayc_bruteForceParallel_neon,
        ayc_benchmark_neon, ayc_selfTest_neon,
    },
#else
    {
        BSIMD_NEON, "neon", "NEON (128-bit)",
        3, 128,
        /*built=*/false, /*supported=*/false,
        nullptr, nullptr, nullptr, nullptr,
    },
#endif
};

#undef AYCW_X86
#undef AYCW_ARM64

void bs_detect_cpu(void) {
    for (int i = 0; i < BSIMD__COUNT; i++) {
        switch (g_drivers[i].id) {
        case BSIMD_SCALAR: g_drivers[i].supported = true; break;
        case BSIMD_SSE2:   g_drivers[i].supported = cpuHas("sse2"); break;
        case BSIMD_AVX2:   g_drivers[i].supported =
                               cpuHas("avx") && cpuHas("avx2"); break;
        case BSIMD_NEON:   g_drivers[i].supported = cpuHasNeon(); break;
        default:           g_drivers[i].supported = false; break;
        }
    }
}

const BSDriver* bs_drivers(int* count) {
    if (count) *count = BSIMD__COUNT;
    return g_drivers;
}

const BSDriver* bs_select(const char* name) {
    if (!name || !*name) return nullptr;

    if (strcmp(name, "auto") == 0) {
        /* fastest first: avx2 > sse2 > neon > scalar */
        static const BSimdB priority[BSIMD__COUNT] = {
            BSIMD_AVX2, BSIMD_SSE2, BSIMD_NEON, BSIMD_SCALAR,
        };
        for (int i = 0; i < BSIMD__COUNT; i++) {
            for (int d = 0; d < BSIMD__COUNT; d++) {
                if (g_drivers[d].id == priority[i] &&
                    g_drivers[d].built && g_drivers[d].supported)
                    return &g_drivers[d];
            }
        }
        return nullptr;   /* nothing runs on this CPU (shouldn't happen) */
    }

    for (int d = 0; d < BSIMD__COUNT; d++) {
        if (strcmp(g_drivers[d].name, name) == 0)
            return &g_drivers[d];
    }
    return nullptr;   /* unknown name */
}

static const char* bs_status(const BSDriver* d) {
    if (!d->built)    return "not built for this architecture";
    if (!d->supported) return "CPU does not support it";
    return "available";
}

void bs_print_list(FILE* out) {
    fprintf(out, "SIMD backends:\n");
    for (int i = 0; i < BSIMD__COUNT; i++) {
        const BSDriver* d = &g_drivers[i];
        fprintf(out, "  %-7s %-24s batch %3d keys  [%s]\n",
                d->name, d->desc, d->batchSize, bs_status(d));
    }
}