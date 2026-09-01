#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>

#include "config.h"
#include "bs_dispatch.h"
#include "ts.h"
#include "ocl.hpp"

using namespace std;

#define VERSION         "V2.1"
#define GITHASH_STR     GITHASH
#define RESUMEFILENAME  "resume"

/* --------------------------------------------------------------------------
   Settings
   -------------------------------------------------------------------------- */
struct Settings {
    Settings()
        : benchmark(false)
        , selftest(false)
        , useGPU(false)
        , simd("auto")
        , keystart(0)
        , keystop(0xFFFFFFFF)
        , numThreads(1)
    {}

    bool     benchmark;
    bool     selftest;
    bool     useGPU;
    string   simd;              /* auto | scalar | sse2 | avx2 | neon */
    string   tsFilename;
    uint32_t keystart;
    uint32_t keystop;
    int      numThreads;
};

/* --------------------------------------------------------------------------
   Utility: timing (also used by the GPU progress report)
   -------------------------------------------------------------------------- */
static uint64_t getTicksMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

/* --------------------------------------------------------------------------
   Resume file (read side; write side lives in the per-backend driver)
   -------------------------------------------------------------------------- */
static void bfReadResumeFile(uint32_t *key) {
    FILE *f = fopen(RESUMEFILENAME, "rb");
    if (f) {
        char buf[8 * 3 + 2 + 1];
        unsigned char tmp[8 + 3];
        fseek(f, 0, SEEK_SET);
        fread(buf, sizeof(buf), 1, f);
        fclose(f);
        if (8 == sscanf(buf, "%02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX\n",
                        &tmp[0], &tmp[1], &tmp[2], &tmp[3],
                        &tmp[4], &tmp[5], &tmp[6], &tmp[7])) {
            *key = tmp[0] << 24 | tmp[1] << 16 | tmp[2] << 8 | tmp[4];
            printf("resuming at key %08X\n", *key);
        }
    }
}

/* --------------------------------------------------------------------------
   Found-key file (shared with the per-backend driver)
   -------------------------------------------------------------------------- */
void bfWriteKeyFoundFile(const unsigned char *cw) {
    printf("writing result to file \"keyfound\"\n");
    FILE *f = fopen("keyfound", "w");
    if (f) {
        char buf[8 * 3 + 2 + 1];
        sprintf(buf, "%02X %02X %02X %02X %02X %02X %02X %02X\n",
                cw[0], cw[1], cw[2], cw[3], cw[4], cw[5], cw[6], cw[7]);
        fwrite(buf, 1, strlen(buf), f);
        fclose(f);
    } else {
        printf("error opening file \"keyfound\" for writing\n");
    }
}

/* --------------------------------------------------------------------------
   Argument parsing
   -------------------------------------------------------------------------- */
static void printUsage() {
    printf("Usage: aycwabtu [OPTION]\n");
    printf("   -t filename      transport stream file to obtain three packets\n");
    printf("                    for brute force attack\n");
    printf("   -a start cw      cw to start the brute force attack with. Checksum\n");
    printf("                    bytes are omittted, e.g. 112233556677 [000000000000]\n");
    printf("   -o stop cw       when this cw is reached, program terminates [FFFFFFFFFFFF]\n");
    printf("   -p threads       number of parallel threads (default: 1)\n");
    printf("   -g               use GPU (OpenCL) for brute force search\n");
    printf("   -S backend       SIMD backend: auto (default) | scalar | sse2 | avx2\n");
    printf("                    | neon. 'auto' detects the best one on this CPU.\n");
    printf("                    Use -S list to show which backends are available.\n");
    printf("   -b               start benchmark run with internal demo ts data and quit\n");
    printf("   -s               execute algorithm self test for every available\n");
    printf("                    backend and quit (use -S to test one backend only)\n");
    printf("   -h               print this help message and quit\n");
}

static uint32_t scan_cw_param(const char *s) {
    uint8_t tmp[8];
    if ((strlen(s) != 12) ||
        (6 != sscanf(s, "%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
                     &tmp[0], &tmp[1], &tmp[2], &tmp[4], &tmp[5], &tmp[6]))) {
        throw runtime_error("Key parameter format incorrect. 6 hex bytes expected.");
    }
    return tmp[0] << 24 | tmp[1] << 16 | tmp[2] << 8 | tmp[4];
}

