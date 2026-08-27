#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <functional>
#include <string_view>
#include <thread>
#include <atomic>

#include "config.h"
#include "bs_stream.h"
#include "bs_block_ab.h"
#include "bs_algo.h"
#include "bs_testcases.h"
#include "dvbcsa.h"
#include "ts.h"
#include "ocl.hpp"

using namespace std;

#define VERSION         "V2.0"
#define INNERKEYBITS    16
#define KEYSPERINNERLOOP (1 << INNERKEYBITS)
#define RESUMEFILENAME  "resume"
#define FOUNDFILENAME   "keyfound"

/* --------------------------------------------------------------------------
   Settings
   -------------------------------------------------------------------------- */
struct Settings {
    Settings()
        : benchmark(false)
        , selftest(false)
        , useGPU(false)
        , keystart(0)
        , keystop(0xFFFFFFFF)
        , numThreads(1)
    {}

    bool     benchmark;
    bool     selftest;
    bool     useGPU;
    string   tsFilename;
    uint32_t keystart;
    uint32_t keystop;
    int      numThreads;
};

/* --------------------------------------------------------------------------
   Forward declarations
   -------------------------------------------------------------------------- */
struct BFShared;
static void bruteForceRange(uint32_t keyStart, uint32_t keyStop,
                            const unsigned char probedata[3][16],
                            bool isBenchmark, int tid, int nThreads,
                            atomic<int>& keyFound,
                            BFShared* shared = nullptr);
static void bruteForceParallel(const Settings& settings,
                               unsigned char probedata[3][16]);
static void bruteForceGPU(const Settings& settings,
                          unsigned char probedata[3][16]);

/* --------------------------------------------------------------------------
   Utility: hex print
   -------------------------------------------------------------------------- */
static void printHexBytes(const unsigned char *c, int len) {
    for (int i = 0; i < len; i++) {
        if (i && !(i % 4)) printf(" ");
        printf("%02X", c[i]);
    }
    printf("\n");
}

/* --------------------------------------------------------------------------
   Utility: timing
   -------------------------------------------------------------------------- */
static uint64_t getTicksMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

/* --------------------------------------------------------------------------
   Brute-force state (replaces C globals)
   -------------------------------------------------------------------------- */
struct BFState {
    uint32_t currentkey32;
    uint32_t stopkey32;
    uint64_t time_start   = 0;
    uint64_t deltaticks   = 0;
    uint64_t totalticks   = 0;
    int      totalloops   = 0;
    int      divider      = 0;
    bool     benchmark;
};

/* Shared cross-thread progress.  Each thread adds the keys it processed to
   keysDone; thread 0 samples that counter and reports the AGGREGATE rate over
   all threads.  Used only in multi-threaded mode. */
struct BFShared {
    std::atomic<uint64_t> keysDone{0};
    uint64_t lastKeys  = 0;
    uint64_t lastTicks = 0;
    int      reports   = 0;
};

static void bfPerformanceStart(BFState& st) {
    if (!st.divider) st.time_start = getTicksMs();
}

static void bfPerfShow(BFState& st, int tid) {
    const char prop[] = "|/-\\";

#ifdef _DEBUG
#define DIVIDER 1
#else
#define DIVIDER 16
#endif

    st.divider++;
    if (st.divider >= DIVIDER) st.divider = 0;
    if (!st.divider) {
        putc(prop[(st.totalloops & 3)], stdout);
        st.deltaticks = getTicksMs() - st.time_start;
        st.totalticks += st.deltaticks;
        st.totalloops++;
        if (st.deltaticks) {
            printf(" %.3f Mcw/s ",
                   ((float)KEYSPERINNERLOOP * DIVIDER / st.deltaticks / 1000));
        }
        if (st.totalticks) {
            printf("avg: %.3f Mcw/s  ",
                   ((float)KEYSPERINNERLOOP * DIVIDER / ((float)st.totalticks / st.totalloops)) / 1000);
        }
        if (tid >= 0) {
            printf("[T%d] %02X %02X %02X [] %02X .. .. []\r",
                   tid,
                   st.currentkey32 >> 24,
                   st.currentkey32 >> 16 & 0xFF,
                   st.currentkey32 >> 8 & 0xFF,
                   st.currentkey32 & 0xFF);
        } else {
            printf("%02X %02X %02X [] %02X .. .. []\r",
                   st.currentkey32 >> 24,
                   st.currentkey32 >> 16 & 0xFF,
                   st.currentkey32 >> 8 & 0xFF,
                   st.currentkey32 & 0xFF);
        }
    }
#undef DIVIDER
}

