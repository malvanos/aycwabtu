#ifndef AYCW_BS_RENAME_H_
#define AYCW_BS_RENAME_H_

/*
   Per-backend symbol renaming for run-time SIMD dispatch.

   The bit-sliced algorithm (bs_algo.c, bs_stream.c, bs_block_ab.c,
   bs_testcases.c, bs_<backend>.c) and the per-backend driver
   (bs_driver_impl.cpp) are compiled once per SIMD backend with
   -DAYCW_BACKEND=<name> (see makefile).  Every extern symbol they export is
   suffixed with the backend name so that ALL backends can be linked into one
   binary and selected at run time (auto-detect or `-S`), instead of being a
   build-time choice.

   This header is only active in backend builds (AYCW_BACKEND defined by the
   makefile).  It must be included from a backend TU BEFORE any header that
   declares one of the renamed symbols, i.e. right after "config.h".
*/

#ifdef AYCW_BACKEND

#define AYCW_RN_JOIN2(a, b) a##b
#define AYCW_RN_JOIN(a, b)  AYCW_RN_JOIN2(a, b)
#define AYCW_RN(sym)        AYCW_RN_JOIN(sym, AYCW_RN_JOIN(_, AYCW_BACKEND))

/* ---- bit-sliced core (declared in bs_algo.h / bs_stream.h / bs_block_ab.h
        / bs_testcases.h, defined in the per-backend algorithm TUs) ---- */
#define aycw_extractbsdata            AYCW_RN(aycw_extractbsdata)
#define aycw_assert_decrypt_result    AYCW_RN(aycw_assert_decrypt_result)
#define aycw_assert_key_transpose     AYCW_RN(aycw_assert_key_transpose)
#define aycw_assert_stream            AYCW_RN(aycw_assert_stream)
#define aycw_bs_xor24                 AYCW_RN(aycw_bs_xor24)
#define aycw_bs_increment_keys_inner  AYCW_RN(aycw_bs_increment_keys_inner)
#define aycw_checkPESheader           AYCW_RN(aycw_checkPESheader)
#define aycw_assertKeyBatch           AYCW_RN(aycw_assertKeyBatch)
#define aycw_bit2byteslice            AYCW_RN(aycw_bit2byteslice)
#define aycw_fatal_error              AYCW_RN(aycw_fatal_error)

#define aycw_stream_key_schedule      AYCW_RN(aycw_stream_key_schedule)
#define aycw_key_transpose            AYCW_RN(aycw_key_transpose)
#define aycw_vTransformKey            AYCW_RN(aycw_vTransformKey)
#define aycw_init_stream              AYCW_RN(aycw_init_stream)
#define aycw_stream_decrypt           AYCW_RN(aycw_stream_decrypt)

#define aycw_block_key_perm           AYCW_RN(aycw_block_key_perm)
#define aycw_block_key_schedule       AYCW_RN(aycw_block_key_schedule)
#define aycw_init_block               AYCW_RN(aycw_init_block)
#define aycw_block_decrypt            AYCW_RN(aycw_block_decrypt)
#define aycw_block_sbox               AYCW_RN(aycw_block_sbox)
#define aycw_block_xor                AYCW_RN(aycw_block_xor)

#define aycw__vRound                  AYCW_RN(aycw__vRound)
#define aycw__vInitRound              AYCW_RN(aycw__vInitRound)
#define aycw__vCaculatePQXYZ          AYCW_RN(aycw__vCaculatePQXYZ)

#define aycw_check_bs_testcases       AYCW_RN(aycw_check_bs_testcases)

#define bs_tc_crypteddata             AYCW_RN(bs_tc_crypteddata)
#define bs_tc_keys                    AYCW_RN(bs_tc_keys)
#define bs_tc_expstream               AYCW_RN(bs_tc_expstream)
#define bs_tc_expblock                AYCW_RN(bs_tc_expblock)
#define bs_tc_expected                AYCW_RN(bs_tc_expected)

/* ---- per-backend driver entry points (bs_driver_impl.cpp) ---- */
#define ayc_bruteForceRange           AYCW_RN(ayc_bruteForceRange)
#define ayc_bruteForceParallel        AYCW_RN(ayc_bruteForceParallel)
#define ayc_benchmark                 AYCW_RN(ayc_benchmark)
#define ayc_selfTest                  AYCW_RN(ayc_selfTest)

#endif /* AYCW_BACKEND */

#endif /* AYCW_BS_RENAME_H_ */