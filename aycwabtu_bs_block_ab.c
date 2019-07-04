/*
   aycwabtu_bs_block_ab.c

   this file contains the block implementation in bit sliced form where
   the sbox is implemented as boolean equations. See #define  USEALLBITSLICE

*/

#include <string.h>
#include <stdio.h>

#include "aycwabtu_config.h"
#include "aycwabtu_bs_block_ab.h"

#ifdef USEALLBITSLICE


/* table 19,27,55 is achieved from table 17, 35, 8,... by applying 7-x to lower 3 bits */
static const uint8 bf_key_perm[64] = { 19, 27, 55, 46, 1, 15, 36, 22, 56, 61, 39, 21, 54, 58, 50, 28, 7, 29, 51, 6, 33, 35, 20, 16, 47, 30, 32, 63, 10, 11, 4, 38, 62, 26, 40, 18, 12, 52, 37, 53, 23, 59, 41, 17, 31, 0, 25, 43, 44, 14, 2, 13, 45, 48, 3, 60, 49, 8, 34, 5, 9, 42, 57, 24, };


static void AYCW_INLINE aycw_block_key_perm(dvbcsa_bs_word_t* in, dvbcsa_bs_word_t* out)
{
/* csa block key schedule bit permutation */
#define CPY(a,b) (out)[(b)] = (in)[(a)]
   CPY(0, 19);
   CPY(1, 27);
   CPY(2, 55);
   CPY(3, 46);
   CPY(4, 1);
   CPY(5, 15);
   CPY(6, 36);
   CPY(7, 22);
   CPY(8, 56);
   CPY(9, 61);
   CPY(10, 39);
   CPY(11, 21);
   CPY(12, 54);
   CPY(13, 58);
   CPY(14, 50);
   CPY(15, 28);
   CPY(16, 7);
   CPY(17, 29);
   CPY(18, 51);
   CPY(19, 6);
   CPY(20, 33);
   CPY(21, 35);
   CPY(22, 20);
   CPY(23, 16);
   CPY(24, 47);
   CPY(25, 30);
   CPY(26, 32);
   CPY(27, 63);
   CPY(28, 10);
   CPY(29, 11);
   CPY(30, 4);
   CPY(31, 38);
   CPY(32, 62);
   CPY(33, 26);
   CPY(34, 40);
   CPY(35, 18);
   CPY(36, 12);
   CPY(37, 52);
   CPY(38, 37);
   CPY(39, 53);
   CPY(40, 23);
   CPY(41, 59);
   CPY(42, 41);
   CPY(43, 17);
   CPY(44, 31);
   CPY(45, 0);
   CPY(46, 25);
   CPY(47, 43);
   CPY(48, 44);
   CPY(49, 14);
   CPY(50, 2);
   CPY(51, 13);
   CPY(52, 45);
   CPY(53, 48);
   CPY(54, 3);
   CPY(55, 60);
   CPY(56, 49);
   CPY(57, 8);
   CPY(58, 34);
   CPY(59, 5);
   CPY(60, 9);
   CPY(61, 42);
   CPY(62, 57);
   CPY(63, 24);
}

/**
 Calculates expanded keys - all in bit sliced form
 @parameter keys[in]    input key array BS_BATCH_SIZE x 64
            kk[out]     output expanded key array BS_BATCH_SIZE x 448
*/
void aycw_block_key_schedule(const dvbcsa_bs_word_t* keys, dvbcsa_bs_word_t* kk)
{
   int i,j;

	 // OPTIMIZEME this by using memcpy( &kk[6 * 64], keys, 64*sizeof(dvbcsa_bs_word_t)) or CPY;
   for (i = 0; i < 64; i++)  
      kk[6 * 64 + i] = keys[i];


   aycw_block_key_perm(&kk[6*64], &kk[5*64]);
   aycw_block_key_perm(&kk[5*64], &kk[4*64]);
   aycw_block_key_perm(&kk[4*64], &kk[3*64]);
   aycw_block_key_perm(&kk[3*64], &kk[2*64]);
   aycw_block_key_perm(&kk[2*64], &kk[1*64]);
   aycw_block_key_perm(&kk[1*64], &kk[0*64]);

   for (i = 6; i>0; i--)      /* i = 6...1  xor 0 skipped */
   {
      for (j = 7; j>=0; j--)
      {
         switch(i)
         {
         case 1:
            kk[1*64 + j*8 + 0] = BS_NOT(kk[1*64 + j*8 + 0]);
            break;
         case 2:
            kk[2*64 + j*8 + 1] = BS_NOT(kk[2*64 + j*8 + 1]);
            break;
         case 3:
            kk[3*64 + j*8 + 0] = BS_NOT(kk[3*64 + j*8 + 0]);
            kk[3*64 + j*8 + 1] = BS_NOT(kk[3*64 + j*8 + 1]);
            break;
         case 4:
            kk[4*64 + j*8 + 2] = BS_NOT(kk[4*64 + j*8 + 2]);
            break;
         case 5:
            kk[5*64 + j*8 + 0] = BS_NOT(kk[5*64 + j*8 + 0]);
            kk[5*64 + j*8 + 2] = BS_NOT(kk[5*64 + j*8 + 2]);
            break;
         case 6:
            kk[6*64 + j*8 + 1] = BS_NOT(kk[6*64 + j*8 + 1]);
            kk[6*64 + j*8 + 2] = BS_NOT(kk[6*64 + j*8 + 2]);
            break;
         }
      }
   }
}