/* Aggregate progress: called by thread 0 only.  Reads the shared keysDone
   counter (which all threads keep bumping) and reports the combined Mcw/s.
   This is why -p N visibly shows ~N x the single-thread rate. */
static void bfPerfAggregate(BFShared& sh, int nThreads, const BFState& st) {
    static const char prop[] = "|/-\\";

    uint64_t now  = getTicksMs();
    uint64_t keys = sh.keysDone.load(std::memory_order_relaxed);

    if (sh.reports == 0) {
        sh.lastKeys  = keys;
        sh.lastTicks = now;
        sh.reports++;
        return;
    }

    uint64_t dk = keys - sh.lastKeys;
    uint64_t dt = now - sh.lastTicks;
    sh.lastKeys  = keys;
    sh.lastTicks = now;
    sh.reports++;

    putc(prop[(sh.reports & 3)], stdout);
    if (dt) {
        printf(" %.3f Mcw/s aggregate (%d threads)  ",
               dk / (double)dt / 1000.0, nThreads);
    }
    printf("[T0] %02X %02X %02X [] %02X .. .. []\r",
           st.currentkey32 >> 24,
           st.currentkey32 >> 16 & 0xFF,
           st.currentkey32 >> 8 & 0xFF,
           st.currentkey32 & 0xFF);
}

/* --------------------------------------------------------------------------
   Resume file
   -------------------------------------------------------------------------- */
static void bfWriteResumeFile(BFState& st, int tid) {
    static int divider = 10;
    divider++;
    divider &= 0x1ff;
    if (!divider) {
        char fname[64];
        if (tid >= 0)
            snprintf(fname, sizeof(fname), "%s-%d", RESUMEFILENAME, tid);
        else
            snprintf(fname, sizeof(fname), "%s", RESUMEFILENAME);
        FILE *f = fopen(fname, "w");
        if (f) {
            char buf[64];
            sprintf(buf, "%02X %02X %02X %02X %02X %02X %02X %02X\n",
                    (uint8)(st.currentkey32 >> 24),
                    (uint8)(st.currentkey32 >> 16),
                    (uint8)(st.currentkey32 >> 8), 0,
                    (uint8)st.currentkey32, 0, 0, 0);
            fwrite(buf, 1, strlen(buf), f);
            fclose(f);
        } else {
            printf("error writing resume file\n");
        }
    }
}

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
   Found-key file
   -------------------------------------------------------------------------- */
