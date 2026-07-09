#ifndef AYCW_NEON_H_
#define AYCW_NEON_H_

#include <arm_neon.h>

typedef uint32x4_t dvbcsa_bs_word_t;

#define BS_BATCH_SIZE 128
#define BS_BATCH_BYTES 16
#define BS_BATCH_SHIFT  7

/* Helper: construct a 128-bit vector from four 32-bit lanes.
   Lane order matches _mm_set_epi32: n=hi, m, o, p=lo. */
static inline uint32x4_t neon_set_epi32(uint32_t n, uint32_t m,
                                        uint32_t o, uint32_t p) {
    uint32_t vals[4] = {p, o, m, n};
    return vld1q_u32(vals);
}

#define BS_VAL_LSDW(n)  vdupq_n_u32((uint32_t)(n))
#define BS_VAL(n,m,o,p) neon_set_epi32((n),(m),(o),(p))
#define BS_VAL32(n)     vdupq_n_u32(0x##n##UL)
#define BS_VAL16(n)     BS_VAL32(n##n)
#define BS_VAL8(n)      BS_VAL16(n##n)

/* Bitwise ops */
#define BS_AND(a, b)    vandq_u32((a), (b))
#define BS_OR(a, b)     vorrq_u32((a), (b))
#define BS_XOR(a, b)    veorq_u32((a), (b))
#define BS_XOREQ(a, b)  { dvbcsa_bs_word_t *_t = &(a); *_t = veorq_u32(*_t, (b)); }
#define BS_NOT(a)       vmvnq_u32(a)

/* Runtime 128-bit bitwise shift — same approach as SSE: cast to
   __uint128_t, shift, cast back.  Per-lane NEON shifts would lose
   carry bits between 32-bit lanes, which is incorrect for bitsliced
   operations that treat the 128-bit word as a single bit-vector. */
static inline uint32x4_t neon_shl(uint32x4_t v, int n) {
    __uint128_t x;
    __builtin_memcpy(&x, &v, sizeof(x));
    x <<= n;
    __builtin_memcpy(&v, &x, sizeof(x));
    return v;
}
static inline uint32x4_t neon_shr(uint32x4_t v, int n) {
    __uint128_t x;
    __builtin_memcpy(&x, &v, sizeof(x));
    x >>= n;
    __builtin_memcpy(&v, &x, sizeof(x));
    return v;
}

#define BS_SHL(a, n)    neon_shl((a), (n))
#define BS_SHR(a, n)    neon_shr((a), (n))

/* Byte-level shifts — just delegate to the 128-bit bit shift (n bytes = n*8 bits) */
#define BS_SHL8(a, n)   neon_shl((a), (n) * 8)
#define BS_SHR8(a, n)   neon_shr((a), (n) * 8)

#define BS_EXTRACT8(a, n) ((uint8_t*)&(a))[n]
#define BS_EXTLS32(a)      vgetq_lane_u32((a), 0)
#define BS_EXTRACT32(a, n) vgetq_lane_u32((a), (n))
#define CHECK_ZERO(a)      (vmaxvq_u32(a) == 0)

#define BS_EMPTY()

#endif  /* AYCW_NEON_H_ */