static Settings parse(int argc, char *argv[]) {
    const vector<string_view> args(argv + 1, argv + argc);
    Settings settings;

    for (auto it = args.begin(); it != args.end(); ++it) {
        if (*it == "-t") {
            it++;
            if (it == args.end()) throw runtime_error("Missing argument for -t");
            settings.tsFilename = *it;
            continue;
        }

        if (*it == "-a") {
            it++;
            if (it == args.end()) throw runtime_error("Missing argument for -a");
            settings.keystart = scan_cw_param(string(*it).c_str());
            continue;
        }

        if (*it == "-o") {
            it++;
            if (it == args.end()) throw runtime_error("Missing argument for -o");
            settings.keystop = scan_cw_param(string(*it).c_str());
            continue;
        }

        if (*it == "-p") {
            it++;
            if (it == args.end()) throw runtime_error("Missing argument for -p");
            settings.numThreads = stoi(string(*it));
            if (settings.numThreads < 1)
                throw runtime_error("Thread count must be >= 1");
            continue;
        }

        if (*it == "-S") {
            it++;
            if (it == args.end()) throw runtime_error("Missing argument for -S");
            settings.simd = string(*it);
            if (settings.simd == "list") {
                bs_detect_cpu();
                bs_print_list(stdout);
                exit(EXIT_SUCCESS);
            }
            continue;
        }

        if (*it == "-b") {
            settings.benchmark = true;
            continue;
        }

        if (*it == "-g") {
            settings.useGPU = true;
            continue;
        }

        if (*it == "-s") {
            settings.selftest = true;
            continue;
        }

        if (*it == "-h") {
            printUsage();
            exit(EXIT_SUCCESS);
        }

        throw runtime_error(string("unknown option: ") + string(*it));
    }

    return settings;
}

/* --------------------------------------------------------------------------
   Backend selection with a helpful error on unknown / unavailable choices
   -------------------------------------------------------------------------- */
static const BSDriver* chooseBackend(const Settings& settings) {
    bs_detect_cpu();

    const BSDriver* se = bs_select(settings.simd.c_str());
    if (!se) {
        cerr << "Error: unknown SIMD backend \"" << settings.simd << "\".\n";
        bs_print_list(stderr);
        cerr << "Valid values: auto, scalar, sse2, avx2, neon (see -S list)\n";
        exit(ERR_USAGE);
    }
    if (!se->built) {
        cerr << "Error: SIMD backend \"" << se->name
             << "\" is not compiled into this binary ("
             << "not available on this architecture).\n";
        bs_print_list(stderr);
        exit(ERR_USAGE);
    }
    if (!se->supported) {
        cerr << "Error: SIMD backend \"" << se->name
             << "\" is not supported by this CPU.\n";
        bs_print_list(stderr);
        cerr << "Run with -S auto to pick the best available backend.\n";
        exit(ERR_USAGE);
    }
    return se;
}

/* --------------------------------------------------------------------------
   Banner
   -------------------------------------------------------------------------- */
static void printBanner(const Settings& settings, const BSDriver* se) {
    const bool isAuto = (settings.simd == "auto");
    cout << "AYCWABTU CSA brute forcer " << VERSION << " " << GITHASH_STR
         << " built on " << __DATE__;
#ifdef _DEBUG
    cout << " DEBUG";
#endif
    if (settings.useGPU)
        cout << "\nGPU mode (OpenCL)";
    else {
        cout << "\nCPU only";
        if (settings.numThreads > 1)
            cout << ", " << settings.numThreads << " threads";
        else
            cout << ", single threaded";
        cout << "\nSIMD backend   : " << se->name << " - " << se->desc
             << (isAuto ? " (auto-detected)" : " (manual)");
        cout << "\nparallel bitslice batch size is " << se->batchSize;
    }
    cout << "\n----------------------------------------\n";
    setbuf(stdout, NULL);   // disable buffering
}

/* --------------------------------------------------------------------------
   Self-test mode
   -------------------------------------------------------------------------- */
/* Runs the built-in algorithm self test.  With -S auto the test is executed
   for EVERY backend that is compiled in and supported by this CPU — that is
   the unit test that every SIMD implementation produces identical
   libdvbcsa-verified results.  A specific backend can be tested alone with
   -s -S <backend>. */
