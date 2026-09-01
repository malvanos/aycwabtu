/*
   Per-backend brute-force driver.

   This translation unit is compiled ONCE PER SIMD BACKEND (see makefile:
   obj/<backend>/bs_driver.o).  bs_rename.h renames every aycw_* / ayc_*
   symbol with the backend suffix, so several backend objects can be linked
   into the same binary.  bs_dispatch.cpp picks one backend at run time and
   calls the matching entry point through a function-pointer table.

   All helpers below are `static` (internal linkage): they are private to
   one backend instantiation and do not collide.

   The code is the historical main.cpp driver, moved here unchanged apart
   from linkage so every backend runs the exact same search logic.
*/

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <functional>
#include <atomic>

#include "config.h"        /* PARALLEL_MODE, dvbcsa_bs_word_t, exit codes */
#include "bs_rename.h"     /* per-backend symbol renaming                  */
#include "bs_algo.h"
#include "bs_stream.h"
#include "bs_block_ab.h"
#include "bs_testcases.h"
#include "dvbcsa.h"
#include "bs_driver_api.h"

using namespace std;

#define INNERKEYBITS    16
#define KEYSPERINNERLOOP (1 << INNERKEYBITS)
#define RESUMEFILENAME  "resume"

/* defined in main.cpp (shared with the GPU search path) */
extern void bfWriteKeyFoundFile(const unsigned char *cw);

/* Self-test / normal-search mode switch.  The brute-force core used to
   exit(OK) the whole process as soon as a key was verified.  That is what
   a real search wants (stop ASAP), but the self-test driver mode needs the
   core to RETURN after a find so the caller can report PASS and continue
   with the next SIMD backend.  bf_selftest_mode is per-backend (static TU). */
static std::atomic<bool> bf_selftest_mode{false};

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

/* --------------------------------------------------------------------------
   Core brute-force search — single key range, runs on one thread
   tid = -1 means single-threaded mode (no thread ID in output)
   -------------------------------------------------------------------------- */