static void bfWriteKeyFoundFile(const unsigned char *cw) {
    printf("writing result to file \"%s\"\n", FOUNDFILENAME);
    FILE *f = fopen(FOUNDFILENAME, "w");
    if (f) {
        char buf[8 * 3 + 2 + 1];
        sprintf(buf, "%02X %02X %02X %02X %02X %02X %02X %02X\n",
                cw[0], cw[1], cw[2], cw[3], cw[4], cw[5], cw[6], cw[7]);
        fwrite(buf, 1, strlen(buf), f);
        fclose(f);
    } else {
        printf("error opening file \"%s\" for writing\n", FOUNDFILENAME);
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
    printf("   -b               start benchmark run with internal demo ts data and quit\n");
    printf("   -s               execute algorithm self test and quit\n");
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
   Banner
   -------------------------------------------------------------------------- */
static void printBanner(const Settings& settings) {
    cout << "AYCWABTU CSA brute forcer " << VERSION << " " << GITHASH
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
#ifdef USEALLBITSLICE
        cout << " - all bit slice (bool sbox)";
#else
        cout << " - table sbox";
#endif
        cout << "\nparallel bitslice batch size is " << BS_BATCH_SIZE;
    }
    cout << "\n";
    cout << "----------------------------------------\n";
    setbuf(stdout, NULL);   // disable buffering
}

/* --------------------------------------------------------------------------
   Micro-benchmarks  (only when USE_MEASURE is defined)
   -------------------------------------------------------------------------- */
static void partsBenchmark() {
#ifdef USE_MEASURE
    aycw_tstRegister stRegister;
    dvbcsa_bs_word_t r[8 * (1 + 8 + 56)];
    dvbcsa_bs_word_t bs_128[8 * 16];
    dvbcsa_bs_word_t bs_64_1[64];
    dvbcsa_bs_word_t bs_64_2[64];
    dvbcsa_bs_word_t bs_448[448];

    const long maxIter = 1 << 19;

    cout << "Performance measurement of all algorithmic parts for "
         << maxIter << " loops" << endl;

    auto measure = [&](string_view name, function<void()>&& fn) {
        uint64_t start = getTicksMs();
        for (long i = 0; i < maxIter; i++) fn();
        cout << name << " " << (getTicksMs() - start) << " ms" << endl;
    };

    measure("aycw_stream_decrypt()",
            [&]() { aycw_stream_decrypt(bs_64_2, 25, bs_64_1, bs_128); });
    measure("aycw__vInitShiftRegister()",
            [&]() { aycw__vInitShiftRegister(bs_64_1, &stRegister); });
    measure("aycw_bit2byteslice(7)",
            [&]() { aycw_bit2byteslice(bs_448, 7); });
    measure("aycw_block_key_schedule",
            [&]() { aycw_block_key_schedule(bs_64_1, bs_448); });
    measure("aycw_block_decrypt",
            [&]() { aycw_block_decrypt(bs_448, r); });
    measure("aycw_block_sbox (56x)",
            [&]() { for (int j = 0; j < 56; j++) aycw_block_sbox(bs_448, r); });
    measure("aycw_checkPESheader",
            [&]() { aycw_checkPESheader(r, bs_64_1); });
#endif
}

/* --------------------------------------------------------------------------
   Benchmark mode
   -------------------------------------------------------------------------- */
static void benchmark(Settings& settings) {
    cout << "Starting micro-benchmarking" << endl;
    partsBenchmark();

    cout << "Starting benchmarking" << endl;

    unsigned char probedata[3][16] = {
        { 0xB2, 0x74, 0x85, 0x51, 0xF9, 0x3C, 0x9B, 0xD2,
          0x30, 0x9E, 0x8E, 0x78, 0xFB, 0x16, 0x55, 0xA9 },
        { 0x25, 0x2D, 0x3D, 0xAB, 0x5E, 0x3B, 0x31, 0x39,
          0xFE, 0xDF, 0xCD, 0x84, 0x51, 0x5A, 0x86, 0x4A },
        { 0xD0, 0xE1, 0x78, 0x48, 0xB3, 0x41, 0x63, 0x22,
          0x25, 0xA3, 0x63, 0x0A, 0x0E, 0xD3, 0x1C, 0x70 }
    };

    settings.keystart = 0x00 << 24 | 0x11 << 16 | 0x15 << 8 | 0x00;
    settings.keystop = 0xFFFFFFFF;

    if (settings.numThreads > 1)
        bruteForceParallel(settings, probedata);
    else {
        atomic<int> keyFound{0};
        bruteForceRange(settings.keystart, settings.keystop,
                        probedata, true, -1, 1, keyFound);
    }
}

/* --------------------------------------------------------------------------
   Core brute-force search — single key range, runs on one thread
   tid = -1 means single-threaded mode (no thread ID in output)
   -------------------------------------------------------------------------- */
static void bruteForceRange(uint32_t keyStart, uint32_t keyStop,
                            const unsigned char probedata[3][16],
                            bool isBenchmark, int tid, int nThreads,
                            atomic<int>& keyFound,
                            BFShared* shared) {
    int i, k;

    dvbcsa_bs_word_t bs_data_sb0[8 * 16];
    dvbcsa_bs_word_t bs_data_ib0[8 * 16];
    dvbcsa_bs_word_t keys_bs[64];
    dvbcsa_bs_word_t keyskk[448];

#ifdef USEBLOCKVIRTUALSHIFT
    dvbcsa_bs_word_t r[8 * (1 + 8 + 56)];
#else
    dvbcsa_bs_word_t r[8 * (1 + 8 + 0)];
#endif

    dvbcsa_bs_word_t candidates;
    uint8 keylist[BS_BATCH_SIZE][8];

    BFState st;
    st.currentkey32 = keyStart;
    st.stopkey32    = keyStop;
    st.benchmark    = isBenchmark;

    if (tid <= 0) {  // only thread 0 (or single-threaded) prints
        printf("start key is %02X %02X %02X [] %02X %02X %02X []\n",
               (uint8)(keyStart >> 24), (uint8)(keyStart >> 16),
               (uint8)(keyStart >> 8), (uint8)keyStart, 0, 0);
        printf("stop key is  %02X %02X %02X [] %02X %02X %02X []\n",
               (uint8)(keyStop >> 24), (uint8)(keyStop >> 16),
               (uint8)(keyStop >> 8), (uint8)keyStop, 0xFF, 0xFF);
    }

    aycw_init_block();
    aycw_init_stream(probedata[0], bs_data_sb0);

    for (i = 0; i < 8 * 8; i++) {
        bs_data_ib0[i] = bs_data_sb0[i];
    }
#ifndef USEALLBITSLICE
    aycw_bit2byteslice(bs_data_ib0, 1);
#endif

    /* ======== outer loop ======== */
    while (st.currentkey32 <= st.stopkey32 && keyFound.load() == 0) {
        bfPerformanceStart(st);

#if BS_BATCH_SIZE > 256
#error keylist calculation cannot yet handle BS_BATCH_SIZE>256
#endif
        for (i = 0; i < BS_BATCH_SIZE; i++) {
            keylist[i][0] = st.currentkey32 >> 24;
            keylist[i][1] = st.currentkey32 >> 16;
            keylist[i][2] = st.currentkey32 >> 8;
            keylist[i][3] = keylist[i][0] + keylist[i][1] + keylist[i][2];
            keylist[i][4] = st.currentkey32;
            keylist[i][5] = 0;
            keylist[i][6] = (0x0100 >> BS_BATCH_SHIFT) * i;
            keylist[i][7] = keylist[i][4] + keylist[i][5] + keylist[i][6];
        }

        aycw_key_transpose(&keylist[0][0], keys_bs);
        aycw_assert_key_transpose(&keylist[0][0], keys_bs);

        /* ======== inner loop: process 2^16 keys ======== */
        for (k = 0; k < KEYSPERINNERLOOP / BS_BATCH_SIZE; k++) {

            aycw_assertKeyBatch(keys_bs);

            /* ---- stream decrypt ---- */
            aycw_stream_decrypt(&bs_data_ib0[64], 24, keys_bs, bs_data_sb0);
            aycw_assert_stream(&bs_data_ib0[64], 24, keys_bs, bs_data_sb0);

#ifndef USEALLBITSLICE
            aycw_bit2byteslice(&bs_data_ib0[64], 1);
#endif

            /* ---- block decrypt ---- */
#ifdef USEBLOCKVIRTUALSHIFT
            std::memcpy(&r[8 * 56], bs_data_ib0, 8 * 8 * sizeof(dvbcsa_bs_word_t));
#else
            std::memcpy(r, bs_data_ib0, 8 * 8 * sizeof(dvbcsa_bs_word_t));
#endif

            aycw_block_key_schedule(keys_bs, keyskk);

#ifndef USEALLBITSLICE
            aycw_bit2byteslice(keyskk, 7);
#endif

            aycw_block_decrypt(keyskk, r);

            aycw_bs_xor24(r, r, &bs_data_ib0[64]);

            aycw_assert_decrypt_result((unsigned char *)&probedata[0][0], &keylist[0][0], r);

            /* ---- PES header check ---- */
            i = aycw_checkPESheader(r, &candidates);
            if (i) {
                for (i = 0; i < BS_BATCH_SIZE; i++) {
                    unsigned char cw[8];
                    dvbcsa_key_t   key;
                    unsigned char  data[16];
                    memset(cw, 255, sizeof(cw));

                    if (1 == BS_EXTLS32(BS_AND(BS_SHR(candidates, i), BS_VAL8(01)))) {
                        aycw_extractbsdata(keys_bs, i, 64, cw);
                        dvbcsa_key_set(cw, &key);

                        memcpy(&data, &probedata[0], 16);
                        dvbcsa_decrypt(&key, data, 16);
                        if (data[0] != 0x00 || data[1] != 0x00 || data[2] != 0x01) {
                            printf("\n[T%d] Fatal error: candidate verification failed!\n", tid);
                            printf("last key was: %02X %02X %02X [%02X]  %02X %02X %02X [%02X]\n",
                                   cw[0], cw[1], cw[2], cw[3],
                                   cw[4], cw[5], cw[6], cw[7]);
                            exit(ERR_FATAL);
                        }

                        memcpy(&data, &probedata[1], 16);
                        dvbcsa_decrypt(&key, data, 16);
                        if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) {

                            memcpy(&data, &probedata[2], 16);
                            dvbcsa_decrypt(&key, data, 16);
                            if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) {

                                /* Claim the find — only first thread wins */
                                int expected = 0;
                                if (keyFound.compare_exchange_strong(expected, tid + 1)) {
                                    printf("\n[T%d] key candidate successfully decrypted three packets\n", tid);
                                    printf("KEY FOUND!!!    %02X %02X %02X [%02X]  %02X %02X %02X [%02X]\n",
                                           cw[0], cw[1], cw[2], cw[3],
                                           cw[4], cw[5], cw[6], cw[7]);
                                    if (!st.benchmark) bfWriteKeyFoundFile(cw);
                                }
                                exit(OK);
                            }
                        }
                    }
                }
            }

            aycw_bs_increment_keys_inner(keys_bs);
        }

        /* count the 2^16 keys this outer-loop iteration just processed */
        if (shared) shared->keysDone.fetch_add(KEYSPERINNERLOOP,
                                               std::memory_order_relaxed);

        if (shared) {
            /* multi-threaded: thread 0 prints the aggregate rate */
            if (tid == 0) bfPerfAggregate(*shared, nThreads, st);
        } else if (tid <= 0) {
            /* single-threaded: keep the old per-thread display */
            bfPerfShow(st, tid);
        }

        if (!st.benchmark) bfWriteResumeFile(st, tid);

        st.currentkey32++;
    }

    /* Only print "stop reached" in single-threaded mode or from thread 0 */
    if (keyFound.load() == 0 && tid <= 0) {
        printf("\nStop key reached. No key found\n");
    }
}