static void runSelfTest(const Settings& settings, const BSDriver* se) {
    const bool all = (settings.simd == "auto");

    int count = 0;
    const BSDriver* drivers = bs_drivers(&count);

    /* count the backends we are going to test */
    int nTest = 0;
    for (int i = 0; i < count; i++)
        if (drivers[i].built && drivers[i].supported) nTest++;

    if (all) {
        cout << "Algorithm self-test (" << nTest
             << " SIMD backend(s) detected on this CPU)\n"
             << "----------------------------------------\n";
        for (int i = 0; i < count; i++) {
            const BSDriver* d = &drivers[i];
            if (!d->built || !d->supported) continue;
            cout << "\n===== backend: " << d->name << " (" << d->desc
                 << ", batch " << d->batchSize << " keys) =====\n";
            d->selfTest();          /* exits(ERR_FATAL) on any failure */
            cout << "===== backend " << d->name << ": PASSED =====\n\n";
        }
    } else {
        cout << "Algorithm self-test (" << settings.simd << ")\n"
             << "----------------------------------------\n";
        se->selfTest();
        cout << "===== backend " << se->name << ": PASSED =====\n";
    }
    cout << "Self-test PASSED\n";
}

/* --------------------------------------------------------------------------
   GPU (OpenCL) brute-force search
   -------------------------------------------------------------------------- */

/* verifyCw() now lives in ocl.hpp (single shared implementation). */

/* Find kernel source file — try several paths relative to CWD */
static string findKernelPath() {
    const char *candidates[] = {
        "src/aycwabtu.cl",
        "../src/aycwabtu.cl",
        "aycwabtu.cl",
        nullptr
    };
    for (int i = 0; candidates[i]; i++) {
        FILE *f = fopen(candidates[i], "r");
        if (f) { fclose(f); return candidates[i]; }
    }
    return "";
}

static string loadKernelSource(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return "";
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    string src(sz, '\0');
    fread(&src[0], 1, sz, f);
    fclose(f);
    return src;
}

