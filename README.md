AYCWABTU
========

overview
--------
AYCWABTU is a proof of concept for a brute force control word calculation tool for the common scrambling algorithm used in digital video broadcasting.

AYCWABTU is not useful for live decryption of pay TV channels because the search for one key needs much more time than the key renewal interval. Majority of channels change keys multiple times a minute and AYCWABTU needs months to brute force one key. AYCWABTU is intended as proof of concept, and is not intended to be used for illegal purposes. The author does not accept responsibility for ANY damage incurred by the use of it.

It uses parallel bit slice technique. Other csa parallel bit slice implementations (like libdvbcsa) are meant for stream processing. They encrypt or decrypt many packets with one key. AYCWABTU uses parallel bit slice for decrypting one packet with many keys.

features
--------
* fast brute force key calculation due to bit sliced crack algorithm (AVX2, SSE2, NEON, and 32-bit scalar CPU versions available)
* **run-time SIMD auto-detection** — every backend the architecture can compile is linked into one binary and the best one is picked at start-up (`-S auto`, default), or selected manually with `-S sse2|avx2|neon|scalar`. `-S list` shows what is available
* **OpenCL / GPU support** — `-g` flag offloads the brute force to the GPU via an OpenCL kernel (`src/aycwabtu.cl`)
* **multi-threaded** — `-p <n>` splits the key space across n parallel threads (near-linear scaling)
* **self-test** — `-s` verifies the algorithm against known vectors and a known-key
  brute force, then exits (returns 0 on success, 16 on failure — CI-friendly)
* open source. License: GPL
* read three encrypted data packets from ts file with many checks for valid data
* writes a small probe ts file with these packets for sharing and distributed attack
* test frame included to make sure, it really finds the keys. Also suitable for other brute force tools
* **C++17 codebase** — converted from C, with modern C++ argument parsing, RAII, and thread support
* **cross-platform** — builds on macOS (ARM64 / Apple Silicon), Linux (x86_64 / ARM), and Windows
* much potential for speed improvements

performance (Apple M2 Pro, 10 cores)
------------------------------------
Single-thread (NEON SIMD, 128-bit batch): **~9.5 Mcw/s**

| Threads | Mcw/s | Scaling |
|---------|-------|---------|
| 1       | 9.5   | 1.0x    |
| 2       | 19    | 2.0x    |
| 4       | 38    | 4.0x    |
| 8       | 68    | 7.2x    |

OpenCL **GPU** (Apple M2 Pro): **~86 Mcw/s** (best single result)

performance (x86_64)
--------------------
Single-thread (AVX2 SIMD, 256-bit batch): **~23.6 Mcw/s**
Single-thread (SSE2 SIMD, 128-bit batch): **~12.8 Mcw/s** (AVX2 ≈ 1.8× faster)

performance (Linux x86_64, AMD Radeon RX 6600 / ROCm OpenCL)
------------------------------------------------------------
AMD Ryzen 7 5700X (8 cores / 16 threads), Radeon RX 6600 (gfx1032).

| Threads | Mcw/s | Scaling |
|---------|-------|---------|
| 1       | 12.7  | 1.0x    |
| 4       | 65.5  | 5.2x    |
| 8       | 131.1 | 10.4x   |

OpenCL **GPU** (Radeon RX 6600 / ROCm): `Mcw/s` vs. launch chunk size.
The default is now 262144 outer keys per launch.

| Chunk (outer keys) | Mcw/s |
|--------------------|-------|
| 4096   (old default) | 187.9 |
| 65536               | 499.2 |
| 262144 (default)    | **528.1** |

Larger launches amortize per-enqueue overhead: on discrete AMD GPUs raising
`AYCWABTU_GPU_CHUNK_SIZE` (or the build default) roughly triples GPU speed.

Background math: a full CW brute force covers 2^32 outer x 2^16 inner =
**2^48** candidates (bytes 3 and 7 are derived checksums). At 528.1 Mcw/s
that is ~6.2 days worst case / ~3.1 days average per control word.

ROCm build:
```
make WITH_OPENCL=1
./aycwabtu -g -t test/Testfile_CW_7FFAE9A02486.ts -a 7FFAE9A00000
```

to do list
----------
* pin the search thread(s) to a specific core (CPU affinity)
* auto-set the number of threads from the number of available cores (fall back to a sensible default)
* optimize the block sbox boolean equations. Only slightly faster with 128 bits. See da_diett.pdf Chpt. 3.1
* Ctrl-C handling on linux/windows
* block decrypt first (does not depend on stream). Then stream afterwards, stop XORing immediately if foreseeable there is no PES header

