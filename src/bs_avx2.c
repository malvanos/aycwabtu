#include "config.h"
#include "bs_rename.h"

#if PARALLEL_MODE == PARALLEL_256_AVX2

/* input transpose key from normal to bitslice
   uint8 key[BS_BATCH_SIZE][8] -> dvbcsa_bs_word_t row[64] */

void aycw_key_transpose(const uint8 *key_in, dvbcsa_bs_word_t *row)
{
   const uint8 (*key)[8] = (const uint8 (*)[8])key_in;
   unsigned int i, j, k;

   /* each 256-bit register packs 4 keys (k, k+64, k+128, k+192); each key is
      two little-endian dwords (bytes 0..3 | bytes 4..7).  The remaining
      transposition stages are batch-width independent. */
   for (k = 0; k < 64; k++)
   {
      row[k] = BS_VAL(
         acyw_load_le32(&key[(k + 192)][4]),
         acyw_load_le32(&key[(k + 192)][0]),
         acyw_load_le32(&key[(k + 128)][4]),
         acyw_load_le32(&key[(k + 128)][0]),
         acyw_load_le32(&key[(k + 64)][4]),
         acyw_load_le32(&key[(k + 64)][0]),
         acyw_load_le32(&key[(k + 0)][4]),
         acyw_load_le32(&key[(k + 0)][0]));
   }

   for (i = 0; i < 32; i++)
   {
      dvbcsa_bs_word_t t, b;
      t = row[i];
      b = row[32 + i];
      row[i]      = BS_OR(BS_AND(t, BS_VAL64(00000000ffffffff)), BS_SHL8(BS_AND(b, BS_VAL64(00000000ffffffff)), 4));
      row[32 + i] = BS_OR(BS_AND(b, BS_VAL64(ffffffff00000000)), BS_SHR8(BS_AND(t, BS_VAL64(ffffffff00000000)), 4));
   }

   for (j = 0; j < 64; j += 32)
   {
      dvbcsa_bs_word_t t, b;
      for (i = 0; i < 16; i++)
      {
         t = row[j + i];
         b = row[j + 16 + i];
         row[j + i]      = BS_OR(BS_AND(t, BS_VAL32(0000ffff)), BS_SHL8(BS_AND(b, BS_VAL32(0000ffff)), 2));
         row[j + 16 + i] = BS_OR(BS_AND(b, BS_VAL32(ffff0000)), BS_SHR8(BS_AND(t, BS_VAL32(ffff0000)), 2));
      }
   }

   for (j = 0; j < 64; j += 16)
   {
      dvbcsa_bs_word_t t, b;
      for (i = 0; i < 8; i++)
      {
         t = row[j + i];
         b = row[j + 8 + i];
         row[j + i]     = BS_OR(BS_AND(t, BS_VAL16(00ff)), BS_SHL8(BS_AND(b, BS_VAL16(00ff)), 1));
         row[j + 8 + i] = BS_OR(BS_AND(b, BS_VAL16(ff00)), BS_SHR8(BS_AND(t, BS_VAL16(ff00)), 1));
      }
   }

   for (j = 0; j < 64; j += 8)
   {
      dvbcsa_bs_word_t t, b;
      for (i = 0; i < 4; i++)
      {
         t = row[j + i];
         b = row[j + 4 + i];
         row[j + i]     = BS_OR(BS_AND(t, BS_VAL8(0f)), BS_SHL(BS_AND(b, BS_VAL8(0f)), 4));
         row[j + 4 + i] = BS_OR(BS_AND(b, BS_VAL8(f0)), BS_SHR(BS_AND(t, BS_VAL8(f0)), 4));
      }
   }

   for (j = 0; j < 64; j += 4)
   {
      dvbcsa_bs_word_t t, b;
      for (i = 0; i < 2; i++)
      {
         t = row[j + i];
         b = row[j + 2 + i];
         row[j + i]     = BS_OR(BS_AND(t, BS_VAL8(33)), BS_SHL(BS_AND(b, BS_VAL8(33)), 2));
         row[j + 2 + i] = BS_OR(BS_AND(b, BS_VAL8(cc)), BS_SHR(BS_AND(t, BS_VAL8(cc)), 2));
      }
   }

   for (j = 0; j < 64; j += 2)
   {
      dvbcsa_bs_word_t t, b;
      t = row[j];
      b = row[j + 1];
      row[j]     = BS_OR(BS_AND(t, BS_VAL8(55)), BS_SHL(BS_AND(b, BS_VAL8(55)), 1));
      row[j + 1] = BS_OR(BS_AND(b, BS_VAL8(aa)), BS_SHR(BS_AND(t, BS_VAL8(aa)), 1));
   }
}

/* byte-slicing fallback for !USEALLBITSLICE builds; not on the default
   all-bit-slice path, but kept correct and self-contained. */
#ifndef USE_SLOW_BIT2BYTESLICE
void aycw_extractbsdata(dvbcsa_bs_word_t*, unsigned char, unsigned char, uint8*); /* from bs_algo.c */

void aycw_bit2byteslice(dvbcsa_bs_word_t *data, int count)
{
   int i, j, k;
   dvbcsa_bs_word_t *p = data;

   for (k = 0; k < 8 * count; k++)
   {
      dvbcsa_bs_word_t bs[8];
      for (j = 0; j < 8; j++) bs[j] = BS_VAL8(00);
      for (i = 0; i < BS_BATCH_SIZE; i++)
      {
         uint8 tmp2 = 0;
         aycw_extractbsdata(p, (unsigned char)i, 8, &tmp2);
         for (j = 0; j < 8; j++)
            if (tmp2 & (1 << j))
               bs[j] = BS_OR(bs[j], BS_SHL(BS_VAL_LSDW(1), i));
      }
      for (i = 0; i < 8; i++) p[i] = bs[i];
      p += 8;
   }
}
#endif /* !USE_SLOW_BIT2BYTESLICE */

#endif /* PARALLEL_256_AVX2 */