static void bruteForceRangeImpl(uint32_t keyStart, uint32_t keyStop,
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
                                if (!bf_selftest_mode.load())
                                    exit(OK);   /* real search: stop ASAP */
                                return;         /* self-test: report & continue */
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
static void bruteForceParallelImpl(uint32_t keystart, uint32_t keystop,
                                   int nThreads, bool isBenchmark,
                                   unsigned char probedata[3][16]) {
    const uint32_t range = keystop - keystart;
    const uint32_t chunk = range / nThreads;

    printf("Splitting key space across %d threads (chunk size: %u per thread)\n",
           nThreads, chunk);

    atomic<int> keyFound{0};
    BFShared shared;                      // cross-thread aggregate counter
    vector<thread> threads;
    threads.reserve(nThreads - 1);

    for (int t = 1; t < nThreads; t++) {
        uint32_t start = keystart + t * chunk;
        uint32_t stop  = (t == nThreads - 1) ? keystop : (start + chunk - 1);
        threads.emplace_back(bruteForceRangeImpl, start, stop,
                             probedata, isBenchmark,
                             t, nThreads, ref(keyFound), &shared);
    }

    /* Thread 0 runs in the main thread */
    uint32_t start0 = keystart;
    uint32_t stop0  = (nThreads == 1) ? keystop : (start0 + chunk - 1);
    bruteForceRangeImpl(start0, stop0, probedata, isBenchmark,
                        0, nThreads, keyFound, &shared);

    /* Wait for all worker threads */
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
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
static const unsigned char bfDemoData[3][16] = {
    { 0xB2, 0x74, 0x85, 0x51, 0xF9, 0x3C, 0x9B, 0xD2,
      0x30, 0x9E, 0x8E, 0x78, 0xFB, 0x16, 0x55, 0xA9 },
    { 0x25, 0x2D, 0x3D, 0xAB, 0x5E, 0x3B, 0x31, 0x39,
      0xFE, 0xDF, 0xCD, 0x84, 0x51, 0x5A, 0x86, 0x4A },
    { 0xD0, 0xE1, 0x78, 0x48, 0xB3, 0x41, 0x63, 0x22,
      0x25, 0xA3, 0x63, 0x0A, 0x0E, 0xD3, 0x1C, 0x70 }
};

static void benchmarkImpl(int numThreads) {
    cout << "Starting micro-benchmarking" << endl;
    partsBenchmark();

    cout << "Starting benchmarking" << endl;

    unsigned char probedata[3][16];
    memcpy(probedata, bfDemoData, sizeof(bfDemoData));

    uint32_t keystart = 0x00 << 24 | 0x11 << 16 | 0x15 << 8 | 0x00;
    uint32_t keystop  = 0xFFFFFFFF;

    if (numThreads > 1) {
        bruteForceParallelImpl(keystart, keystop, numThreads, true, probedata);
    } else {
        atomic<int> keyFound{0};
        bruteForceRangeImpl(keystart, keystop, probedata,
                            true, -1, 1, keyFound, nullptr);
    }
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
static void selfTestImpl() {
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
    cout << "[1a] key transpose round-trip: PASSED\n";

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
    cout << "[1b] bit-sliced decrypt vs libdvbcsa (";
    cout << BS_BATCH_SIZE << " keys): PASSED\n";

    /* ---------- Test 2: end-to-end known-key brute force ---------- */
    /* Internal demo packets (same as benchmark mode).  The known key
       00 11 22 33  44 00 00 44 decrypts all three to a 0x000001 PES header.
       Outer key (bytes 0,1,2,4) == 00 11 22 44; bytes 5,6 are the inner
       loop, so a single-outer-key range finds it on the first inner key. */
    unsigned char probedata[3][16];
    memcpy(probedata, bfDemoData, sizeof(bfDemoData));

    cout << "[2/2] end-to-end known-key brute force\n";
    cout << "      searching outer key 00 11 22 44 for key "
         << "00 11 22 33  44 00 00 44\n";

    atomic<int> keyFound{0};
    /* Self-test mode: the brute-force core returns here instead of exit(OK)
       so we can report PASS and go on to the next backend.  isBenchmark=true
       -> no resume/keyfound files are written.  bruteForceRangeImpl sets
       keyFound != 0 as soon as the key is found and libdvbcsa-verified. */
    bf_selftest_mode.store(true);
    bruteForceRangeImpl(0x00112244, 0x00112244, probedata,
                        /*isBenchmark=*/true, /*tid=*/0, /*nThreads=*/1,
                        keyFound, /*shared=*/nullptr);
    bf_selftest_mode.store(false);
    if (keyFound.load() == 0) {
        cerr << "\nFAIL: known key was not found during the end-to-end search\n";
        exit(ERR_FATAL);
    }
    cout << "[2/2] end-to-end known-key brute force: PASSED\n";
}

/* --------------------------------------------------------------------------
   Exported entry points (renamed per backend by bs_rename.h)
   -------------------------------------------------------------------------- */
extern "C" void ayc_bruteForceRange(uint32_t keyStart, uint32_t keyStop,
                                    const unsigned char probedata[3][16],
                                    bool isBenchmark) {
    atomic<int> keyFound{0};
    /* tid = -1 marks the real single-threaded search: it writes the resume
       file as plain "resume" (matching the reader in main.cpp) and uses the
       clean (no [T0]) progress format.  On a find the core exit()s, so the
       keyFound value is irrelevant here. */
    bruteForceRangeImpl(keyStart, keyStop, probedata,
                        isBenchmark, /*tid=*/-1, /*nThreads=*/1,
                        keyFound, /*shared=*/nullptr);
}

extern "C" void ayc_bruteForceParallel(uint32_t keyStart, uint32_t keyStop,
                                       int numThreads, bool isBenchmark,
                                       const unsigned char probedata[3][16]) {
    unsigned char data[3][16];
    memcpy(data, probedata, sizeof(data));
    bruteForceParallelImpl(keyStart, keyStop, numThreads, isBenchmark, data);
}

extern "C" void ayc_benchmark(int numThreads) {
    benchmarkImpl(numThreads);
}

extern "C" void ayc_selfTest(void) {
    selfTestImpl();
}