/* apply the sbox algo to the batches w.....w+7 */
void aycw_block_sbox(dvbcsa_bs_word_t *out, dvbcsa_bs_word_t *in)
{
      dvbcsa_bs_word_t b_7 = in[7];
      dvbcsa_bs_word_t b_6 = in[6];
      dvbcsa_bs_word_t b_5 = in[5];
      dvbcsa_bs_word_t b_4 = in[4];
      dvbcsa_bs_word_t b_3 = in[3];
      dvbcsa_bs_word_t b_2 = in[2];
      dvbcsa_bs_word_t b_1 = in[1];
      dvbcsa_bs_word_t b_0 = in[0];

      dvbcsa_bs_word_t all1 = BS_VAL8(ff);

      dvbcsa_bs_word_t t_0 = BS_AND(b_7, b_6);
      dvbcsa_bs_word_t t_1 = BS_AND(b_7, b_5);
      dvbcsa_bs_word_t t_2 = BS_AND(b_7, b_4);
      dvbcsa_bs_word_t t_3 = BS_AND(b_7, b_3);
      dvbcsa_bs_word_t t_4 = BS_AND(b_7, b_2);
      dvbcsa_bs_word_t t_5 = BS_AND(b_7, b_1);
      dvbcsa_bs_word_t t_6 = BS_AND(b_6, b_5);
      dvbcsa_bs_word_t t_7 = BS_AND(b_6, b_4);
      dvbcsa_bs_word_t t_8 = BS_AND(b_6, b_3);
      dvbcsa_bs_word_t t_9 = BS_AND(b_6, b_2);
      dvbcsa_bs_word_t t_10 = BS_AND(b_6, b_1);
      dvbcsa_bs_word_t t_11 = BS_AND(b_5, b_4);
      dvbcsa_bs_word_t t_12 = BS_AND(b_5, b_3);
      dvbcsa_bs_word_t t_13 = BS_AND(b_5, b_2);
      dvbcsa_bs_word_t t_14 = BS_AND(b_5, b_1);
      dvbcsa_bs_word_t t_15 = BS_AND(b_4, b_3);
      dvbcsa_bs_word_t t_16 = BS_AND(b_4, b_2);
      dvbcsa_bs_word_t t_17 = BS_AND(b_4, b_1);
      dvbcsa_bs_word_t t_18 = BS_AND(b_4, b_0);
      dvbcsa_bs_word_t t_19 = BS_AND(b_3, b_2);
      dvbcsa_bs_word_t t_20 = BS_AND(b_3, b_1);
      dvbcsa_bs_word_t t_21 = BS_AND(b_3, b_0);
      dvbcsa_bs_word_t t_22 = BS_AND(b_2, b_1);
      dvbcsa_bs_word_t t_23 = BS_AND(b_2, b_0);
      dvbcsa_bs_word_t t_24 = BS_AND(b_1, b_0);
      dvbcsa_bs_word_t t_25 = BS_AND(t_0, t_11);
      dvbcsa_bs_word_t t_26 = BS_AND(t_0, t_12);
      dvbcsa_bs_word_t t_27 = BS_AND(t_0, t_13);
      dvbcsa_bs_word_t t_28 = BS_AND(t_0, t_14);
      dvbcsa_bs_word_t t_29 = BS_AND(t_0, t_15);
      dvbcsa_bs_word_t t_30 = BS_AND(t_0, t_16);
      dvbcsa_bs_word_t t_31 = BS_AND(t_0, t_17);
      dvbcsa_bs_word_t t_32 = BS_AND(t_0, t_19);
      dvbcsa_bs_word_t t_33 = BS_AND(t_0, t_20);
      dvbcsa_bs_word_t t_34 = BS_AND(t_0, t_22);
      dvbcsa_bs_word_t t_35 = BS_AND(t_1, t_15);
      dvbcsa_bs_word_t t_36 = BS_AND(t_1, t_16);
      dvbcsa_bs_word_t t_37 = BS_AND(t_1, t_17);
      dvbcsa_bs_word_t t_38 = BS_AND(t_1, t_19);
      dvbcsa_bs_word_t t_39 = BS_AND(t_1, t_20);
      dvbcsa_bs_word_t t_40 = BS_AND(t_1, t_22);
      dvbcsa_bs_word_t t_41 = BS_AND(t_2, t_19);
      dvbcsa_bs_word_t t_42 = BS_AND(t_2, t_22);
      dvbcsa_bs_word_t t_43 = BS_AND(t_3, t_22);
      dvbcsa_bs_word_t t_44 = BS_AND(t_6, t_15);
      dvbcsa_bs_word_t t_45 = BS_AND(t_6, t_16);
      dvbcsa_bs_word_t t_46 = BS_AND(t_6, t_19);
      dvbcsa_bs_word_t t_47 = BS_AND(t_6, t_20);
      dvbcsa_bs_word_t t_48 = BS_AND(t_6, t_22);
      dvbcsa_bs_word_t t_49 = BS_AND(t_7, t_19);
      dvbcsa_bs_word_t t_50 = BS_AND(t_7, t_20);
      dvbcsa_bs_word_t t_51 = BS_AND(t_7, t_22);
      dvbcsa_bs_word_t t_52 = BS_AND(t_11, t_19);
      dvbcsa_bs_word_t t_53 = BS_AND(t_11, t_20);
      dvbcsa_bs_word_t t_54 = BS_AND(t_12, t_22);
      dvbcsa_bs_word_t t_55 = BS_AND(t_15, t_22);

      // Equations were taken from da_diett.pdf chpt. 3.1.2 / A.1 (thanks Markus)
      /* da_diett XOR - pairs of variables */
      out[7] = BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((b_5), (b_4)), BS_XOR((b_3), (b_0))), BS_XOR(BS_XOR((t_0), (t_4)), BS_XOR((t_5), BS_AND(b_7, b_0)))), BS_XOR(BS_XOR(BS_XOR((t_6), (t_7)), BS_XOR((t_8), (t_9))), BS_XOR(BS_XOR((t_10), (t_11)), BS_XOR((t_12), (t_14))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_15), (t_16)), BS_XOR((t_17), (t_21))), BS_XOR(BS_XOR((t_22), BS_AND(t_0, b_3)), BS_XOR(BS_AND(t_1, b_3), BS_AND(t_1, b_2)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_1, b_1), BS_AND(t_2, b_3)), BS_XOR(BS_AND(t_2, b_1), BS_AND(t_3, b_1))), BS_XOR(BS_XOR(BS_AND(t_4, b_1), BS_AND(t_4, b_0)), BS_XOR(BS_AND(t_6, b_4), BS_AND(t_6, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_7, b_1), BS_AND(t_8, b_0)), BS_XOR(BS_AND(t_9, b_1), BS_AND(t_12, b_0))), BS_XOR(BS_XOR(BS_AND(t_14, b_0), BS_AND(t_15, b_1)), BS_XOR(BS_AND(t_15, b_0), BS_AND(t_16, b_1)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_16, b_0), BS_AND(t_20, b_0)), BS_XOR((t_26), BS_AND(BS_AND(t_0, b_5), b_0))), BS_XOR(BS_XOR((t_30), (t_31)), BS_XOR((t_32), BS_AND(t_0, t_24))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_35), (t_36)), BS_XOR(BS_AND(t_1, t_18), (t_38))), BS_XOR(BS_XOR(BS_AND(t_1, t_21), BS_AND(t_1, t_23)), BS_XOR((t_41), BS_AND(t_2, t_20)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_2, t_21), (t_42)), BS_XOR(BS_AND(t_2, t_23), (t_43))), BS_XOR(BS_XOR(BS_AND(t_3, t_23), BS_AND(t_3, t_24)), BS_XOR(BS_AND(t_4, t_24), BS_AND(t_6, t_17))))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_47), BS_AND(t_6, t_21)), BS_XOR(BS_AND(t_6, t_24), (t_49))), BS_XOR(BS_XOR(BS_AND(t_7, t_21), (t_51)), BS_XOR(BS_AND(t_7, t_23), BS_AND(t_8, t_23)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_9, t_24), (t_53)), BS_XOR(BS_AND(t_11, t_21), BS_AND(t_11, t_24))), BS_XOR(BS_XOR((t_54), BS_AND(t_15, t_23)), BS_XOR(BS_AND(t_15, t_24), BS_AND(t_16, t_24))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_19, t_24), BS_AND(t_25, b_3)), BS_XOR(BS_AND(t_26, b_1), BS_AND(t_27, b_1))), BS_XOR(BS_XOR(BS_AND(t_28, b_0), BS_AND(t_29, b_2)), BS_XOR(BS_AND(t_30, b_0), BS_AND(t_31, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_32, b_0), BS_AND(t_33, b_0)), BS_XOR(BS_AND(t_34, b_0), BS_AND(t_37, b_0))), BS_XOR(BS_XOR(BS_AND(t_38, b_1), BS_AND(t_41, b_0)), BS_XOR(BS_AND(BS_AND(t_2, t_20), b_0), BS_AND(t_44, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_45, b_0), BS_AND(t_46, b_1)), BS_XOR(BS_AND(t_46, b_0), BS_AND(t_48, b_0))), BS_XOR(BS_XOR(BS_AND(t_49, b_1), BS_AND(t_49, b_0)), BS_XOR(BS_AND(t_50, b_0), BS_AND(BS_AND(t_8, t_22), b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_52, b_0), BS_AND(t_53, b_0)), BS_XOR(BS_AND(t_54, b_0), BS_AND(t_55, b_0))), BS_XOR(BS_XOR(BS_AND(t_25, t_19), BS_AND(t_25, t_21)), BS_XOR(BS_AND(t_25, t_22), BS_AND(t_25, t_23))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_25, t_24), BS_AND(t_26, t_23)), BS_XOR(BS_AND(t_27, t_24), BS_AND(t_29, t_23))), BS_XOR(BS_XOR(BS_AND(t_32, t_24), BS_AND(t_35, t_22)), BS_XOR(BS_AND(t_35, t_23), BS_AND(t_36, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_41, t_24), BS_AND(t_44, t_24)), BS_XOR(BS_AND(t_49, t_24), BS_AND(BS_AND(t_25, t_19), b_1))), BS_XOR(BS_XOR(BS_AND(BS_AND(t_25, t_19), b_0), BS_AND(BS_AND(t_25, t_20), b_0)), BS_AND(BS_AND(t_25, t_22), b_0)))))));
      out[6] = BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((b_4), (b_1)), BS_XOR((b_0), (t_0))), BS_XOR(BS_XOR(BS_AND(b_7, b_0), (t_6)), BS_XOR((t_9), (t_10)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(b_6, b_0), (t_12)), BS_XOR((t_13), (t_15))), BS_XOR(BS_XOR((t_16), (t_17)), BS_XOR((t_22), (t_24))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_0, b_5), BS_AND(t_0, b_4)), BS_XOR(BS_AND(t_0, b_1), BS_AND(t_0, b_0))), BS_XOR(BS_XOR(BS_AND(t_1, b_4), BS_AND(t_1, b_3)), BS_XOR(BS_AND(t_2, b_1), BS_AND(t_3, b_1)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_3, b_0), BS_AND(t_6, b_4)), BS_XOR(BS_AND(t_6, b_0), BS_AND(t_7, b_3))), BS_XOR(BS_XOR(BS_AND(t_7, b_2), BS_AND(t_7, b_1)), BS_XOR(BS_AND(t_7, b_0), BS_AND(t_8, b_1)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_8, b_0), BS_AND(t_11, b_3)), BS_XOR(BS_AND(t_11, b_0), BS_AND(t_12, b_2))), BS_XOR(BS_XOR(BS_AND(t_12, b_1), BS_AND(t_12, b_0)), BS_XOR(BS_AND(t_13, b_1), BS_AND(t_13, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_15, b_2), BS_AND(t_15, b_0)), BS_XOR(BS_AND(t_16, b_0), BS_AND(t_17, b_0))), BS_XOR(BS_XOR((t_25), BS_AND(BS_AND(t_0, b_5), b_0)), BS_XOR((t_29), BS_AND(t_0, t_18))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_32), (t_33)), BS_XOR(BS_AND(t_0, t_21), (t_34))), BS_XOR(BS_XOR(BS_AND(t_0, t_24), (t_36)), BS_XOR((t_37), (t_39)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_1, t_21), (t_40)), BS_XOR(BS_AND(t_1, t_24), BS_AND(t_2, t_21))), BS_XOR(BS_XOR((t_43), BS_AND(t_3, t_24)), BS_XOR((t_45), BS_AND(t_6, t_17))))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_48), BS_AND(t_7, t_24)), BS_XOR(BS_AND(t_8, t_23), BS_AND(t_8, t_24))), BS_XOR(BS_XOR(BS_AND(t_9, t_24), (t_52)), BS_XOR((t_53), BS_AND(t_11, t_24)))), BS_XOR(BS_XOR(BS_XOR((t_54), (t_55)), BS_XOR(BS_AND(t_15, t_23), BS_AND(t_15, t_24))), BS_XOR(BS_XOR(BS_AND(t_26, b_2), BS_AND(t_26, b_1)), BS_XOR(BS_AND(t_26, b_0), BS_AND(t_27, b_1))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_27, b_0), BS_AND(t_29, b_0)), BS_XOR(BS_AND(t_30, b_0), BS_AND(t_31, b_0))), BS_XOR(BS_XOR(BS_AND(t_32, b_1), BS_AND(t_35, b_2)), BS_XOR(BS_AND(t_35, b_0), BS_AND(t_36, b_1)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_37, b_0), BS_AND(t_38, b_0)), BS_XOR(BS_AND(t_39, b_0), BS_AND(t_40, b_0))), BS_XOR(BS_XOR(BS_AND(t_42, b_0), BS_AND(t_43, b_0)), BS_XOR(BS_AND(t_44, b_2), BS_AND(t_44, b_1)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_44, b_0), BS_AND(t_45, b_1)), BS_XOR(BS_AND(t_45, b_0), BS_AND(BS_AND(t_6, t_17), b_0))), BS_XOR(BS_XOR(BS_AND(t_46, b_0), BS_AND(t_47, b_0)), BS_XOR(BS_AND(t_48, b_0), BS_AND(t_49, b_1)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_50, b_0), BS_AND(BS_AND(t_8, t_22), b_0)), BS_XOR(BS_AND(t_52, b_0), BS_AND(t_54, b_0))), BS_XOR(BS_XOR(BS_AND(t_25, t_19), BS_AND(t_25, t_20)), BS_XOR(BS_AND(t_25, t_23), BS_AND(t_27, t_24))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_29, t_23), BS_AND(t_29, t_24)), BS_XOR(BS_AND(t_30, t_24), BS_AND(t_32, t_24))), BS_XOR(BS_XOR(BS_AND(t_35, t_23), BS_AND(t_35, t_24)), BS_XOR(BS_AND(t_38, t_24), BS_AND(t_44, t_22)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_45, t_24), BS_AND(t_52, t_24)), BS_XOR(BS_AND(BS_AND(t_25, t_19), b_1), BS_AND(BS_AND(t_25, t_19), b_0))), BS_XOR(BS_XOR(BS_AND(BS_AND(t_25, t_20), b_0), BS_AND(BS_AND(t_26, t_22), b_0)), BS_AND(BS_AND(t_29, t_22), b_0)))))));
      out[5] = BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((all1), (b_6)), BS_XOR((b_3), (t_0))), BS_XOR(BS_XOR((t_1), (t_3)), BS_XOR((t_4), (t_6)))), BS_XOR(BS_XOR(BS_XOR((t_7), (t_9)), BS_XOR((t_10), BS_AND(b_6, b_0))), BS_XOR(BS_XOR((t_13), (t_15)), BS_XOR((t_16), (t_19))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_20), (t_22)), BS_XOR(BS_AND(t_0, b_0), BS_AND(t_1, b_3))), BS_XOR(BS_XOR(BS_AND(t_2, b_0), BS_AND(t_3, b_2)), BS_XOR(BS_AND(t_3, b_0), BS_AND(t_5, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_6, b_4), BS_AND(t_6, b_2)), BS_XOR(BS_AND(t_7, b_1), BS_AND(t_7, b_0))), BS_XOR(BS_XOR(BS_AND(t_8, b_2), BS_AND(t_8, b_1)), BS_XOR(BS_AND(t_9, b_0), BS_AND(t_11, b_2)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_13, b_1), BS_AND(t_14, b_0)), BS_XOR(BS_AND(t_15, b_2), BS_AND(t_15, b_0))), BS_XOR(BS_XOR(BS_AND(t_16, b_0), BS_AND(t_17, b_0)), BS_XOR((t_27), (t_28)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(BS_AND(t_0, b_5), b_0), (t_29)), BS_XOR((t_30), (t_31))), BS_XOR(BS_XOR(BS_AND(t_0, t_18), (t_32)), BS_XOR(BS_AND(t_0, t_24), (t_35))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_37), BS_AND(t_1, t_18)), BS_XOR((t_38), (t_39))), BS_XOR(BS_XOR((t_40), (t_41)), BS_XOR(BS_AND(t_2, t_21), (t_42)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_2, t_23), BS_AND(t_2, t_24)), BS_XOR((t_43), (t_44))), BS_XOR(BS_XOR((t_45), BS_AND(t_6, t_17)), BS_XOR(BS_AND(t_6, t_18), (t_46))))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_47), (t_48)), BS_XOR(BS_AND(t_6, t_23), BS_AND(t_6, t_24))), BS_XOR(BS_XOR((t_49), (t_50)), BS_XOR(BS_AND(t_7, t_21), BS_AND(t_8, t_22)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_8, t_24), BS_AND(t_11, t_21)), BS_XOR(BS_AND(t_11, t_23), BS_AND(t_11, t_24))), BS_XOR(BS_XOR((t_54), BS_AND(t_12, t_24)), BS_XOR((t_55), BS_AND(t_15, t_23))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_15, t_24), BS_AND(t_19, t_24)), BS_XOR(BS_AND(t_25, b_1), BS_AND(t_25, b_0))), BS_XOR(BS_XOR(BS_AND(t_26, b_2), BS_AND(t_29, b_2)), BS_XOR(BS_AND(t_30, b_0), BS_AND(t_31, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_32, b_0), BS_AND(t_33, b_0)), BS_XOR(BS_AND(t_35, b_2), BS_AND(t_35, b_1))), BS_XOR(BS_XOR(BS_AND(t_38, b_0), BS_AND(t_40, b_0)), BS_XOR(BS_AND(t_41, b_1), BS_AND(t_44, b_2)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_45, b_1), BS_AND(BS_AND(t_6, t_17), b_0)), BS_XOR(BS_AND(t_46, b_1), BS_AND(t_46, b_0))), BS_XOR(BS_XOR(BS_AND(t_47, b_0), BS_AND(t_49, b_0)), BS_XOR(BS_AND(t_50, b_0), BS_AND(t_51, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_52, b_1), BS_AND(t_53, b_0)), BS_XOR(BS_AND(BS_AND(t_11, t_22), b_0), BS_AND(t_54, b_0))), BS_XOR(BS_XOR(BS_AND(t_55, b_0), BS_AND(t_25, t_19)), BS_XOR(BS_AND(t_25, t_23), BS_AND(t_25, t_24))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_26, t_23), BS_AND(t_27, t_24)), BS_XOR(BS_AND(t_29, t_22), BS_AND(t_29, t_24))), BS_XOR(BS_XOR(BS_AND(t_30, t_24), BS_AND(t_35, t_23)), BS_XOR(BS_AND(t_36, t_24), BS_AND(t_38, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_41, t_24), BS_AND(t_44, t_22)), BS_XOR(BS_AND(t_44, t_24), BS_AND(t_52, t_24))), BS_XOR(BS_XOR(BS_AND(BS_AND(t_25, t_19), b_1), BS_AND(BS_AND(t_25, t_20), b_0)), BS_XOR(BS_AND(BS_AND(t_29, t_22), b_0), BS_AND(BS_AND(t_44, t_22), b_0))))))));
      out[4] = BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((all1), (b_7)), BS_XOR((b_4), (b_3))), BS_XOR(BS_XOR((b_1), (b_0)), BS_XOR((t_0), (t_1)))), BS_XOR(BS_XOR(BS_XOR((t_2), (t_3)), BS_XOR((t_5), (t_6))), BS_XOR(BS_XOR((t_7), (t_8)), BS_XOR((t_11), (t_13))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(b_5, b_0), (t_15)), BS_XOR((t_17), (t_18))), BS_XOR(BS_XOR((t_19), (t_20)), BS_XOR((t_21), BS_AND(t_0, b_4)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_0, b_3), BS_AND(t_0, b_1)), BS_XOR(BS_AND(t_0, b_0), BS_AND(t_1, b_2))), BS_XOR(BS_XOR(BS_AND(t_1, b_1), BS_AND(t_2, b_2)), BS_XOR(BS_AND(t_3, b_0), BS_AND(t_6, b_1)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_6, b_0), BS_AND(t_7, b_2)), BS_XOR(BS_AND(t_7, b_0), BS_AND(t_8, b_2))), BS_XOR(BS_XOR(BS_AND(t_10, b_0), BS_AND(t_11, b_1)), BS_XOR(BS_AND(t_13, b_1), BS_AND(t_13, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_15, b_1), BS_AND(t_15, b_0)), BS_XOR(BS_AND(t_16, b_0), BS_AND(t_17, b_0))), BS_XOR(BS_XOR(BS_AND(t_19, b_0), BS_AND(t_20, b_0)), BS_XOR((t_27), (t_28))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_29), (t_31)), BS_XOR((t_33), BS_AND(t_0, t_21))), BS_XOR(BS_XOR((t_34), BS_AND(t_0, t_23)), BS_XOR(BS_AND(t_0, t_24), (t_35)))), BS_XOR(BS_XOR(BS_XOR((t_39), (t_40)), BS_XOR(BS_AND(t_1, t_24), (t_43))), BS_XOR(BS_XOR(BS_AND(t_3, t_23), BS_AND(t_3, t_24)), BS_XOR(BS_AND(t_4, t_24), (t_44))))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_45), (t_48)), BS_XOR(BS_AND(t_6, t_23), BS_AND(t_6, t_24))), BS_XOR(BS_XOR((t_49), (t_50)), BS_XOR(BS_AND(t_8, t_22), BS_AND(t_9, t_24)))), BS_XOR(BS_XOR(BS_XOR((t_53), BS_AND(t_11, t_21)), BS_XOR(BS_AND(t_11, t_23), (t_54))), BS_XOR(BS_XOR(BS_AND(t_12, t_24), (t_55)), BS_XOR(BS_AND(t_15, t_23), BS_AND(t_15, t_24))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_16, t_24), BS_AND(t_25, b_1)), BS_XOR(BS_AND(t_26, b_1), BS_AND(t_28, b_0))), BS_XOR(BS_XOR(BS_AND(t_29, b_2), BS_AND(t_29, b_1)), BS_XOR(BS_AND(t_29, b_0), BS_AND(t_30, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_32, b_0), BS_AND(t_33, b_0)), BS_XOR(BS_AND(t_34, b_0), BS_AND(t_35, b_2))), BS_XOR(BS_XOR(BS_AND(t_35, b_1), BS_AND(t_36, b_1)), BS_XOR(BS_AND(t_36, b_0), BS_AND(t_37, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_38, b_1), BS_AND(t_39, b_0)), BS_XOR(BS_AND(t_41, b_1), BS_AND(BS_AND(t_2, t_20), b_0))), BS_XOR(BS_XOR(BS_AND(t_43, b_0), BS_AND(t_46, b_1)), BS_XOR(BS_AND(t_47, b_0), BS_AND(t_49, b_1)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_51, b_0), BS_AND(t_52, b_1)), BS_XOR(BS_AND(t_52, b_0), BS_AND(t_53, b_0))), BS_XOR(BS_XOR(BS_AND(t_54, b_0), BS_AND(t_55, b_0)), BS_XOR(BS_AND(t_25, t_20), BS_AND(t_25, t_22))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_25, t_24), BS_AND(t_26, t_22)), BS_XOR(BS_AND(t_29, t_22), BS_AND(t_29, t_24))), BS_XOR(BS_XOR(BS_AND(t_32, t_24), BS_AND(t_35, t_23)), BS_XOR(BS_AND(t_38, t_24), BS_AND(t_41, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_49, t_24), BS_AND(t_52, t_24)), BS_XOR(BS_AND(BS_AND(t_25, t_19), b_1), BS_AND(BS_AND(t_25, t_20), b_0))), BS_XOR(BS_AND(BS_AND(t_25, t_22), b_0), BS_AND(BS_AND(t_35, t_22), b_0)))))));
      out[3] = BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((all1), (b_6)), BS_XOR((b_3), (b_2))), BS_XOR(BS_XOR((t_0), (t_5)), BS_XOR((t_6), (t_9)))), BS_XOR(BS_XOR(BS_XOR((t_10), BS_AND(b_6, b_0)), BS_XOR((t_12), (t_16))), BS_XOR(BS_XOR((t_17), (t_18)), BS_XOR((t_21), (t_22))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_23), BS_AND(t_0, b_5)), BS_XOR(BS_AND(t_0, b_4), BS_AND(t_0, b_3))), BS_XOR(BS_XOR(BS_AND(t_0, b_2), BS_AND(t_0, b_1)), BS_XOR(BS_AND(t_0, b_0), BS_AND(t_1, b_2)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_1, b_1), BS_AND(t_2, b_3)), BS_XOR(BS_AND(t_2, b_1), BS_AND(t_3, b_2))), BS_XOR(BS_XOR(BS_AND(t_3, b_1), BS_AND(t_3, b_0)), BS_XOR(BS_AND(t_4, b_1), BS_AND(t_6, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_7, b_3), BS_AND(t_7, b_1)), BS_XOR(BS_AND(t_8, b_2), BS_AND(t_8, b_0))), BS_XOR(BS_XOR(BS_AND(t_9, b_1), BS_AND(t_9, b_0)), BS_XOR(BS_AND(t_10, b_0), BS_AND(t_11, b_1)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_11, b_0), BS_AND(t_12, b_2)), BS_XOR(BS_AND(t_12, b_1), BS_AND(t_12, b_0))), BS_XOR(BS_XOR(BS_AND(t_13, b_0), BS_AND(t_14, b_0)), BS_XOR(BS_AND(t_15, b_1), BS_AND(t_16, b_1))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_16, b_0), BS_AND(t_17, b_0)), BS_XOR(BS_AND(t_19, b_1), BS_AND(t_19, b_0))), BS_XOR(BS_XOR(BS_AND(t_22, b_0), (t_25)), BS_XOR((t_26), (t_27)))), BS_XOR(BS_XOR(BS_XOR((t_28), (t_29)), BS_XOR((t_30), BS_AND(t_0, t_18))), BS_XOR(BS_XOR(BS_AND(t_0, t_21), (t_38)), BS_XOR((t_39), BS_AND(t_1, t_24))))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_42), BS_AND(t_2, t_24)), BS_XOR((t_44), (t_45))), BS_XOR(BS_XOR((t_46), BS_AND(t_6, t_21)), BS_XOR(BS_AND(t_6, t_23), BS_AND(t_6, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_7, t_21), (t_51)), BS_XOR(BS_AND(t_7, t_23), BS_AND(t_9, t_24))), BS_XOR(BS_XOR((t_52), (t_53)), BS_XOR(BS_AND(t_11, t_21), BS_AND(t_11, t_23))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_11, t_24), (t_54)), BS_XOR(BS_AND(t_12, t_23), BS_AND(t_13, t_24))), BS_XOR(BS_XOR((t_55), BS_AND(t_15, t_23)), BS_XOR(BS_AND(t_16, t_24), BS_AND(t_19, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_25, b_2), BS_AND(t_25, b_1)), BS_XOR(BS_AND(t_25, b_0), BS_AND(t_26, b_1))), BS_XOR(BS_XOR(BS_AND(t_27, b_0), BS_AND(t_29, b_2)), BS_XOR(BS_AND(t_30, b_1), BS_AND(t_30, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_31, b_0), BS_AND(t_32, b_1)), BS_XOR(BS_AND(t_34, b_0), BS_AND(t_35, b_2))), BS_XOR(BS_XOR(BS_AND(t_36, b_1), BS_AND(t_36, b_0)), BS_XOR(BS_AND(t_37, b_0), BS_AND(t_39, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_41, b_1), BS_AND(t_42, b_0)), BS_XOR(BS_AND(t_45, b_1), BS_AND(t_45, b_0))), BS_XOR(BS_XOR(BS_AND(t_46, b_1), BS_AND(t_49, b_0)), BS_XOR(BS_AND(t_51, b_0), BS_AND(t_52, b_0))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(BS_AND(t_11, t_22), b_0), BS_AND(t_55, b_0)), BS_XOR(BS_AND(t_25, t_20), BS_AND(t_25, t_21))), BS_XOR(BS_XOR(BS_AND(t_26, t_23), BS_AND(t_29, t_22)), BS_XOR(BS_AND(t_29, t_23), BS_AND(t_29, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_30, t_24), BS_AND(t_32, t_24)), BS_XOR(BS_AND(t_35, t_22), BS_AND(t_35, t_23))), BS_XOR(BS_XOR(BS_AND(t_44, t_22), BS_AND(t_44, t_23)), BS_XOR(BS_AND(t_44, t_24), BS_AND(t_46, t_24)))))))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_49, t_24), BS_AND(t_52, t_24)), BS_XOR(BS_AND(BS_AND(t_25, t_19), b_0), BS_AND(BS_AND(t_25, t_20), b_0))), BS_XOR(BS_XOR(BS_AND(BS_AND(t_26, t_22), b_0), BS_AND(BS_AND(t_29, t_22), b_0)), BS_XOR(BS_AND(BS_AND(t_35, t_22), b_0), BS_AND(BS_AND(t_44, t_22), b_0)))));
      out[2] = BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((b_6), (b_5)), BS_XOR((t_0), (t_1))), BS_XOR(BS_XOR((t_3), (t_4)), BS_XOR((t_5), BS_AND(b_7, b_0)))), BS_XOR(BS_XOR(BS_XOR((t_6), (t_7)), BS_XOR((t_8), (t_9))), BS_XOR(BS_XOR(BS_AND(b_6, b_0), (t_13)), BS_XOR(BS_AND(b_5, b_0), (t_16))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_18), (t_21)), BS_XOR((t_24), BS_AND(t_0, b_5))), BS_XOR(BS_XOR(BS_AND(t_0, b_4), BS_AND(t_0, b_3)), BS_XOR(BS_AND(t_0, b_1), BS_AND(t_0, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_1, b_4), BS_AND(t_1, b_0)), BS_XOR(BS_AND(t_3, b_2), BS_AND(t_3, b_1))), BS_XOR(BS_XOR(BS_AND(t_5, b_0), BS_AND(t_6, b_4)), BS_XOR(BS_AND(t_6, b_1), BS_AND(t_6, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_7, b_3), BS_AND(t_7, b_2)), BS_XOR(BS_AND(t_8, b_2), BS_AND(t_8, b_1))), BS_XOR(BS_XOR(BS_AND(t_8, b_0), BS_AND(t_9, b_1)), BS_XOR(BS_AND(t_9, b_0), BS_AND(t_11, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_12, b_2), BS_AND(t_12, b_0)), BS_XOR(BS_AND(t_13, b_1), BS_AND(t_13, b_0))), BS_XOR(BS_XOR(BS_AND(t_15, b_0), BS_AND(t_16, b_0)), BS_XOR(BS_AND(t_17, b_0), BS_AND(t_19, b_0))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_20, b_0), BS_AND(t_22, b_0)), BS_XOR((t_25), (t_26))), BS_XOR(BS_XOR((t_27), (t_29)), BS_XOR(BS_AND(t_0, t_18), (t_32)))), BS_XOR(BS_XOR(BS_XOR((t_33), BS_AND(t_0, t_21)), BS_XOR(BS_AND(t_0, t_24), (t_35))), BS_XOR(BS_XOR((t_37), BS_AND(t_1, t_18)), BS_XOR((t_39), BS_AND(t_1, t_21))))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_40), BS_AND(t_1, t_23)), BS_XOR(BS_AND(t_2, t_20), BS_AND(t_2, t_24))), BS_XOR(BS_XOR(BS_AND(t_3, t_23), (t_44)), BS_XOR((t_45), (t_47)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_6, t_21), (t_48)), BS_XOR(BS_AND(t_6, t_23), (t_49))), BS_XOR(BS_XOR((t_50), BS_AND(t_7, t_21)), BS_XOR((t_51), BS_AND(t_7, t_23))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_7, t_24), BS_AND(t_8, t_24)), BS_XOR((t_52), (t_53))), BS_XOR(BS_XOR(BS_AND(t_12, t_24), BS_AND(t_13, t_24)), BS_XOR(BS_AND(t_15, t_24), BS_AND(t_19, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_25, b_1), BS_AND(t_26, b_2)), BS_XOR(BS_AND(t_26, b_1), BS_AND(t_29, b_2))), BS_XOR(BS_XOR(BS_AND(t_32, b_0), BS_AND(t_33, b_0)), BS_XOR(BS_AND(t_34, b_0), BS_AND(t_35, b_2)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_36, b_1), BS_AND(t_36, b_0)), BS_XOR(BS_AND(t_37, b_0), BS_AND(t_39, b_0))), BS_XOR(BS_XOR(BS_AND(t_40, b_0), BS_AND(t_41, b_1)), BS_XOR(BS_AND(BS_AND(t_2, t_20), b_0), BS_AND(t_43, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_44, b_1), BS_AND(t_45, b_1)), BS_XOR(BS_AND(t_45, b_0), BS_AND(t_46, b_1))), BS_XOR(BS_XOR(BS_AND(t_47, b_0), BS_AND(t_48, b_0)), BS_XOR(BS_AND(t_50, b_0), BS_AND(t_51, b_0))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_52, b_0), BS_AND(t_54, b_0)), BS_XOR(BS_AND(t_25, t_19), BS_AND(t_25, t_20))), BS_XOR(BS_XOR(BS_AND(t_25, t_21), BS_AND(t_25, t_22)), BS_XOR(BS_AND(t_26, t_22), BS_AND(t_26, t_23)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_26, t_24), BS_AND(t_29, t_22)), BS_XOR(BS_AND(t_29, t_23), BS_AND(t_30, t_24))), BS_XOR(BS_XOR(BS_AND(t_35, t_23), BS_AND(t_41, t_24)), BS_XOR(BS_AND(t_44, t_22), BS_AND(t_44, t_24)))))))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_45, t_24), BS_AND(t_49, t_24)), BS_XOR(BS_AND(t_52, t_24), BS_AND(BS_AND(t_25, t_19), b_1))), BS_XOR(BS_AND(BS_AND(t_25, t_20), b_0), BS_AND(BS_AND(t_35, t_22), b_0))));
      out[1] = BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((all1), (b_7)), BS_XOR((b_4), (b_1))), BS_XOR(BS_XOR((t_4), (t_6)), BS_XOR((t_9), (t_11)))), BS_XOR(BS_XOR(BS_XOR((t_12), (t_13)), BS_XOR((t_15), (t_17))), BS_XOR(BS_XOR((t_18), (t_23)), BS_XOR((t_24), BS_AND(t_0, b_4))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_0, b_1), BS_AND(t_1, b_4)), BS_XOR(BS_AND(t_1, b_2), BS_AND(t_1, b_1))), BS_XOR(BS_XOR(BS_AND(t_1, b_0), BS_AND(t_2, b_1)), BS_XOR(BS_AND(t_2, b_0), BS_AND(t_3, b_2)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_3, b_0), BS_AND(t_4, b_1)), BS_XOR(BS_AND(t_4, b_0), BS_AND(t_5, b_0))), BS_XOR(BS_XOR(BS_AND(t_6, b_4), BS_AND(t_7, b_0)), BS_XOR(BS_AND(t_8, b_0), BS_AND(t_10, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_11, b_2), BS_AND(t_12, b_2)), BS_XOR(BS_AND(t_12, b_0), BS_AND(t_13, b_0))), BS_XOR(BS_XOR(BS_AND(t_15, b_0), BS_AND(t_16, b_0)), BS_XOR(BS_AND(t_19, b_0), BS_AND(t_22, b_0)))), BS_XOR(BS_XOR(BS_XOR((t_28), BS_AND(BS_AND(t_0, b_5), b_0)), BS_XOR((t_29), BS_AND(t_0, t_21))), BS_XOR(BS_XOR(BS_AND(t_0, t_24), (t_38)), BS_XOR((t_39), BS_AND(t_1, t_21))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_40), BS_AND(t_1, t_23)), BS_XOR(BS_AND(t_1, t_24), BS_AND(t_2, t_21))), BS_XOR(BS_XOR(BS_AND(t_2, t_23), BS_AND(t_3, t_23)), BS_XOR(BS_AND(t_4, t_24), (t_45)))), BS_XOR(BS_XOR(BS_XOR((t_46), (t_47)), BS_XOR(BS_AND(t_6, t_21), BS_AND(t_6, t_23))), BS_XOR(BS_XOR(BS_AND(t_6, t_24), BS_AND(t_7, t_21)), BS_XOR((t_51), BS_AND(t_7, t_24))))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_8, t_23), BS_AND(t_8, t_24)), BS_XOR(BS_AND(t_9, t_24), (t_53))), BS_XOR(BS_XOR(BS_AND(t_11, t_23), (t_54)), BS_XOR(BS_AND(t_12, t_24), BS_AND(t_15, t_23)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_16, t_24), BS_AND(t_19, t_24)), BS_XOR(BS_AND(t_25, b_3), BS_AND(t_25, b_1))), BS_XOR(BS_XOR(BS_AND(t_27, b_0), BS_AND(t_29, b_2)), BS_XOR(BS_AND(t_29, b_1), BS_AND(t_30, b_1))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_35, b_1), BS_AND(t_36, b_1)), BS_XOR(BS_AND(t_36, b_0), BS_AND(t_37, b_0))), BS_XOR(BS_XOR(BS_AND(t_41, b_0), BS_AND(t_42, b_0)), BS_XOR(BS_AND(t_43, b_0), BS_AND(t_44, b_2)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_44, b_0), BS_AND(t_45, b_1)), BS_XOR(BS_AND(t_46, b_1), BS_AND(t_46, b_0))), BS_XOR(BS_XOR(BS_AND(t_47, b_0), BS_AND(t_49, b_1)), BS_XOR(BS_AND(t_50, b_0), BS_AND(t_51, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_52, b_0), BS_AND(t_53, b_0)), BS_XOR(BS_AND(BS_AND(t_11, t_22), b_0), BS_AND(t_55, b_0))), BS_XOR(BS_XOR(BS_AND(t_25, t_19), BS_AND(t_25, t_20)), BS_XOR(BS_AND(t_25, t_21), BS_AND(t_25, t_22)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_25, t_24), BS_AND(t_26, t_22)), BS_XOR(BS_AND(t_26, t_24), BS_AND(t_29, t_23))), BS_XOR(BS_XOR(BS_AND(t_30, t_24), BS_AND(t_35, t_23)), BS_XOR(BS_AND(t_35, t_24), BS_AND(t_36, t_24))))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_38, t_24), BS_AND(t_49, t_24)), BS_XOR(BS_AND(t_52, t_24), BS_AND(BS_AND(t_25, t_20), b_0))), BS_XOR(BS_AND(BS_AND(t_26, t_22), b_0), BS_AND(BS_AND(t_44, t_22), b_0))))));
      out[0] = BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((b_7), (b_3)), BS_XOR((b_2), (t_0))), BS_XOR(BS_XOR((t_3), (t_4)), BS_XOR((t_7), BS_AND(b_6, b_0)))), BS_XOR(BS_XOR(BS_XOR((t_11), (t_12)), BS_XOR((t_14), (t_15))), BS_XOR(BS_XOR((t_16), (t_17)), BS_XOR((t_18), (t_22))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_0, b_5), BS_AND(t_0, b_3)), BS_XOR(BS_AND(t_0, b_2), BS_AND(t_0, b_1))), BS_XOR(BS_XOR(BS_AND(t_0, b_0), BS_AND(t_1, b_2)), BS_XOR(BS_AND(t_1, b_1), BS_AND(t_1, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_2, b_0), BS_AND(t_4, b_1)), BS_XOR(BS_AND(t_5, b_0), BS_AND(t_6, b_1))), BS_XOR(BS_XOR(BS_AND(t_7, b_2), BS_AND(t_7, b_1)), BS_XOR(BS_AND(t_9, b_1), BS_AND(t_10, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_11, b_3), BS_AND(t_11, b_2)), BS_XOR(BS_AND(t_11, b_0), BS_AND(t_12, b_0))), BS_XOR(BS_XOR(BS_AND(t_15, b_2), BS_AND(t_15, b_1)), BS_XOR(BS_AND(t_15, b_0), BS_AND(t_16, b_1)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_16, b_0), BS_AND(t_17, b_0)), BS_XOR(BS_AND(t_19, b_1), (t_26))), BS_XOR(BS_XOR((t_28), BS_AND(BS_AND(t_0, b_5), b_0)), BS_XOR((t_34), (t_36))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_1, t_18), (t_38)), BS_XOR((t_39), BS_AND(t_1, t_21))), BS_XOR(BS_XOR((t_40), BS_AND(t_2, t_20)), BS_XOR((t_42), BS_AND(t_2, t_23)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_2, t_24), BS_AND(t_3, t_23)), BS_XOR(BS_AND(t_4, t_24), BS_AND(t_6, t_18))), BS_XOR(BS_XOR((t_48), BS_AND(t_6, t_24)), BS_XOR((t_50), BS_AND(t_7, t_21))))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_7, t_23), BS_AND(t_8, t_23)), BS_XOR(BS_AND(t_8, t_24), BS_AND(t_9, t_24))), BS_XOR(BS_XOR((t_52), BS_AND(t_11, t_21)), BS_XOR(BS_AND(t_11, t_23), BS_AND(t_11, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_12, t_24), BS_AND(t_15, t_23)), BS_XOR(BS_AND(t_15, t_24), BS_AND(t_25, b_2))), BS_XOR(BS_XOR(BS_AND(t_25, b_1), BS_AND(t_26, b_0)), BS_XOR(BS_AND(t_27, b_1), BS_AND(t_27, b_0))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_28, b_0), BS_AND(t_29, b_0)), BS_XOR(BS_AND(t_30, b_0), BS_AND(t_31, b_0))), BS_XOR(BS_XOR(BS_AND(t_32, b_0), BS_AND(t_33, b_0)), BS_XOR(BS_AND(t_34, b_0), BS_AND(t_35, b_1)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_35, b_0), BS_AND(t_36, b_1)), BS_XOR(BS_AND(t_37, b_0), BS_AND(t_38, b_0))), BS_XOR(BS_XOR(BS_AND(t_40, b_0), BS_AND(t_41, b_0)), BS_XOR(BS_AND(BS_AND(t_2, t_20), b_0), BS_AND(t_42, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_43, b_0), BS_AND(t_44, b_0)), BS_XOR(BS_AND(BS_AND(t_6, t_17), b_0), BS_AND(t_46, b_1))), BS_XOR(BS_XOR(BS_AND(t_46, b_0), BS_AND(t_47, b_0)), BS_XOR(BS_AND(t_49, b_1), BS_AND(t_49, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_50, b_0), BS_AND(t_52, b_0)), BS_XOR(BS_AND(t_53, b_0), BS_AND(t_54, b_0))), BS_XOR(BS_XOR(BS_AND(t_55, b_0), BS_AND(t_25, t_20)), BS_XOR(BS_AND(t_25, t_21), BS_AND(t_25, t_23))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_25, t_24), BS_AND(t_26, t_22)), BS_XOR(BS_AND(t_29, t_22), BS_AND(t_29, t_23))), BS_XOR(BS_XOR(BS_AND(t_29, t_24), BS_AND(t_32, t_24)), BS_XOR(BS_AND(t_36, t_24), BS_AND(t_38, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_41, t_24), BS_AND(t_44, t_23)), BS_XOR(BS_AND(t_44, t_24), BS_AND(t_45, t_24))), BS_XOR(BS_XOR(BS_AND(BS_AND(t_25, t_20), b_0), BS_AND(BS_AND(t_26, t_22), b_0)), BS_XOR(BS_AND(BS_AND(t_35, t_22), b_0), BS_AND(BS_AND(t_44, t_22), b_0))))))));
}