static void bruteForceGPU(const Settings& settings,
                          unsigned char probedata[3][16]) {
#ifndef HAVE_OPENCL
    (void)settings; (void)probedata;
    cerr << "Error: GPU (OpenCL) support was not compiled into this build.\n"
         << "       Rebuild on a machine with an OpenCL toolchain (Linux:\n"
         << "       install ocl-icd-opencl-dev + headers), or run in CPU mode\n"
         << "       (omit -g or use -p <threads>)." << endl;
    exit(ERR_FATAL);
#else
    /* Find and load kernel */
    string kernelPath = findKernelPath();
    if (kernelPath.empty()) {
        cerr << "Error: cannot find OpenCL kernel source (aycwabtu.cl)" << endl;
        cerr << "Searched: src/aycwabtu.cl, ../src/aycwabtu.cl, aycwabtu.cl" << endl;
        exit(ERR_FATAL);
    }
    cout << "Loading kernel from: " << kernelPath << endl;

    string kernelSrc = loadKernelSource(kernelPath.c_str());
    if (kernelSrc.empty()) {
        cerr << "Error: failed to read kernel source" << endl;
        exit(ERR_FATAL);
    }

    /* Initialize OpenCL */
    OclContext ocl;
    if (!ocl_init(ocl, kernelSrc.c_str())) {
        cerr << "Error: OpenCL initialization failed" << endl;
        exit(ERR_FATAL);
    }

    /* Flatten probedata: [3][16] -> [48] */
    uint8_t probe[48];
    memcpy(probe, probedata, 48);

    /* Search in chunks to allow progress reporting while amortizing per-
       launch overhead.  Override with AYCWABTU_GPU_CHUNK_SIZE. */
    uint32_t chunkSize = 262144;  /* outer keys per GPU launch */
    char* env_chunk = std::getenv("AYCWABTU_GPU_CHUNK_SIZE");
    if (env_chunk && env_chunk[0]) {
        long cs = std::atol(env_chunk);
        if (cs < 1) {
            cerr << "Error: AYCWABTU_GPU_CHUNK_SIZE must be a positive number"
                 << endl;
            exit(ERR_USAGE);
        }
        chunkSize = (uint32_t)cs;
    }
    uint32_t keyStart = settings.keystart;
    uint32_t keyStop  = settings.keystop;

    cout << "GPU search: keys " << hex << keyStart << " .. " << keyStop << dec << endl;
    cout << "Chunk size: " << chunkSize << " outer keys per launch" << endl;

    uint64_t startTime = getTicksMs();
    uint64_t outerKeysDone = 0;
    uint64_t totalOuterKeys = (uint64_t)keyStop - keyStart + 1;

    /* 64-bit cursor so the loop ALWAYS terminates even when keyStop is
       near 0xFFFFFFFF: a 32-bit `chunkStart += chunkSize` would wrap past
       the end and re-scan from the beginning forever. */
    for (uint64_t cur = keyStart; cur <= (uint64_t)keyStop; ) {
        uint32_t chunkStart = (uint32_t)cur;
        uint32_t remaining  = (uint32_t)((uint64_t)keyStop - cur + 1);
        uint32_t count = chunkSize < remaining ? chunkSize : remaining;

        uint8_t cw_out[8] = {0};
        bool found = ocl_search(ocl, probe, chunkStart, count,
                                0, 65536, cw_out);

        outerKeysDone += count;
        cur += count;

        /* Progress report */
        uint64_t now = getTicksMs();
        float elapsed = (now - startTime) / 1000.0f;
        float mcwPerSec = elapsed > 0.0f
            ? (outerKeysDone * 65536.0f) / elapsed / 1e6f : 0.0f;
        float pctDone = totalOuterKeys
            ? (outerKeysDone * 100.0f) / totalOuterKeys : 0.0f;

        printf("\rGPU: %.1f%% done, %.1f Mcw/s, key %08X .. ",
               pctDone, mcwPerSec, chunkStart + count);
        fflush(stdout);

        if (found) {
            printf("\n\nGPU reports KEY FOUND: %02X %02X %02X [%02X]  %02X %02X %02X [%02X]\n",
                   cw_out[0], cw_out[1], cw_out[2], cw_out[3],
                   cw_out[4], cw_out[5], cw_out[6], cw_out[7]);

            /* ocl_search already CPU-validated this winner (returns true only
               for a real PES-valid key), so accept it directly.  No retry. */
            if (verifyCw(probe, cw_out)) {
                printf("CPU verify: all 3 packets decrypted to 0x000001 - accepting key\n");
                if (!settings.benchmark) {
                    bfWriteKeyFoundFile(cw_out);
                }
                ocl_cleanup(ocl);
                exit(OK);
            }
            printf("CPU verify failed on GPU-reported key - not accepted (no retry)\n");
        }
    }

    printf("\nGPU search complete. No key found.\n");
    ocl_cleanup(ocl);
#endif /* HAVE_OPENCL */
}

/* --------------------------------------------------------------------------
   main
   -------------------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    Settings settings;

    try {
        settings = parse(argc, argv);
    } catch (const exception& x) {
        cerr << "Error: " << x.what() << '\n';
        cerr << "Usage: " << argv[0]
             << " [-t filename] [-a start_cw] [-o stop_cw] [-p threads]\n"
             << "                 [-S backend] -b -s -h\n";
        return EXIT_FAILURE;
    }

    /* Validate arguments */
    if (!settings.benchmark && !settings.selftest && settings.tsFilename.empty()) {
        cerr << "Neither ts filename provided nor benchmark enabled" << endl;
        printUsage();
        return ERR_USAGE;
    }

    /* Is the requested (or best) SIMD backend usable? */
    const BSDriver* se = chooseBackend(settings);

    printBanner(settings, se);

    /* Algorithm self test and quit */
    if (settings.selftest) {
        runSelfTest(settings, se);
        return EXIT_SUCCESS;
    }

    /* Read resume file for non-benchmark runs (single-threaded only) */
    if (!settings.benchmark && settings.numThreads == 1) {
        bfReadResumeFile(&settings.keystart);
    }

    if (settings.benchmark) {
        se->benchmark(settings.numThreads);
        return EXIT_SUCCESS;
    }

    /* Load TS data */
    unsigned char probedata[3][16];
    ayc_read_ts(settings.tsFilename.c_str(), &probedata[0][0]);

    if (settings.useGPU) {
        bruteForceGPU(settings, probedata);
    } else if (settings.numThreads > 1) {
        se->bruteForceParallel(settings.keystart, settings.keystop,
                               settings.numThreads, /*isBenchmark=*/false,
                               probedata);
    } else {
        se->bruteForceRange(settings.keystart, settings.keystop,
                            probedata, /*isBenchmark=*/false);
    }

    return 0;
}