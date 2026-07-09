AYCWABTU
========

overview
--------
AYCWABTU is a proof of concept for a brute force control word calculation tool for the common scrambling algorithm used in digital video broadcasting.

AYCWABTU is not useful for live decryption of pay TV channels because the search for one key needs much more time than the key renewal interval. Majority of channels change keys multiple times a minute and AYCWABTU needs months to brute force one key. AYCWABTU is intended as proof of concept, and is not intended to be used for illegal purposes. The author does not accept responsibility for ANY damage incurred by the use of it.

It uses parallel bit slice technique. Other csa parallel bit slice implementations (like libdvbcsa) are meant for stream processing. They encrypt or decrypt many packets with one key. AYCWABTU uses parallel bit slice for decrypting one packet with many keys.

features
--------
* fast brute force key calculation due to bit sliced crack algorithm (SSE2, NEON, and 32-bit scalar versions available)
* **multi-threaded** — `-p <n>` splits the key space across n parallel threads (near-linear scaling)
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

to do list
----------
* NEON SIMD support for ARM (128-bit batch, ~4x single-thread gain)
* support for 256 bits parallel with advanced vector extensions AVX2
* OpenCL / GPU support
* optimize the block sbox boolean equations. Only slightly faster with 128 bits. See da_diett.pdf Chpt. 3.1
* Ctrl-C handling on linux/windows
* implement self-test mode (-s flag)
* block decrypt first (does not depend on stream). Then stream afterwards, stop XORing immediately if foreseeable there is no PES header

recent updates (2026-07)
------------------------
* **NEON SIMD support** — ARM64 128-bit SIMD via NEON intrinsics, 2.7x faster single-thread throughput on Apple Silicon
* **Multi-threading** — `-p <n>` flag for parallel brute force across n threads with near-linear scaling
* **C++17 conversion** — rewritten main in C++17 with Settings struct, exception-based error handling, `std::string_view` argument parsing, `std::thread` parallelism, `std::atomic` coordination
* **Compiler optimizations** — `-flto -march=native` for +9% single-thread throughput
* **Algorithmic improvements** — stream decrypt reduced from 25 to 24 bits (only 3 bytes needed for PES check), `std::memcpy` for block init copy
* **C++17 compatibility fixes** — removed `register` keywords, added explicit casts, fixed const-correctness throughout
* **Build system** — platform detection (SSE on x86_64, NEON on ARM64, scalar fallback), C++17 standard, LTO

developers
----------
* after changing the code, run tests with SELFTEST enabled to make sure the algorithm still works. It's too easy to break things.
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