/* sbox init not needed in all bitsliced mode */
void aycw_init_block(void) {}

/**
   out[i] = in1[i] ^ in2[i]  for i 0...7
*/
AYCW_INLINE void aycw_block_xor8(dvbcsa_bs_word_t *out, dvbcsa_bs_word_t *in1, dvbcsa_bs_word_t *in2)
{
   out[0] = BS_XOR(in1[0], in2[0]);
   out[1] = BS_XOR(in1[1], in2[1]);
   out[2] = BS_XOR(in1[2], in2[2]);
   out[3] = BS_XOR(in1[3], in2[3]);
   out[4] = BS_XOR(in1[4], in2[4]);
   out[5] = BS_XOR(in1[5], in2[5]);
   out[6] = BS_XOR(in1[6], in2[6]);
   out[7] = BS_XOR(in1[7], in2[7]);
}

/**
   Nearly the same as dvbcsa_bs_block_decrypt_register. kkmulti is now 8 times in size.
   @parameter keys[in]  bit sliced key array [56][8][BS_BATCH_BYTES]
   @parameter r         ts input data ib0 + some free space left in front for virtual shift
   https://upload.wikimedia.org/wikipedia/commons/e/e4/Dvbcsa_Block_decrypt_shift.svg
*/
void aycw_block_decrypt(const dvbcsa_bs_word_t* keys, dvbcsa_bs_word_t* r)
{
   int	i,j;

   r += 8 * 56;

   // loop over kk[55]..kk[0]
   for (i = 55; i >= 0; i--)
   {
      // OPTIMIZEME: can this function use less temp batch variables?
      dvbcsa_bs_word_t r6xK[8];       // copy of r6 (before shift) after xoring with keys
      dvbcsa_bs_word_t r7xS[8];       // sbox output xor r7 (before shift), aka 'L'
      dvbcsa_bs_word_t sbox_out[8];   // sbox output
      dvbcsa_bs_word_t perm_temp_0;

      r -= 8;   /* virtual shift of registers */

      aycw_block_xor8(r6xK, &r[8 * 7], (dvbcsa_bs_word_t*)&keys[i * 8]);   // cast to suppress 'const' warning
      aycw_block_sbox(sbox_out, r6xK);
      aycw_block_xor8(r7xS, sbox_out, &r[8 * 8]);

#ifdef BLOCKDEBUG
      {
         // debug dump slice 0 regs before 1st shift
         uint8 dump[9]r6xK_,sout,r7xS_;
         aycw_extractbsdata(r, 0, 64+8, dump);
         aycw_extractbsdata(&r6xK, 0, 8, &r6xK_);
         aycw_extractbsdata(&sbox_out, 0, 8, &sout);
         aycw_extractbsdata(&r7xS, 0, 8, &r7xS_);
         printf("%d: %02x %02x %02x %02x %02x %02x %02x %02x r6xK %02x sout %02x\n", i, dump[1], dump[2], dump[3], dump[4], dump[5], dump[6], dump[7], dump[8], r6xK_, sout);
     }
#endif

      // bit permutation
      perm_temp_0 = sbox_out[0];
      sbox_out[0] = sbox_out[6];
      sbox_out[6] = sbox_out[5];
      sbox_out[5] = sbox_out[2];
      sbox_out[2] = sbox_out[4];
      sbox_out[4] = sbox_out[3];
      sbox_out[3] = sbox_out[7];
      sbox_out[7] = sbox_out[1];
      sbox_out[1] = perm_temp_0;

      // r1, r5, r7 remain at shifted value
      for (j = 0; j < 8; j++) r[8 * 0 + j] = r7xS[j];
      aycw_block_xor8(&r[8 * 2], &r[8 * 2], r7xS);
      aycw_block_xor8(&r[8 * 3], &r[8 * 3], r7xS);
      aycw_block_xor8(&r[8 * 4], &r[8 * 4], r7xS);
      aycw_block_xor8(&r[8 * 6], &r[8 * 6], sbox_out);
   }
}

