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
        , keystart(0)
        , keystop(0xFFFFFFFF)
        , numThreads(1)
    {}

    bool     benchmark;
    bool     selftest;
    string   tsFilename;
    uint32_t keystart;
    uint32_t keystop;
    int      numThreads;
};

/* --------------------------------------------------------------------------
   Forward declarations
   -------------------------------------------------------------------------- */
static void bruteForceRange(uint32_t keyStart, uint32_t keyStop,
                            const unsigned char probedata[3][16],
                            bool isBenchmark, int tid, int nThreads,
                            atomic<int>& keyFound);
static void bruteForceParallel(const Settings& settings,
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
    cout << "\nparallel bitslice batch size is " << BS_BATCH_SIZE << "\n";
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
                            bool isBenchmark, int tid, int /*nThreads*/,
                            atomic<int>& keyFound) {
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

        if (tid <= 0)  // only thread 0 prints progress
            bfPerfShow(st, tid);

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
    vector<thread> threads;
    threads.reserve(nThreads - 1);

    for (int t = 1; t < nThreads; t++) {
        uint32_t start = settings.keystart + t * chunk;
        uint32_t stop  = (t == nThreads - 1) ? settings.keystop : (start + chunk - 1);
        threads.emplace_back(bruteForceRange, start, stop,
                             probedata, settings.benchmark,
                             t, nThreads, ref(keyFound));
    }

    /* Thread 0 runs in the main thread */
    uint32_t start0 = settings.keystart;
    uint32_t stop0  = (nThreads == 1) ? settings.keystop : (start0 + chunk - 1);
    bruteForceRange(start0, stop0, probedata, settings.benchmark,
                    0, nThreads, keyFound);

    /* Wait for all worker threads */
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
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
    if (settings.selftest) {
        cout << "Option self-test not available yet, sorry" << endl;
        return EXIT_SUCCESS;
    }

    if (!settings.benchmark && settings.tsFilename.empty()) {
        cerr << "Neither ts filename provided nor benchmark enabled" << endl;
        printUsage();
        return ERR_USAGE;
    }

    printBanner(settings);

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

    if (settings.numThreads > 1)
        bruteForceParallel(settings, probedata);
    else {
        atomic<int> keyFound{0};
        bruteForceRange(settings.keystart, settings.keystop,
                        probedata, false, 0, 1, keyFound);
    }

    return 0;
}