recent updates
--------------
* **Run-time SIMD auto-detection** — the SIMD backend is no longer a build-time choice. One binary contains every backend the host architecture can compile (x86_64: scalar+sse2+avx2, ARM64: scalar+neon); at start-up the CPU is detected and the best supported backend is selected (`-S auto`, the default). Manual selection with `-S <backend>`, `-S list` prints the availability table. `-s` runs the algorithm self-test for every available backend; `make test` runs the full SIMD unit-test suite (`test/test_simd.sh`: auto + per-backend self-tests, per-backend end-to-end key finds, negative tests for unknown/unavailable backends)
* **AVX2 SIMD support** — 256-bit parallel mode on x86_64 (`PARALLEL_256_AVX2`, batch of 256 keys/register). ~23.6 Mcw/s single-thread vs ~12.8 Mcw/s for SSE2 (~1.8×), selectable at run time with `-S avx2`

recent updates (2026-07)
------------------------
* **OpenCL / GPU support** — bit sliced brute force offloaded to the GPU via OpenCL (kernel in `src/aycwabtu.cl`, `-g` flag). ~86 Mcw/s on an Apple M2 Pro; **~528 Mcw/s on an AMD Radeon RX 6600 (ROCm)**. Default GPU launch chunk raised to 262144 outer keys (was 4096) — larger launches cut per-enqueue overhead ~3x on discrete AMD GPUs (older Apple hardware stays at its 64 group cap). ROCm/AMD platform is auto-selected when multiple OpenCL platforms are present. Override chunk size at runtime with `AYCWABTU_GPU_CHUNK_SIZE`.
* **NEON SIMD support** — ARM64 128-bit SIMD via NEON intrinsics, 2.7x faster single-thread throughput on Apple Silicon
* **Multi-threading** — `-p <n>` flag for parallel brute force across n threads with near-linear scaling
* **C++17 conversion** — rewritten main in C++17 with Settings struct, exception-based error handling, `std::string_view` argument parsing, `std::thread` parallelism, `std::atomic` coordination
* **Compiler optimizations** — `-flto` for +9% single-thread throughput (per-backend SIMD flags only; no global `-march=native` so the binary runs on any CPU of that family)
* **Algorithmic improvements** — stream decrypt reduced from 25 to 24 bits (only 3 bytes needed for PES check), `std::memcpy` for block init copy
* **C++17 compatibility fixes** — removed `register` keywords, added explicit casts, fixed const-correctness throughout
* **Build system** — platform detection decoupled from SIMD choice: all compilable backends are built (per-backend `obj/<backend>/` objects with `-DPARALLEL_MODE=<n>` + backend flags and symbol renaming via `src/bs_rename.h`), linked into one binary, selected at run time by `src/bs_dispatch.cpp`; C++17 standard, LTO

developers
----------
* after changing the code, run `make test` (SIMD unit tests + self-tests) and `make check` to make sure the algorithm still works. It's too easy to break things.
* run "make check"
* test all the batch size implementations
* share your benchmark values in the pull request
* publish all your work please, AYCWABTU is released under GPL

credits
-------
* **Michail Alvanos** — C++17 conversion, multi-threading, performance optimizations, ARM64/Apple Silicon support (2026)
* FFdecsa, Copyright 2003-2004, fatih89r
* libdvbcsa, http://www.videolan.org/developers/libdvbcsa.html
* ANALYSIS OF THE DVB COMMON SCRAMBLING ALGORITHM, Ralf-Philipp Weinmann and Kai Wirt, Technical University of Darmstadt Department of Computer Science Darmstadt, Germany
* On the Security of Digital Video Broadcast Encryption, Markus Diett
* http://en.wikipedia.org/wiki/Common_Scrambling_Algorithm
* Breaking DVB-CSA, Erik Tews, Julian Waelde, Michael Weiner, Technische Universitaet Darmstadt
* TSDEC - the DVB transport stream offline decrypter, http://sourceforge.net/projects/tsdec/
* http://csa.irde.to   This page disappeared unfortunately but it still accessible here https://web.archive.org/web/20040903151642/http://csa.irde.to/
* and last not least my good friend johnf

***
"Sorry, it is hard to understand and modify but it was harder to design and implement!!!"        fatih89r

Have fun.

ganymede