/* --------------------------------------------------------------------------
   Parallel dispatcher: splits key range across threads
   -------------------------------------------------------------------------- */
static void bruteForceParallel(const Settings& settings,
                               unsigned char probedata[3][16]) {
    const int nThreads = settings.numThreads;
    const uint32_t range = settings.keystop - settings.keystart;
    const uint32_t chunk = range / nThreads;

    printf("Splitting key space across %d threads (chunk size: %u per thread)\n",
           nThreads, chunk);

    atomic<int> keyFound{0};
    BFShared shared;                      // cross-thread aggregate counter
    vector<thread> threads;
    threads.reserve(nThreads - 1);

    for (int t = 1; t < nThreads; t++) {
        uint32_t start = settings.keystart + t * chunk;
        uint32_t stop  = (t == nThreads - 1) ? settings.keystop : (start + chunk - 1);
        threads.emplace_back(bruteForceRange, start, stop,
                             probedata, settings.benchmark,
                             t, nThreads, ref(keyFound), &shared);
    }

    /* Thread 0 runs in the main thread */
    uint32_t start0 = settings.keystart;
    uint32_t stop0  = (nThreads == 1) ? settings.keystop : (start0 + chunk - 1);
    bruteForceRange(start0, stop0, probedata, settings.benchmark,
                    0, nThreads, keyFound, &shared);

    /* Wait for all worker threads */
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
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

    /* Search in chunks to avoid GPU timeout and allow progress reporting.
       Each work-item tests 65536 inner keys, so chunkSize outer keys
       means chunkSize work-items.  At ~13 Mcw/s, 4096 outer keys takes
       about 20 seconds per chunk — safe from GPU timeouts. */
    uint32_t chunkSize = 4096;  /* outer keys per GPU launch */
    char* env_chunk = std::getenv("AYCWABTU_GPU_CHUNK_SIZE");
    if (env_chunk && env_chunk[0]) {
        chunkSize = (uint32_t)std::atoi(env_chunk);
    }
    uint32_t keyStart = settings.keystart;
    uint32_t keyStop  = settings.keystop;

    cout << "GPU search: keys " << hex << keyStart << " .. " << keyStop << dec << endl;
    cout << "Chunk size: " << chunkSize << " outer keys per launch" << endl;

    uint64_t startTime = getTicksMs();
    uint32_t outerKeysDone = 0;
    uint32_t totalOuterKeys = keyStop - keyStart + 1;

    for (uint32_t chunkStart = keyStart; chunkStart <= keyStop; chunkStart += chunkSize) {
        uint32_t count = min(chunkSize, keyStop - chunkStart + 1);

        uint8_t cw_out[8] = {0};
        bool found = ocl_search(ocl, probe, chunkStart, count,
                                0, 65536, cw_out);

        outerKeysDone += count;

        /* Progress report */
        uint64_t now = getTicksMs();
        float elapsed = (now - startTime) / 1000.0f;
        float mcwPerSec = (outerKeysDone * 65536.0f) / elapsed / 1e6f;
        float pctDone = (outerKeysDone * 100.0f) / totalOuterKeys;

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
   Self-test mode
   -------------------------------------------------------------------------- */
/* Runs two independent correctness checks and exits:
     1. Bit-sliced test cases: decrypt bs_tc_crypteddata with the whole
        batch of known bs_tc_keys and compare against bs_tc_expected.
     2. End-to-end brute force: search the internal demo data over a single
        outer key and require the known key (00 11 22 33 44 00 00 44) to be
        found and verified by the libdvbcsa reference decryptor.
   Any failure returns a non-zero exit code (suitable for CI / `make check`).
   -------------------------------------------------------------------------- */
static void selfTest() {
    cout << "Algorithm self-test\n"
         << "  bit-slice batch size : " << BS_BATCH_SIZE << " keys\n"
         << "  parallel mode        : "
#if PARALLEL_MODE == PARALLEL_128_NEON
         << "NEON (128-bit)\n"
#elif PARALLEL_MODE == PARALLEL_128_SSE2
         << "SSE2 (128-bit)\n"
#elif PARALLEL_MODE == PARALLEL_256_AVX2
         << "AVX2 (256-bit)\n"
#else
         << "scalar 32-bit\n"
#endif
         << "----------------------------------------\n";

    /* ---------- Test 1: bit-sliced algorithm vs libdvbcsa reference ---------- */
    /* Drives the exact decrypt pipeline the brute force uses (stream ->
       block -> xor), for the whole batch of known test keys, and cross-checks
       the DB0 plaintext of every slice against the libdvbcsa reference. */
    dvbcsa_bs_word_t bs_data_sb0[8 * 16];
    dvbcsa_bs_word_t bs_data_ib0[8 * 16];
    dvbcsa_bs_word_t keys_bs[64];
    dvbcsa_bs_word_t keyskk[448];
#ifdef USEBLOCKVIRTUALSHIFT
    dvbcsa_bs_word_t r[8 * (1 + 8 + 56)];
#else
    dvbcsa_bs_word_t r[8 * (1 + 8 + 0)];
#endif

    aycw_init_block();
    aycw_init_stream(bs_tc_crypteddata, bs_data_sb0);

    for (int i = 0; i < 8 * 8; i++) bs_data_ib0[i] = bs_data_sb0[i];
#ifndef USEALLBITSLICE
    aycw_bit2byteslice(bs_data_ib0, 1);
#endif

    /* Build a full deterministic batch of size BS_BATCH_SIZE so the round
       trip and decrypt checks cover every slice, independent of the fixed
       128-entry bs_tc_keys table. */
    uint8 tc_keys[BS_BATCH_SIZE][8];
    for (int b = 0; b < BS_BATCH_SIZE; b++) {
        tc_keys[b][0] = 0x01;
        tc_keys[b][1] = 0x02;
        tc_keys[b][2] = 0x03;
        tc_keys[b][3] = 0x01 + 0x02 + 0x03;
        tc_keys[b][4] = (uint8)(b >> 8);
        tc_keys[b][5] = (uint8)(b & 0xFF);
        tc_keys[b][6] = 0;
        tc_keys[b][7] = tc_keys[b][4] + tc_keys[b][5] + tc_keys[b][6];
    }

    /* transpose the whole batch of keys into bit-sliced form */
    aycw_key_transpose((const uint8 *)&tc_keys[0][0], keys_bs);

    /* 1a. round-trip: transpose(p) then extract(p) must reproduce the keys */
    for (int b = 0; b < BS_BATCH_SIZE; b++) {
        uint8 cw[8];
        aycw_extractbsdata(keys_bs, (unsigned char)b, 64, cw);
        if (memcmp(cw, &tc_keys[b][0], 8) != 0) {
            cerr << "FAIL: key transpose round-trip mismatch for slice "
                 << b << "\n";
            exit(ERR_FATAL);
        }
    }
    cout << "[1a/2b] key transpose round-trip: PASSED\n";

    /* stream decrypt (24 bits -> DB0[0..2], as used by the PES check) */
    aycw_stream_decrypt(&bs_data_ib0[64], 24, keys_bs, bs_data_sb0);
#ifndef USEALLBITSLICE
    aycw_bit2byteslice(&bs_data_ib0[64], 1);
#endif

    /* block decrypt */
#ifdef USEBLOCKVIRTUALSHIFT
    std::memcpy(&r[8 * 56], bs_data_ib0, 8 * 8 * sizeof(dvbcsa_bs_word_t));
#else
    std::memcpy(r, bs_data_ib0, 8 * 8 * sizeof(dvbcsa_bs_word_t));
#endif
    aycw_block_key_schedule(keys_bs, keyskk);
#ifndef USEALLBITSLICE
    aycw_bit2byteslice(keyskk, 7);
#endif
    aycw_block_decrypt(keyskk, r);

    /* block XOR stream -> DB0 plaintext bytes (first 3 bytes are the PES
       start-code we cross-check below; the bytes beyond are block-only) */
    aycw_bs_xor24(r, r, &bs_data_ib0[64]);

    for (int b = 0; b < BS_BATCH_SIZE; b++) {
        dvbcsa_key_t key;
        uint8 cw[8], ref[16], rc[8];

        /* Matlab/slice b key, reference-decrypt with libdvbcsa */
        aycw_extractbsdata(keys_bs, (unsigned char)b, 64, cw);
        dvbcsa_key_set(cw, &key);
        memcpy(ref, bs_tc_crypteddata, 16);
        dvbcsa_decrypt(&key, ref, 16);

        /* Bitsliced DB0 plaintext (bytes 0..2 feed the PES check) */
        aycw_extractbsdata(r, (unsigned char)b, 4 * 8, rc);

        if (memcmp(ref, rc, 3) != 0) {
            cerr << "FAIL: bit-sliced decrypt for slice " << b << " (key ";
            for (int k = 0; k < 8; k++) cerr << (k ? ":" : "") << hex
                << (int)cw[k] << dec;
            cerr << ") differs from libdvbcsa\n";
            exit(ERR_FATAL);
        }
    }
    cout << "[1b/2b] bit-sliced decrypt vs libdvbcsa (";
    cout << BS_BATCH_SIZE << " keys): PASSED\n";

    /* ---------- Test 2: end-to-end known-key brute force ---------- */
    /* Internal demo packets (same as benchmark mode).  The known key
       00 11 22 33  44 00 00 44 decrypts all three to a 0x000001 PES header.
       Outer key (bytes 0,1,2,4) == 00 11 22 44; bytes 5,6 are the inner
       loop, so a single-outer-key range finds it on the first inner key. */
    unsigned char probedata[3][16] = {
        { 0xB2, 0x74, 0x85, 0x51, 0xF9, 0x3C, 0x9B, 0xD2,
          0x30, 0x9E, 0x8E, 0x78, 0xFB, 0x16, 0x55, 0xA9 },
        { 0x25, 0x2D, 0x3D, 0xAB, 0x5E, 0x3B, 0x31, 0x39,
          0xFE, 0xDF, 0xCD, 0x84, 0x51, 0x5A, 0x86, 0x4A },
        { 0xD0, 0xE1, 0x78, 0x48, 0xB3, 0x41, 0x63, 0x22,
          0x25, 0xA3, 0x63, 0x0A, 0x0E, 0xD3, 0x1C, 0x70 }
    };

    cout << "[2/2] end-to-end known-key brute force\n";
    cout << "      searching outer key 00 11 22 44 for key "
         << "00 11 22 33  44 00 00 44\n";

    atomic<int> keyFound{0};
    /* isBenchmark=true -> no resume/keyfound files are written.  bruteForceRange
       prints "KEY FOUND!!!" and exits(OK) as soon as the key is found and
       libdvbcsa-verified, so reaching the line after it means the search failed
       and we report FAIL below. */
    bruteForceRange(0x00112244, 0x00112244, probedata,
                    /*isBenchmark=*/true, /*tid=*/0, /*nThreads=*/1, keyFound);

    cerr << "\nFAIL: known key was not found during the end-to-end search\n";
    exit(ERR_FATAL);
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
             << " [-t filename] [-a start_cw] [-o stop_cw] [-p threads] -b -s -h\n";
        return EXIT_FAILURE;
    }

    /* Validate arguments */
    if (!settings.benchmark && !settings.selftest && settings.tsFilename.empty()) {
        cerr << "Neither ts filename provided nor benchmark enabled" << endl;
        printUsage();
        return ERR_USAGE;
    }

    printBanner(settings);

    /* Algorithm self test and quit */
    if (settings.selftest) {
        selfTest();
        return EXIT_SUCCESS;
    }

    /* Read resume file for non-benchmark runs (single-threaded only) */
    if (!settings.benchmark && settings.numThreads == 1) {
        bfReadResumeFile(&settings.keystart);
    }

    if (settings.benchmark) {
        benchmark(settings);
        return EXIT_SUCCESS;
    }

    /* Load TS data */
    unsigned char probedata[3][16];
    ayc_read_ts(settings.tsFilename.c_str(), &probedata[0][0]);

    if (settings.useGPU) {
        bruteForceGPU(settings, probedata);
    } else if (settings.numThreads > 1) {
        bruteForceParallel(settings, probedata);
    } else {
        atomic<int> keyFound{0};
        bruteForceRange(settings.keystart, settings.keystop,
                        probedata, false, 0, 1, keyFound);
    }

    return 0;
}
