#include "hybrid.hpp"
#include "config.h"
#include "ocl.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace std;

extern void bfWriteKeyFoundFile(const unsigned char *cw);

static uint64_t getTicksMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

string findKernelPath() {
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

string loadKernelSource(const char *path) {
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

#ifdef HAVE_OPENCL

static void gpuChunkWorker(WorkCoordinator* coordinator,
                           OclContext* ocl,
                           const uint8_t probe[48],
                           uint32_t gpuChunkSize,
                           bool isBenchmark) {
    uint32_t chunkStart = 0;
    uint32_t count = 0;

    while (coordinator->getNextChunk(gpuChunkSize, chunkStart, count)) {
        uint8_t cw_out[8] = {0};
        uint32_t keysProcessed = 0;
        bool found = ocl_search(*ocl, probe, chunkStart, count,
                                0, 65536, cw_out,
                                &coordinator->keyFound,
                                &keysProcessed);

        coordinator->gpuKeysDone.fetch_add((uint64_t)keysProcessed * 65536,
                                           std::memory_order_relaxed);

        if (ocl->error) {
            std::cerr << "\n[GPU] OpenCL error - GPU worker stopping. "
                      << "CPU threads continue alone.\n";
            return;
        }

        if (found) {
            /* ocl_search already verified cw_out against all 3 packets */
            if (coordinator->claimFound(999 /* GPU ID */, cw_out)) {
                printf("\n[GPU] key candidate successfully decrypted three packets\n");
                printf("KEY FOUND!!!    %02X %02X %02X [%02X]  %02X %02X %02X [%02X]\n",
                       cw_out[0], cw_out[1], cw_out[2], cw_out[3],
                       cw_out[4], cw_out[5], cw_out[6], cw_out[7]);
                printf("CPU verify: all 3 packets decrypted to 0x000001 - accepting key\n");
                if (!isBenchmark) {
                    bfWriteKeyFoundFile(cw_out);
                }
            }
            return;
        }

        if (coordinator->keyFound.load(std::memory_order_relaxed) != 0) {
            break;
        }
    }
}

#endif /* HAVE_OPENCL */

bool runHybridSearch(const Settings& settings,
                     const BSDriver* cpuDriver,
                     const unsigned char probedata[3][16],
                     uint32_t keyStart,
                     uint32_t keyStop,
                     bool isBenchmark,
                     bool isSelfTest,
                     uint8_t* foundCwOut) {
#ifndef HAVE_OPENCL
    (void)settings; (void)cpuDriver; (void)probedata;
    (void)keyStart; (void)keyStop; (void)isBenchmark;
    (void)isSelfTest; (void)foundCwOut;
    cerr << "Error: GPU (OpenCL) support was not compiled into this build.\n"
         << "       Rebuild on a machine with an OpenCL toolchain or run in CPU mode.\n";
    if (isSelfTest) return false;
    exit(ERR_FATAL);
#else
    string kernelPath = findKernelPath();
    if (kernelPath.empty()) {
        cerr << "Error: cannot find OpenCL kernel source (aycwabtu.cl)" << endl;
        cerr << "Searched: src/aycwabtu.cl, ../src/aycwabtu.cl, aycwabtu.cl" << endl;
        if (isSelfTest) return false;
        exit(ERR_FATAL);
    }
    if (!isBenchmark && !isSelfTest) {
        cout << "Loading kernel from: " << kernelPath << endl;
    }

    string kernelSrc = loadKernelSource(kernelPath.c_str());
    if (kernelSrc.empty()) {
        cerr << "Error: failed to read kernel source" << endl;
        if (isSelfTest) return false;
        exit(ERR_FATAL);
    }

    OclContext ocl;
    if (!ocl_init(ocl, kernelSrc.c_str())) {
        cerr << "Error: OpenCL initialization failed" << endl;
        if (isSelfTest) return false;
        exit(ERR_FATAL);
    }

    uint8_t probe[48];
    memcpy(probe, probedata, 48);

    uint64_t totalOuterKeys = (uint64_t)keyStop - keyStart + 1;
    uint32_t gpuChunkSize = 262144;
    char* env_chunk = std::getenv("AYCWABTU_GPU_CHUNK_SIZE");
    if (env_chunk && env_chunk[0]) {
        long cs = std::atol(env_chunk);
        if (cs >= 1) gpuChunkSize = (uint32_t)cs;
    } else {
        if (totalOuterKeys <= 128) {
            gpuChunkSize = 16;
        } else if (totalOuterKeys <= 65536) {
            gpuChunkSize = 512;
        }
    }

    if (!isSelfTest) {
        cout << "Hybrid search: keys " << hex << keyStart << " .. " << keyStop << dec
             << " (" << settings.numThreads << " CPU thread"
             << (settings.numThreads > 1 ? "s" : "") << " + GPU)" << endl;
        cout << "GPU chunk size: " << gpuChunkSize << " outer keys per launch" << endl;
    }

    WorkCoordinator coordinator;
    coordinator.keyStart = keyStart;
    coordinator.keyStop  = keyStop;
    coordinator.nextKey.store(keyStart);
    coordinator.keyFound.store(0);

    int numCpuThreads = settings.numThreads > 0 ? settings.numThreads : 1;
    vector<thread> threads;
    threads.reserve(numCpuThreads + 1);

    /* Spawn GPU worker */
    threads.emplace_back(gpuChunkWorker, &coordinator, &ocl,
                         probe, gpuChunkSize, isBenchmark);

    /* Spawn CPU workers */
    for (int t = 0; t < numCpuThreads; t++) {
        threads.emplace_back(cpuDriver->bruteForceChunkWorker,
                             &coordinator, t, numCpuThreads,
                             probedata, isBenchmark);
    }

    uint64_t startTime = getTicksMs();
    uint64_t totalCandidateKeys = totalOuterKeys * 65536ULL;
    const char prop[] = "|/-\\";
    int tick = 0;
    bool showedProgress = false;

    /* Monitor progress from main thread while workers are running */
    while (coordinator.keyFound.load(std::memory_order_relaxed) == 0 &&
           coordinator.nextKey.load(std::memory_order_relaxed) <= keyStop) {
        this_thread::sleep_for(chrono::milliseconds(200));

        uint64_t now = getTicksMs();
        double elapsed = (now - startTime) / 1000.0;
        if (elapsed <= 0.0) continue;

        uint64_t cpuDone = coordinator.cpuKeysDone.load(std::memory_order_relaxed);
        uint64_t gpuDone = coordinator.gpuKeysDone.load(std::memory_order_relaxed);
        uint64_t totalDone = cpuDone + gpuDone;

        double cpuMcw = (cpuDone / elapsed) / 1e6;
        double gpuMcw = (gpuDone / elapsed) / 1e6;
        double totalMcw = (totalDone / elapsed) / 1e6;
        double pctDone = totalCandidateKeys > 0 ? (totalDone * 100.0) / totalCandidateKeys : 0.0;
        if (pctDone > 100.0) pctDone = 100.0;

        uint64_t curKey = coordinator.nextKey.load(std::memory_order_relaxed);
        if (curKey > keyStop) curKey = keyStop;

        if (!isSelfTest) {
            printf("\r%c [CPU+GPU] %.1f%% done | CPU: %.1f Mcw/s (%d th), GPU: %.1f Mcw/s | Total: %.1f Mcw/s | key %08X .. ",
                   prop[(tick++) & 3], pctDone, cpuMcw, numCpuThreads, gpuMcw, totalMcw, (uint32_t)curKey);
            fflush(stdout);
            showedProgress = true;
        }
    }

    /* Wait for all workers to finish */
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    if (showedProgress) {
        printf("\n");
        fflush(stdout);
    }

    ocl_cleanup(ocl);

    bool found = (coordinator.keyFound.load() != 0);
    if (found) {
        if (foundCwOut) {
            coordinator.getFoundCw(foundCwOut);
        }
        uint64_t endTime = getTicksMs();
        double totalElapsed = (endTime - startTime) / 1000.0;
        if (totalElapsed > 0.0 && isBenchmark) {
            uint64_t cpuDone = coordinator.cpuKeysDone.load();
            uint64_t gpuDone = coordinator.gpuKeysDone.load();
            uint64_t totalDone = cpuDone + gpuDone;
            printf("Benchmark complete in %.2f s: %.1f Mcw/s (CPU: %.1f Mcw/s, GPU: %.1f Mcw/s)\n",
                   totalElapsed,
                   (totalDone / totalElapsed) / 1e6,
                   (cpuDone / totalElapsed) / 1e6,
                   (gpuDone / totalElapsed) / 1e6);
        }
        return true;
    }

    if (!isSelfTest) {
        printf("\nStop key reached. No key found\n");
    }
    return false;
#endif
}

bool runGpuSearch(const Settings& settings,
                  const unsigned char probedata[3][16],
                  uint32_t keyStart,
                  uint32_t keyStop,
                  bool isBenchmark,
                  bool isSelfTest,
                  uint8_t* foundCwOut) {
#ifndef HAVE_OPENCL
    (void)settings; (void)probedata; (void)keyStart;
    (void)keyStop; (void)isBenchmark; (void)isSelfTest; (void)foundCwOut;
    cerr << "Error: GPU (OpenCL) support was not compiled into this build.\n"
         << "       Rebuild on a machine with an OpenCL toolchain or run in CPU mode.\n";
    if (isSelfTest) return false;
    exit(ERR_FATAL);
#else
    string kernelPath = findKernelPath();
    if (kernelPath.empty()) {
        cerr << "Error: cannot find OpenCL kernel source (aycwabtu.cl)" << endl;
        cerr << "Searched: src/aycwabtu.cl, ../src/aycwabtu.cl, aycwabtu.cl" << endl;
        if (isSelfTest) return false;
        exit(ERR_FATAL);
    }
    if (!isBenchmark && !isSelfTest) {
        cout << "Loading kernel from: " << kernelPath << endl;
    }

    string kernelSrc = loadKernelSource(kernelPath.c_str());
    if (kernelSrc.empty()) {
        cerr << "Error: failed to read kernel source" << endl;
        if (isSelfTest) return false;
        exit(ERR_FATAL);
    }

    OclContext ocl;
    if (!ocl_init(ocl, kernelSrc.c_str())) {
        cerr << "Error: OpenCL initialization failed" << endl;
        if (isSelfTest) return false;
        exit(ERR_FATAL);
    }

    uint8_t probe[48];
    memcpy(probe, probedata, 48);

    uint32_t chunkSize = 262144;
    char* env_chunk = std::getenv("AYCWABTU_GPU_CHUNK_SIZE");
    if (env_chunk && env_chunk[0]) {
        long cs = std::atol(env_chunk);
        if (cs >= 1) chunkSize = (uint32_t)cs;
    }

    if (!isSelfTest) {
        cout << "GPU search: keys " << hex << keyStart << " .. " << keyStop << dec << endl;
        cout << "Chunk size: " << chunkSize << " outer keys per launch" << endl;
    }

    uint64_t startTime = getTicksMs();
    uint64_t outerKeysDone = 0;
    uint64_t totalOuterKeys = (uint64_t)keyStop - keyStart + 1;

    for (uint64_t cur = keyStart; cur <= (uint64_t)keyStop; ) {
        uint32_t chunkStart = (uint32_t)cur;
        uint64_t remaining  = (uint64_t)keyStop - cur + 1;
        uint32_t count = (remaining < (uint64_t)chunkSize) ? (uint32_t)remaining : chunkSize;

        uint8_t cw_out[8] = {0};
        uint32_t keysProcessed = 0;
        bool found = ocl_search(ocl, probe, chunkStart, count,
                                0, 65536, cw_out,
                                nullptr,
                                &keysProcessed);

        outerKeysDone += keysProcessed;
        cur += keysProcessed;

        if (ocl.error) {
            std::cerr << "\nOpenCL error during GPU search - aborting.\n";
            ocl_cleanup(ocl);
            return false;
        }

        uint64_t now = getTicksMs();
        float elapsed = (now - startTime) / 1000.0f;
        float mcwPerSec = elapsed > 0.0f
            ? (outerKeysDone * 65536.0f) / elapsed / 1e6f : 0.0f;
        float pctDone = totalOuterKeys
            ? (outerKeysDone * 100.0f) / totalOuterKeys : 0.0f;

        if (!isSelfTest) {
            printf("\rGPU: %.1f%% done, %.1f Mcw/s, key %08X .. ",
                   pctDone, mcwPerSec, chunkStart + count);
            fflush(stdout);
        }

        if (found) {
            printf("\n\nGPU reports KEY FOUND: %02X %02X %02X [%02X]  %02X %02X %02X [%02X]\n",
                   cw_out[0], cw_out[1], cw_out[2], cw_out[3],
                   cw_out[4], cw_out[5], cw_out[6], cw_out[7]);

            if (verifyCw(probe, cw_out)) {
                printf("CPU verify: all 3 packets decrypted to 0x000001 - accepting key\n");
                if (!isBenchmark) {
                    bfWriteKeyFoundFile(cw_out);
                }
                if (foundCwOut) {
                    memcpy(foundCwOut, cw_out, 8);
                }
                ocl_cleanup(ocl);
                return true;
            }
            printf("CPU verify failed on GPU-reported key - not accepted (no retry)\n");
        }
    }

    if (!isSelfTest) {
        printf("\nGPU search complete. No key found.\n");
    }
    ocl_cleanup(ocl);
    return false;
#endif
}
