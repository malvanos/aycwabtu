#include "config.h"

#if PARALLEL_MODE == PARALLEL_128_NEON

/* Key transpose: BS_BATCH_SIZE keys (128) → 64 bitsliced words.
   key_in is a flat array of BS_BATCH_SIZE × 8 bytes.
   Uses NEON intrinsics via BS_* macros. */
void aycw_key_transpose(const uint8 *key_in, dvbcsa_bs_word_t *row)
{
    unsigned int i, j, k;
#define KEY(r, c) key_in[(r) * 8 + (c)]

    for (k = 0; k < 64; k++) {
        row[k] = BS_VAL(
            acyw_load_le32(&KEY(k + 64, 4)),
            acyw_load_le32(&KEY(k + 64, 0)),
            acyw_load_le32(&KEY(k +  0, 4)),
            acyw_load_le32(&KEY(k +  0, 0)));
    }
#undef KEY

    /* Butterfly transpose: 32-bit → 16-bit → 8-bit → 4-bit → 2-bit → 1-bit */
    for (i = 0; i < 32; i++) {
        dvbcsa_bs_word_t t = row[i];
        dvbcsa_bs_word_t b = row[32 + i];
        row[i]      = BS_OR(BS_AND(t, BS_VAL(0x00000000, 0xffffffff, 0x00000000, 0xffffffff)),
                            BS_SHL8(BS_AND(b, BS_VAL(0x00000000, 0xffffffff, 0x00000000, 0xffffffff)), 4));
        row[32 + i] = BS_OR(BS_AND(b, BS_VAL(0xffffffff, 0x00000000, 0xffffffff, 0x00000000)),
                            BS_SHR8(BS_AND(t, BS_VAL(0xffffffff, 0x00000000, 0xffffffff, 0x00000000)), 4));
    }

    for (j = 0; j < 64; j += 32) {
        for (i = 0; i < 16; i++) {
            dvbcsa_bs_word_t t = row[j + i];
            dvbcsa_bs_word_t b = row[j + 16 + i];
            row[j + i]      = BS_OR(BS_AND(t, BS_VAL32(0000ffff)), BS_SHL8(BS_AND(b, BS_VAL32(0000ffff)), 2));
            row[j + 16 + i] = BS_OR(BS_AND(b, BS_VAL32(ffff0000)), BS_SHR8(BS_AND(t, BS_VAL32(ffff0000)), 2));
        }
    }

    for (j = 0; j < 64; j += 16) {
        for (i = 0; i < 8; i++) {
            dvbcsa_bs_word_t t = row[j + i];
            dvbcsa_bs_word_t b = row[j + 8 + i];
            row[j + i]     = BS_OR(BS_AND(t, BS_VAL16(00ff)), BS_SHL8(BS_AND(b, BS_VAL16(00ff)), 1));
            row[j + 8 + i] = BS_OR(BS_AND(b, BS_VAL16(ff00)), BS_SHR8(BS_AND(t, BS_VAL16(ff00)), 1));
        }
    }

    for (j = 0; j < 64; j += 8) {
        for (i = 0; i < 4; i++) {
            dvbcsa_bs_word_t t = row[j + i];
            dvbcsa_bs_word_t b = row[j + 4 + i];
            row[j + i]     = BS_OR(BS_AND(t, BS_VAL8(0f)), BS_SHL(BS_AND(b, BS_VAL8(0f)), 4));
            row[j + 4 + i] = BS_OR(BS_AND(b, BS_VAL8(f0)), BS_SHR(BS_AND(t, BS_VAL8(f0)), 4));
        }
    }

    for (j = 0; j < 64; j += 4) {
        for (i = 0; i < 2; i++) {
            dvbcsa_bs_word_t t = row[j + i];
            dvbcsa_bs_word_t b = row[j + 2 + i];
            row[j + i]     = BS_OR(BS_AND(t, BS_VAL8(33)), BS_SHL(BS_AND(b, BS_VAL8(33)), 2));
            row[j + 2 + i] = BS_OR(BS_AND(b, BS_VAL8(cc)), BS_SHR(BS_AND(t, BS_VAL8(cc)), 2));
        }
    }

    for (j = 0; j < 64; j += 2) {
        dvbcsa_bs_word_t t = row[j];
        dvbcsa_bs_word_t b = row[j + 1];
        row[j]     = BS_OR(BS_AND(t, BS_VAL8(55)), BS_SHL(BS_AND(b, BS_VAL8(55)), 1));
        row[j + 1] = BS_OR(BS_AND(b, BS_VAL8(aa)), BS_SHR(BS_AND(t, BS_VAL8(aa)), 1));
    }
}

/* Bit-to-byte-slice transpose for NEON.
   With USEALLBITSLICE this is never called on the hot path,
   but the function must exist for the linker. */
