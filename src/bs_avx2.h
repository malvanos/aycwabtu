#ifndef AYCW_AVX2_H_
#define AYCW_AVX2_H_

#include <stdint.h>
#include <immintrin.h>

typedef __m256i dvbcsa_bs_word_t;

#define BS_BATCH_SIZE 256
#define BS_BATCH_BYTES 32
#define BS_BATCH_SHIFT 8

/* ---- 256-bit logical shift helpers (composed from two 128-bit halves) ---- */
#if defined(__clang__) || defined(__GNUC__) || defined(__MINGW32__)
static inline __m128i avx2_shl128(__m128i v, int n) {
    __uint128_t x = (__uint128_t)v;
    return (__m128i)(x << n);
}
static inline __m128i avx2_shr128(__m128i v, int n) {
    __uint128_t x = (__uint128_t)v;
    return (__m128i)(x >> n);
}
#else
static inline __m128i avx2_shl128(__m128i v, int n) {
    __m128i v1, v2;
    if (n >= 64) { v1 = _mm_slli_si128(v, 8); v1 = _mm_slli_epi64(v1, n - 64); }
    else { v1 = _mm_slli_epi64(v, n); v2 = _mm_slli_si128(v, 8); v2 = _mm_srli_epi64(v2, 64 - n); v1 = _mm_or_si128(v1, v2); }
    return v1;
}
static inline __m128i avx2_shr128(__m128i v, int n) {
    __m128i v1, v2;
    if (n >= 64) { v1 = _mm_srli_si128(v, 8); v1 = _mm_srli_epi64(v1, n - 64); }
    else { v1 = _mm_srli_epi64(v, n); v2 = _mm_srli_si128(v, 8); v2 = _mm_slli_epi64(v2, 64 - n); v1 = _mm_or_si128(v1, v2); }
    return v1;
}
#endif

static inline __m256i avx2_shl(__m256i v, int n) {
    int r = n & 127, q = n >> 7;
    __m128i lo = _mm256_extracti128_si256(v, 0);
    __m128i hi = _mm256_extracti128_si256(v, 1);
    __m128i nlo, nhi;
    if (q == 0) {
        nlo = avx2_shl128(lo, r);
        nhi = (r == 0) ? hi : _mm_or_si128(avx2_shl128(hi, r), avx2_shr128(lo, 128 - r));
    } else if (q == 1) {        /* left shift by 128+r bits: high = lo<<r, low = 0 */
        nlo = _mm_setzero_si128();
        nhi = avx2_shl128(lo, r);
    } else {
        nlo = _mm_setzero_si128();
        nhi = _mm_setzero_si128();
    }
    return _mm256_set_m128i(nhi, nlo);
}
static inline __m256i avx2_shr(__m256i v, int n) {
    int r = n & 127, q = n >> 7;
    __m128i lo = _mm256_extracti128_si256(v, 0);
    __m128i hi = _mm256_extracti128_si256(v, 1);
    __m128i nlo, nhi;
    if (q == 0) {
        nhi = avx2_shr128(hi, r);
        nlo = (r == 0) ? lo : _mm_or_si128(avx2_shr128(lo, r), avx2_shl128(hi, 128 - r));
    } else if (q == 1) {        /* right shift by 128+r bits: low = hi>>r, high = 0 */
        nlo = avx2_shr128(hi, r);
        nhi = _mm_setzero_si128();
    } else {
        nlo = _mm_setzero_si128();
        nhi = _mm_setzero_si128();
    }
    return _mm256_set_m128i(nhi, nlo);
}

/* ---- construction / constants ---- */
#define BS_VAL_LSDW(n)  _mm256_set_epi32(0,0,0,0,0,0,0,(uint32_t)(n))
#define BS_VAL(a7,a6,a5,a4,a3,a2,a1,a0)  _mm256_set_epi32((a7),(a6),(a5),(a4),(a3),(a2),(a1),(a0))
#define BS_VAL64(n)     _mm256_set1_epi64x(0x##n##ULL)
#define BS_VAL32(n)     _mm256_set1_epi32(0x##n##UL)
#define BS_VAL16(n)     BS_VAL32(n##n)
#define BS_VAL8(n)      BS_VAL16(n##n)

/* ---- logic ---- */
#define BS_AND(a, b)    _mm256_and_si256((a), (b))
#define BS_OR(a, b)     _mm256_or_si256((a), (b))
#define BS_XOR(a, b)    _mm256_xor_si256((a), (b))
#define BS_XOREQ(a, b)  { dvbcsa_bs_word_t *_t = &(a); *_t = _mm256_xor_si256(*_t, (b)); }
#define BS_NOT(a)       _mm256_andnot_si256((a), BS_VAL8(ff))

/* ---- shifts ---- */
#define BS_SHL(a, n)    avx2_shl((a), (n))
#define BS_SHR(a, n)    avx2_shr((a), (n))
#define BS_SHL8(a, n)   avx2_shl((a), (n) * 8)
#define BS_SHR8(a, n)   avx2_shr((a), (n) * 8)

/* ---- extraction / tests ---- */
#define BS_EXTRACT8(a, n)  ((uint8_t*)&(a))[(n)]
#define BS_EXTLS32(a)      _mm256_cvtsi256_si32(a)
#define BS_EXTRACT32(a, n) _mm256_extract_epi32((a), (n))
#define CHECK_ZERO(a)      _mm256_testz_si256((a), (a))

#define BS_EMPTY()

#endif /* AYCW_AVX2_H_ */