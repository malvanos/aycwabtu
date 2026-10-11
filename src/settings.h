#ifndef AYCW_SETTINGS_H
#define AYCW_SETTINGS_H

#include <cstdint>
#include <string>

struct Settings {
    Settings()
        : benchmark(false)
        , selftest(false)
        , useGPU(false)
        , useCPU(true)
        , hybrid(false)
        , simd("auto")
        , keystart(0)
        , keystop(0xFFFFFFFF)
        , numThreads(1)
        , keystartSpecified(false)
        , numThreadsSpecified(false)
    {}

    bool        benchmark;
    bool        selftest;
    bool        useGPU;
    bool        useCPU;
    bool        hybrid;
    std::string simd;              /* auto | scalar | sse2 | avx2 | neon */
    std::string tsFilename;
    uint32_t    keystart;
    uint32_t    keystop;
    int         numThreads;
    bool        keystartSpecified;   /* true when -a was given on the CLI */
    bool        numThreadsSpecified; /* true when -p was given on the CLI */
};

#endif /* AYCW_SETTINGS_H */