#ifndef USE_SLOW_BIT2BYTESLICE
void aycw_bit2byteslice(dvbcsa_bs_word_t *data, int count)
{
    int i, j, k;
    dvbcsa_bs_word_t *p = data;

    for (k = 0; k < 8 * count; k++) {
        /* 32-bit groups */
        for (i = 0; i < 4; i++) {
            dvbcsa_bs_word_t t = p[i];
            dvbcsa_bs_word_t b = p[4 + i];
            p[i + 0] = BS_OR(BS_AND(t, BS_VAL32(0000ffff)), BS_SHL8(BS_AND(b, BS_VAL32(0000ffff)), 2));
            p[i + 4] = BS_OR(BS_AND(b, BS_VAL32(ffff0000)), BS_SHR8(BS_AND(t, BS_VAL32(ffff0000)), 2));
        }

        for (j = 0; j < 8; j += 4) {
            for (i = 0; i < 2; i++) {
                dvbcsa_bs_word_t t = p[j + i];
                dvbcsa_bs_word_t b = p[j + 2 + i];
                p[j + i + 0] = BS_OR(BS_AND(t, BS_VAL16(00ff)), BS_SHL8(BS_AND(b, BS_VAL16(00ff)), 1));
                p[j + i + 2] = BS_OR(BS_AND(b, BS_VAL16(ff00)), BS_SHR8(BS_AND(t, BS_VAL16(ff00)), 1));
            }
        }

        for (j = 0; j < 8; j += 2) {
            dvbcsa_bs_word_t t = p[j];
            dvbcsa_bs_word_t b = p[j + 1];
            p[j + 0] = BS_OR(BS_AND(t, BS_VAL8(0f)), BS_SHL(BS_AND(b, BS_VAL8(0f)), 4));
            p[j + 1] = BS_OR(BS_AND(b, BS_VAL8(f0)), BS_SHR(BS_AND(t, BS_VAL8(f0)), 4));
        }

        for (j = 0; j < 8; j++) {
            dvbcsa_bs_word_t t = p[j];

            t = BS_OR(BS_AND(t, BS_VAL32(cccc3333)),
                 BS_OR(BS_SHR(BS_AND(t, BS_VAL32(33330000)), 14),
                       BS_SHL(BS_AND(t, BS_VAL32(0000cccc)), 14)));

            t = BS_OR(BS_AND(t, BS_VAL16(aa55)),
                 BS_OR(BS_SHR(BS_AND(t, BS_VAL16(5500)), 7),
                       BS_SHL(BS_AND(t, BS_VAL16(00aa)), 7)));

            t = BS_OR(BS_AND(t, BS_VAL8(81)),
                 BS_OR(BS_SHR(BS_AND(t, BS_VAL8(10)), 3),
                 BS_OR(BS_SHR(BS_AND(t, BS_VAL8(20)), 2),
                 BS_OR(BS_SHR(BS_AND(t, BS_VAL8(40)), 1),
                 BS_OR(BS_SHL(BS_AND(t, BS_VAL8(02)), 1),
                 BS_OR(BS_SHL(BS_AND(t, BS_VAL8(04)), 2),
                       BS_SHL(BS_AND(t, BS_VAL8(08)), 3)))))));

            p[j] = t;
        }

        /* Byte reordering */
        for (i = 0; i < 4; i++) {
            dvbcsa_bs_word_t t = p[i];
            dvbcsa_bs_word_t b = p[4 + i];
            p[i + 0] = BS_OR(BS_AND(t, BS_VAL(0x00000000, 0x00000000, 0xffffffff, 0xffffffff)),
                             BS_SHL8(BS_AND(b, BS_VAL(0x00000000, 0x00000000, 0xffffffff, 0xffffffff)), 8));
            p[i + 4] = BS_OR(BS_AND(b, BS_VAL(0xffffffff, 0xffffffff, 0x00000000, 0x00000000)),
                             BS_SHR8(BS_AND(t, BS_VAL(0xffffffff, 0xffffffff, 0x00000000, 0x00000000)), 8));
        }
        for (j = 0; j < 8; j += 4) {
            for (i = 0; i < 2; i++) {
                dvbcsa_bs_word_t t = p[j + i];
                dvbcsa_bs_word_t b = p[j + 2 + i];
                p[j + i + 0] = BS_OR(BS_AND(t, BS_VAL(0x00000000, 0x00000000, 0xffffffff, 0xffffffff)),
                                     BS_SHL8(BS_AND(b, BS_VAL(0x00000000, 0x00000000, 0xffffffff, 0xffffffff)), 8));
                p[j + i + 2] = BS_OR(BS_AND(b, BS_VAL(0xffffffff, 0xffffffff, 0x00000000, 0x00000000)),
                                     BS_SHR8(BS_AND(t, BS_VAL(0xffffffff, 0xffffffff, 0x00000000, 0x00000000)), 8));
                t = p[j + i];
                b = p[j + 2 + i];
                p[j + i + 0] = BS_OR(BS_AND(t, BS_VAL(0x00000000, 0xffffffff, 0x00000000, 0xffffffff)),
                                     BS_SHL8(BS_AND(b, BS_VAL(0x00000000, 0xffffffff, 0x00000000, 0xffffffff)), 4));
                p[j + i + 2] = BS_OR(BS_AND(b, BS_VAL(0xffffffff, 0x00000000, 0xffffffff, 0x00000000)),
                                     BS_SHR8(BS_AND(t, BS_VAL(0xffffffff, 0x00000000, 0xffffffff, 0x00000000)), 4));
            }
        }
        for (j = 0; j < 8; j += 2) {
            dvbcsa_bs_word_t t = p[j];
            dvbcsa_bs_word_t b = p[j + 1];
            p[j + 0] = BS_OR(BS_AND(t, BS_VAL(0x00000000, 0xffffffff, 0x00000000, 0xffffffff)),
                             BS_SHL8(BS_AND(b, BS_VAL(0x00000000, 0xffffffff, 0x00000000, 0xffffffff)), 4));
            p[j + 1] = BS_OR(BS_AND(b, BS_VAL(0xffffffff, 0x00000000, 0xffffffff, 0x00000000)),
                             BS_SHR8(BS_AND(t, BS_VAL(0xffffffff, 0x00000000, 0xffffffff, 0x00000000)), 4));
        }
        p += 8;
    }
}
#endif  /* !USE_SLOW_BIT2BYTESLICE */

#endif  /* PARALLEL_MODE == PARALLEL_128_NEON */
