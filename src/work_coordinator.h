#ifndef AYCW_WORK_COORDINATOR_H
#define AYCW_WORK_COORDINATOR_H

#include <cstdint>
#include <atomic>
#include <cstring>
#include <mutex>

/* Internal demo packets for benchmark and self-test modes.
   The known key 00 11 22 33  44 00 00 44 decrypts all three packets
   to a 0x000001 PES header. */
inline constexpr unsigned char bfDemoData[3][16] = {
    { 0xB2, 0x74, 0x85, 0x51, 0xF9, 0x3C, 0x9B, 0xD2,
      0x30, 0x9E, 0x8E, 0x78, 0xFB, 0x16, 0x55, 0xA9 },
    { 0x25, 0x2D, 0x3D, 0xAB, 0x5E, 0x3B, 0x31, 0x39,
      0xFE, 0xDF, 0xCD, 0x84, 0x51, 0x5A, 0x86, 0x4A },
    { 0xD0, 0xE1, 0x78, 0x48, 0xB3, 0x41, 0x63, 0x22,
      0x25, 0xA3, 0x63, 0x0A, 0x0E, 0xD3, 0x1C, 0x70 }
};

/* Coordinates work sharing between concurrent CPU thread(s) and GPU worker.
   Outer keys (each with 2^16 inner keys) are fetched dynamically in chunks
   using atomic operations so both devices stay 100% utilized regardless of
   their relative execution speeds. */
struct WorkCoordinator {
    uint64_t              keyStart = 0;
    uint64_t              keyStop  = 0;
    std::atomic<uint64_t> nextKey{0};
    std::atomic<int>      keyFound{0};       /* 0: searching; >0: winner id */

    /* Per-device key counters (updated by worker threads) */
    std::atomic<uint64_t> cpuKeysDone{0};
    std::atomic<uint64_t> gpuKeysDone{0};

    /* foundCw is written by the winning worker under foundMutex and read
       by the main thread via getFoundCw() — the mutex makes the write
       atomic with respect to any reader, even without join() ordering. */
    std::mutex            foundMutex;
    uint8_t               foundCw[8]{0};

    inline bool getNextChunk(uint32_t chunkSize, uint32_t& chunkStart, uint32_t& count) {
        while (keyFound.load(std::memory_order_relaxed) == 0) {
            uint64_t cur = nextKey.load(std::memory_order_relaxed);
            if (cur > keyStop) return false;
            uint64_t remaining = keyStop - cur + 1;
            uint32_t n = (remaining < (uint64_t)chunkSize) ? (uint32_t)remaining : chunkSize;
            if (nextKey.compare_exchange_weak(cur, cur + n, std::memory_order_relaxed)) {
                chunkStart = (uint32_t)cur;
                count = n;
                return true;
            }
        }
        return false;
    }

    inline bool claimFound(int id, const uint8_t cw[8]) {
        std::lock_guard<std::mutex> lock(foundMutex);
        int expected = 0;
        if (keyFound.compare_exchange_strong(expected, id)) {
            std::memcpy(foundCw, cw, 8);
            return true;
        }
        return false;
    }

    inline void getFoundCw(uint8_t out[8]) {
        std::lock_guard<std::mutex> lock(foundMutex);
        std::memcpy(out, foundCw, 8);
    }
};

#endif /* AYCW_WORK_COORDINATOR_H */