int aycw_checkPESheader(dvbcsa_bs_word_t *data, dvbcsa_bs_word_t *candidates)
{
   int i, ret;
   dvbcsa_bs_word_t c;

   // check also for 4th byte Audio streams (0xC0-0xDF), Video streams (0xE0-0xEF) ?
   // 0x00 | 0x00 | 0x01^0x01 == 0x00

   c = data[0];
   for (i = 1; i < 16; i++)
   {
      c = BS_OR(data[i], c);
   }

   // OPTIMIZEME: return if 1st two bytes were not 00 00
   c = BS_OR(BS_NOT(data[16]), c);

   for (i = 17; i < 24; i++)
   {
      c = BS_OR(data[i], c);
   }
   // bit in c batch is zero if the slice data is a hit

   // ret is not number of hits, but slice position MOD 32
#if PARALLEL_MODE == PARALLEL_32_INT
   ret  = BS_EXTLS32(c);
#elif PARALLEL_MODE == PARALLEL_128_SSE2
   ret  = BS_EXTLS32(BS_SHR8(c, 0));
   ret &= BS_EXTLS32(BS_SHR8(c, 4));
   ret &= BS_EXTLS32(BS_SHR8(c, 8));
   ret &= BS_EXTLS32(BS_SHR8(c, 12));
#elif PARALLEL_MODE == PARALLEL_256_AVX
   ret  = BS_EXTLS32(BS_SHR8(c, 0));
   ret &= BS_EXTLS32(BS_SHR8(c, 4));
   ret &= BS_EXTLS32(BS_SHR8(c, 8));
   ret &= BS_EXTLS32(BS_SHR8(c, 12));
   ret &= BS_EXTLS32(BS_SHR8(c, 16));
   ret &= BS_EXTLS32(BS_SHR8(c, 20));
   ret &= BS_EXTLS32(BS_SHR8(c, 24));
   ret &= BS_EXTLS32(BS_SHR8(c, 28));
#else
#error wrong parallel mode
#endif

   *candidates = BS_NOT(c);
   return ~ret;
}

#endif //#ifdef USEALLBITSLICE

