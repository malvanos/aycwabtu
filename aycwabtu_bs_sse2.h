
#ifndef AYCW_SSE_H_
#define AYCW_SSE_H_

#include <xmmintrin.h>
#include <emmintrin.h>

typedef __m128i dvbcsa_bs_word_t;

#define BS_BATCH_SIZE 128
#define BS_BATCH_BYTES 16
#define BS_BATCH_SHIFT  7

//#define BS_SSE_X64

#ifdef BS_SSE_X64
#define BS_VAL(n, m)	_mm_set_epi64x(n, m)
#define BS_VAL64(n)	BS_VAL(0x##n##ULL, 0x##n##ULL)
#define BS_VAL32(n)	BS_VAL64(n##n)
#else
#define BS_VAL_LSDW(n)	BS_VAL(0,0,0,n)      // load 32 bit value to least significant dword
#define BS_VAL(n, m, o, p)	_mm_set_epi32(n, m, o, p)
#define BS_VAL32(n)	BS_VAL(0x##n##UL, 0x##n##UL, 0x##n##UL, 0x##n##UL)
#endif
#define BS_VAL16(n)	BS_VAL32(n##n)
#define BS_VAL8(n)	BS_VAL16(n##n)

#define BS_AND(a, b)	_mm_and_si128((a), (b))
#define BS_OR(a, b)	_mm_or_si128((a), (b))
#define BS_XOR(a, b)	_mm_xor_si128((a), (b))
#define BS_XOREQ(a, b)	{ dvbcsa_bs_word_t *_t = &(a); *_t = _mm_xor_si128(*_t, (b)); }
#define BS_NOT(a)	_mm_andnot_si128((a), BS_VAL8(ff))

// there is no intrinsic in sse for bitwise logical shift. See http://stackoverflow.com/questions/17610696/shift-a-m128i-of-n-bits
// _mm_slli_epi64 does not work!
// So a function is needed - unfortunately
dvbcsa_bs_word_t BS_SHL(dvbcsa_bs_word_t v, int n);
dvbcsa_bs_word_t BS_SHR(dvbcsa_bs_word_t v, int n);

#define BS_SHL8(a, n)	_mm_slli_si128(a, n)
#define BS_SHR8(a, n)	_mm_srli_si128(a, n)

#define BS_EXTRACT8(a, n) ((uint8_t*)&(a))[n]
#define BS_EXTLS32(a)      _mm_cvtsi128_si32(a)       // Moves the least significant 32 bits of a to a 32-bit integer.

#define BS_EMPTY()


#ifdef __SSE4_2__

#define CHECK_ZERO(a) _mm_testz_si128((a),(a))

#else

#define CHECK_ZERO(a) (_mm_movemask_epi8(_mm_cmpeq_epi32((a),_mm_setzero_si128())) == 0xFFFF)

#endif

#endif

