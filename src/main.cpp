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
#include "settings.h"
#include "hybrid.hpp"

using namespace std;

#define VERSION         "V2.1"
#define GITHASH_STR     GITHASH
#define RESUMEFILENAME  "resume"

/* --------------------------------------------------------------------------
   Resume file (read side; write side lives in the per-backend driver)
   -------------------------------------------------------------------------- */
static void bfReadResumeFile(uint32_t *key) {
    FILE *f = fopen(RESUMEFILENAME, "rb");
    if (f) {
        char buf[64] = {0};
        unsigned char tmp[8 + 3];
        fseek(f, 0, SEEK_SET);
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        if (n > 0) {
            buf[n] = '\0';
            if (8 == sscanf(buf, "%02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX\n",
                            &tmp[0], &tmp[1], &tmp[2], &tmp[3],
                            &tmp[4], &tmp[5], &tmp[6], &tmp[7])) {
                *key = (uint32_t)tmp[0] << 24 | (uint32_t)tmp[1] << 16
                      | (uint32_t)tmp[2] << 8 | (uint32_t)tmp[4];
                printf("resuming at key %08X\n", *key);
            }
        }
    }
}

/* --------------------------------------------------------------------------
   Found-key file (shared with the per-backend driver and hybrid runner)
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
    printf("   -c, --cpu-gpu    run CPU and GPU at the same time\n");
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
    return (uint32_t)tmp[0] << 24 | (uint32_t)tmp[1] << 16
         | (uint32_t)tmp[2] << 8 | (uint32_t)tmp[4];
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
            settings.keystartSpecified = true;
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
            settings.numThreadsSpecified = true;
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

        if (*it == "-c" || *it == "--cpu-gpu" || *it == "--hybrid") {
            settings.hybrid = true;
            settings.useGPU = true;
            settings.useCPU = true;
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

    /* Resolve mode */
    if (settings.hybrid) {
        settings.useGPU = true;
        settings.useCPU = true;
    } else if (settings.useGPU) {
        if (settings.numThreadsSpecified) {
            settings.hybrid = true;
            settings.useCPU = true;
        } else {
            settings.useCPU = false;
        }
    } else {
        settings.useCPU = true;
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
    if (settings.hybrid) {
        cout << "\nHybrid mode (CPU + GPU simultaneously)";
        cout << "\nCPU threads    : " << settings.numThreads << " thread"
             << (settings.numThreads > 1 ? "s" : "");
        cout << "\nSIMD backend   : " << se->name << " - " << se->desc
             << (isAuto ? " (auto-detected)" : " (manual)");
        cout << "\nparallel bitslice batch size is " << se->batchSize;
        cout << "\nGPU backend    : OpenCL";
    } else if (settings.useGPU) {
        cout << "\nGPU mode (OpenCL)";
    } else {
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

    if (settings.hybrid) {
        cout << "\n===== CPU+GPU hybrid self-test (" << se->name << " + OpenCL) =====\n";
        cout << "Searching demo key 00 11 22 33 44 00 00 44 with CPU and GPU simultaneously...\n";
        uint8_t foundKey[8] = {0};
        bool ok = runHybridSearch(settings, se, bfDemoData,
                                  0x00112200, 0x00112244,
                                  /*isBenchmark=*/true, /*isSelfTest=*/true,
                                  foundKey);
        if (!ok) {
            cerr << "FAIL: CPU+GPU hybrid self-test did not find known key!\n";
            exit(ERR_FATAL);
        }
        cout << "===== CPU+GPU hybrid self-test: PASSED =====\n\n";
    } else if (settings.useGPU) {
        cout << "\n===== GPU (OpenCL) self-test =====\n";
        uint8_t foundKey[8] = {0};
        bool ok = runGpuSearch(settings, bfDemoData,
                               0x00112200, 0x00112244,
                               /*isBenchmark=*/true, /*isSelfTest=*/true,
                               foundKey);
        if (!ok) {
            cerr << "FAIL: GPU self-test did not find known key!\n";
            exit(ERR_FATAL);
        }
        cout << "===== GPU (OpenCL) self-test: PASSED =====\n\n";
    }

    cout << "Self-test PASSED\n";
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
             << "                 [-g] [-c] [-S backend] -b -s -h\n";
        return EXIT_FAILURE;
    }

    /* Validate arguments */
    if (!settings.benchmark && !settings.selftest && settings.tsFilename.empty()) {
        cerr << "Neither ts filename provided nor benchmark enabled" << endl;
        printUsage();
        return ERR_USAGE;
    }

    /* Validate key range for real searches (benchmark and self-test use
       their own hardcoded ranges and ignore the CLI -a/-o values). */
    if (!settings.benchmark && !settings.selftest &&
        settings.keystart > settings.keystop) {
        cerr << "Error: start key must be <= stop key (got start=0x"
             << hex << settings.keystart << " stop=0x" << settings.keystop
             << dec << ")" << endl;
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

    /* Read resume file for non-benchmark runs (single-threaded CPU only), unless
       the user already supplied -a explicitly (CLI wins over resume). */
    if (!settings.benchmark && !settings.useGPU && !settings.hybrid &&
        settings.numThreads == 1 && !settings.keystartSpecified) {
        bfReadResumeFile(&settings.keystart);
    }

    if (settings.benchmark) {
        if (settings.hybrid) {
            cout << "Starting CPU + GPU hybrid benchmarking" << endl;
            uint32_t keystart = 0x00 << 24 | 0x11 << 16 | 0x15 << 8 | 0x00;
            uint32_t keystop  = 0xFFFFFFFF;
            runHybridSearch(settings, se, bfDemoData, keystart, keystop,
                            /*isBenchmark=*/true, /*isSelfTest=*/false);
            return EXIT_SUCCESS;
        } else if (settings.useGPU) {
            cout << "Starting GPU benchmarking" << endl;
            uint32_t keystart = 0x00 << 24 | 0x11 << 16 | 0x15 << 8 | 0x00;
            uint32_t keystop  = 0xFFFFFFFF;
            runGpuSearch(settings, bfDemoData, keystart, keystop,
                         /*isBenchmark=*/true, /*isSelfTest=*/false);
            return EXIT_SUCCESS;
        } else {
            se->benchmark(settings.numThreads);
            return EXIT_SUCCESS;
        }
    }

    /* Load TS data */
    unsigned char probedata[3][16];
    ayc_read_ts(settings.tsFilename.c_str(), &probedata[0][0]);

    if (settings.hybrid) {
        bool found = runHybridSearch(settings, se, probedata,
                                     settings.keystart, settings.keystop,
                                     /*isBenchmark=*/false, /*isSelfTest=*/false);
        if (found) return 0;
        return 0;
    } else if (settings.useGPU) {
        bool found = runGpuSearch(settings, probedata,
                                  settings.keystart, settings.keystop,
                                  /*isBenchmark=*/false, /*isSelfTest=*/false);
        if (found) return 0;
        return 0;
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