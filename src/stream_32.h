#ifndef AYCW_STREAM_32_H_
#define AYCW_STREAM_32_H_

// This header is a self-contained C++ reimplementation of the CSA stream
// cipher for the scalar (PARALLEL_MODE=1) path.  It is mutually exclusive
// with the bs_stream.h / bs_stream.c stack — include one or the other, not
// both, because the struct and typedef names intentionally overlap.
//
// When PARALLEL_MODE != 1 (SSE path), use bs_stream.h + bs_stream.c instead.

#include <cstdint>

#include "config.h"

// Structs mirror those in bs_stream.h; kept here so this header can be
// used standalone without pulling in the C headers.
#ifndef aycw_bs_stream_h   // guard from bs_stream.h
typedef struct aycw_stRegister
{
    dvbcsa_bs_word_t A_BS[80];
    dvbcsa_bs_word_t B_BS[80];
} aycw_stRegister;

typedef struct aycw_stPQXYZ
{
    dvbcsa_bs_word_t BS_P;
    dvbcsa_bs_word_t BS_Q;
    dvbcsa_bs_word_t BS_X[4];
    dvbcsa_bs_word_t BS_Y[4];
    dvbcsa_bs_word_t BS_Z[4];
} aycw_tstPQXYZ;

typedef struct aycw_stCDEF
{
    dvbcsa_bs_word_t BS_C;
    dvbcsa_bs_word_t BS_D[4];
    dvbcsa_bs_word_t BS_E[4];
    dvbcsa_bs_word_t BS_F[4];
} aycw_tstCDEF;
#endif  // !aycw_bs_stream_h

static inline void aycw__vInitShiftRegister(dvbcsa_bs_word_t *BS_key, aycw_stRegister *stRegister)
{
    /******   A & B init ********************/
    /* set everything outside bit 32 to zero */
    /* load A and B into A_BS and B_BS */
    for (int i = 0; i < 32; i += 8)
    {
        stRegister->A_BS[i + 0] = BS_key[i + 4];
        stRegister->A_BS[i + 1] = BS_key[i + 5];
        stRegister->A_BS[i + 2] = BS_key[i + 6];
        stRegister->A_BS[i + 3] = BS_key[i + 7];
        stRegister->A_BS[i + 4] = BS_key[i + 0];
        stRegister->A_BS[i + 5] = BS_key[i + 1];
        stRegister->A_BS[i + 6] = BS_key[i + 2];
        stRegister->A_BS[i + 7] = BS_key[i + 3];
        stRegister->B_BS[i + 0] = BS_key[i + 4 + 32];
        stRegister->B_BS[i + 1] = BS_key[i + 5 + 32];
        stRegister->B_BS[i + 2] = BS_key[i + 6 + 32];
        stRegister->B_BS[i + 3] = BS_key[i + 7 + 32];
        stRegister->B_BS[i + 4] = BS_key[i + 0 + 32];
        stRegister->B_BS[i + 5] = BS_key[i + 1 + 32];
        stRegister->B_BS[i + 6] = BS_key[i + 2 + 32];
        stRegister->B_BS[i + 7] = BS_key[i + 3 + 32];
    }

    for (int i = 32; i < 40; i++)
    {
        stRegister->A_BS[i] = BS_VAL8(0);
        stRegister->B_BS[i] = BS_VAL8(0);
    }
}

inline static void aycw__ShiftRegisterLeft(dvbcsa_bs_word_t *RegisterValue, uint8_t u8ShiftValue, uint8_t u8Arraysize)
{
    uint8_t i;
    /* move first */
    for (i = (u8Arraysize - 1); i >= u8ShiftValue; i--)
    {
        RegisterValue[i] = RegisterValue[i - u8ShiftValue];
    }
    /* set rest to zero... */
    for (i = 0; i < u8ShiftValue; i++)
    {
        RegisterValue[i] = BS_VAL8(00);
    }
}
/* error C2719: '*fd': formal parameter with __declspec(align('16')) won't be aligned
Q: Can't align(16) structures be used as parameters?
A: Why would you want to pass a (large?) structure by value anyway?
Why not pass a pointer (or a reference) to the structure, then it will maintain its alignment since it's not actually copied anywhere.
*/
static inline void aycw_bs_stream_sbox1(dvbcsa_bs_word_t *fa, dvbcsa_bs_word_t *fb,
               dvbcsa_bs_word_t *fc, 
               dvbcsa_bs_word_t *fd,
               dvbcsa_bs_word_t *fe,
               dvbcsa_bs_word_t *sa, 
               dvbcsa_bs_word_t *sb)
{
  dvbcsa_bs_word_t tmp0, tmp1, tmp2, tmp3;

  tmp0 = BS_XOR (*fa, BS_XOR (*fb, BS_NOT (BS_OR (BS_XOR (BS_OR (*fa, *fb), *fc), BS_XOR (*fc, *fd)))));
  tmp1 = BS_XOR (BS_OR (*fa, *fb), BS_NOT (BS_AND (*fc, BS_OR (*fa, BS_XOR (*fb, *fd)))));
  tmp2 = BS_XOR (*fa, BS_XOR (BS_AND (*fb, *fd), BS_OR (BS_AND (*fa, *fd), *fc)));
  tmp3 = BS_XOR (BS_AND (*fa, *fc), BS_XOR (*fa, BS_OR (BS_AND (*fa, *fb), *fd)));

  *sa = BS_XOR (tmp0, BS_AND (*fe, tmp1));
  *sb = BS_XOR (tmp2, BS_AND (*fe, tmp3));
}

static inline void aycw_bs_stream_sbox2(dvbcsa_bs_word_t *fa, dvbcsa_bs_word_t *fb,
               dvbcsa_bs_word_t *fc, dvbcsa_bs_word_t *fd,
               dvbcsa_bs_word_t *fe,
               dvbcsa_bs_word_t *sa, dvbcsa_bs_word_t *sb)
{
  dvbcsa_bs_word_t tmp0, tmp1, tmp2, tmp3;

  tmp0 = BS_XOR (*fa, BS_XOR (BS_AND (*fb, BS_OR (*fc, *fd)), BS_XOR (*fc, BS_NOT (*fd))));
  tmp1 = BS_OR (BS_AND (*fa, BS_XOR (*fb, *fd)), BS_AND (BS_OR (*fa, *fb), *fc));
  tmp2 = BS_XOR (BS_AND (*fb, *fd), BS_OR (BS_AND (*fa, *fd), BS_XOR (*fb, BS_NOT (*fc))));
  tmp3 = BS_OR (BS_AND (*fa, *fd), BS_XOR (*fa, BS_XOR (*fb, BS_AND (*fc, *fd))));

  *sa = BS_XOR (tmp0, BS_AND (*fe, tmp1));
  *sb = BS_XOR (tmp2, BS_AND (*fe, tmp3));
}

static inline void aycw_bs_stream_sbox3(dvbcsa_bs_word_t *fa, dvbcsa_bs_word_t *fb,
               dvbcsa_bs_word_t *fc, dvbcsa_bs_word_t *fd,
               dvbcsa_bs_word_t *fe,
               dvbcsa_bs_word_t *sa, dvbcsa_bs_word_t *sb)
{
  dvbcsa_bs_word_t tmp0, tmp1, tmp2;

  tmp0 = BS_XOR (*fa, BS_XOR (*fb, BS_XOR (BS_AND (*fc, BS_OR (*fa, *fd)), *fd)));
  tmp1 = BS_XOR (BS_AND (*fa, *fc), BS_OR (BS_XOR (*fa, *fd), BS_XOR (BS_OR (*fb, *fc), BS_NOT (*fd))));
  tmp2 = BS_XOR (*fa, BS_XOR (BS_AND (BS_XOR (*fb, *fc), *fd), *fc));

  *sa = BS_XOR (tmp0, BS_AND (BS_NOT (*fe), tmp1));
  *sb = BS_XOR (tmp2, *fe);
}

static inline void aycw_bs_stream_sbox4(dvbcsa_bs_word_t *fa, dvbcsa_bs_word_t *fb,
               dvbcsa_bs_word_t *fc, dvbcsa_bs_word_t *fd,
               dvbcsa_bs_word_t *fe,
               dvbcsa_bs_word_t *sa, dvbcsa_bs_word_t *sb)
{
  dvbcsa_bs_word_t tmp0, tmp1, tmp2;

  tmp0 = BS_XOR (*fa, BS_OR (BS_AND (*fc, BS_XOR (*fa, *fd)), BS_XOR (*fb, BS_OR (*fc, BS_NOT (*fd)))));
  tmp1 = BS_XOR (BS_AND (*fa, *fb), BS_XOR (*fb, BS_XOR (BS_AND (BS_OR (*fa, *fc), *fd), *fc)));
  tmp2 = BS_XOR (*fa, BS_OR (BS_AND (*fb, *fc), BS_XOR (BS_OR (BS_AND (*fa, BS_XOR (*fb, *fd)), *fc), *fd)));

  *sa = BS_XOR (tmp0, BS_AND (*fe, BS_XOR (tmp1, tmp0)));
  *sb = BS_XOR (BS_XOR (*sa, tmp2), *fe);
}

static inline void aycw_bs_stream_sbox5(dvbcsa_bs_word_t *fa, dvbcsa_bs_word_t *fb,
               dvbcsa_bs_word_t *fc, dvbcsa_bs_word_t *fd,
               dvbcsa_bs_word_t *fe,
               dvbcsa_bs_word_t *sa, dvbcsa_bs_word_t *sb)
{
  dvbcsa_bs_word_t tmp0, tmp1, tmp2, tmp3;

  tmp0 = BS_OR (BS_XOR (BS_AND (*fa, BS_OR (*fb, *fc)), *fb), BS_XOR (BS_OR (BS_XOR (*fa, *fc), *fd), BS_VAL8(FF)));
  tmp1 = BS_XOR (*fb, BS_AND (BS_XOR (*fc, *fd), BS_XOR (*fc, BS_OR (*fb, BS_XOR (*fa, *fd)))));
  tmp2 = BS_XOR (BS_AND (*fa, *fc), BS_XOR (*fb, BS_AND (BS_OR (*fb, BS_XOR (*fa, *fc)), *fd)));
  tmp3 = BS_OR (BS_AND (BS_XOR (*fa, *fb), BS_XOR (*fc, BS_VAL8(FF))), *fd);

  *sa = BS_XOR (tmp0, BS_AND (*fe, tmp1));
  *sb = BS_XOR (tmp2, BS_AND (*fe, tmp3));
}

static inline void aycw_bs_stream_sbox6(dvbcsa_bs_word_t *fa, dvbcsa_bs_word_t *fb,
               dvbcsa_bs_word_t *fc, dvbcsa_bs_word_t *fd,
               dvbcsa_bs_word_t *fe,
               dvbcsa_bs_word_t *sa, dvbcsa_bs_word_t *sb)
{
  dvbcsa_bs_word_t tmp0, tmp1, tmp2, tmp3;

  tmp0 = BS_XOR (BS_AND (BS_AND (*fa, *fc), *fd), BS_XOR (BS_AND (*fb, BS_OR (*fa, *fd)), *fc));
  tmp1 = BS_NOT (BS_AND (BS_XOR (*fa, *fc), *fd));
  tmp2 = BS_XOR (BS_AND (*fa, BS_OR (*fb, *fc)), BS_XOR (*fb, BS_OR (BS_AND (*fb, *fc), *fd)));
  tmp3 = BS_AND (*fc, BS_XOR (BS_AND (*fa, BS_XOR (*fb, *fd)), BS_OR (*fb, *fd)));

  *sa = BS_XOR (tmp0, BS_AND (*fe, tmp1));
  *sb = BS_XOR (tmp2, BS_AND (*fe, tmp3));
}

static inline void aycw_bs_stream_sbox7(dvbcsa_bs_word_t *fa, dvbcsa_bs_word_t *fb,
               dvbcsa_bs_word_t *fc, dvbcsa_bs_word_t *fd,
               dvbcsa_bs_word_t *fe,
               dvbcsa_bs_word_t *sa, dvbcsa_bs_word_t *sb)
{
  dvbcsa_bs_word_t tmp0, tmp1, tmp2, tmp3;

  tmp0 = BS_XOR (*fb, BS_OR (BS_AND (*fc, *fd), BS_XOR (*fa, BS_XOR (*fc, *fd))));
  tmp1 = BS_AND (BS_OR (*fb, *fd), BS_OR (BS_AND (*fa, *fc), BS_XOR (*fb, BS_XOR (*fc, *fd))));
  tmp2 = BS_XOR (BS_OR (*fa, *fb), BS_XOR (BS_AND (*fc, BS_OR (*fb, *fd)), *fd));
  tmp3 = BS_OR (*fd, BS_XOR (BS_AND (*fa, *fc), BS_VAL8(FF)));

  *sa = BS_XOR (tmp0, BS_AND (*fe, tmp1));
  *sb = BS_XOR (tmp2, BS_AND (*fe, tmp3));
}


void aycw__vCaculatePQXYZ(dvbcsa_bs_word_t * A_local, aycw_tstPQXYZ * stPQXYZ)
{
    aycw_bs_stream_sbox1(&A_local[6],  &A_local[25], &A_local[31], &A_local[36], &A_local[16], &stPQXYZ->BS_X[0], &stPQXYZ->BS_Z[2]);
    aycw_bs_stream_sbox2(&A_local[14], &A_local[27], &A_local[28], &A_local[37], &A_local[9],  &stPQXYZ->BS_X[1], &stPQXYZ->BS_Z[3]);
    aycw_bs_stream_sbox3(&A_local[8],  &A_local[21], &A_local[23], &A_local[26], &A_local[7],  &stPQXYZ->BS_Y[0], &stPQXYZ->BS_X[2]);
    aycw_bs_stream_sbox4(&A_local[5],  &A_local[11], &A_local[18], &A_local[32], &A_local[15], &stPQXYZ->BS_Y[1], &stPQXYZ->BS_X[3]);
    aycw_bs_stream_sbox5(&A_local[19], &A_local[24], &A_local[33], &A_local[38], &A_local[22], &stPQXYZ->BS_Z[0], &stPQXYZ->BS_Y[2]);
    aycw_bs_stream_sbox6(&A_local[17], &A_local[20], &A_local[30], &A_local[39], &A_local[13], &stPQXYZ->BS_Z[1], &stPQXYZ->BS_Y[3]);
    aycw_bs_stream_sbox7(&A_local[12], &A_local[29], &A_local[34], &A_local[35], &A_local[10], &stPQXYZ->BS_P ,   &stPQXYZ->BS_Q);
}


static inline void aycw__vInitRound(uint8_t j, uint8_t u8Byte,
                                    aycw_tstPQXYZ *stPQXYZ,
                                    aycw_tstCDEF *stCDEF,
                                    aycw_stRegister *stRegister,
                                    dvbcsa_bs_word_t *bs_data_sb0)
{

    uint8_t k, b;
    dvbcsa_bs_word_t tmp0, tmp4, tmp1, tmp3;
    dvbcsa_bs_word_t BS_TMP_B[5];
    dvbcsa_bs_word_t BS_TMP_B_Result[4];
    dvbcsa_bs_word_t BS_Bout[4];
    dvbcsa_bs_word_t BS_Enew[4];

    aycw__ShiftRegisterLeft(stRegister->A_BS, 4, 44);
    for (k = 0; k < 4; k++)
    {
        stRegister->A_BS[k] = stRegister->A_BS[k + 40];
        BS_XOREQ(stRegister->A_BS[k], stPQXYZ->BS_X[k]);
        BS_XOREQ(stRegister->A_BS[k], stCDEF->BS_D[k]); /* data, byte, bit */
        BS_XOREQ(stRegister->A_BS[k], ((j & 0x1) ? bs_data_sb0[k + u8Byte * 8] : bs_data_sb0[k + 4 + u8Byte * 8]));
    }

    for (k = 0; k < 4; k++)
    {

        BS_TMP_B[k] = BS_XOR(stRegister->B_BS[k + 6 * 4], stRegister->B_BS[k + 9 * 4]);
        BS_TMP_B[k] = BS_XOR(BS_TMP_B[k], stPQXYZ->BS_Y[k]);
        // OPTIMIZEME: optimizeable by using the upper A_tBITVALUE calculation
        /*tmp = (j & 0x1)?bs_data_sb0[k+4+u8Byte*8]:bs_data_sb0[k+u8Byte*8];
        tmp0 = aycw__BitExpandOfByteToBsWord(&u32IV, 0, k);
        if (tmp0 != tmp)
            return 0;*/
        BS_TMP_B[k] = BS_XOR(BS_TMP_B[k], ((j & 0x1) ? bs_data_sb0[k + 4 + u8Byte * 8] : bs_data_sb0[k + u8Byte * 8]));
        BS_TMP_B_Result[k] = BS_AND(BS_TMP_B[k], BS_NOT(stPQXYZ->BS_P)); // Die nicht zu rotierenden Daten zun�chst zwischenspeichern.
    }
    /* yet rotate B */
    /* OPTIMIZEME: write into B directly?? */
    for (k = 4; k > 0; k--)
    {
        BS_TMP_B[k] = BS_TMP_B[k - 1];
    }
    BS_TMP_B[0] = BS_TMP_B[4];

    /* B must not be moved by 4 before handling is finished */
    aycw__ShiftRegisterLeft(stRegister->B_BS, 4, 44);

    /* now write the result, both rotated and unrotated */
    for (k = 0; k < 4; k++)
    { // rotated         //not rotated
        stRegister->B_BS[k] = BS_OR(BS_AND(BS_TMP_B[k], stPQXYZ->BS_P), BS_TMP_B_Result[k]);
    }

    /********** Combiner calculation **********/

    BS_Bout[3] = BS_XOR(BS_XOR(stRegister->B_BS[12], stRegister->B_BS[25]), BS_XOR(stRegister->B_BS[30], stRegister->B_BS[39]));
    BS_Bout[2] = BS_XOR(BS_XOR(stRegister->B_BS[24], stRegister->B_BS[33]), BS_XOR(stRegister->B_BS[15], stRegister->B_BS[18]));
    BS_Bout[1] = BS_XOR(BS_XOR(stRegister->B_BS[23], stRegister->B_BS[34]), BS_XOR(stRegister->B_BS[16], stRegister->B_BS[21]));
    BS_Bout[0] = BS_XOR(BS_XOR(stRegister->B_BS[38], stRegister->B_BS[27]), BS_XOR(stRegister->B_BS[13], stRegister->B_BS[32]));

    /* calc D */
    for (k = 0; k < 4; k++)
    {
        stCDEF->BS_D[k] = (BS_Bout[k] ^ stCDEF->BS_E[k]) ^ stPQXYZ->BS_Z[k];
    }

    for (b = 0; b < 4; b++)
        BS_Enew[b] = stCDEF->BS_F[b];

    tmp0 = BS_XOR(stPQXYZ->BS_Z[0], stCDEF->BS_E[0]);
    tmp1 = BS_AND(stPQXYZ->BS_Z[0], stCDEF->BS_E[0]);
    stCDEF->BS_F[0] = BS_XOR(stCDEF->BS_E[0], BS_AND(stPQXYZ->BS_Q, BS_XOR(stPQXYZ->BS_Z[0], stCDEF->BS_C)));
    tmp3 = BS_AND(tmp0, stCDEF->BS_C);
    tmp4 = BS_OR(tmp1, tmp3);

    tmp0 = BS_XOR(stPQXYZ->BS_Z[1], stCDEF->BS_E[1]);
    tmp1 = BS_AND(stPQXYZ->BS_Z[1], stCDEF->BS_E[1]);
    stCDEF->BS_F[1] = BS_XOR(stCDEF->BS_E[1], BS_AND(stPQXYZ->BS_Q, BS_XOR(stPQXYZ->BS_Z[1], tmp4)));
    tmp3 = BS_AND(tmp0, tmp4);
    tmp4 = BS_OR(tmp1, tmp3);

    tmp0 = BS_XOR(stPQXYZ->BS_Z[2], stCDEF->BS_E[2]);
    tmp1 = BS_AND(stPQXYZ->BS_Z[2], stCDEF->BS_E[2]);
    stCDEF->BS_F[2] = BS_XOR(stCDEF->BS_E[2], BS_AND(stPQXYZ->BS_Q, BS_XOR(stPQXYZ->BS_Z[2], tmp4)));
    tmp3 = BS_AND(tmp0, tmp4);
    tmp4 = BS_OR(tmp1, tmp3);

    tmp0 = BS_XOR(stPQXYZ->BS_Z[3], stCDEF->BS_E[3]);
    tmp1 = BS_AND(stPQXYZ->BS_Z[3], stCDEF->BS_E[3]);
    stCDEF->BS_F[3] = BS_XOR(stCDEF->BS_E[3], BS_AND(stPQXYZ->BS_Q, BS_XOR(stPQXYZ->BS_Z[3], tmp4)));
    tmp3 = BS_AND(tmp0, tmp4);
    stCDEF->BS_C = BS_XOR(stCDEF->BS_C, BS_AND(stPQXYZ->BS_Q, BS_XOR(BS_OR(tmp1, tmp3), stCDEF->BS_C))); // ultimate carry

    for (b = 0; b < 4; b++)
        stCDEF->BS_E[b] = BS_Enew[b];

    aycw__vCaculatePQXYZ(stRegister->A_BS, stPQXYZ);
}



/****** execute round **************/
static inline void aycw__vRound(aycw_tstPQXYZ     *stPQXYZ,
                              aycw_tstCDEF      *stCDEF ,
                              aycw_stRegister  *stRegister,
                              dvbcsa_bs_word_t  *BS_Streambit0,
                              dvbcsa_bs_word_t  *BS_Streambit1)
{

    uint8_t k,b;
    dvbcsa_bs_word_t tmp0,tmp4,tmp1,tmp3;
    dvbcsa_bs_word_t  BS_TMP_B[5];
    dvbcsa_bs_word_t  BS_TMP_B_Result[4];
    dvbcsa_bs_word_t  BS_Bout[4];
    dvbcsa_bs_word_t  BS_Enew[4];

    aycw__ShiftRegisterLeft( stRegister->A_BS, 4, 80);
    for (k = 0; k < 4; k++)
    {
        stRegister->A_BS[k] = stRegister->A_BS[k+40]; 
        BS_XOREQ(stRegister->A_BS[k], stPQXYZ->BS_X[k]);

    }

    for (k = 0; k < 4; k++)
    {

        //A: 0x0000000bb6510468 
        //B: 0x00000006baf6a610
    //  BS_ConverToDoubleArray(&tmpDoubleArray,B_BS,40);
        BS_TMP_B[k] = BS_XOR(stRegister->B_BS[k+6*4], stRegister->B_BS[k+9*4]);
    //  BS_ConverToByteArray(&tmpByteArray,BS_TMP_B,4);
        BS_TMP_B[k] = BS_XOR(BS_TMP_B[k], stPQXYZ->BS_Y[k]);
    //  BS_ConverToIntArray(tmpIntArray,B_BS,40);
#if Init    
        BS_TMP_B[k] = BS_TMP_B[k] ^ A_tBITVALUE(&u32IV, 0, k);   
        BS_ConverToIntArray(tmpIntArray,B_BS,40);
#endif
        BS_TMP_B_Result[k]  = BS_AND(BS_TMP_B[k], BS_NOT(stPQXYZ->BS_P)); //Die nicht zu rotierenden Daten zun�chst zwischenspeichern.

    //  BS_ConverToIntArray(tmpIntArray,B_BS,40);
        //B: 0x00000006baf6a617

    }
    for (k = 4; k > 0; k--)
    {
        BS_TMP_B[k] = BS_TMP_B[k-1];
    }
    BS_TMP_B[0] = BS_TMP_B[4];

    aycw__ShiftRegisterLeft( stRegister->B_BS, 4, 80);

    for ( k = 0; k < 4; k++)
    {
        stRegister->B_BS[k] = BS_OR(BS_AND(BS_TMP_B[k], stPQXYZ->BS_P), BS_TMP_B_Result[k]);
    }

    BS_Bout[3] = BS_XOR(BS_XOR(stRegister->B_BS[12], stRegister->B_BS[25]), BS_XOR(stRegister->B_BS[30], stRegister->B_BS[39]));
    BS_Bout[2] = BS_XOR(BS_XOR(stRegister->B_BS[24], stRegister->B_BS[33]), BS_XOR(stRegister->B_BS[15], stRegister->B_BS[18]));
    BS_Bout[1] = BS_XOR(BS_XOR(stRegister->B_BS[23], stRegister->B_BS[34]), BS_XOR(stRegister->B_BS[16], stRegister->B_BS[21]));
    BS_Bout[0] = BS_XOR(BS_XOR(stRegister->B_BS[38], stRegister->B_BS[27]), BS_XOR(stRegister->B_BS[13], stRegister->B_BS[32]));

    /* calc of D */
    for (k = 0; k < 4; k++)
    {
        /*   use old E????*/
        stCDEF->BS_D[k] = BS_XOR(BS_XOR(BS_Bout[k], stCDEF->BS_E [k]), stPQXYZ->BS_Z[k]);

    }

    for (b = 0; b < 4; b++)
        BS_Enew[b] = stCDEF->BS_F[b];

    tmp0 = BS_XOR (stPQXYZ->BS_Z[0], stCDEF->BS_E[0]);
    tmp1 = BS_AND (stPQXYZ->BS_Z[0], stCDEF->BS_E[0]);
    stCDEF->BS_F[0] = BS_XOR (stCDEF->BS_E[0], BS_AND (stPQXYZ->BS_Q, BS_XOR (stPQXYZ->BS_Z[0], stCDEF->BS_C)));
    tmp3 = BS_AND (tmp0, stCDEF->BS_C);
    tmp4 = BS_OR (tmp1, tmp3);

    tmp0 = BS_XOR (stPQXYZ->BS_Z[1], stCDEF->BS_E[1]);
    tmp1 = BS_AND (stPQXYZ->BS_Z[1], stCDEF->BS_E[1]);
    stCDEF->BS_F[1] = BS_XOR (stCDEF->BS_E[1], BS_AND (stPQXYZ->BS_Q, BS_XOR (stPQXYZ->BS_Z[1], tmp4)));
    tmp3 = BS_AND (tmp0, tmp4);
    tmp4 = BS_OR (tmp1, tmp3);

    tmp0 = BS_XOR (stPQXYZ->BS_Z[2], stCDEF->BS_E[2]);
    tmp1 = BS_AND (stPQXYZ->BS_Z[2], stCDEF->BS_E[2]);
    stCDEF->BS_F[2] = BS_XOR (stCDEF->BS_E[2], BS_AND (stPQXYZ->BS_Q, BS_XOR (stPQXYZ->BS_Z[2], tmp4)));
    tmp3 = BS_AND (tmp0, tmp4);
    tmp4 = BS_OR (tmp1, tmp3);

    tmp0 = BS_XOR (stPQXYZ->BS_Z[3], stCDEF->BS_E[3]);
    tmp1 = BS_AND (stPQXYZ->BS_Z[3], stCDEF->BS_E[3]);
    stCDEF->BS_F[3] = BS_XOR (stCDEF->BS_E[3], BS_AND (stPQXYZ->BS_Q, BS_XOR (stPQXYZ->BS_Z[3], tmp4)));
    tmp3 = BS_AND (tmp0, tmp4);
    stCDEF->BS_C = BS_XOR (stCDEF->BS_C, BS_AND (stPQXYZ->BS_Q, BS_XOR (BS_OR (tmp1, tmp3), stCDEF->BS_C)));    // ultimate carry

    for (b = 0; b < 4; b++)
        stCDEF->BS_E[b] = BS_Enew[b];

    aycw__vCaculatePQXYZ(stRegister->A_BS, stPQXYZ);

    /* calc stream bit */
    *BS_Streambit0 = BS_XOR(stCDEF->BS_D[0],  stCDEF->BS_D[1]);
    *BS_Streambit1 = BS_XOR(stCDEF->BS_D[2],  stCDEF->BS_D[3]);

}


/**
set up data used for stream. Depends on scrambled data only, so can be global
@param   data_return[out]     bit sliced output data IB1
@param   outbits[in]          number of bits to calculate
@param   BS_key[in]           bit sliced key array 64 elements
@param   bs_data_sb0[in]      bit sliced 1st data block used for initialization
*/
static inline void aycwStreamDecrypt(uint32_t *data_return, unsigned int outbits, uint32_t *BS_key, uint32_t *bs_data_sb0)
{
    unsigned int i;
    uint32_t BS_Streambit0, BS_Streambit1;
    aycw_stRegister stRegister;
    aycw_tstPQXYZ stPQXYZ =
        {
            0,
            0,
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0};
    aycw_tstCDEF stCDEF =
        {
            0,
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0};

    /* init A and B - bs */
    aycw__vInitShiftRegister(BS_key, &stRegister);
    /* init A and B - bs */
    for (i = 0; i < 8; i++)
    {
        aycw__vInitRound(0, i, &stPQXYZ, &stCDEF, &stRegister, bs_data_sb0);
        aycw__vInitRound(1, i, &stPQXYZ, &stCDEF, &stRegister, bs_data_sb0);
        aycw__vInitRound(2, i, &stPQXYZ, &stCDEF, &stRegister, bs_data_sb0);
        aycw__vInitRound(3, i, &stPQXYZ, &stCDEF, &stRegister, bs_data_sb0);
    }

    /* OPTIMIZEME: add blokc+stream xor + PES check, to stop bit generation immediately if foreseeable its no PES header */
    for (i = 0; i < outbits; i += 8)
    {
        aycw__vRound(&stPQXYZ, &stCDEF, &stRegister, &BS_Streambit0, &BS_Streambit1);
        data_return[i + 6] = BS_XOR(bs_data_sb0[i + 64 + 6], BS_Streambit0);
        data_return[i + 7] = BS_XOR(bs_data_sb0[i + 64 + 7], BS_Streambit1);
        aycw__vRound(&stPQXYZ, &stCDEF, &stRegister, &BS_Streambit0, &BS_Streambit1);
        data_return[i + 4] = BS_XOR(bs_data_sb0[i + 64 + 4], BS_Streambit0);
        data_return[i + 5] = BS_XOR(bs_data_sb0[i + 64 + 5], BS_Streambit1);
        aycw__vRound(&stPQXYZ, &stCDEF, &stRegister, &BS_Streambit0, &BS_Streambit1);
        data_return[i + 2] = BS_XOR(bs_data_sb0[i + 64 + 2], BS_Streambit0);
        data_return[i + 3] = BS_XOR(bs_data_sb0[i + 64 + 3], BS_Streambit1);
        aycw__vRound(&stPQXYZ, &stCDEF, &stRegister, &BS_Streambit0, &BS_Streambit1);
        data_return[i + 0] = BS_XOR(bs_data_sb0[i + 64 + 0], BS_Streambit0);
        data_return[i + 1] = BS_XOR(bs_data_sb0[i + 64 + 1], BS_Streambit1);
    }
}

static inline void aycw_bit2byteslice(dvbcsa_bs_word_t *data, int count)
{
   int i, j, k;
   dvbcsa_bs_word_t  *p = data;

   for (k = 0; k < 8 * count; k++)
   {
      for (i = 0; i < 4; i++)
      {
         dvbcsa_bs_word_t t, b;

         t = p[i];
         b = p[4 + i];
         p[i    ] = BS_OR(BS_AND(t, BS_VAL32(0000ffff)), BS_SHL8(BS_AND(b, BS_VAL32(0000ffff)), 2));
         p[4 + i] = BS_OR(BS_AND(b, BS_VAL32(ffff0000)), BS_SHR8(BS_AND(t, BS_VAL32(ffff0000)), 2));
      }

      for (j = 0; j < 8; j += 4)
      {
         dvbcsa_bs_word_t t, b;

         for (i = 0; i < 2; i++)
         {
            t = p[j + i];
            b = p[j + 2 + i];
            p[j + i    ] = BS_OR(BS_AND(t, BS_VAL16(00ff)), BS_SHL8(BS_AND(b, BS_VAL16(00ff)), 1));
            p[j + i + 2] = BS_OR(BS_AND(b, BS_VAL16(ff00)), BS_SHR8(BS_AND(t, BS_VAL16(ff00)), 1));
         }
      }

      for (j = 0; j < 8; j += 2)
      {
         dvbcsa_bs_word_t t, b;

         t = p[j];
         b = p[j + 1];
         p[j    ] = BS_OR(BS_AND(t, BS_VAL8(0f)), BS_SHL(BS_AND(b, BS_VAL8(0f)), 4)); //(t & 0x0f0f0f0f) | ((b & 0x0f0f0f0f) << 4);
         p[j + 1] = BS_OR(BS_AND(b, BS_VAL8(f0)), BS_SHR(BS_AND(t, BS_VAL8(f0)), 4));//((t & 0xf0f0f0f0) >> 4) | (b & 0xf0f0f0f0);
      }

      for (j = 0; j < 8; j++)
      {
         dvbcsa_bs_word_t t;

         t = p[j];

         t = BS_OR(
            BS_AND(t, BS_VAL32(cccc3333)),
            BS_OR(
            BS_SHR(BS_AND(t, BS_VAL32(33330000)), 14),
            BS_SHL(BS_AND(t, BS_VAL32(0000cccc)), 14)
            )
            );

         t = BS_OR(
            BS_AND(t, BS_VAL16(aa55)),
            BS_OR(
            BS_SHR(BS_AND(t, BS_VAL16(5500)), 7),
            BS_SHL(BS_AND(t, BS_VAL16(00aa)), 7)
            )
            );

         t = BS_OR(BS_AND(t, BS_VAL8(81)),

            BS_OR(BS_SHR(BS_AND(t, BS_VAL8(10)), 3),
            BS_OR(BS_SHR(BS_AND(t, BS_VAL8(20)), 2),
            BS_OR(BS_SHR(BS_AND(t, BS_VAL8(40)), 1),

            BS_OR(BS_SHL(BS_AND(t, BS_VAL8(02)), 1),
            BS_OR(BS_SHL(BS_AND(t, BS_VAL8(04)), 2),
            BS_SHL(BS_AND(t, BS_VAL8(08)), 3)))))));

         p[j] = t;
      }  /* for (j = 0; j < 8; j++) */
      p += 8;
   } /* for (k=0; k<8 ; k++) */
}

static const uint8_t bf_key_perm[64] = {19, 27, 55, 46,  1, 15, 36, 22, 56, 61, 39, 21, 54, 58, 50, 28, 7, 29, 51,  6, 33, 35, 20, 16, 47, 30, 32, 63, 10, 11,  4, 38, 62, 26, 40, 18, 12, 52, 37, 53, 23, 59, 41, 17, 31,  0, 25, 43, 44, 14,  2, 13, 45, 48,  3, 60, 49,  8, 34,  5,  9, 42, 57, 24,};

static inline void aycw_block_key_perm(dvbcsa_bs_word_t* in, dvbcsa_bs_word_t* out)
{

/* csa block key schedule bit permutation */
#define CPY(a,b) (out)[(b)] = (in)[(a)]
CPY(0	,19 );
CPY(1	,27 );
CPY(2	,55 );
CPY(3	,46 );
CPY(4	,1  );
CPY(5	,15 );
CPY(6	,36 );
CPY(7	,22 );
CPY(8	,56 );
CPY(9	,61 );
CPY(10,39 );
CPY(11,21 );
CPY(12,54 );
CPY(13,58 );
CPY(14,50 );
CPY(15,28 );
CPY(16,7  );
CPY(17,29 );
CPY(18,51 );
CPY(19,6  );
CPY(20,33 );
CPY(21,35 );
CPY(22,20 );
CPY(23,16 );
CPY(24,47 );
CPY(25,30 );
CPY(26,32 );
CPY(27,63 );
CPY(28,10 );
CPY(29,11 );
CPY(30,4  );
CPY(31,38 );
CPY(32,62 );
CPY(33,26 );
CPY(34,40 );
CPY(35,18 );
CPY(36,12 );
CPY(37,52 );
CPY(38,37 );
CPY(39,53 );
CPY(40,23 );
CPY(41,59 );
CPY(42,41 );
CPY(43,17 );
CPY(44,31 );
CPY(45,0  );
CPY(46,25 );
CPY(47,43 );
CPY(48,44 );
CPY(49,14 );
CPY(50,2  );
CPY(51,13 );
CPY(52,45 );
CPY(53,48 );
CPY(54,3  );
CPY(55,60 );
CPY(56,49 );
CPY(57,8  );
CPY(58,34 );
CPY(59,5  );
CPY(60,9  );
CPY(61,42 );
CPY(62,57 );
CPY(63,24 );
}


/**
   out[i] = in1[i] ^ in2[i]  for i 0...7
*/
static inline void aycw_block_xor8(dvbcsa_bs_word_t *out, dvbcsa_bs_word_t *in1, dvbcsa_bs_word_t *in2)
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
 Calculates expanded key 
 @parameter keys[in]    input bitsliced key array BS_BATCH_SIZE x 64
            kk[out]     output bytesliced expanded key array BS_BATCH_SIZE x 448
*/
static inline void aycw_block_key_schedule(const dvbcsa_bs_word_t* keys, dvbcsa_bs_word_t* kk)
{
   int i,j;

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
            kk[1*64 + j*8 + 0] 
				= 
				BS_NOT(
				kk[1*64 + j*8 + 0]
				);
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
static inline void aycw_block_sbox(dvbcsa_bs_word_t *out, dvbcsa_bs_word_t *in)
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

      /* Equations were taken from da_diett.pdf chpt. 3.1.2 / A.1 (thanks Markus)
        OPTIMIZEME: check if other boolean representation are faster...
        "After a large number of time measurement experiments the algebraic normal form seems to be the fastest representation" */
#define BOOLMODE 1
#if BOOLMODE==0
      /* da_diett XOR - nested deep */
      out[7] = BS_XOR((b_5), BS_XOR((b_4), BS_XOR((b_3), BS_XOR((b_0), BS_XOR((t_0), BS_XOR((t_4), BS_XOR((t_5), BS_XOR((BS_AND(b_7, b_0)), BS_XOR((t_6), BS_XOR((t_7), BS_XOR((t_8), BS_XOR((t_9), BS_XOR((t_10), BS_XOR((t_11), BS_XOR((t_12), BS_XOR((t_14), BS_XOR((t_15), BS_XOR((t_16), BS_XOR((t_17), BS_XOR((t_21), BS_XOR((t_22), BS_XOR((BS_AND(t_0, b_3)), BS_XOR((BS_AND(t_1, b_3)), BS_XOR((BS_AND(t_1, b_2)), BS_XOR((BS_AND(t_1, b_1)), BS_XOR((BS_AND(t_2, b_3)), BS_XOR((BS_AND(t_2, b_1)), BS_XOR((BS_AND(t_3, b_1)), BS_XOR((BS_AND(t_4, b_1)), BS_XOR((BS_AND(t_4, b_0)), BS_XOR((BS_AND(t_6, b_4)), BS_XOR((BS_AND(t_6, b_0)), BS_XOR((BS_AND(t_7, b_1)), BS_XOR((BS_AND(t_8, b_0)), BS_XOR((BS_AND(t_9, b_1)), BS_XOR((BS_AND(t_12, b_0)), BS_XOR((BS_AND(t_14, b_0)), BS_XOR((BS_AND(t_15, b_1)), BS_XOR((BS_AND(t_15, b_0)), BS_XOR((BS_AND(t_16, b_1)), BS_XOR((BS_AND(t_16, b_0)), BS_XOR((BS_AND(t_20, b_0)), BS_XOR((t_26), BS_XOR((BS_AND(BS_AND(t_0, b_5), b_0)), BS_XOR((t_30), BS_XOR((t_31), BS_XOR((t_32), BS_XOR((BS_AND(t_0, t_24)), BS_XOR((t_35), BS_XOR((t_36), BS_XOR((BS_AND(t_1, t_18)), BS_XOR((t_38), BS_XOR((BS_AND(t_1, t_21)), BS_XOR((BS_AND(t_1, t_23)), BS_XOR((t_41), BS_XOR((BS_AND(t_2, t_20)), BS_XOR((BS_AND(t_2, t_21)), BS_XOR((t_42), BS_XOR((BS_AND(t_2, t_23)), BS_XOR((t_43), BS_XOR((BS_AND(t_3, t_23)), BS_XOR((BS_AND(t_3, t_24)), BS_XOR((BS_AND(t_4, t_24)), BS_XOR((BS_AND(t_6, t_17)), BS_XOR((t_47), BS_XOR((BS_AND(t_6, t_21)), BS_XOR((BS_AND(t_6, t_24)), BS_XOR((t_49), BS_XOR((BS_AND(t_7, t_21)), BS_XOR((t_51), BS_XOR((BS_AND(t_7, t_23)), BS_XOR((BS_AND(t_8, t_23)), BS_XOR((BS_AND(t_9, t_24)), BS_XOR((t_53), BS_XOR((BS_AND(t_11, t_21)), BS_XOR((BS_AND(t_11, t_24)), BS_XOR((t_54), BS_XOR((BS_AND(t_15, t_23)), BS_XOR((BS_AND(t_15, t_24)), BS_XOR((BS_AND(t_16, t_24)), BS_XOR((BS_AND(t_19, t_24)), BS_XOR((BS_AND(t_25, b_3)), BS_XOR((BS_AND(t_26, b_1)), BS_XOR((BS_AND(t_27, b_1)), BS_XOR((BS_AND(t_28, b_0)), BS_XOR((BS_AND(t_29, b_2)), BS_XOR((BS_AND(t_30, b_0)), BS_XOR((BS_AND(t_31, b_0)), BS_XOR((BS_AND(t_32, b_0)), BS_XOR((BS_AND(t_33, b_0)), BS_XOR((BS_AND(t_34, b_0)), BS_XOR((BS_AND(t_37, b_0)), BS_XOR((BS_AND(t_38, b_1)), BS_XOR((BS_AND(t_41, b_0)), BS_XOR((BS_AND(BS_AND(t_2, t_20), b_0)), BS_XOR((BS_AND(t_44, b_0)), BS_XOR((BS_AND(t_45, b_0)), BS_XOR((BS_AND(t_46, b_1)), BS_XOR((BS_AND(t_46, b_0)), BS_XOR((BS_AND(t_48, b_0)), BS_XOR((BS_AND(t_49, b_1)), BS_XOR((BS_AND(t_49, b_0)), BS_XOR((BS_AND(t_50, b_0)), BS_XOR((BS_AND(BS_AND(t_8, t_22), b_0)), BS_XOR((BS_AND(t_52, b_0)), BS_XOR((BS_AND(t_53, b_0)), BS_XOR((BS_AND(t_54, b_0)), BS_XOR((BS_AND(t_55, b_0)), BS_XOR((BS_AND(t_25, t_19)), BS_XOR((BS_AND(t_25, t_21)), BS_XOR((BS_AND(t_25, t_22)), BS_XOR((BS_AND(t_25, t_23)), BS_XOR((BS_AND(t_25, t_24)), BS_XOR((BS_AND(t_26, t_23)), BS_XOR((BS_AND(t_27, t_24)), BS_XOR((BS_AND(t_29, t_23)), BS_XOR((BS_AND(t_32, t_24)), BS_XOR((BS_AND(t_35, t_22)), BS_XOR((BS_AND(t_35, t_23)), BS_XOR((BS_AND(t_36, t_24)), BS_XOR((BS_AND(t_41, t_24)), BS_XOR((BS_AND(t_44, t_24)), BS_XOR((BS_AND(t_49, t_24)), BS_XOR((BS_AND(BS_AND(t_25, t_19), b_1)), BS_XOR((BS_AND(BS_AND(t_25, t_19), b_0)), BS_XOR((BS_AND(BS_AND(t_25, t_20), b_0)), (BS_AND(BS_AND(t_25, t_22), b_0))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))));
      out[6] = BS_XOR((b_4), BS_XOR((b_1), BS_XOR((b_0), BS_XOR((t_0), BS_XOR((BS_AND(b_7, b_0)), BS_XOR((t_6), BS_XOR((t_9), BS_XOR((t_10), BS_XOR((BS_AND(b_6, b_0)), BS_XOR((t_12), BS_XOR((t_13), BS_XOR((t_15), BS_XOR((t_16), BS_XOR((t_17), BS_XOR((t_22), BS_XOR((t_24), BS_XOR((BS_AND(t_0, b_5)), BS_XOR((BS_AND(t_0, b_4)), BS_XOR((BS_AND(t_0, b_1)), BS_XOR((BS_AND(t_0, b_0)), BS_XOR((BS_AND(t_1, b_4)), BS_XOR((BS_AND(t_1, b_3)), BS_XOR((BS_AND(t_2, b_1)), BS_XOR((BS_AND(t_3, b_1)), BS_XOR((BS_AND(t_3, b_0)), BS_XOR((BS_AND(t_6, b_4)), BS_XOR((BS_AND(t_6, b_0)), BS_XOR((BS_AND(t_7, b_3)), BS_XOR((BS_AND(t_7, b_2)), BS_XOR((BS_AND(t_7, b_1)), BS_XOR((BS_AND(t_7, b_0)), BS_XOR((BS_AND(t_8, b_1)), BS_XOR((BS_AND(t_8, b_0)), BS_XOR((BS_AND(t_11, b_3)), BS_XOR((BS_AND(t_11, b_0)), BS_XOR((BS_AND(t_12, b_2)), BS_XOR((BS_AND(t_12, b_1)), BS_XOR((BS_AND(t_12, b_0)), BS_XOR((BS_AND(t_13, b_1)), BS_XOR((BS_AND(t_13, b_0)), BS_XOR((BS_AND(t_15, b_2)), BS_XOR((BS_AND(t_15, b_0)), BS_XOR((BS_AND(t_16, b_0)), BS_XOR((BS_AND(t_17, b_0)), BS_XOR((t_25), BS_XOR((BS_AND(BS_AND(t_0, b_5), b_0)), BS_XOR((t_29), BS_XOR((BS_AND(t_0, t_18)), BS_XOR((t_32), BS_XOR((t_33), BS_XOR((BS_AND(t_0, t_21)), BS_XOR((t_34), BS_XOR((BS_AND(t_0, t_24)), BS_XOR((t_36), BS_XOR((t_37), BS_XOR((t_39), BS_XOR((BS_AND(t_1, t_21)), BS_XOR((t_40), BS_XOR((BS_AND(t_1, t_24)), BS_XOR((BS_AND(t_2, t_21)), BS_XOR((t_43), BS_XOR((BS_AND(t_3, t_24)), BS_XOR((t_45), BS_XOR((BS_AND(t_6, t_17)), BS_XOR((t_48), BS_XOR((BS_AND(t_7, t_24)), BS_XOR((BS_AND(t_8, t_23)), BS_XOR((BS_AND(t_8, t_24)), BS_XOR((BS_AND(t_9, t_24)), BS_XOR((t_52), BS_XOR((t_53), BS_XOR((BS_AND(t_11, t_24)), BS_XOR((t_54), BS_XOR((t_55), BS_XOR((BS_AND(t_15, t_23)), BS_XOR((BS_AND(t_15, t_24)), BS_XOR((BS_AND(t_26, b_2)), BS_XOR((BS_AND(t_26, b_1)), BS_XOR((BS_AND(t_26, b_0)), BS_XOR((BS_AND(t_27, b_1)), BS_XOR((BS_AND(t_27, b_0)), BS_XOR((BS_AND(t_29, b_0)), BS_XOR((BS_AND(t_30, b_0)), BS_XOR((BS_AND(t_31, b_0)), BS_XOR((BS_AND(t_32, b_1)), BS_XOR((BS_AND(t_35, b_2)), BS_XOR((BS_AND(t_35, b_0)), BS_XOR((BS_AND(t_36, b_1)), BS_XOR((BS_AND(t_37, b_0)), BS_XOR((BS_AND(t_38, b_0)), BS_XOR((BS_AND(t_39, b_0)), BS_XOR((BS_AND(t_40, b_0)), BS_XOR((BS_AND(t_42, b_0)), BS_XOR((BS_AND(t_43, b_0)), BS_XOR((BS_AND(t_44, b_2)), BS_XOR((BS_AND(t_44, b_1)), BS_XOR((BS_AND(t_44, b_0)), BS_XOR((BS_AND(t_45, b_1)), BS_XOR((BS_AND(t_45, b_0)), BS_XOR((BS_AND(BS_AND(t_6, t_17), b_0)), BS_XOR((BS_AND(t_46, b_0)), BS_XOR((BS_AND(t_47, b_0)), BS_XOR((BS_AND(t_48, b_0)), BS_XOR((BS_AND(t_49, b_1)), BS_XOR((BS_AND(t_50, b_0)), BS_XOR((BS_AND(BS_AND(t_8, t_22), b_0)), BS_XOR((BS_AND(t_52, b_0)), BS_XOR((BS_AND(t_54, b_0)), BS_XOR((BS_AND(t_25, t_19)), BS_XOR((BS_AND(t_25, t_20)), BS_XOR((BS_AND(t_25, t_23)), BS_XOR((BS_AND(t_27, t_24)), BS_XOR((BS_AND(t_29, t_23)), BS_XOR((BS_AND(t_29, t_24)), BS_XOR((BS_AND(t_30, t_24)), BS_XOR((BS_AND(t_32, t_24)), BS_XOR((BS_AND(t_35, t_23)), BS_XOR((BS_AND(t_35, t_24)), BS_XOR((BS_AND(t_38, t_24)), BS_XOR((BS_AND(t_44, t_22)), BS_XOR((BS_AND(t_45, t_24)), BS_XOR((BS_AND(t_52, t_24)), BS_XOR((BS_AND(BS_AND(t_25, t_19), b_1)), BS_XOR((BS_AND(BS_AND(t_25, t_19), b_0)), BS_XOR((BS_AND(BS_AND(t_25, t_20), b_0)), BS_XOR((BS_AND(BS_AND(t_26, t_22), b_0)), (BS_AND(BS_AND(t_29, t_22), b_0))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))));
      out[5] = BS_XOR((all1), BS_XOR((b_6), BS_XOR((b_3), BS_XOR((t_0), BS_XOR((t_1), BS_XOR((t_3), BS_XOR((t_4), BS_XOR((t_6), BS_XOR((t_7), BS_XOR((t_9), BS_XOR((t_10), BS_XOR((BS_AND(b_6, b_0)), BS_XOR((t_13), BS_XOR((t_15), BS_XOR((t_16), BS_XOR((t_19), BS_XOR((t_20), BS_XOR((t_22), BS_XOR((BS_AND(t_0, b_0)), BS_XOR((BS_AND(t_1, b_3)), BS_XOR((BS_AND(t_2, b_0)), BS_XOR((BS_AND(t_3, b_2)), BS_XOR((BS_AND(t_3, b_0)), BS_XOR((BS_AND(t_5, b_0)), BS_XOR((BS_AND(t_6, b_4)), BS_XOR((BS_AND(t_6, b_2)), BS_XOR((BS_AND(t_7, b_1)), BS_XOR((BS_AND(t_7, b_0)), BS_XOR((BS_AND(t_8, b_2)), BS_XOR((BS_AND(t_8, b_1)), BS_XOR((BS_AND(t_9, b_0)), BS_XOR((BS_AND(t_11, b_2)), BS_XOR((BS_AND(t_13, b_1)), BS_XOR((BS_AND(t_14, b_0)), BS_XOR((BS_AND(t_15, b_2)), BS_XOR((BS_AND(t_15, b_0)), BS_XOR((BS_AND(t_16, b_0)), BS_XOR((BS_AND(t_17, b_0)), BS_XOR((t_27), BS_XOR((t_28), BS_XOR((BS_AND(BS_AND(t_0, b_5), b_0)), BS_XOR((t_29), BS_XOR((t_30), BS_XOR((t_31), BS_XOR((BS_AND(t_0, t_18)), BS_XOR((t_32), BS_XOR((BS_AND(t_0, t_24)), BS_XOR((t_35), BS_XOR((t_37), BS_XOR((BS_AND(t_1, t_18)), BS_XOR((t_38), BS_XOR((t_39), BS_XOR((t_40), BS_XOR((t_41), BS_XOR((BS_AND(t_2, t_21)), BS_XOR((t_42), BS_XOR((BS_AND(t_2, t_23)), BS_XOR((BS_AND(t_2, t_24)), BS_XOR((t_43), BS_XOR((t_44), BS_XOR((t_45), BS_XOR((BS_AND(t_6, t_17)), BS_XOR((BS_AND(t_6, t_18)), BS_XOR((t_46), BS_XOR((t_47), BS_XOR((t_48), BS_XOR((BS_AND(t_6, t_23)), BS_XOR((BS_AND(t_6, t_24)), BS_XOR((t_49), BS_XOR((t_50), BS_XOR((BS_AND(t_7, t_21)), BS_XOR((BS_AND(t_8, t_22)), BS_XOR((BS_AND(t_8, t_24)), BS_XOR((BS_AND(t_11, t_21)), BS_XOR((BS_AND(t_11, t_23)), BS_XOR((BS_AND(t_11, t_24)), BS_XOR((t_54), BS_XOR((BS_AND(t_12, t_24)), BS_XOR((t_55), BS_XOR((BS_AND(t_15, t_23)), BS_XOR((BS_AND(t_15, t_24)), BS_XOR((BS_AND(t_19, t_24)), BS_XOR((BS_AND(t_25, b_1)), BS_XOR((BS_AND(t_25, b_0)), BS_XOR((BS_AND(t_26, b_2)), BS_XOR((BS_AND(t_29, b_2)), BS_XOR((BS_AND(t_30, b_0)), BS_XOR((BS_AND(t_31, b_0)), BS_XOR((BS_AND(t_32, b_0)), BS_XOR((BS_AND(t_33, b_0)), BS_XOR((BS_AND(t_35, b_2)), BS_XOR((BS_AND(t_35, b_1)), BS_XOR((BS_AND(t_38, b_0)), BS_XOR((BS_AND(t_40, b_0)), BS_XOR((BS_AND(t_41, b_1)), BS_XOR((BS_AND(t_44, b_2)), BS_XOR((BS_AND(t_45, b_1)), BS_XOR((BS_AND(BS_AND(t_6, t_17), b_0)), BS_XOR((BS_AND(t_46, b_1)), BS_XOR((BS_AND(t_46, b_0)), BS_XOR((BS_AND(t_47, b_0)), BS_XOR((BS_AND(t_49, b_0)), BS_XOR((BS_AND(t_50, b_0)), BS_XOR((BS_AND(t_51, b_0)), BS_XOR((BS_AND(t_52, b_1)), BS_XOR((BS_AND(t_53, b_0)), BS_XOR((BS_AND(BS_AND(t_11, t_22), b_0)), BS_XOR((BS_AND(t_54, b_0)), BS_XOR((BS_AND(t_55, b_0)), BS_XOR((BS_AND(t_25, t_19)), BS_XOR((BS_AND(t_25, t_23)), BS_XOR((BS_AND(t_25, t_24)), BS_XOR((BS_AND(t_26, t_23)), BS_XOR((BS_AND(t_27, t_24)), BS_XOR((BS_AND(t_29, t_22)), BS_XOR((BS_AND(t_29, t_24)), BS_XOR((BS_AND(t_30, t_24)), BS_XOR((BS_AND(t_35, t_23)), BS_XOR((BS_AND(t_36, t_24)), BS_XOR((BS_AND(t_38, t_24)), BS_XOR((BS_AND(t_41, t_24)), BS_XOR((BS_AND(t_44, t_22)), BS_XOR((BS_AND(t_44, t_24)), BS_XOR((BS_AND(t_52, t_24)), BS_XOR((BS_AND(BS_AND(t_25, t_19), b_1)), BS_XOR((BS_AND(BS_AND(t_25, t_20), b_0)), BS_XOR((BS_AND(BS_AND(t_29, t_22), b_0)), (BS_AND(BS_AND(t_44, t_22), b_0)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))));
      out[4] = BS_XOR((all1), BS_XOR((b_7), BS_XOR((b_4), BS_XOR((b_3), BS_XOR((b_1), BS_XOR((b_0), BS_XOR((t_0), BS_XOR((t_1), BS_XOR((t_2), BS_XOR((t_3), BS_XOR((t_5), BS_XOR((t_6), BS_XOR((t_7), BS_XOR((t_8), BS_XOR((t_11), BS_XOR((t_13), BS_XOR((BS_AND(b_5, b_0)), BS_XOR((t_15), BS_XOR((t_17), BS_XOR((t_18), BS_XOR((t_19), BS_XOR((t_20), BS_XOR((t_21), BS_XOR((BS_AND(t_0, b_4)), BS_XOR((BS_AND(t_0, b_3)), BS_XOR((BS_AND(t_0, b_1)), BS_XOR((BS_AND(t_0, b_0)), BS_XOR((BS_AND(t_1, b_2)), BS_XOR((BS_AND(t_1, b_1)), BS_XOR((BS_AND(t_2, b_2)), BS_XOR((BS_AND(t_3, b_0)), BS_XOR((BS_AND(t_6, b_1)), BS_XOR((BS_AND(t_6, b_0)), BS_XOR((BS_AND(t_7, b_2)), BS_XOR((BS_AND(t_7, b_0)), BS_XOR((BS_AND(t_8, b_2)), BS_XOR((BS_AND(t_10, b_0)), BS_XOR((BS_AND(t_11, b_1)), BS_XOR((BS_AND(t_13, b_1)), BS_XOR((BS_AND(t_13, b_0)), BS_XOR((BS_AND(t_15, b_1)), BS_XOR((BS_AND(t_15, b_0)), BS_XOR((BS_AND(t_16, b_0)), BS_XOR((BS_AND(t_17, b_0)), BS_XOR((BS_AND(t_19, b_0)), BS_XOR((BS_AND(t_20, b_0)), BS_XOR((t_27), BS_XOR((t_28), BS_XOR((t_29), BS_XOR((t_31), BS_XOR((t_33), BS_XOR((BS_AND(t_0, t_21)), BS_XOR((t_34), BS_XOR((BS_AND(t_0, t_23)), BS_XOR((BS_AND(t_0, t_24)), BS_XOR((t_35), BS_XOR((t_39), BS_XOR((t_40), BS_XOR((BS_AND(t_1, t_24)), BS_XOR((t_43), BS_XOR((BS_AND(t_3, t_23)), BS_XOR((BS_AND(t_3, t_24)), BS_XOR((BS_AND(t_4, t_24)), BS_XOR((t_44), BS_XOR((t_45), BS_XOR((t_48), BS_XOR((BS_AND(t_6, t_23)), BS_XOR((BS_AND(t_6, t_24)), BS_XOR((t_49), BS_XOR((t_50), BS_XOR((BS_AND(t_8, t_22)), BS_XOR((BS_AND(t_9, t_24)), BS_XOR((t_53), BS_XOR((BS_AND(t_11, t_21)), BS_XOR((BS_AND(t_11, t_23)), BS_XOR((t_54), BS_XOR((BS_AND(t_12, t_24)), BS_XOR((t_55), BS_XOR((BS_AND(t_15, t_23)), BS_XOR((BS_AND(t_15, t_24)), BS_XOR((BS_AND(t_16, t_24)), BS_XOR((BS_AND(t_25, b_1)), BS_XOR((BS_AND(t_26, b_1)), BS_XOR((BS_AND(t_28, b_0)), BS_XOR((BS_AND(t_29, b_2)), BS_XOR((BS_AND(t_29, b_1)), BS_XOR((BS_AND(t_29, b_0)), BS_XOR((BS_AND(t_30, b_0)), BS_XOR((BS_AND(t_32, b_0)), BS_XOR((BS_AND(t_33, b_0)), BS_XOR((BS_AND(t_34, b_0)), BS_XOR((BS_AND(t_35, b_2)), BS_XOR((BS_AND(t_35, b_1)), BS_XOR((BS_AND(t_36, b_1)), BS_XOR((BS_AND(t_36, b_0)), BS_XOR((BS_AND(t_37, b_0)), BS_XOR((BS_AND(t_38, b_1)), BS_XOR((BS_AND(t_39, b_0)), BS_XOR((BS_AND(t_41, b_1)), BS_XOR((BS_AND(BS_AND(t_2, t_20), b_0)), BS_XOR((BS_AND(t_43, b_0)), BS_XOR((BS_AND(t_46, b_1)), BS_XOR((BS_AND(t_47, b_0)), BS_XOR((BS_AND(t_49, b_1)), BS_XOR((BS_AND(t_51, b_0)), BS_XOR((BS_AND(t_52, b_1)), BS_XOR((BS_AND(t_52, b_0)), BS_XOR((BS_AND(t_53, b_0)), BS_XOR((BS_AND(t_54, b_0)), BS_XOR((BS_AND(t_55, b_0)), BS_XOR((BS_AND(t_25, t_20)), BS_XOR((BS_AND(t_25, t_22)), BS_XOR((BS_AND(t_25, t_24)), BS_XOR((BS_AND(t_26, t_22)), BS_XOR((BS_AND(t_29, t_22)), BS_XOR((BS_AND(t_29, t_24)), BS_XOR((BS_AND(t_32, t_24)), BS_XOR((BS_AND(t_35, t_23)), BS_XOR((BS_AND(t_38, t_24)), BS_XOR((BS_AND(t_41, t_24)), BS_XOR((BS_AND(t_49, t_24)), BS_XOR((BS_AND(t_52, t_24)), BS_XOR((BS_AND(BS_AND(t_25, t_19), b_1)), BS_XOR((BS_AND(BS_AND(t_25, t_20), b_0)), BS_XOR((BS_AND(BS_AND(t_25, t_22), b_0)), (BS_AND(BS_AND(t_35, t_22), b_0)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))));
      out[3] = BS_XOR((all1), BS_XOR((b_6), BS_XOR((b_3), BS_XOR((b_2), BS_XOR((t_0), BS_XOR((t_5), BS_XOR((t_6), BS_XOR((t_9), BS_XOR((t_10), BS_XOR((BS_AND(b_6, b_0)), BS_XOR((t_12), BS_XOR((t_16), BS_XOR((t_17), BS_XOR((t_18), BS_XOR((t_21), BS_XOR((t_22), BS_XOR((t_23), BS_XOR((BS_AND(t_0, b_5)), BS_XOR((BS_AND(t_0, b_4)), BS_XOR((BS_AND(t_0, b_3)), BS_XOR((BS_AND(t_0, b_2)), BS_XOR((BS_AND(t_0, b_1)), BS_XOR((BS_AND(t_0, b_0)), BS_XOR((BS_AND(t_1, b_2)), BS_XOR((BS_AND(t_1, b_1)), BS_XOR((BS_AND(t_2, b_3)), BS_XOR((BS_AND(t_2, b_1)), BS_XOR((BS_AND(t_3, b_2)), BS_XOR((BS_AND(t_3, b_1)), BS_XOR((BS_AND(t_3, b_0)), BS_XOR((BS_AND(t_4, b_1)), BS_XOR((BS_AND(t_6, b_0)), BS_XOR((BS_AND(t_7, b_3)), BS_XOR((BS_AND(t_7, b_1)), BS_XOR((BS_AND(t_8, b_2)), BS_XOR((BS_AND(t_8, b_0)), BS_XOR((BS_AND(t_9, b_1)), BS_XOR((BS_AND(t_9, b_0)), BS_XOR((BS_AND(t_10, b_0)), BS_XOR((BS_AND(t_11, b_1)), BS_XOR((BS_AND(t_11, b_0)), BS_XOR((BS_AND(t_12, b_2)), BS_XOR((BS_AND(t_12, b_1)), BS_XOR((BS_AND(t_12, b_0)), BS_XOR((BS_AND(t_13, b_0)), BS_XOR((BS_AND(t_14, b_0)), BS_XOR((BS_AND(t_15, b_1)), BS_XOR((BS_AND(t_16, b_1)), BS_XOR((BS_AND(t_16, b_0)), BS_XOR((BS_AND(t_17, b_0)), BS_XOR((BS_AND(t_19, b_1)), BS_XOR((BS_AND(t_19, b_0)), BS_XOR((BS_AND(t_22, b_0)), BS_XOR((t_25), BS_XOR((t_26), BS_XOR((t_27), BS_XOR((t_28), BS_XOR((t_29), BS_XOR((t_30), BS_XOR((BS_AND(t_0, t_18)), BS_XOR((BS_AND(t_0, t_21)), BS_XOR((t_38), BS_XOR((t_39), BS_XOR((BS_AND(t_1, t_24)), BS_XOR((t_42), BS_XOR((BS_AND(t_2, t_24)), BS_XOR((t_44), BS_XOR((t_45), BS_XOR((t_46), BS_XOR((BS_AND(t_6, t_21)), BS_XOR((BS_AND(t_6, t_23)), BS_XOR((BS_AND(t_6, t_24)), BS_XOR((BS_AND(t_7, t_21)), BS_XOR((t_51), BS_XOR((BS_AND(t_7, t_23)), BS_XOR((BS_AND(t_9, t_24)), BS_XOR((t_52), BS_XOR((t_53), BS_XOR((BS_AND(t_11, t_21)), BS_XOR((BS_AND(t_11, t_23)), BS_XOR((BS_AND(t_11, t_24)), BS_XOR((t_54), BS_XOR((BS_AND(t_12, t_23)), BS_XOR((BS_AND(t_13, t_24)), BS_XOR((t_55), BS_XOR((BS_AND(t_15, t_23)), BS_XOR((BS_AND(t_16, t_24)), BS_XOR((BS_AND(t_19, t_24)), BS_XOR((BS_AND(t_25, b_2)), BS_XOR((BS_AND(t_25, b_1)), BS_XOR((BS_AND(t_25, b_0)), BS_XOR((BS_AND(t_26, b_1)), BS_XOR((BS_AND(t_27, b_0)), BS_XOR((BS_AND(t_29, b_2)), BS_XOR((BS_AND(t_30, b_1)), BS_XOR((BS_AND(t_30, b_0)), BS_XOR((BS_AND(t_31, b_0)), BS_XOR((BS_AND(t_32, b_1)), BS_XOR((BS_AND(t_34, b_0)), BS_XOR((BS_AND(t_35, b_2)), BS_XOR((BS_AND(t_36, b_1)), BS_XOR((BS_AND(t_36, b_0)), BS_XOR((BS_AND(t_37, b_0)), BS_XOR((BS_AND(t_39, b_0)), BS_XOR((BS_AND(t_41, b_1)), BS_XOR((BS_AND(t_42, b_0)), BS_XOR((BS_AND(t_45, b_1)), BS_XOR((BS_AND(t_45, b_0)), BS_XOR((BS_AND(t_46, b_1)), BS_XOR((BS_AND(t_49, b_0)), BS_XOR((BS_AND(t_51, b_0)), BS_XOR((BS_AND(t_52, b_0)), BS_XOR((BS_AND(BS_AND(t_11, t_22), b_0)), BS_XOR((BS_AND(t_55, b_0)), BS_XOR((BS_AND(t_25, t_20)), BS_XOR((BS_AND(t_25, t_21)), BS_XOR((BS_AND(t_26, t_23)), BS_XOR((BS_AND(t_29, t_22)), BS_XOR((BS_AND(t_29, t_23)), BS_XOR((BS_AND(t_29, t_24)), BS_XOR((BS_AND(t_30, t_24)), BS_XOR((BS_AND(t_32, t_24)), BS_XOR((BS_AND(t_35, t_22)), BS_XOR((BS_AND(t_35, t_23)), BS_XOR((BS_AND(t_44, t_22)), BS_XOR((BS_AND(t_44, t_23)), BS_XOR((BS_AND(t_44, t_24)), BS_XOR((BS_AND(t_46, t_24)), BS_XOR((BS_AND(t_49, t_24)), BS_XOR((BS_AND(t_52, t_24)), BS_XOR((BS_AND(BS_AND(t_25, t_19), b_0)), BS_XOR((BS_AND(BS_AND(t_25, t_20), b_0)), BS_XOR((BS_AND(BS_AND(t_26, t_22), b_0)), BS_XOR((BS_AND(BS_AND(t_29, t_22), b_0)), BS_XOR((BS_AND(BS_AND(t_35, t_22), b_0)), (BS_AND(BS_AND(t_44, t_22), b_0)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))));
      out[2] = BS_XOR((b_6), BS_XOR((b_5), BS_XOR((t_0), BS_XOR((t_1), BS_XOR((t_3), BS_XOR((t_4), BS_XOR((t_5), BS_XOR((BS_AND(b_7, b_0)), BS_XOR((t_6), BS_XOR((t_7), BS_XOR((t_8), BS_XOR((t_9), BS_XOR((BS_AND(b_6, b_0)), BS_XOR((t_13), BS_XOR((BS_AND(b_5, b_0)), BS_XOR((t_16), BS_XOR((t_18), BS_XOR((t_21), BS_XOR((t_24), BS_XOR((BS_AND(t_0, b_5)), BS_XOR((BS_AND(t_0, b_4)), BS_XOR((BS_AND(t_0, b_3)), BS_XOR((BS_AND(t_0, b_1)), BS_XOR((BS_AND(t_0, b_0)), BS_XOR((BS_AND(t_1, b_4)), BS_XOR((BS_AND(t_1, b_0)), BS_XOR((BS_AND(t_3, b_2)), BS_XOR((BS_AND(t_3, b_1)), BS_XOR((BS_AND(t_5, b_0)), BS_XOR((BS_AND(t_6, b_4)), BS_XOR((BS_AND(t_6, b_1)), BS_XOR((BS_AND(t_6, b_0)), BS_XOR((BS_AND(t_7, b_3)), BS_XOR((BS_AND(t_7, b_2)), BS_XOR((BS_AND(t_8, b_2)), BS_XOR((BS_AND(t_8, b_1)), BS_XOR((BS_AND(t_8, b_0)), BS_XOR((BS_AND(t_9, b_1)), BS_XOR((BS_AND(t_9, b_0)), BS_XOR((BS_AND(t_11, b_0)), BS_XOR((BS_AND(t_12, b_2)), BS_XOR((BS_AND(t_12, b_0)), BS_XOR((BS_AND(t_13, b_1)), BS_XOR((BS_AND(t_13, b_0)), BS_XOR((BS_AND(t_15, b_0)), BS_XOR((BS_AND(t_16, b_0)), BS_XOR((BS_AND(t_17, b_0)), BS_XOR((BS_AND(t_19, b_0)), BS_XOR((BS_AND(t_20, b_0)), BS_XOR((BS_AND(t_22, b_0)), BS_XOR((t_25), BS_XOR((t_26), BS_XOR((t_27), BS_XOR((t_29), BS_XOR((BS_AND(t_0, t_18)), BS_XOR((t_32), BS_XOR((t_33), BS_XOR((BS_AND(t_0, t_21)), BS_XOR((BS_AND(t_0, t_24)), BS_XOR((t_35), BS_XOR((t_37), BS_XOR((BS_AND(t_1, t_18)), BS_XOR((t_39), BS_XOR((BS_AND(t_1, t_21)), BS_XOR((t_40), BS_XOR((BS_AND(t_1, t_23)), BS_XOR((BS_AND(t_2, t_20)), BS_XOR((BS_AND(t_2, t_24)), BS_XOR((BS_AND(t_3, t_23)), BS_XOR((t_44), BS_XOR((t_45), BS_XOR((t_47), BS_XOR((BS_AND(t_6, t_21)), BS_XOR((t_48), BS_XOR((BS_AND(t_6, t_23)), BS_XOR((t_49), BS_XOR((t_50), BS_XOR((BS_AND(t_7, t_21)), BS_XOR((t_51), BS_XOR((BS_AND(t_7, t_23)), BS_XOR((BS_AND(t_7, t_24)), BS_XOR((BS_AND(t_8, t_24)), BS_XOR((t_52), BS_XOR((t_53), BS_XOR((BS_AND(t_12, t_24)), BS_XOR((BS_AND(t_13, t_24)), BS_XOR((BS_AND(t_15, t_24)), BS_XOR((BS_AND(t_19, t_24)), BS_XOR((BS_AND(t_25, b_1)), BS_XOR((BS_AND(t_26, b_2)), BS_XOR((BS_AND(t_26, b_1)), BS_XOR((BS_AND(t_29, b_2)), BS_XOR((BS_AND(t_32, b_0)), BS_XOR((BS_AND(t_33, b_0)), BS_XOR((BS_AND(t_34, b_0)), BS_XOR((BS_AND(t_35, b_2)), BS_XOR((BS_AND(t_36, b_1)), BS_XOR((BS_AND(t_36, b_0)), BS_XOR((BS_AND(t_37, b_0)), BS_XOR((BS_AND(t_39, b_0)), BS_XOR((BS_AND(t_40, b_0)), BS_XOR((BS_AND(t_41, b_1)), BS_XOR((BS_AND(BS_AND(t_2, t_20), b_0)), BS_XOR((BS_AND(t_43, b_0)), BS_XOR((BS_AND(t_44, b_1)), BS_XOR((BS_AND(t_45, b_1)), BS_XOR((BS_AND(t_45, b_0)), BS_XOR((BS_AND(t_46, b_1)), BS_XOR((BS_AND(t_47, b_0)), BS_XOR((BS_AND(t_48, b_0)), BS_XOR((BS_AND(t_50, b_0)), BS_XOR((BS_AND(t_51, b_0)), BS_XOR((BS_AND(t_52, b_0)), BS_XOR((BS_AND(t_54, b_0)), BS_XOR((BS_AND(t_25, t_19)), BS_XOR((BS_AND(t_25, t_20)), BS_XOR((BS_AND(t_25, t_21)), BS_XOR((BS_AND(t_25, t_22)), BS_XOR((BS_AND(t_26, t_22)), BS_XOR((BS_AND(t_26, t_23)), BS_XOR((BS_AND(t_26, t_24)), BS_XOR((BS_AND(t_29, t_22)), BS_XOR((BS_AND(t_29, t_23)), BS_XOR((BS_AND(t_30, t_24)), BS_XOR((BS_AND(t_35, t_23)), BS_XOR((BS_AND(t_41, t_24)), BS_XOR((BS_AND(t_44, t_22)), BS_XOR((BS_AND(t_44, t_24)), BS_XOR((BS_AND(t_45, t_24)), BS_XOR((BS_AND(t_49, t_24)), BS_XOR((BS_AND(t_52, t_24)), BS_XOR((BS_AND(BS_AND(t_25, t_19), b_1)), BS_XOR((BS_AND(BS_AND(t_25, t_20), b_0)), (BS_AND(BS_AND(t_35, t_22), b_0)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))));
      out[1] = BS_XOR((all1), BS_XOR((b_7), BS_XOR((b_4), BS_XOR((b_1), BS_XOR((t_4), BS_XOR((t_6), BS_XOR((t_9), BS_XOR((t_11), BS_XOR((t_12), BS_XOR((t_13), BS_XOR((t_15), BS_XOR((t_17), BS_XOR((t_18), BS_XOR((t_23), BS_XOR((t_24), BS_XOR((BS_AND(t_0, b_4)), BS_XOR((BS_AND(t_0, b_1)), BS_XOR((BS_AND(t_1, b_4)), BS_XOR((BS_AND(t_1, b_2)), BS_XOR((BS_AND(t_1, b_1)), BS_XOR((BS_AND(t_1, b_0)), BS_XOR((BS_AND(t_2, b_1)), BS_XOR((BS_AND(t_2, b_0)), BS_XOR((BS_AND(t_3, b_2)), BS_XOR((BS_AND(t_3, b_0)), BS_XOR((BS_AND(t_4, b_1)), BS_XOR((BS_AND(t_4, b_0)), BS_XOR((BS_AND(t_5, b_0)), BS_XOR((BS_AND(t_6, b_4)), BS_XOR((BS_AND(t_7, b_0)), BS_XOR((BS_AND(t_8, b_0)), BS_XOR((BS_AND(t_10, b_0)), BS_XOR((BS_AND(t_11, b_2)), BS_XOR((BS_AND(t_12, b_2)), BS_XOR((BS_AND(t_12, b_0)), BS_XOR((BS_AND(t_13, b_0)), BS_XOR((BS_AND(t_15, b_0)), BS_XOR((BS_AND(t_16, b_0)), BS_XOR((BS_AND(t_19, b_0)), BS_XOR((BS_AND(t_22, b_0)), BS_XOR((t_28), BS_XOR((BS_AND(BS_AND(t_0, b_5), b_0)), BS_XOR((t_29), BS_XOR((BS_AND(t_0, t_21)), BS_XOR((BS_AND(t_0, t_24)), BS_XOR((t_38), BS_XOR((t_39), BS_XOR((BS_AND(t_1, t_21)), BS_XOR((t_40), BS_XOR((BS_AND(t_1, t_23)), BS_XOR((BS_AND(t_1, t_24)), BS_XOR((BS_AND(t_2, t_21)), BS_XOR((BS_AND(t_2, t_23)), BS_XOR((BS_AND(t_3, t_23)), BS_XOR((BS_AND(t_4, t_24)), BS_XOR((t_45), BS_XOR((t_46), BS_XOR((t_47), BS_XOR((BS_AND(t_6, t_21)), BS_XOR((BS_AND(t_6, t_23)), BS_XOR((BS_AND(t_6, t_24)), BS_XOR((BS_AND(t_7, t_21)), BS_XOR((t_51), BS_XOR((BS_AND(t_7, t_24)), BS_XOR((BS_AND(t_8, t_23)), BS_XOR((BS_AND(t_8, t_24)), BS_XOR((BS_AND(t_9, t_24)), BS_XOR((t_53), BS_XOR((BS_AND(t_11, t_23)), BS_XOR((t_54), BS_XOR((BS_AND(t_12, t_24)), BS_XOR((BS_AND(t_15, t_23)), BS_XOR((BS_AND(t_16, t_24)), BS_XOR((BS_AND(t_19, t_24)), BS_XOR((BS_AND(t_25, b_3)), BS_XOR((BS_AND(t_25, b_1)), BS_XOR((BS_AND(t_27, b_0)), BS_XOR((BS_AND(t_29, b_2)), BS_XOR((BS_AND(t_29, b_1)), BS_XOR((BS_AND(t_30, b_1)), BS_XOR((BS_AND(t_35, b_1)), BS_XOR((BS_AND(t_36, b_1)), BS_XOR((BS_AND(t_36, b_0)), BS_XOR((BS_AND(t_37, b_0)), BS_XOR((BS_AND(t_41, b_0)), BS_XOR((BS_AND(t_42, b_0)), BS_XOR((BS_AND(t_43, b_0)), BS_XOR((BS_AND(t_44, b_2)), BS_XOR((BS_AND(t_44, b_0)), BS_XOR((BS_AND(t_45, b_1)), BS_XOR((BS_AND(t_46, b_1)), BS_XOR((BS_AND(t_46, b_0)), BS_XOR((BS_AND(t_47, b_0)), BS_XOR((BS_AND(t_49, b_1)), BS_XOR((BS_AND(t_50, b_0)), BS_XOR((BS_AND(t_51, b_0)), BS_XOR((BS_AND(t_52, b_0)), BS_XOR((BS_AND(t_53, b_0)), BS_XOR((BS_AND(BS_AND(t_11, t_22), b_0)), BS_XOR((BS_AND(t_55, b_0)), BS_XOR((BS_AND(t_25, t_19)), BS_XOR((BS_AND(t_25, t_20)), BS_XOR((BS_AND(t_25, t_21)), BS_XOR((BS_AND(t_25, t_22)), BS_XOR((BS_AND(t_25, t_24)), BS_XOR((BS_AND(t_26, t_22)), BS_XOR((BS_AND(t_26, t_24)), BS_XOR((BS_AND(t_29, t_23)), BS_XOR((BS_AND(t_30, t_24)), BS_XOR((BS_AND(t_35, t_23)), BS_XOR((BS_AND(t_35, t_24)), BS_XOR((BS_AND(t_36, t_24)), BS_XOR((BS_AND(t_38, t_24)), BS_XOR((BS_AND(t_49, t_24)), BS_XOR((BS_AND(t_52, t_24)), BS_XOR((BS_AND(BS_AND(t_25, t_20), b_0)), BS_XOR((BS_AND(BS_AND(t_26, t_22), b_0)), (BS_AND(BS_AND(t_44, t_22), b_0)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))));
      out[0] = BS_XOR((b_7), BS_XOR((b_3), BS_XOR((b_2), BS_XOR((t_0), BS_XOR((t_3), BS_XOR((t_4), BS_XOR((t_7), BS_XOR((BS_AND(b_6, b_0)), BS_XOR((t_11), BS_XOR((t_12), BS_XOR((t_14), BS_XOR((t_15), BS_XOR((t_16), BS_XOR((t_17), BS_XOR((t_18), BS_XOR((t_22), BS_XOR((BS_AND(t_0, b_5)), BS_XOR((BS_AND(t_0, b_3)), BS_XOR((BS_AND(t_0, b_2)), BS_XOR((BS_AND(t_0, b_1)), BS_XOR((BS_AND(t_0, b_0)), BS_XOR((BS_AND(t_1, b_2)), BS_XOR((BS_AND(t_1, b_1)), BS_XOR((BS_AND(t_1, b_0)), BS_XOR((BS_AND(t_2, b_0)), BS_XOR((BS_AND(t_4, b_1)), BS_XOR((BS_AND(t_5, b_0)), BS_XOR((BS_AND(t_6, b_1)), BS_XOR((BS_AND(t_7, b_2)), BS_XOR((BS_AND(t_7, b_1)), BS_XOR((BS_AND(t_9, b_1)), BS_XOR((BS_AND(t_10, b_0)), BS_XOR((BS_AND(t_11, b_3)), BS_XOR((BS_AND(t_11, b_2)), BS_XOR((BS_AND(t_11, b_0)), BS_XOR((BS_AND(t_12, b_0)), BS_XOR((BS_AND(t_15, b_2)), BS_XOR((BS_AND(t_15, b_1)), BS_XOR((BS_AND(t_15, b_0)), BS_XOR((BS_AND(t_16, b_1)), BS_XOR((BS_AND(t_16, b_0)), BS_XOR((BS_AND(t_17, b_0)), BS_XOR((BS_AND(t_19, b_1)), BS_XOR((t_26), BS_XOR((t_28), BS_XOR((BS_AND(BS_AND(t_0, b_5), b_0)), BS_XOR((t_34), BS_XOR((t_36), BS_XOR((BS_AND(t_1, t_18)), BS_XOR((t_38), BS_XOR((t_39), BS_XOR((BS_AND(t_1, t_21)), BS_XOR((t_40), BS_XOR((BS_AND(t_2, t_20)), BS_XOR((t_42), BS_XOR((BS_AND(t_2, t_23)), BS_XOR((BS_AND(t_2, t_24)), BS_XOR((BS_AND(t_3, t_23)), BS_XOR((BS_AND(t_4, t_24)), BS_XOR((BS_AND(t_6, t_18)), BS_XOR((t_48), BS_XOR((BS_AND(t_6, t_24)), BS_XOR((t_50), BS_XOR((BS_AND(t_7, t_21)), BS_XOR((BS_AND(t_7, t_23)), BS_XOR((BS_AND(t_8, t_23)), BS_XOR((BS_AND(t_8, t_24)), BS_XOR((BS_AND(t_9, t_24)), BS_XOR((t_52), BS_XOR((BS_AND(t_11, t_21)), BS_XOR((BS_AND(t_11, t_23)), BS_XOR((BS_AND(t_11, t_24)), BS_XOR((BS_AND(t_12, t_24)), BS_XOR((BS_AND(t_15, t_23)), BS_XOR((BS_AND(t_15, t_24)), BS_XOR((BS_AND(t_25, b_2)), BS_XOR((BS_AND(t_25, b_1)), BS_XOR((BS_AND(t_26, b_0)), BS_XOR((BS_AND(t_27, b_1)), BS_XOR((BS_AND(t_27, b_0)), BS_XOR((BS_AND(t_28, b_0)), BS_XOR((BS_AND(t_29, b_0)), BS_XOR((BS_AND(t_30, b_0)), BS_XOR((BS_AND(t_31, b_0)), BS_XOR((BS_AND(t_32, b_0)), BS_XOR((BS_AND(t_33, b_0)), BS_XOR((BS_AND(t_34, b_0)), BS_XOR((BS_AND(t_35, b_1)), BS_XOR((BS_AND(t_35, b_0)), BS_XOR((BS_AND(t_36, b_1)), BS_XOR((BS_AND(t_37, b_0)), BS_XOR((BS_AND(t_38, b_0)), BS_XOR((BS_AND(t_40, b_0)), BS_XOR((BS_AND(t_41, b_0)), BS_XOR((BS_AND(BS_AND(t_2, t_20), b_0)), BS_XOR((BS_AND(t_42, b_0)), BS_XOR((BS_AND(t_43, b_0)), BS_XOR((BS_AND(t_44, b_0)), BS_XOR((BS_AND(BS_AND(t_6, t_17), b_0)), BS_XOR((BS_AND(t_46, b_1)), BS_XOR((BS_AND(t_46, b_0)), BS_XOR((BS_AND(t_47, b_0)), BS_XOR((BS_AND(t_49, b_1)), BS_XOR((BS_AND(t_49, b_0)), BS_XOR((BS_AND(t_50, b_0)), BS_XOR((BS_AND(t_52, b_0)), BS_XOR((BS_AND(t_53, b_0)), BS_XOR((BS_AND(t_54, b_0)), BS_XOR((BS_AND(t_55, b_0)), BS_XOR((BS_AND(t_25, t_20)), BS_XOR((BS_AND(t_25, t_21)), BS_XOR((BS_AND(t_25, t_23)), BS_XOR((BS_AND(t_25, t_24)), BS_XOR((BS_AND(t_26, t_22)), BS_XOR((BS_AND(t_29, t_22)), BS_XOR((BS_AND(t_29, t_23)), BS_XOR((BS_AND(t_29, t_24)), BS_XOR((BS_AND(t_32, t_24)), BS_XOR((BS_AND(t_36, t_24)), BS_XOR((BS_AND(t_38, t_24)), BS_XOR((BS_AND(t_41, t_24)), BS_XOR((BS_AND(t_44, t_23)), BS_XOR((BS_AND(t_44, t_24)), BS_XOR((BS_AND(t_45, t_24)), BS_XOR((BS_AND(BS_AND(t_25, t_20), b_0)), BS_XOR((BS_AND(BS_AND(t_26, t_22), b_0)), BS_XOR((BS_AND(BS_AND(t_35, t_22), b_0)), (BS_AND(BS_AND(t_44, t_22), b_0)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))));
#elif BOOLMODE==1
      /* da_diett XOR - pairs of variables */
      out[7] = BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((b_5), (b_4)), BS_XOR((b_3), (b_0))), BS_XOR(BS_XOR((t_0), (t_4)), BS_XOR((t_5), BS_AND(b_7, b_0)))), BS_XOR(BS_XOR(BS_XOR((t_6), (t_7)), BS_XOR((t_8), (t_9))), BS_XOR(BS_XOR((t_10), (t_11)), BS_XOR((t_12), (t_14))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_15), (t_16)), BS_XOR((t_17), (t_21))), BS_XOR(BS_XOR((t_22), BS_AND(t_0, b_3)), BS_XOR(BS_AND(t_1, b_3), BS_AND(t_1, b_2)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_1, b_1), BS_AND(t_2, b_3)), BS_XOR(BS_AND(t_2, b_1), BS_AND(t_3, b_1))), BS_XOR(BS_XOR(BS_AND(t_4, b_1), BS_AND(t_4, b_0)), BS_XOR(BS_AND(t_6, b_4), BS_AND(t_6, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_7, b_1), BS_AND(t_8, b_0)), BS_XOR(BS_AND(t_9, b_1), BS_AND(t_12, b_0))), BS_XOR(BS_XOR(BS_AND(t_14, b_0), BS_AND(t_15, b_1)), BS_XOR(BS_AND(t_15, b_0), BS_AND(t_16, b_1)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_16, b_0), BS_AND(t_20, b_0)), BS_XOR((t_26), BS_AND(BS_AND(t_0, b_5), b_0))), BS_XOR(BS_XOR((t_30), (t_31)), BS_XOR((t_32), BS_AND(t_0, t_24))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_35), (t_36)), BS_XOR(BS_AND(t_1, t_18), (t_38))), BS_XOR(BS_XOR(BS_AND(t_1, t_21), BS_AND(t_1, t_23)), BS_XOR((t_41), BS_AND(t_2, t_20)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_2, t_21), (t_42)), BS_XOR(BS_AND(t_2, t_23), (t_43))), BS_XOR(BS_XOR(BS_AND(t_3, t_23), BS_AND(t_3, t_24)), BS_XOR(BS_AND(t_4, t_24), BS_AND(t_6, t_17))))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_47), BS_AND(t_6, t_21)), BS_XOR(BS_AND(t_6, t_24), (t_49))), BS_XOR(BS_XOR(BS_AND(t_7, t_21), (t_51)), BS_XOR(BS_AND(t_7, t_23), BS_AND(t_8, t_23)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_9, t_24), (t_53)), BS_XOR(BS_AND(t_11, t_21), BS_AND(t_11, t_24))), BS_XOR(BS_XOR((t_54), BS_AND(t_15, t_23)), BS_XOR(BS_AND(t_15, t_24), BS_AND(t_16, t_24))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_19, t_24), BS_AND(t_25, b_3)), BS_XOR(BS_AND(t_26, b_1), BS_AND(t_27, b_1))), BS_XOR(BS_XOR(BS_AND(t_28, b_0), BS_AND(t_29, b_2)), BS_XOR(BS_AND(t_30, b_0), BS_AND(t_31, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_32, b_0), BS_AND(t_33, b_0)), BS_XOR(BS_AND(t_34, b_0), BS_AND(t_37, b_0))), BS_XOR(BS_XOR(BS_AND(t_38, b_1), BS_AND(t_41, b_0)), BS_XOR(BS_AND(BS_AND(t_2, t_20), b_0), BS_AND(t_44, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_45, b_0), BS_AND(t_46, b_1)), BS_XOR(BS_AND(t_46, b_0), BS_AND(t_48, b_0))), BS_XOR(BS_XOR(BS_AND(t_49, b_1), BS_AND(t_49, b_0)), BS_XOR(BS_AND(t_50, b_0), BS_AND(BS_AND(t_8, t_22), b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_52, b_0), BS_AND(t_53, b_0)), BS_XOR(BS_AND(t_54, b_0), BS_AND(t_55, b_0))), BS_XOR(BS_XOR(BS_AND(t_25, t_19), BS_AND(t_25, t_21)), BS_XOR(BS_AND(t_25, t_22), BS_AND(t_25, t_23))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_25, t_24), BS_AND(t_26, t_23)), BS_XOR(BS_AND(t_27, t_24), BS_AND(t_29, t_23))), BS_XOR(BS_XOR(BS_AND(t_32, t_24), BS_AND(t_35, t_22)), BS_XOR(BS_AND(t_35, t_23), BS_AND(t_36, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_41, t_24), BS_AND(t_44, t_24)), BS_XOR(BS_AND(t_49, t_24), BS_AND(BS_AND(t_25, t_19), b_1))), BS_XOR(BS_XOR(BS_AND(BS_AND(t_25, t_19), b_0), BS_AND(BS_AND(t_25, t_20), b_0)), BS_AND(BS_AND(t_25, t_22), b_0)))))));
      out[6] = BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((b_4), (b_1)), BS_XOR((b_0), (t_0))), BS_XOR(BS_XOR(BS_AND(b_7, b_0), (t_6)), BS_XOR((t_9), (t_10)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(b_6, b_0), (t_12)), BS_XOR((t_13), (t_15))), BS_XOR(BS_XOR((t_16), (t_17)), BS_XOR((t_22), (t_24))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_0, b_5), BS_AND(t_0, b_4)), BS_XOR(BS_AND(t_0, b_1), BS_AND(t_0, b_0))), BS_XOR(BS_XOR(BS_AND(t_1, b_4), BS_AND(t_1, b_3)), BS_XOR(BS_AND(t_2, b_1), BS_AND(t_3, b_1)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_3, b_0), BS_AND(t_6, b_4)), BS_XOR(BS_AND(t_6, b_0), BS_AND(t_7, b_3))), BS_XOR(BS_XOR(BS_AND(t_7, b_2), BS_AND(t_7, b_1)), BS_XOR(BS_AND(t_7, b_0), BS_AND(t_8, b_1)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_8, b_0), BS_AND(t_11, b_3)), BS_XOR(BS_AND(t_11, b_0), BS_AND(t_12, b_2))), BS_XOR(BS_XOR(BS_AND(t_12, b_1), BS_AND(t_12, b_0)), BS_XOR(BS_AND(t_13, b_1), BS_AND(t_13, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_15, b_2), BS_AND(t_15, b_0)), BS_XOR(BS_AND(t_16, b_0), BS_AND(t_17, b_0))), BS_XOR(BS_XOR((t_25), BS_AND(BS_AND(t_0, b_5), b_0)), BS_XOR((t_29), BS_AND(t_0, t_18))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_32), (t_33)), BS_XOR(BS_AND(t_0, t_21), (t_34))), BS_XOR(BS_XOR(BS_AND(t_0, t_24), (t_36)), BS_XOR((t_37), (t_39)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_1, t_21), (t_40)), BS_XOR(BS_AND(t_1, t_24), BS_AND(t_2, t_21))), BS_XOR(BS_XOR((t_43), BS_AND(t_3, t_24)), BS_XOR((t_45), BS_AND(t_6, t_17))))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_48), BS_AND(t_7, t_24)), BS_XOR(BS_AND(t_8, t_23), BS_AND(t_8, t_24))), BS_XOR(BS_XOR(BS_AND(t_9, t_24), (t_52)), BS_XOR((t_53), BS_AND(t_11, t_24)))), BS_XOR(BS_XOR(BS_XOR((t_54), (t_55)), BS_XOR(BS_AND(t_15, t_23), BS_AND(t_15, t_24))), BS_XOR(BS_XOR(BS_AND(t_26, b_2), BS_AND(t_26, b_1)), BS_XOR(BS_AND(t_26, b_0), BS_AND(t_27, b_1))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_27, b_0), BS_AND(t_29, b_0)), BS_XOR(BS_AND(t_30, b_0), BS_AND(t_31, b_0))), BS_XOR(BS_XOR(BS_AND(t_32, b_1), BS_AND(t_35, b_2)), BS_XOR(BS_AND(t_35, b_0), BS_AND(t_36, b_1)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_37, b_0), BS_AND(t_38, b_0)), BS_XOR(BS_AND(t_39, b_0), BS_AND(t_40, b_0))), BS_XOR(BS_XOR(BS_AND(t_42, b_0), BS_AND(t_43, b_0)), BS_XOR(BS_AND(t_44, b_2), BS_AND(t_44, b_1)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_44, b_0), BS_AND(t_45, b_1)), BS_XOR(BS_AND(t_45, b_0), BS_AND(BS_AND(t_6, t_17), b_0))), BS_XOR(BS_XOR(BS_AND(t_46, b_0), BS_AND(t_47, b_0)), BS_XOR(BS_AND(t_48, b_0), BS_AND(t_49, b_1)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_50, b_0), BS_AND(BS_AND(t_8, t_22), b_0)), BS_XOR(BS_AND(t_52, b_0), BS_AND(t_54, b_0))), BS_XOR(BS_XOR(BS_AND(t_25, t_19), BS_AND(t_25, t_20)), BS_XOR(BS_AND(t_25, t_23), BS_AND(t_27, t_24))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_29, t_23), BS_AND(t_29, t_24)), BS_XOR(BS_AND(t_30, t_24), BS_AND(t_32, t_24))), BS_XOR(BS_XOR(BS_AND(t_35, t_23), BS_AND(t_35, t_24)), BS_XOR(BS_AND(t_38, t_24), BS_AND(t_44, t_22)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_45, t_24), BS_AND(t_52, t_24)), BS_XOR(BS_AND(BS_AND(t_25, t_19), b_1), BS_AND(BS_AND(t_25, t_19), b_0))), BS_XOR(BS_XOR(BS_AND(BS_AND(t_25, t_20), b_0), BS_AND(BS_AND(t_26, t_22), b_0)), BS_AND(BS_AND(t_29, t_22), b_0)))))));
      out[5] = BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((all1), (b_6)), BS_XOR((b_3), (t_0))), BS_XOR(BS_XOR((t_1), (t_3)), BS_XOR((t_4), (t_6)))), BS_XOR(BS_XOR(BS_XOR((t_7), (t_9)), BS_XOR((t_10), BS_AND(b_6, b_0))), BS_XOR(BS_XOR((t_13), (t_15)), BS_XOR((t_16), (t_19))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_20), (t_22)), BS_XOR(BS_AND(t_0, b_0), BS_AND(t_1, b_3))), BS_XOR(BS_XOR(BS_AND(t_2, b_0), BS_AND(t_3, b_2)), BS_XOR(BS_AND(t_3, b_0), BS_AND(t_5, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_6, b_4), BS_AND(t_6, b_2)), BS_XOR(BS_AND(t_7, b_1), BS_AND(t_7, b_0))), BS_XOR(BS_XOR(BS_AND(t_8, b_2), BS_AND(t_8, b_1)), BS_XOR(BS_AND(t_9, b_0), BS_AND(t_11, b_2)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_13, b_1), BS_AND(t_14, b_0)), BS_XOR(BS_AND(t_15, b_2), BS_AND(t_15, b_0))), BS_XOR(BS_XOR(BS_AND(t_16, b_0), BS_AND(t_17, b_0)), BS_XOR((t_27), (t_28)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(BS_AND(t_0, b_5), b_0), (t_29)), BS_XOR((t_30), (t_31))), BS_XOR(BS_XOR(BS_AND(t_0, t_18), (t_32)), BS_XOR(BS_AND(t_0, t_24), (t_35))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_37), BS_AND(t_1, t_18)), BS_XOR((t_38), (t_39))), BS_XOR(BS_XOR((t_40), (t_41)), BS_XOR(BS_AND(t_2, t_21), (t_42)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_2, t_23), BS_AND(t_2, t_24)), BS_XOR((t_43), (t_44))), BS_XOR(BS_XOR((t_45), BS_AND(t_6, t_17)), BS_XOR(BS_AND(t_6, t_18), (t_46))))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_47), (t_48)), BS_XOR(BS_AND(t_6, t_23), BS_AND(t_6, t_24))), BS_XOR(BS_XOR((t_49), (t_50)), BS_XOR(BS_AND(t_7, t_21), BS_AND(t_8, t_22)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_8, t_24), BS_AND(t_11, t_21)), BS_XOR(BS_AND(t_11, t_23), BS_AND(t_11, t_24))), BS_XOR(BS_XOR((t_54), BS_AND(t_12, t_24)), BS_XOR((t_55), BS_AND(t_15, t_23))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_15, t_24), BS_AND(t_19, t_24)), BS_XOR(BS_AND(t_25, b_1), BS_AND(t_25, b_0))), BS_XOR(BS_XOR(BS_AND(t_26, b_2), BS_AND(t_29, b_2)), BS_XOR(BS_AND(t_30, b_0), BS_AND(t_31, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_32, b_0), BS_AND(t_33, b_0)), BS_XOR(BS_AND(t_35, b_2), BS_AND(t_35, b_1))), BS_XOR(BS_XOR(BS_AND(t_38, b_0), BS_AND(t_40, b_0)), BS_XOR(BS_AND(t_41, b_1), BS_AND(t_44, b_2)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_45, b_1), BS_AND(BS_AND(t_6, t_17), b_0)), BS_XOR(BS_AND(t_46, b_1), BS_AND(t_46, b_0))), BS_XOR(BS_XOR(BS_AND(t_47, b_0), BS_AND(t_49, b_0)), BS_XOR(BS_AND(t_50, b_0), BS_AND(t_51, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_52, b_1), BS_AND(t_53, b_0)), BS_XOR(BS_AND(BS_AND(t_11, t_22), b_0), BS_AND(t_54, b_0))), BS_XOR(BS_XOR(BS_AND(t_55, b_0), BS_AND(t_25, t_19)), BS_XOR(BS_AND(t_25, t_23), BS_AND(t_25, t_24))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_26, t_23), BS_AND(t_27, t_24)), BS_XOR(BS_AND(t_29, t_22), BS_AND(t_29, t_24))), BS_XOR(BS_XOR(BS_AND(t_30, t_24), BS_AND(t_35, t_23)), BS_XOR(BS_AND(t_36, t_24), BS_AND(t_38, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_41, t_24), BS_AND(t_44, t_22)), BS_XOR(BS_AND(t_44, t_24), BS_AND(t_52, t_24))), BS_XOR(BS_XOR(BS_AND(BS_AND(t_25, t_19), b_1), BS_AND(BS_AND(t_25, t_20), b_0)), BS_XOR(BS_AND(BS_AND(t_29, t_22), b_0), BS_AND(BS_AND(t_44, t_22), b_0))))))));
      out[4] = BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((all1), (b_7)), BS_XOR((b_4), (b_3))), BS_XOR(BS_XOR((b_1), (b_0)), BS_XOR((t_0), (t_1)))), BS_XOR(BS_XOR(BS_XOR((t_2), (t_3)), BS_XOR((t_5), (t_6))), BS_XOR(BS_XOR((t_7), (t_8)), BS_XOR((t_11), (t_13))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(b_5, b_0), (t_15)), BS_XOR((t_17), (t_18))), BS_XOR(BS_XOR((t_19), (t_20)), BS_XOR((t_21), BS_AND(t_0, b_4)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_0, b_3), BS_AND(t_0, b_1)), BS_XOR(BS_AND(t_0, b_0), BS_AND(t_1, b_2))), BS_XOR(BS_XOR(BS_AND(t_1, b_1), BS_AND(t_2, b_2)), BS_XOR(BS_AND(t_3, b_0), BS_AND(t_6, b_1)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_6, b_0), BS_AND(t_7, b_2)), BS_XOR(BS_AND(t_7, b_0), BS_AND(t_8, b_2))), BS_XOR(BS_XOR(BS_AND(t_10, b_0), BS_AND(t_11, b_1)), BS_XOR(BS_AND(t_13, b_1), BS_AND(t_13, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_15, b_1), BS_AND(t_15, b_0)), BS_XOR(BS_AND(t_16, b_0), BS_AND(t_17, b_0))), BS_XOR(BS_XOR(BS_AND(t_19, b_0), BS_AND(t_20, b_0)), BS_XOR((t_27), (t_28))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_29), (t_31)), BS_XOR((t_33), BS_AND(t_0, t_21))), BS_XOR(BS_XOR((t_34), BS_AND(t_0, t_23)), BS_XOR(BS_AND(t_0, t_24), (t_35)))), BS_XOR(BS_XOR(BS_XOR((t_39), (t_40)), BS_XOR(BS_AND(t_1, t_24), (t_43))), BS_XOR(BS_XOR(BS_AND(t_3, t_23), BS_AND(t_3, t_24)), BS_XOR(BS_AND(t_4, t_24), (t_44))))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_45), (t_48)), BS_XOR(BS_AND(t_6, t_23), BS_AND(t_6, t_24))), BS_XOR(BS_XOR((t_49), (t_50)), BS_XOR(BS_AND(t_8, t_22), BS_AND(t_9, t_24)))), BS_XOR(BS_XOR(BS_XOR((t_53), BS_AND(t_11, t_21)), BS_XOR(BS_AND(t_11, t_23), (t_54))), BS_XOR(BS_XOR(BS_AND(t_12, t_24), (t_55)), BS_XOR(BS_AND(t_15, t_23), BS_AND(t_15, t_24))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_16, t_24), BS_AND(t_25, b_1)), BS_XOR(BS_AND(t_26, b_1), BS_AND(t_28, b_0))), BS_XOR(BS_XOR(BS_AND(t_29, b_2), BS_AND(t_29, b_1)), BS_XOR(BS_AND(t_29, b_0), BS_AND(t_30, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_32, b_0), BS_AND(t_33, b_0)), BS_XOR(BS_AND(t_34, b_0), BS_AND(t_35, b_2))), BS_XOR(BS_XOR(BS_AND(t_35, b_1), BS_AND(t_36, b_1)), BS_XOR(BS_AND(t_36, b_0), BS_AND(t_37, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_38, b_1), BS_AND(t_39, b_0)), BS_XOR(BS_AND(t_41, b_1), BS_AND(BS_AND(t_2, t_20), b_0))), BS_XOR(BS_XOR(BS_AND(t_43, b_0), BS_AND(t_46, b_1)), BS_XOR(BS_AND(t_47, b_0), BS_AND(t_49, b_1)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_51, b_0), BS_AND(t_52, b_1)), BS_XOR(BS_AND(t_52, b_0), BS_AND(t_53, b_0))), BS_XOR(BS_XOR(BS_AND(t_54, b_0), BS_AND(t_55, b_0)), BS_XOR(BS_AND(t_25, t_20), BS_AND(t_25, t_22))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_25, t_24), BS_AND(t_26, t_22)), BS_XOR(BS_AND(t_29, t_22), BS_AND(t_29, t_24))), BS_XOR(BS_XOR(BS_AND(t_32, t_24), BS_AND(t_35, t_23)), BS_XOR(BS_AND(t_38, t_24), BS_AND(t_41, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_49, t_24), BS_AND(t_52, t_24)), BS_XOR(BS_AND(BS_AND(t_25, t_19), b_1), BS_AND(BS_AND(t_25, t_20), b_0))), BS_XOR(BS_AND(BS_AND(t_25, t_22), b_0), BS_AND(BS_AND(t_35, t_22), b_0)))))));
      out[3] = BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((all1), (b_6)), BS_XOR((b_3), (b_2))), BS_XOR(BS_XOR((t_0), (t_5)), BS_XOR((t_6), (t_9)))), BS_XOR(BS_XOR(BS_XOR((t_10), BS_AND(b_6, b_0)), BS_XOR((t_12), (t_16))), BS_XOR(BS_XOR((t_17), (t_18)), BS_XOR((t_21), (t_22))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_23), BS_AND(t_0, b_5)), BS_XOR(BS_AND(t_0, b_4), BS_AND(t_0, b_3))), BS_XOR(BS_XOR(BS_AND(t_0, b_2), BS_AND(t_0, b_1)), BS_XOR(BS_AND(t_0, b_0), BS_AND(t_1, b_2)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_1, b_1), BS_AND(t_2, b_3)), BS_XOR(BS_AND(t_2, b_1), BS_AND(t_3, b_2))), BS_XOR(BS_XOR(BS_AND(t_3, b_1), BS_AND(t_3, b_0)), BS_XOR(BS_AND(t_4, b_1), BS_AND(t_6, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_7, b_3), BS_AND(t_7, b_1)), BS_XOR(BS_AND(t_8, b_2), BS_AND(t_8, b_0))), BS_XOR(BS_XOR(BS_AND(t_9, b_1), BS_AND(t_9, b_0)), BS_XOR(BS_AND(t_10, b_0), BS_AND(t_11, b_1)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_11, b_0), BS_AND(t_12, b_2)), BS_XOR(BS_AND(t_12, b_1), BS_AND(t_12, b_0))), BS_XOR(BS_XOR(BS_AND(t_13, b_0), BS_AND(t_14, b_0)), BS_XOR(BS_AND(t_15, b_1), BS_AND(t_16, b_1))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_16, b_0), BS_AND(t_17, b_0)), BS_XOR(BS_AND(t_19, b_1), BS_AND(t_19, b_0))), BS_XOR(BS_XOR(BS_AND(t_22, b_0), (t_25)), BS_XOR((t_26), (t_27)))), BS_XOR(BS_XOR(BS_XOR((t_28), (t_29)), BS_XOR((t_30), BS_AND(t_0, t_18))), BS_XOR(BS_XOR(BS_AND(t_0, t_21), (t_38)), BS_XOR((t_39), BS_AND(t_1, t_24))))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_42), BS_AND(t_2, t_24)), BS_XOR((t_44), (t_45))), BS_XOR(BS_XOR((t_46), BS_AND(t_6, t_21)), BS_XOR(BS_AND(t_6, t_23), BS_AND(t_6, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_7, t_21), (t_51)), BS_XOR(BS_AND(t_7, t_23), BS_AND(t_9, t_24))), BS_XOR(BS_XOR((t_52), (t_53)), BS_XOR(BS_AND(t_11, t_21), BS_AND(t_11, t_23))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_11, t_24), (t_54)), BS_XOR(BS_AND(t_12, t_23), BS_AND(t_13, t_24))), BS_XOR(BS_XOR((t_55), BS_AND(t_15, t_23)), BS_XOR(BS_AND(t_16, t_24), BS_AND(t_19, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_25, b_2), BS_AND(t_25, b_1)), BS_XOR(BS_AND(t_25, b_0), BS_AND(t_26, b_1))), BS_XOR(BS_XOR(BS_AND(t_27, b_0), BS_AND(t_29, b_2)), BS_XOR(BS_AND(t_30, b_1), BS_AND(t_30, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_31, b_0), BS_AND(t_32, b_1)), BS_XOR(BS_AND(t_34, b_0), BS_AND(t_35, b_2))), BS_XOR(BS_XOR(BS_AND(t_36, b_1), BS_AND(t_36, b_0)), BS_XOR(BS_AND(t_37, b_0), BS_AND(t_39, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_41, b_1), BS_AND(t_42, b_0)), BS_XOR(BS_AND(t_45, b_1), BS_AND(t_45, b_0))), BS_XOR(BS_XOR(BS_AND(t_46, b_1), BS_AND(t_49, b_0)), BS_XOR(BS_AND(t_51, b_0), BS_AND(t_52, b_0))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(BS_AND(t_11, t_22), b_0), BS_AND(t_55, b_0)), BS_XOR(BS_AND(t_25, t_20), BS_AND(t_25, t_21))), BS_XOR(BS_XOR(BS_AND(t_26, t_23), BS_AND(t_29, t_22)), BS_XOR(BS_AND(t_29, t_23), BS_AND(t_29, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_30, t_24), BS_AND(t_32, t_24)), BS_XOR(BS_AND(t_35, t_22), BS_AND(t_35, t_23))), BS_XOR(BS_XOR(BS_AND(t_44, t_22), BS_AND(t_44, t_23)), BS_XOR(BS_AND(t_44, t_24), BS_AND(t_46, t_24)))))))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_49, t_24), BS_AND(t_52, t_24)), BS_XOR(BS_AND(BS_AND(t_25, t_19), b_0), BS_AND(BS_AND(t_25, t_20), b_0))), BS_XOR(BS_XOR(BS_AND(BS_AND(t_26, t_22), b_0), BS_AND(BS_AND(t_29, t_22), b_0)), BS_XOR(BS_AND(BS_AND(t_35, t_22), b_0), BS_AND(BS_AND(t_44, t_22), b_0)))));
      out[2] = BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((b_6), (b_5)), BS_XOR((t_0), (t_1))), BS_XOR(BS_XOR((t_3), (t_4)), BS_XOR((t_5), BS_AND(b_7, b_0)))), BS_XOR(BS_XOR(BS_XOR((t_6), (t_7)), BS_XOR((t_8), (t_9))), BS_XOR(BS_XOR(BS_AND(b_6, b_0), (t_13)), BS_XOR(BS_AND(b_5, b_0), (t_16))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_18), (t_21)), BS_XOR((t_24), BS_AND(t_0, b_5))), BS_XOR(BS_XOR(BS_AND(t_0, b_4), BS_AND(t_0, b_3)), BS_XOR(BS_AND(t_0, b_1), BS_AND(t_0, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_1, b_4), BS_AND(t_1, b_0)), BS_XOR(BS_AND(t_3, b_2), BS_AND(t_3, b_1))), BS_XOR(BS_XOR(BS_AND(t_5, b_0), BS_AND(t_6, b_4)), BS_XOR(BS_AND(t_6, b_1), BS_AND(t_6, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_7, b_3), BS_AND(t_7, b_2)), BS_XOR(BS_AND(t_8, b_2), BS_AND(t_8, b_1))), BS_XOR(BS_XOR(BS_AND(t_8, b_0), BS_AND(t_9, b_1)), BS_XOR(BS_AND(t_9, b_0), BS_AND(t_11, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_12, b_2), BS_AND(t_12, b_0)), BS_XOR(BS_AND(t_13, b_1), BS_AND(t_13, b_0))), BS_XOR(BS_XOR(BS_AND(t_15, b_0), BS_AND(t_16, b_0)), BS_XOR(BS_AND(t_17, b_0), BS_AND(t_19, b_0))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_20, b_0), BS_AND(t_22, b_0)), BS_XOR((t_25), (t_26))), BS_XOR(BS_XOR((t_27), (t_29)), BS_XOR(BS_AND(t_0, t_18), (t_32)))), BS_XOR(BS_XOR(BS_XOR((t_33), BS_AND(t_0, t_21)), BS_XOR(BS_AND(t_0, t_24), (t_35))), BS_XOR(BS_XOR((t_37), BS_AND(t_1, t_18)), BS_XOR((t_39), BS_AND(t_1, t_21))))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_40), BS_AND(t_1, t_23)), BS_XOR(BS_AND(t_2, t_20), BS_AND(t_2, t_24))), BS_XOR(BS_XOR(BS_AND(t_3, t_23), (t_44)), BS_XOR((t_45), (t_47)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_6, t_21), (t_48)), BS_XOR(BS_AND(t_6, t_23), (t_49))), BS_XOR(BS_XOR((t_50), BS_AND(t_7, t_21)), BS_XOR((t_51), BS_AND(t_7, t_23))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_7, t_24), BS_AND(t_8, t_24)), BS_XOR((t_52), (t_53))), BS_XOR(BS_XOR(BS_AND(t_12, t_24), BS_AND(t_13, t_24)), BS_XOR(BS_AND(t_15, t_24), BS_AND(t_19, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_25, b_1), BS_AND(t_26, b_2)), BS_XOR(BS_AND(t_26, b_1), BS_AND(t_29, b_2))), BS_XOR(BS_XOR(BS_AND(t_32, b_0), BS_AND(t_33, b_0)), BS_XOR(BS_AND(t_34, b_0), BS_AND(t_35, b_2)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_36, b_1), BS_AND(t_36, b_0)), BS_XOR(BS_AND(t_37, b_0), BS_AND(t_39, b_0))), BS_XOR(BS_XOR(BS_AND(t_40, b_0), BS_AND(t_41, b_1)), BS_XOR(BS_AND(BS_AND(t_2, t_20), b_0), BS_AND(t_43, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_44, b_1), BS_AND(t_45, b_1)), BS_XOR(BS_AND(t_45, b_0), BS_AND(t_46, b_1))), BS_XOR(BS_XOR(BS_AND(t_47, b_0), BS_AND(t_48, b_0)), BS_XOR(BS_AND(t_50, b_0), BS_AND(t_51, b_0))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_52, b_0), BS_AND(t_54, b_0)), BS_XOR(BS_AND(t_25, t_19), BS_AND(t_25, t_20))), BS_XOR(BS_XOR(BS_AND(t_25, t_21), BS_AND(t_25, t_22)), BS_XOR(BS_AND(t_26, t_22), BS_AND(t_26, t_23)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_26, t_24), BS_AND(t_29, t_22)), BS_XOR(BS_AND(t_29, t_23), BS_AND(t_30, t_24))), BS_XOR(BS_XOR(BS_AND(t_35, t_23), BS_AND(t_41, t_24)), BS_XOR(BS_AND(t_44, t_22), BS_AND(t_44, t_24)))))))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_45, t_24), BS_AND(t_49, t_24)), BS_XOR(BS_AND(t_52, t_24), BS_AND(BS_AND(t_25, t_19), b_1))), BS_XOR(BS_AND(BS_AND(t_25, t_20), b_0), BS_AND(BS_AND(t_35, t_22), b_0))));
      out[1] = BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((all1), (b_7)), BS_XOR((b_4), (b_1))), BS_XOR(BS_XOR((t_4), (t_6)), BS_XOR((t_9), (t_11)))), BS_XOR(BS_XOR(BS_XOR((t_12), (t_13)), BS_XOR((t_15), (t_17))), BS_XOR(BS_XOR((t_18), (t_23)), BS_XOR((t_24), BS_AND(t_0, b_4))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_0, b_1), BS_AND(t_1, b_4)), BS_XOR(BS_AND(t_1, b_2), BS_AND(t_1, b_1))), BS_XOR(BS_XOR(BS_AND(t_1, b_0), BS_AND(t_2, b_1)), BS_XOR(BS_AND(t_2, b_0), BS_AND(t_3, b_2)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_3, b_0), BS_AND(t_4, b_1)), BS_XOR(BS_AND(t_4, b_0), BS_AND(t_5, b_0))), BS_XOR(BS_XOR(BS_AND(t_6, b_4), BS_AND(t_7, b_0)), BS_XOR(BS_AND(t_8, b_0), BS_AND(t_10, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_11, b_2), BS_AND(t_12, b_2)), BS_XOR(BS_AND(t_12, b_0), BS_AND(t_13, b_0))), BS_XOR(BS_XOR(BS_AND(t_15, b_0), BS_AND(t_16, b_0)), BS_XOR(BS_AND(t_19, b_0), BS_AND(t_22, b_0)))), BS_XOR(BS_XOR(BS_XOR((t_28), BS_AND(BS_AND(t_0, b_5), b_0)), BS_XOR((t_29), BS_AND(t_0, t_21))), BS_XOR(BS_XOR(BS_AND(t_0, t_24), (t_38)), BS_XOR((t_39), BS_AND(t_1, t_21))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR((t_40), BS_AND(t_1, t_23)), BS_XOR(BS_AND(t_1, t_24), BS_AND(t_2, t_21))), BS_XOR(BS_XOR(BS_AND(t_2, t_23), BS_AND(t_3, t_23)), BS_XOR(BS_AND(t_4, t_24), (t_45)))), BS_XOR(BS_XOR(BS_XOR((t_46), (t_47)), BS_XOR(BS_AND(t_6, t_21), BS_AND(t_6, t_23))), BS_XOR(BS_XOR(BS_AND(t_6, t_24), BS_AND(t_7, t_21)), BS_XOR((t_51), BS_AND(t_7, t_24))))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_8, t_23), BS_AND(t_8, t_24)), BS_XOR(BS_AND(t_9, t_24), (t_53))), BS_XOR(BS_XOR(BS_AND(t_11, t_23), (t_54)), BS_XOR(BS_AND(t_12, t_24), BS_AND(t_15, t_23)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_16, t_24), BS_AND(t_19, t_24)), BS_XOR(BS_AND(t_25, b_3), BS_AND(t_25, b_1))), BS_XOR(BS_XOR(BS_AND(t_27, b_0), BS_AND(t_29, b_2)), BS_XOR(BS_AND(t_29, b_1), BS_AND(t_30, b_1))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_35, b_1), BS_AND(t_36, b_1)), BS_XOR(BS_AND(t_36, b_0), BS_AND(t_37, b_0))), BS_XOR(BS_XOR(BS_AND(t_41, b_0), BS_AND(t_42, b_0)), BS_XOR(BS_AND(t_43, b_0), BS_AND(t_44, b_2)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_44, b_0), BS_AND(t_45, b_1)), BS_XOR(BS_AND(t_46, b_1), BS_AND(t_46, b_0))), BS_XOR(BS_XOR(BS_AND(t_47, b_0), BS_AND(t_49, b_1)), BS_XOR(BS_AND(t_50, b_0), BS_AND(t_51, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_52, b_0), BS_AND(t_53, b_0)), BS_XOR(BS_AND(BS_AND(t_11, t_22), b_0), BS_AND(t_55, b_0))), BS_XOR(BS_XOR(BS_AND(t_25, t_19), BS_AND(t_25, t_20)), BS_XOR(BS_AND(t_25, t_21), BS_AND(t_25, t_22)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_25, t_24), BS_AND(t_26, t_22)), BS_XOR(BS_AND(t_26, t_24), BS_AND(t_29, t_23))), BS_XOR(BS_XOR(BS_AND(t_30, t_24), BS_AND(t_35, t_23)), BS_XOR(BS_AND(t_35, t_24), BS_AND(t_36, t_24))))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_38, t_24), BS_AND(t_49, t_24)), BS_XOR(BS_AND(t_52, t_24), BS_AND(BS_AND(t_25, t_20), b_0))), BS_XOR(BS_AND(BS_AND(t_26, t_22), b_0), BS_AND(BS_AND(t_44, t_22), b_0))))));
      out[0] = BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR((b_7), (b_3)), BS_XOR((b_2), (t_0))), BS_XOR(BS_XOR((t_3), (t_4)), BS_XOR((t_7), BS_AND(b_6, b_0)))), BS_XOR(BS_XOR(BS_XOR((t_11), (t_12)), BS_XOR((t_14), (t_15))), BS_XOR(BS_XOR((t_16), (t_17)), BS_XOR((t_18), (t_22))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_0, b_5), BS_AND(t_0, b_3)), BS_XOR(BS_AND(t_0, b_2), BS_AND(t_0, b_1))), BS_XOR(BS_XOR(BS_AND(t_0, b_0), BS_AND(t_1, b_2)), BS_XOR(BS_AND(t_1, b_1), BS_AND(t_1, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_2, b_0), BS_AND(t_4, b_1)), BS_XOR(BS_AND(t_5, b_0), BS_AND(t_6, b_1))), BS_XOR(BS_XOR(BS_AND(t_7, b_2), BS_AND(t_7, b_1)), BS_XOR(BS_AND(t_9, b_1), BS_AND(t_10, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_11, b_3), BS_AND(t_11, b_2)), BS_XOR(BS_AND(t_11, b_0), BS_AND(t_12, b_0))), BS_XOR(BS_XOR(BS_AND(t_15, b_2), BS_AND(t_15, b_1)), BS_XOR(BS_AND(t_15, b_0), BS_AND(t_16, b_1)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_16, b_0), BS_AND(t_17, b_0)), BS_XOR(BS_AND(t_19, b_1), (t_26))), BS_XOR(BS_XOR((t_28), BS_AND(BS_AND(t_0, b_5), b_0)), BS_XOR((t_34), (t_36))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_1, t_18), (t_38)), BS_XOR((t_39), BS_AND(t_1, t_21))), BS_XOR(BS_XOR((t_40), BS_AND(t_2, t_20)), BS_XOR((t_42), BS_AND(t_2, t_23)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_2, t_24), BS_AND(t_3, t_23)), BS_XOR(BS_AND(t_4, t_24), BS_AND(t_6, t_18))), BS_XOR(BS_XOR((t_48), BS_AND(t_6, t_24)), BS_XOR((t_50), BS_AND(t_7, t_21))))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_7, t_23), BS_AND(t_8, t_23)), BS_XOR(BS_AND(t_8, t_24), BS_AND(t_9, t_24))), BS_XOR(BS_XOR((t_52), BS_AND(t_11, t_21)), BS_XOR(BS_AND(t_11, t_23), BS_AND(t_11, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_12, t_24), BS_AND(t_15, t_23)), BS_XOR(BS_AND(t_15, t_24), BS_AND(t_25, b_2))), BS_XOR(BS_XOR(BS_AND(t_25, b_1), BS_AND(t_26, b_0)), BS_XOR(BS_AND(t_27, b_1), BS_AND(t_27, b_0))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_28, b_0), BS_AND(t_29, b_0)), BS_XOR(BS_AND(t_30, b_0), BS_AND(t_31, b_0))), BS_XOR(BS_XOR(BS_AND(t_32, b_0), BS_AND(t_33, b_0)), BS_XOR(BS_AND(t_34, b_0), BS_AND(t_35, b_1)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_35, b_0), BS_AND(t_36, b_1)), BS_XOR(BS_AND(t_37, b_0), BS_AND(t_38, b_0))), BS_XOR(BS_XOR(BS_AND(t_40, b_0), BS_AND(t_41, b_0)), BS_XOR(BS_AND(BS_AND(t_2, t_20), b_0), BS_AND(t_42, b_0)))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_43, b_0), BS_AND(t_44, b_0)), BS_XOR(BS_AND(BS_AND(t_6, t_17), b_0), BS_AND(t_46, b_1))), BS_XOR(BS_XOR(BS_AND(t_46, b_0), BS_AND(t_47, b_0)), BS_XOR(BS_AND(t_49, b_1), BS_AND(t_49, b_0)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_50, b_0), BS_AND(t_52, b_0)), BS_XOR(BS_AND(t_53, b_0), BS_AND(t_54, b_0))), BS_XOR(BS_XOR(BS_AND(t_55, b_0), BS_AND(t_25, t_20)), BS_XOR(BS_AND(t_25, t_21), BS_AND(t_25, t_23))))), BS_XOR(BS_XOR(BS_XOR(BS_XOR(BS_AND(t_25, t_24), BS_AND(t_26, t_22)), BS_XOR(BS_AND(t_29, t_22), BS_AND(t_29, t_23))), BS_XOR(BS_XOR(BS_AND(t_29, t_24), BS_AND(t_32, t_24)), BS_XOR(BS_AND(t_36, t_24), BS_AND(t_38, t_24)))), BS_XOR(BS_XOR(BS_XOR(BS_AND(t_41, t_24), BS_AND(t_44, t_23)), BS_XOR(BS_AND(t_44, t_24), BS_AND(t_45, t_24))), BS_XOR(BS_XOR(BS_AND(BS_AND(t_25, t_20), b_0), BS_AND(BS_AND(t_26, t_22), b_0)), BS_XOR(BS_AND(BS_AND(t_35, t_22), b_0), BS_AND(BS_AND(t_44, t_22), b_0))))))));
#elif BOOLMODE==10
      /* QuineMcCluskey optimized DNF with X(N)OR */
      out[0] = BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_2, BS_NOT(b_4))), BS_XOR(BS_XOR(b_5, b_6), b_7)), BS_AND(BS_AND(BS_AND(b_0, b_1), b_4), BS_NOT(BS_OR(b_2, b_3)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_4), BS_AND(BS_NOT(b_2), b_5)), BS_NOT(BS_XOR(b_3, b_6))), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_3), b_5), BS_AND(BS_NOT(b_2), b_7)), BS_AND(b_4, b_6)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_5), BS_XOR(BS_XOR(b_1, b_3), b_6)), BS_NOT(BS_OR(b_2, b_4))), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_4)), BS_XOR(b_2, b_3)), BS_NOT(BS_OR(b_5, b_6)))), BS_OR(BS_AND(BS_AND(BS_XOR(b_0, b_6), BS_NOT(BS_OR(b_1, b_3))), BS_AND(b_2, BS_NOT(b_4))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_NOT(BS_XOR(b_1, b_7))), BS_AND(b_2, b_5))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_5), BS_XOR(b_2, b_4)), BS_NOT(BS_OR(b_3, b_6))), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_3), BS_XOR(b_4, b_5)), BS_NOT(BS_OR(b_1, b_2))), BS_AND(BS_NOT(b_6), b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_NOT(BS_XOR(b_1, b_5))), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_AND(BS_XOR(b_0, b_4), b_6), BS_AND(b_1, b_2)), BS_AND(b_3, b_5)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_2, BS_NOT(b_6)), BS_NOT(BS_OR(b_3, b_4))), BS_XOR(b_5, b_7)), BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_OR(b_1, b_7))), BS_XOR(BS_XOR(b_2, b_3), BS_XOR(b_4, b_6)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_7), BS_XOR(b_2, b_4)), BS_NOT(BS_OR(b_3, b_6))), BS_AND(BS_AND(BS_AND(BS_XOR(b_0, b_4), BS_NOT(b_6)), BS_NOT(BS_OR(b_1, b_2))), BS_AND(b_3, b_5)))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_XOR(BS_XOR(b_2, b_3), b_4)), BS_AND(BS_NOT(b_5), b_6)), BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_6)), BS_NOT(BS_OR(b_1, b_4))), BS_XOR(b_2, b_3))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_3)), BS_XOR(b_2, b_4)), BS_AND(BS_NOT(b_5), b_7)), BS_AND(BS_AND(BS_AND(b_0, b_7), BS_XOR(b_2, b_4)), BS_NOT(BS_OR(b_3, b_5))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_XOR(b_2, b_4), BS_NOT(BS_OR(b_3, b_6))), BS_AND(b_5, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_XOR(b_1, b_4))), BS_AND(BS_NOT(b_2), b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_4), BS_AND(BS_NOT(b_2), b_7)), BS_XOR(b_3, b_5)), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_AND(BS_NOT(b_1), b_4)), BS_XOR(BS_XOR(b_2, b_6), b_7))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_XOR(BS_XOR(b_1, b_4), b_5))), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(b_7)), BS_AND(b_2, b_3)), BS_XOR(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_4), BS_AND(BS_NOT(b_2), b_7)), BS_NOT(BS_XOR(b_3, b_5))), BS_AND(BS_AND(BS_XOR(b_0, b_7), BS_AND(b_1, b_5)), BS_NOT(BS_OR(b_4, b_6))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_XOR(BS_XOR(b_1, b_4), b_5), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_XOR(b_0, b_2), BS_AND(b_1, b_4)), BS_NOT(BS_OR(b_3, b_5)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_4), BS_NOT(b_5)), BS_NOT(BS_OR(b_2, b_3))), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_NOT(b_7)), BS_XOR(b_1, b_5)), BS_AND(b_2, b_6))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_XOR(b_1, b_7), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_4, BS_NOT(b_5))), BS_AND(BS_AND(BS_AND(b_1, b_5), BS_XOR(b_2, b_4)), BS_NOT(BS_OR(b_3, b_6)))), BS_OR(BS_AND(BS_AND(BS_XOR(b_0, b_1), BS_NOT(BS_OR(b_2, b_4))), BS_AND(b_3, BS_NOT(b_6))), BS_AND(BS_AND(BS_XOR(b_0, b_3), BS_NOT(BS_OR(b_2, b_5))), BS_AND(b_4, b_6)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_XOR(b_3, b_4)), BS_AND(BS_NOT(b_5), b_7)), BS_AND(BS_AND(BS_AND(b_1, BS_NOT(b_7)), BS_NOT(BS_OR(b_2, b_6))), BS_XOR(b_3, b_4))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_2, b_3)), BS_AND(b_4, BS_NOT(b_7))), BS_XOR(b_5, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, BS_NOT(b_5))), BS_XOR(BS_XOR(b_3, b_4), b_7))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_XOR(b_2, b_3)), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_3), b_4), BS_AND(b_1, b_5)), BS_AND(BS_NOT(b_6), b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_XOR(b_3, b_6))), BS_AND(BS_NOT(b_4), b_5)), BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_2)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_NOT(b_6), b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_4)), BS_AND(b_1, b_7)), BS_NOT(BS_OR(b_2, b_6))), BS_AND(BS_AND(BS_AND(b_1, b_5), BS_NOT(b_6)), BS_NOT(BS_OR(b_3, b_4)))), BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_3), b_4), BS_XOR(b_2, b_7)), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_XOR(b_0, b_6), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_4), b_7)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_7)), BS_NOT(BS_OR(b_1, b_2))), BS_XOR(BS_XOR(b_3, b_4), b_6)), BS_AND(BS_AND(BS_AND(BS_XOR(b_0, b_6), b_7), BS_AND(b_1, b_2)), BS_AND(b_3, b_5))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_2)), BS_AND(b_4, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_5)), BS_XOR(BS_XOR(b_2, b_3), b_4)), BS_AND(b_6, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(b_1, b_5)), BS_NOT(BS_OR(b_2, b_3))), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_AND(b_0, b_5), BS_XOR(BS_XOR(b_2, b_3), b_6)), BS_NOT(BS_OR(b_4, b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_4), b_7), BS_NOT(BS_OR(b_2, b_6))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(BS_NOT(b_2), b_5)), BS_NOT(BS_XOR(b_4, b_7))))))));
      out[1] = BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_XOR(b_3, b_7)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_2), b_6), BS_AND(b_1, b_5)), BS_NOT(BS_OR(b_3, b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, b_4)), BS_XOR(b_3, b_5)), BS_AND(BS_AND(BS_AND(b_0, b_6), BS_AND(BS_NOT(b_1), b_7)), BS_XOR(b_3, b_5)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_4), b_6), BS_NOT(BS_OR(b_1, b_2))), BS_XOR(b_5, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_NOT(BS_XOR(BS_XOR(b_1, b_3), BS_XOR(b_4, b_6)))), BS_AND(b_5, BS_NOT(b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_4), BS_AND(BS_NOT(b_2), b_6)), BS_XOR(b_3, b_5)), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_1), b_4), BS_AND(b_2, b_6)), BS_NOT(BS_OR(b_3, b_7)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_XOR(b_3, b_5)), BS_AND(b_4, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_1, b_7), BS_NOT(BS_OR(b_3, b_4))), BS_NOT(BS_OR(b_5, b_6)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_7)), b_6), BS_AND(b_1, b_5)), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_AND(b_3, BS_NOT(b_6))), BS_NOT(BS_XOR(b_5, b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), b_7), BS_XOR(b_3, b_6)), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_3)), b_7), BS_XOR(b_2, b_4)), BS_NOT(BS_OR(b_5, b_6)))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_5), BS_XOR(b_2, b_4)), BS_NOT(BS_OR(b_3, b_7))), BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_6)), BS_NOT(BS_XOR(b_2, b_7))), BS_NOT(BS_OR(b_4, b_5))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_NOT(b_7)), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_XOR(b_0, b_5), BS_NOT(BS_OR(b_1, b_2))), BS_AND(b_4, BS_NOT(b_6)))), BS_OR(BS_AND(BS_AND(BS_AND(BS_XOR(b_0, b_4), BS_NOT(b_7)), BS_AND(b_1, b_2)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_7)), BS_XOR(BS_XOR(b_1, b_4), b_6)), BS_NOT(BS_OR(b_3, b_5))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), b_5), BS_AND(b_2, b_4)), BS_XOR(b_3, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_2)), BS_AND(BS_NOT(b_4), b_7))), BS_OR(BS_AND(BS_AND(BS_XOR(b_0, b_5), BS_AND(b_2, b_7)), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_AND(BS_NOT(b_0), b_5), BS_AND(b_1, b_2)), BS_NOT(BS_XOR(b_3, b_7)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_4), BS_XOR(b_1, b_7)), BS_AND(BS_NOT(b_3), b_6)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_7)), BS_AND(b_2, b_6)), BS_XOR(b_3, b_4))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(b_0), b_6), BS_AND(b_2, b_5)), BS_XOR(BS_XOR(b_3, b_4), b_7)), BS_AND(BS_AND(BS_AND(BS_XOR(b_0, b_1), BS_NOT(b_6)), BS_NOT(BS_OR(b_2, b_5))), BS_AND(b_3, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_7)), BS_NOT(BS_OR(b_1, b_2))), BS_AND(b_3, BS_NOT(b_5))), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_5)), b_3), BS_NOT(BS_OR(b_6, b_7)))), BS_OR(BS_AND(BS_AND(BS_XOR(b_0, b_6), BS_AND(b_1, b_2)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_AND(BS_AND(b_0, b_3), b_7), BS_NOT(BS_OR(b_1, b_5)))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_5), BS_XOR(b_2, b_7)), BS_AND(BS_NOT(b_3), b_6)), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_NOT(b_6)), BS_XOR(b_1, b_7)), BS_AND(b_2, b_5))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_7))), BS_NOT(BS_XOR(b_4, b_5))), BS_AND(BS_AND(BS_NOT(BS_OR(b_2, b_3)), BS_XOR(b_4, b_7)), BS_AND(b_5, b_6)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(b_0), b_6), BS_AND(b_1, b_5)), BS_XOR(b_2, b_4)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_5)), BS_XOR(b_2, b_4))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_2, b_5)), BS_XOR(b_4, b_7)), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_4), b_7), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_3), b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_4)), BS_XOR(b_3, b_6)), BS_AND(b_5, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_4)), BS_AND(b_2, b_7)), BS_NOT(BS_XOR(BS_XOR(b_3, b_5), b_6)))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(b_0), b_6), BS_AND(b_1, b_2)), BS_NOT(BS_XOR(BS_XOR(b_4, b_5), b_7))), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_4), b_7), BS_AND(b_1, b_2)), BS_AND(BS_NOT(b_3), b_5)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_6)), BS_XOR(b_3, b_5)), BS_AND(b_4, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(b_6)), BS_NOT(BS_OR(b_1, b_5)))), BS_OR(BS_AND(BS_AND(BS_XOR(b_0, b_3), BS_AND(b_1, b_4)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_1)), BS_NOT(BS_OR(b_4, b_5))), BS_NOT(BS_OR(b_6, b_7))))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_NOT(BS_XOR(BS_XOR(b_1, b_4), b_5))), BS_AND(b_3, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_6)), BS_NOT(BS_OR(b_1, b_2))), BS_XOR(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_7)), BS_XOR(b_1, b_4)), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), b_4), BS_NOT(BS_OR(b_1, b_5))), BS_XOR(b_3, b_6)))), BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_6)), BS_XOR(b_1, b_3)), BS_NOT(BS_OR(b_4, b_5))))));
      out[2] = BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(b_6)), BS_AND(b_3, b_7)), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_2), b_5), BS_AND(b_1, b_4)), BS_NOT(BS_OR(b_3, b_6)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_4), BS_XOR(BS_XOR(b_1, b_2), b_5)), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_XOR(b_1, b_3)), BS_AND(b_5, BS_NOT(b_6))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), b_4), BS_NOT(BS_OR(b_2, b_6))), BS_AND(BS_AND(BS_AND(BS_XOR(b_0, b_1), BS_NOT(b_5)), BS_AND(b_2, b_3)), BS_AND(b_4, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_XOR(b_4, b_7)), BS_AND(b_5, BS_NOT(b_6))), BS_AND(BS_AND(BS_XOR(b_1, b_3), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_4), b_7))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_NOT(BS_XOR(b_4, b_7))), BS_AND(b_5, BS_NOT(b_6))), BS_AND(BS_AND(BS_AND(b_1, b_6), BS_XOR(b_2, b_3)), BS_NOT(BS_OR(b_4, b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_6)), BS_XOR(b_2, b_4)), BS_AND(b_3, b_7)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_XOR(b_2, b_5))), BS_AND(b_4, BS_NOT(b_6))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_XOR(b_1, b_3)), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_7)), BS_NOT(BS_OR(b_2, b_5)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_5), BS_XOR(b_2, b_3)), BS_AND(b_6, BS_NOT(b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_XOR(b_2, b_7)), BS_AND(b_3, b_4)))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, BS_NOT(b_4))), BS_XOR(BS_XOR(b_5, b_6), b_7)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_6)), BS_AND(b_2, b_7)), BS_XOR(b_3, b_4))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_XOR(b_1, b_5))), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_XOR(b_2, b_3)), BS_AND(b_6, BS_NOT(b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_XOR(BS_XOR(b_1, b_3), b_6)), BS_AND(b_5, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_4))), BS_XOR(b_6, b_7))), BS_OR(BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_1), b_5), BS_AND(b_3, b_7)), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_NOT(b_7)), BS_NOT(BS_OR(b_3, b_4))), BS_XOR(b_5, b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_6)), BS_XOR(b_1, b_7)), BS_NOT(BS_OR(b_2, b_4))), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_6)), BS_AND(b_3, b_5)), BS_XOR(b_4, b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_NOT(b_7)), BS_AND(b_2, b_4)), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_3), BS_XOR(b_5, b_6)), BS_NOT(BS_OR(b_1, b_7))), BS_AND(b_2, b_4)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(BS_XOR(b_0, b_4), b_7)), BS_AND(b_1, b_2)), BS_AND(BS_NOT(b_3), b_5)), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_1), b_3), BS_NOT(BS_OR(b_4, b_6))), BS_AND(b_5, b_7))), BS_OR(BS_AND(BS_AND(BS_XOR(b_0, b_5), BS_NOT(BS_OR(b_1, b_2))), BS_AND(b_3, BS_NOT(b_6))), BS_AND(BS_AND(BS_AND(b_1, BS_NOT(b_7)), BS_NOT(BS_OR(b_2, b_4))), BS_XOR(b_5, b_6))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_4), BS_XOR(BS_XOR(b_1, b_3), b_5)), BS_AND(BS_NOT(b_2), b_7)), BS_AND(BS_AND(BS_AND(b_0, b_4), BS_AND(BS_NOT(b_2), b_7)), BS_XOR(b_3, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(BS_XOR(b_0, b_7), b_6), BS_AND(b_1, b_2)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_XOR(b_2, b_4)), BS_AND(BS_NOT(b_6), b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_XOR(b_2, b_7), BS_AND(b_3, b_4)), BS_AND(b_5, BS_NOT(b_6))), BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_3)), BS_NOT(BS_OR(b_2, b_6))), BS_AND(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_5), BS_XOR(b_2, b_3)), BS_AND(BS_NOT(b_4), b_6)), BS_AND(BS_AND(BS_XOR(b_0, b_2), BS_AND(b_1, b_4)), BS_NOT(BS_OR(b_5, b_7)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_XOR(b_0, b_2), BS_AND(b_4, BS_NOT(b_7))), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_AND(BS_NOT(b_2), b_5)), BS_XOR(b_6, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_XOR(b_1, b_7)), BS_NOT(BS_OR(b_2, b_5))), BS_AND(BS_AND(BS_XOR(b_0, b_7), BS_AND(BS_NOT(b_2), b_5)), BS_AND(b_3, b_4)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_2)), BS_AND(b_3, b_5)), BS_AND(BS_AND(BS_AND(b_0, b_1), b_3), BS_NOT(BS_OR(b_2, b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_XOR(b_3, b_5)), BS_AND(b_4, b_7)), BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_3)), BS_AND(b_1, b_5)), BS_AND(BS_NOT(b_6), b_7)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), b_7), BS_XOR(b_1, b_2)), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_AND(b_3, b_7)), BS_XOR(b_5, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_6), BS_XOR(b_1, b_7)), BS_NOT(BS_OR(b_2, b_5))), BS_AND(BS_AND(BS_AND(b_0, b_7), BS_NOT(BS_OR(b_1, b_2))), BS_XOR(b_5, b_6)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_XOR(b_1, b_3)), BS_AND(b_5, BS_NOT(b_6))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_2, b_7))), BS_XOR(b_4, b_6))), BS_AND(BS_AND(BS_NOT(BS_XOR(BS_XOR(b_0, b_3), b_7)), BS_NOT(BS_OR(b_1, b_2))), BS_AND(b_4, b_5))))));
      out[3] = BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_XOR(b_0, b_4), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_3, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(b_7)), BS_AND(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(b_1, b_7)), BS_AND(BS_NOT(b_3), b_6)), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_XOR(b_0, b_4), BS_NOT(BS_OR(b_1, b_2))), BS_AND(b_5, BS_NOT(b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(b_1, BS_NOT(BS_OR(b_3, b_4))), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(BS_NOT(b_2), b_5)), BS_XOR(b_3, b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), b_5), BS_NOT(BS_OR(b_2, b_3))), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_1), b_6), BS_NOT(BS_OR(b_2, b_3))), BS_AND(BS_NOT(b_4), b_5))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_XOR(b_0, b_7), BS_NOT(b_5)), BS_AND(b_1, b_3)), BS_AND(b_4, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_6)), BS_XOR(b_1, b_3)), BS_AND(b_5, BS_NOT(b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(b_7)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(b_5)), BS_XOR(b_1, b_7)), BS_AND(b_3, b_4)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_3), BS_XOR(b_5, b_7)), BS_AND(b_1, b_2)), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(BS_NOT(b_2), b_5)), BS_XOR(b_4, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), b_7), BS_XOR(b_2, b_4)), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_XOR(b_1, b_5), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_6, BS_NOT(b_7))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_OR(b_1, b_3))), BS_XOR(b_6, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_XOR(b_2, b_5)), BS_AND(BS_NOT(b_3), b_4))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_NOT(b_6)), BS_AND(b_1, b_2)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(b_6)), BS_NOT(BS_OR(b_2, b_3))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(BS_NOT(b_3), b_5)), BS_NOT(BS_XOR(b_4, b_6))), BS_AND(BS_AND(BS_XOR(b_0, b_7), BS_NOT(BS_OR(b_1, b_2))), BS_AND(b_5, b_6))), BS_OR(BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_4), b_5), BS_AND(BS_NOT(b_1), b_7)), BS_AND(b_3, b_6)), BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_7)), BS_NOT(BS_OR(b_2, b_4))), BS_XOR(b_3, b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(b_1, b_2)), BS_AND(b_3, b_5)), BS_NOT(BS_OR(b_4, b_7))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), b_5), BS_NOT(BS_OR(b_1, b_6))), BS_XOR(b_3, b_4))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(BS_NOT(b_2), b_7)), BS_XOR(BS_XOR(b_4, b_5), b_6)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), b_6), BS_XOR(b_1, b_4)), BS_AND(b_3, b_5)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(BS_NOT(b_2), b_7)), BS_AND(b_3, b_4)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_XOR(BS_XOR(b_1, b_3), b_6)), BS_AND(b_5, BS_NOT(b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_AND(b_1, b_3)), BS_AND(b_6, b_7)), BS_AND(BS_AND(b_0, BS_NOT(BS_OR(b_3, b_4))), BS_NOT(BS_OR(b_5, b_7)))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_4)), BS_NOT(BS_OR(b_1, b_3))), BS_XOR(b_6, b_7)), BS_AND(BS_AND(BS_AND(b_0, b_6), BS_NOT(BS_XOR(BS_XOR(b_1, b_2), BS_XOR(b_4, b_5)))), BS_NOT(BS_OR(b_3, b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_5)), BS_XOR(b_2, b_6)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_AND(BS_AND(b_0, b_5), BS_XOR(b_3, b_7)), BS_NOT(BS_OR(b_4, b_6))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_XOR(b_0, b_2), BS_AND(b_3, BS_NOT(b_7))), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_7)), b_6), BS_AND(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(b_0), b_7), BS_NOT(BS_XOR(b_1, b_2))), BS_AND(b_3, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_7)), b_5), BS_XOR(BS_XOR(b_3, b_4), b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_3, b_4)), BS_NOT(b_6)), BS_AND(b_5, b_7)), BS_AND(BS_AND(BS_XOR(b_1, b_7), BS_NOT(BS_OR(b_2, b_3))), BS_NOT(BS_OR(b_4, b_5)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_AND(BS_NOT(b_4), b_6)), BS_XOR(b_5, b_7)), BS_AND(BS_AND(BS_NOT(BS_XOR(BS_XOR(b_0, b_1), b_2)), BS_AND(b_3, b_4)), BS_AND(b_6, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_4))), BS_NOT(BS_XOR(b_3, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_6)), BS_NOT(BS_XOR(b_2, b_3))), BS_AND(b_5, BS_NOT(b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_XOR(b_3, b_4))), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_NOT(BS_XOR(BS_XOR(b_0, b_4), BS_XOR(b_5, b_7))), BS_NOT(BS_OR(b_1, b_2))), BS_AND(b_3, b_6)))))));
      out[4] = BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_XOR(b_2, b_5)), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_XOR(b_0, b_3), BS_AND(b_1, b_5)), BS_AND(b_6, BS_NOT(b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, BS_NOT(b_3))), BS_NOT(BS_XOR(b_5, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_2, b_7)), BS_AND(b_3, b_6)), BS_NOT(BS_XOR(b_4, b_5))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_7)), BS_XOR(b_2, b_4)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(b_1, b_5), BS_XOR(b_2, b_4)), BS_AND(b_6, BS_NOT(b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_AND(b_2, b_7)), BS_XOR(b_3, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_XOR(BS_XOR(b_1, b_2), BS_XOR(b_3, b_6))), BS_AND(b_4, b_7))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_XOR(b_2, b_3)), BS_AND(b_5, b_7)), BS_AND(BS_AND(BS_XOR(b_0, b_3), BS_AND(b_2, b_4)), BS_AND(b_5, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_6)), BS_NOT(BS_XOR(BS_XOR(b_3, b_4), b_5))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_OR(b_3, b_6))), BS_NOT(BS_XOR(b_5, b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_2), b_7), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_XOR(b_1, b_2))), BS_NOT(BS_OR(b_3, b_4)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_3, b_4)), BS_XOR(b_5, b_7)), BS_AND(BS_AND(BS_AND(b_1, b_4), BS_NOT(BS_OR(b_3, b_6))), BS_AND(b_5, b_7)))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_XOR(b_0, b_6), BS_NOT(BS_OR(b_1, b_2))), BS_AND(BS_NOT(b_4), b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, BS_NOT(b_5))), BS_NOT(BS_XOR(b_4, b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(BS_NOT(b_2), b_5)), BS_XOR(b_4, b_7)), BS_AND(BS_AND(BS_AND(BS_NOT(b_0), b_7), BS_AND(b_2, b_4)), BS_XOR(BS_XOR(b_3, b_5), b_6)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_XOR(BS_XOR(b_1, b_2), b_3))), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), b_6), BS_NOT(BS_OR(b_4, b_5)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_4, b_5)), BS_XOR(b_6, b_7)), BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_XOR(b_2, b_6))), BS_AND(BS_NOT(b_3), b_7))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(b_5)), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_7)), b_5), BS_AND(b_3, b_4))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_7)), BS_XOR(b_1, b_5)), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_AND(BS_AND(b_1, b_3), BS_NOT(b_7)), BS_XOR(b_2, b_6)), BS_NOT(BS_OR(b_4, b_5))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_OR(b_2, b_6))), BS_NOT(BS_XOR(b_5, b_7))), BS_AND(BS_AND(BS_NOT(BS_XOR(BS_XOR(b_0, b_2), BS_XOR(b_6, b_7))), BS_NOT(BS_OR(b_1, b_3))), BS_AND(BS_NOT(b_4), b_5))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_5), b_7), BS_NOT(BS_OR(b_2, b_6))), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_5), b_6), BS_NOT(BS_OR(b_1, b_3))), BS_AND(BS_NOT(b_4), b_7))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(BS_XOR(b_0, b_3), b_7)), BS_NOT(BS_OR(b_1, b_2))), BS_AND(BS_NOT(b_5), b_6)), BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_7)), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_4), b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_5), BS_XOR(BS_XOR(b_1, b_3), b_6)), BS_AND(BS_NOT(b_4), b_7)), BS_AND(BS_AND(BS_AND(BS_XOR(b_0, b_4), BS_NOT(b_6)), BS_AND(b_1, b_7)), BS_NOT(BS_OR(b_2, b_3))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(BS_NOT(b_3), b_5)), BS_XOR(b_6, b_7)), BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_5)), BS_NOT(BS_OR(b_1, b_6))), BS_AND(b_2, b_3))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), b_5), BS_NOT(BS_OR(b_1, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_XOR(BS_XOR(b_2, b_4), b_5)), BS_AND(b_3, b_7))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_3), BS_AND(BS_NOT(b_2), b_6)), BS_NOT(BS_XOR(BS_XOR(b_4, b_5), b_7))), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_2), b_3), BS_AND(b_4, b_6)), BS_AND(BS_NOT(b_5), b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(BS_XOR(b_0, b_5), b_7)), BS_NOT(BS_OR(b_1, b_3))), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_XOR(BS_XOR(b_1, b_2), BS_XOR(b_4, b_6))), BS_AND(b_3, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_2)), BS_AND(b_1, b_4)), BS_AND(b_6, BS_NOT(b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_XOR(b_2, b_7)), BS_AND(b_5, b_6))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(b_7)), BS_NOT(BS_OR(b_3, b_6))), BS_XOR(b_4, b_5))))));
      out[5] = BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_3)), BS_XOR(b_2, b_4)), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_5))), BS_XOR(b_4, b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_5)), BS_AND(b_1, b_2)), BS_AND(BS_NOT(b_4), b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_AND(b_1, b_2)), BS_XOR(b_3, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_AND(BS_NOT(b_1), b_5)), BS_AND(b_3, b_4)), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_XOR(BS_XOR(b_1, b_5), b_6)), BS_AND(BS_NOT(b_2), b_4))), BS_OR(BS_AND(BS_AND(BS_XOR(b_0, b_5), BS_NOT(BS_OR(b_1, b_2))), BS_AND(BS_NOT(b_3), b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_XOR(b_1, b_4)), BS_NOT(BS_OR(b_6, b_7)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_6)), BS_NOT(BS_OR(b_3, b_5))), BS_AND(b_4, b_7)), BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_6)), BS_AND(b_1, b_4)), BS_AND(BS_NOT(b_2), b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_3, b_6)), BS_AND(BS_NOT(b_5), b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_NOT(BS_XOR(b_3, b_5))), BS_AND(BS_NOT(b_6), b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_XOR(b_0, b_4), BS_NOT(b_7)), BS_NOT(BS_OR(b_1, b_3))), BS_AND(b_2, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_XOR(BS_XOR(b_1, b_2), b_3)), BS_AND(b_6, BS_NOT(b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_7)), BS_NOT(BS_XOR(BS_XOR(b_2, b_4), BS_XOR(b_5, b_6)))), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_3)), BS_XOR(b_2, b_7)), BS_NOT(BS_OR(b_4, b_5))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_XOR(b_0, b_1), BS_NOT(BS_OR(b_2, b_3))), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_1, b_5))), BS_AND(b_3, BS_NOT(b_6)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_AND(BS_NOT(b_1), b_4)), BS_XOR(BS_XOR(b_2, b_5), b_6)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_3), b_5), BS_XOR(BS_XOR(b_1, b_4), b_6)), BS_NOT(BS_OR(b_2, b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_5)), BS_AND(b_2, b_6)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_AND(BS_XOR(b_1, b_4), BS_AND(b_2, b_6)), BS_NOT(BS_OR(b_5, b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_3), BS_AND(BS_NOT(b_2), b_5)), BS_NOT(BS_XOR(b_4, b_6))), BS_AND(BS_AND(BS_XOR(b_1, b_2), BS_AND(b_3, b_4)), BS_AND(b_5, b_7))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_XOR(b_4, b_7)), BS_AND(BS_NOT(b_5), b_6)), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_5), b_6), BS_AND(b_1, b_4)), BS_AND(BS_NOT(b_3), b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_4), BS_XOR(BS_XOR(b_2, b_3), b_6)), BS_AND(BS_NOT(b_5), b_7)), BS_AND(BS_AND(BS_XOR(b_0, b_5), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_3, b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(b_1, b_5)), BS_AND(b_3, b_4)), BS_AND(b_6, BS_NOT(b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_5)), BS_NOT(b_7)), BS_AND(b_4, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_4), b_7), BS_NOT(BS_OR(b_3, b_5))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_NOT(b_5)), BS_NOT(BS_XOR(BS_XOR(b_1, b_4), BS_XOR(b_6, b_7))))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_XOR(b_1, b_2), BS_NOT(BS_OR(b_4, b_5))), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_XOR(BS_XOR(b_1, b_2), b_4), BS_NOT(BS_OR(b_3, b_5))), BS_NOT(BS_OR(b_6, b_7)))), BS_OR(BS_AND(BS_AND(b_0, BS_NOT(BS_OR(b_1, b_3))), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_XOR(b_1, b_4), BS_NOT(BS_OR(b_2, b_6))), BS_AND(b_3, b_5)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_AND(b_4, BS_NOT(b_7))), BS_XOR(b_5, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_6)), BS_AND(b_1, b_7)), BS_XOR(b_2, b_4))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_XOR(BS_XOR(b_1, b_3), BS_XOR(b_5, b_6))), BS_AND(b_4, b_7)), BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_7)), BS_XOR(BS_XOR(b_1, b_2), BS_XOR(b_5, b_6))), BS_NOT(BS_OR(b_3, b_4)))))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_4), BS_NOT(BS_XOR(BS_XOR(b_2, b_3), b_5))), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_XOR(b_0, b_1), BS_NOT(BS_OR(b_2, b_3))), BS_NOT(BS_OR(b_5, b_7))))));
      out[6] = BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_6)), BS_XOR(BS_XOR(b_2, b_3), b_4)), BS_AND(b_5, BS_NOT(b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(BS_NOT(b_2), b_6)), BS_XOR(b_4, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_2, b_3), b_6), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_7)), BS_NOT(BS_OR(b_1, b_4))), BS_XOR(b_2, b_3)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_7)), BS_AND(b_2, b_5)), BS_XOR(b_4, b_6)), BS_AND(BS_AND(BS_XOR(b_0, b_5), BS_AND(b_1, b_2)), BS_AND(b_4, BS_NOT(b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(BS_XOR(b_0, b_7), b_6), BS_AND(b_1, b_4)), BS_NOT(BS_OR(b_2, b_5))), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_XOR(b_3, b_5)), BS_AND(BS_NOT(b_4), b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(b_1, b_7)), BS_AND(b_2, b_5)), BS_NOT(BS_OR(b_3, b_6))), BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_3)), BS_AND(b_1, b_4)), BS_NOT(BS_OR(b_5, b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_5), BS_XOR(b_1, b_3)), BS_AND(BS_NOT(b_4), b_7)), BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_1)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, b_6)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_2, b_3), BS_AND(b_4, BS_NOT(b_7))), BS_XOR(b_5, b_6)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_4, b_6)), BS_NOT(BS_OR(b_5, b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_3), BS_AND(BS_NOT(b_2), b_7)), BS_AND(b_4, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(BS_OR(b_3, b_5))), BS_AND(b_4, b_7)))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_XOR(BS_XOR(b_1, b_2), BS_XOR(b_4, b_7))), BS_AND(b_3, b_6)), BS_AND(BS_AND(BS_AND(b_0, b_7), BS_NOT(BS_OR(b_1, b_2))), BS_XOR(b_3, b_6))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(b_1, b_4)), BS_AND(b_2, b_6)), BS_NOT(BS_OR(b_3, b_7))), BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_7)), BS_XOR(b_1, b_3)), BS_NOT(BS_OR(b_2, b_4))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_2, b_3), b_6), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_AND(BS_XOR(b_0, b_5), BS_NOT(b_7)), BS_AND(b_1, b_2)), BS_AND(b_3, b_6))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_6)), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_3), b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_4)), BS_AND(b_2, b_6)), BS_XOR(b_3, b_5))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_4))), BS_XOR(b_6, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(BS_OR(b_3, b_5))), BS_AND(b_6, b_7))), BS_OR(BS_AND(BS_AND(BS_XOR(b_0, b_4), BS_NOT(BS_OR(b_3, b_6))), BS_AND(b_5, BS_NOT(b_7))), BS_AND(BS_AND(BS_XOR(b_1, b_2), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, BS_NOT(b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_XOR(b_2, b_7)), BS_AND(BS_NOT(b_4), b_6)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_2)), BS_AND(b_3, b_7)), BS_XOR(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_XOR(b_0, b_1), BS_NOT(BS_OR(b_2, b_4))), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), b_7), BS_XOR(b_2, b_3)), BS_AND(b_5, b_6))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_XOR(b_0, b_6), b_7), BS_AND(b_1, b_3)), BS_NOT(BS_OR(b_2, b_5))), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_6)), b_4), BS_AND(b_1, b_2)), BS_NOT(BS_XOR(BS_XOR(b_3, b_5), b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_4))), BS_XOR(b_2, b_5)), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), b_6), BS_AND(b_1, b_2)), BS_XOR(b_4, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_4)), BS_NOT(b_7)), BS_AND(b_2, b_6)), BS_AND(BS_AND(BS_AND(b_0, b_5), BS_XOR(b_1, b_3)), BS_AND(BS_NOT(b_4), b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), b_5), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_AND(BS_AND(b_0, b_5), BS_XOR(BS_XOR(b_1, b_3), b_4)), BS_AND(b_6, b_7))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), b_7), BS_XOR(b_2, b_3)), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_7)), BS_XOR(BS_XOR(b_1, b_2), b_5)), BS_NOT(BS_OR(b_4, b_6)))), BS_OR(BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_1), b_4), BS_AND(b_3, b_6)), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_XOR(b_0, b_5), BS_NOT(BS_OR(b_1, b_4))), BS_AND(b_2, BS_NOT(b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_5), BS_XOR(b_1, b_3)), BS_NOT(BS_OR(b_2, b_4))), BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_OR(b_1, b_3))), BS_NOT(BS_XOR(b_6, b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_XOR(b_2, b_6))), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_6)), BS_NOT(BS_OR(b_1, b_5))), BS_NOT(BS_XOR(b_4, b_7))))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_XOR(b_1, b_6), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, b_7)), BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_6)), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_XOR(b_4, b_7)))), BS_OR(BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_3), b_7), BS_AND(b_1, BS_NOT(b_6))), BS_NOT(BS_OR(b_2, b_4))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_6)), BS_NOT(BS_XOR(BS_XOR(b_1, b_4), b_7))), BS_AND(b_2, b_5)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_4)), BS_XOR(b_2, b_7)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_6)), BS_NOT(BS_OR(b_2, b_4))), BS_NOT(BS_XOR(b_3, b_7)))), BS_AND(BS_AND(BS_XOR(b_0, b_4), BS_NOT(BS_OR(b_2, b_3))), BS_NOT(BS_OR(b_6, b_7)))))));
      out[7] = BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_XOR(b_1, b_5))), BS_NOT(BS_OR(b_4, b_7))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_2)), BS_XOR(b_3, b_6)), BS_AND(b_4, b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(BS_NOT(b_3), b_7)), BS_XOR(b_5, b_6)), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_1), b_5), BS_NOT(BS_OR(b_2, b_4))), BS_AND(b_3, BS_NOT(b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_XOR(b_1, b_7)), BS_AND(b_4, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_NOT(b_5)), BS_AND(b_1, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_5)), BS_XOR(b_4, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, BS_NOT(b_4))), BS_XOR(b_5, b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_5)), BS_NOT(BS_XOR(b_6, b_7))), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_1), b_5), BS_AND(b_3, b_4)), BS_AND(b_6, b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_3)), BS_AND(BS_NOT(b_1), b_7)), BS_AND(b_4, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_XOR(BS_XOR(b_2, b_5), b_6))), BS_AND(b_3, BS_NOT(b_4))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(b_3)), BS_AND(b_4, b_7)), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_3), b_6), BS_AND(b_1, BS_NOT(b_5))), BS_NOT(BS_OR(b_2, b_4)))), BS_OR(BS_AND(BS_AND(BS_NOT(b_1), BS_AND(b_2, b_3)), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_AND(BS_XOR(b_0, b_7), b_6), BS_NOT(BS_OR(b_1, b_3))), BS_AND(b_2, b_4)))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_NOT(b_7)), BS_AND(b_1, b_4)), BS_XOR(b_2, b_5)), BS_AND(BS_AND(BS_XOR(b_0, b_1), BS_NOT(BS_OR(b_2, b_3))), BS_AND(BS_NOT(b_5), b_6))), BS_OR(BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_2), BS_XOR(b_4, b_7)), BS_NOT(BS_OR(b_1, b_5))), BS_AND(b_3, BS_NOT(b_6))), BS_AND(BS_AND(BS_XOR(b_0, b_5), BS_NOT(BS_OR(b_1, b_2))), BS_AND(b_4, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_XOR(b_1, b_5)), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(b_7)), BS_XOR(b_1, b_6)), BS_AND(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(b_0), b_7), BS_XOR(b_1, b_5)), BS_AND(b_4, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_7)), BS_XOR(BS_XOR(b_4, b_5), b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), b_7), BS_AND(b_1, b_3)), BS_NOT(BS_XOR(b_4, b_6))), BS_AND(BS_AND(BS_XOR(BS_XOR(b_0, b_3), b_6), BS_AND(b_1, BS_NOT(b_7))), BS_NOT(BS_OR(b_2, b_5)))), BS_OR(BS_AND(BS_AND(BS_XOR(BS_XOR(b_1, b_2), b_5), BS_AND(b_3, b_6)), BS_NOT(BS_OR(b_4, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), b_6), BS_AND(b_1, b_4)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_AND(BS_NOT(b_3), b_7)), BS_XOR(b_4, b_5)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_XOR(BS_XOR(b_2, b_4), b_6))), BS_AND(BS_NOT(b_3), b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_XOR(b_2, b_3))), BS_AND(b_5, BS_NOT(b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_XOR(BS_XOR(b_2, b_4), b_5)), BS_AND(b_3, BS_NOT(b_7)))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_3, b_5)), BS_NOT(BS_XOR(b_4, b_7))), BS_AND(BS_AND(BS_XOR(b_2, b_7), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, b_6))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_XOR(b_0, b_5)), BS_AND(b_1, b_4)), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_XOR(BS_XOR(b_2, b_3), BS_XOR(b_4, b_5))), BS_AND(BS_NOT(b_6), b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_XOR(b_1, b_2), BS_AND(b_3, b_6)), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_NOT(BS_XOR(b_1, b_6)), BS_AND(b_2, b_3)), BS_AND(b_5, b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_XOR(BS_XOR(b_1, b_2), b_5)), BS_AND(b_3, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_2, b_3)), BS_XOR(b_4, b_5)), BS_AND(b_6, b_7))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_XOR(BS_XOR(b_1, b_4), b_7)), BS_AND(b_5, BS_NOT(b_6))), BS_AND(BS_AND(BS_AND(b_1, b_2), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, BS_NOT(b_7)))), BS_OR(BS_AND(BS_AND(BS_XOR(b_1, b_4), BS_AND(b_2, b_3)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_XOR(b_2, b_6)), BS_AND(BS_NOT(b_3), b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_XOR(BS_XOR(b_1, b_5), b_6), BS_NOT(BS_OR(b_2, b_3))), BS_AND(BS_NOT(b_4), b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), b_7), BS_AND(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_XOR(b_2, b_6)), BS_AND(b_5, b_7)), BS_AND(BS_AND(BS_XOR(b_0, b_4), BS_NOT(BS_OR(b_1, b_6))), BS_AND(b_5, b_7)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_XOR(b_0, b_5), BS_NOT(BS_OR(b_1, b_4))), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_NOT(b_7)), BS_AND(b_1, b_5)), BS_XOR(b_2, b_4))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_4, BS_NOT(b_6))), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), b_2), BS_XOR(b_1, b_7)), BS_NOT(BS_OR(b_4, b_6))))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_7)), BS_AND(b_2, b_3)), BS_NOT(BS_XOR(b_4, b_5))), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_AND(BS_NOT(b_1), b_3)), BS_NOT(BS_XOR(b_4, b_5)))))));
#elif BOOLMODE==11
      /* QuineMcCluskey optimized DNF without X(N)OR */
      out[0] = BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_1, b_6), b_7), BS_NOT(BS_OR(b_2, b_3))), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_AND(b_0, b_1), b_4), BS_NOT(BS_OR(b_2, b_3)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_4), b_6)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), b_7), BS_NOT(BS_OR(b_1, b_5))), BS_AND(b_3, b_6)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_4), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_5, b_7)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(b_7)), BS_NOT(BS_OR(b_1, b_6))), BS_AND(b_3, b_4))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(BS_OR(b_3, b_5))), BS_AND(b_4, b_7)), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), b_6), BS_AND(b_1, b_3)), BS_NOT(BS_OR(b_5, b_7)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_6)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(BS_NOT(b_2), b_6)), BS_AND(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_6), BS_NOT(b_7)), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_1, b_3))), BS_NOT(BS_OR(b_4, b_6))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_AND(b_4, b_6)), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_2)), BS_AND(BS_NOT(b_6), b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_2, b_7), BS_NOT(BS_OR(b_3, b_4))), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_4, b_6)), BS_NOT(BS_OR(b_5, b_7))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_2), BS_AND(b_3, b_5)), BS_AND(BS_NOT(b_4), b_7)), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_2, b_4))), BS_NOT(BS_OR(b_6, b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_7), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), b_7), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_5, b_6)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_3, b_5)), BS_NOT(BS_OR(b_4, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_AND(BS_NOT(b_2), b_7)), BS_AND(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(b_7)), BS_AND(b_3, b_4)), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, b_5)), BS_NOT(BS_OR(b_4, b_6)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_AND(b_1, b_5), BS_NOT(b_6)), BS_NOT(BS_OR(b_2, b_3)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_2))), BS_AND(b_5, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_AND(b_2, b_5)), BS_AND(b_6, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_1, b_3))), BS_AND(BS_AND(b_4, b_5), BS_AND(b_6, BS_NOT(b_7)))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_NOT(b_6), b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_2, b_3)), BS_AND(b_4, b_5)), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_4)), BS_NOT(BS_OR(b_5, b_7)))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, b_7)), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(b_6)), BS_AND(b_3, b_4)), BS_AND(b_5, b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_AND(b_3, b_7)), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_AND(b_1, b_5)), BS_AND(BS_NOT(b_6), b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(b_4)), BS_NOT(BS_OR(b_1, b_2))), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_3, b_6)), BS_NOT(BS_OR(b_5, b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_3, BS_NOT(b_7))), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), b_6), BS_AND(b_1, b_2)), BS_NOT(BS_OR(b_5, b_7)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_3)), BS_AND(b_2, BS_NOT(b_7))), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_AND(b_0, b_4), BS_NOT(b_5)), BS_NOT(BS_OR(b_2, b_3)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_3, b_4)), BS_AND(BS_NOT(b_5), b_6)), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), b_5), BS_NOT(BS_OR(b_2, b_6))), BS_AND(b_3, b_4)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_3)), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_AND(b_2, b_5), BS_NOT(BS_OR(b_3, b_4))), BS_NOT(BS_OR(b_6, b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_OR(b_2, b_7))), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_5), b_6), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_3, b_4))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_3)), BS_AND(BS_NOT(b_4), b_5)), BS_AND(BS_AND(BS_AND(b_1, b_3), BS_NOT(BS_OR(b_2, b_5))), BS_AND(b_4, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_5), BS_NOT(b_6)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_5)), BS_AND(b_3, b_4)), BS_AND(b_6, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_5)), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), b_6), BS_AND(b_3, b_5)), BS_NOT(BS_OR(b_4, b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, b_3)), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_6)), BS_AND(b_2, b_3)), BS_AND(b_5, b_7))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_2))), BS_AND(BS_NOT(b_4), b_7)), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_AND(BS_NOT(b_2), b_6)), BS_AND(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_3, b_5)), BS_AND(BS_NOT(b_4), b_6)), BS_AND(BS_AND(BS_AND(b_0, b_4), BS_NOT(b_6)), BS_NOT(BS_OR(b_2, b_3))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, BS_NOT(b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_AND(b_1, b_2)), BS_AND(b_5, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_4), b_7), BS_NOT(BS_OR(b_2, b_6))), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_5)), b_4), BS_AND(b_2, b_3)), BS_NOT(BS_OR(b_6, b_7))))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_NOT(b_3)), BS_AND(b_4, b_5)), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_2)), BS_AND(b_4, BS_NOT(b_5)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_2)), BS_AND(b_4, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_3, b_4)), BS_AND(BS_NOT(b_5), b_6)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(b_0), BS_AND(b_1, b_2)), BS_AND(b_3, b_4)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_NOT(b_5), b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_5)), BS_NOT(BS_OR(b_4, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_4, b_6))), BS_AND(b_5, b_7))))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_AND(BS_NOT(b_2), b_7)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(b_1)), BS_AND(b_3, b_4)), BS_AND(b_6, b_7)))));
      out[1] = BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(BS_OR(b_3, b_4))), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_6)), BS_AND(b_2, b_3)), BS_AND(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(b_7)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(b_1, b_3), BS_NOT(BS_OR(b_2, b_5))), BS_AND(b_4, b_6)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_6)), BS_AND(b_3, b_4)), BS_AND(b_5, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, b_4)), BS_AND(b_5, b_6))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_AND(b_4, b_5)), BS_AND(b_6, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_1, b_7), BS_NOT(BS_OR(b_3, b_4))), BS_NOT(BS_OR(b_5, b_6)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_2)), BS_AND(BS_NOT(b_4), b_5)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(BS_NOT(b_3), b_6)), BS_AND(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_AND(b_2, b_7), BS_NOT(BS_OR(b_3, b_4))), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_AND(BS_NOT(b_0), BS_AND(b_2, b_3)), BS_AND(b_4, b_5)), BS_AND(b_6, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_7)), b_6), BS_AND(b_1, b_5)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, b_4)), BS_AND(BS_NOT(b_3), b_5))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_AND(b_3, b_5)), BS_AND(b_6, BS_NOT(b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, b_3)), BS_AND(b_4, BS_NOT(b_5))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_2))), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_AND(b_1, b_2)), BS_AND(b_5, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(b_0), b_6), BS_AND(b_1, b_3)), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_AND(b_0, b_4), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_5, b_6))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_NOT(b_7)), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_AND(b_0, b_4), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_5, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_2), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, BS_NOT(b_6))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_OR(b_3, b_7))), BS_AND(b_4, b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(b_5)), BS_NOT(BS_OR(b_1, b_3))), BS_AND(b_4, b_6))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_AND(b_3, b_4)), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_6)), BS_AND(b_2, b_4)), BS_AND(b_5, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_OR(b_4, b_5))), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_2)), BS_AND(BS_NOT(b_4), b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(BS_NOT(b_4), b_7)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, BS_NOT(b_6))), BS_NOT(BS_OR(b_4, b_5)))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), b_7), BS_AND(b_1, b_2)), BS_AND(b_4, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_3, b_6))), BS_AND(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), b_7), BS_AND(b_1, b_3)), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_6, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_7)), BS_NOT(BS_OR(b_2, b_4))), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_AND(BS_NOT(b_0), b_7), BS_AND(b_1, b_3)), BS_AND(b_4, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_OR(b_3, b_4))), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_2)), BS_AND(BS_NOT(b_4), b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_4), BS_NOT(BS_OR(b_1, b_5))), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_4)), BS_NOT(b_5)), BS_AND(b_2, b_3)), BS_AND(b_6, b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(BS_OR(b_3, b_4))), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_4)), BS_AND(b_3, b_5)), BS_AND(BS_NOT(b_6), b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), b_7), BS_NOT(BS_OR(b_1, b_5))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_OR(b_3, b_4))), BS_NOT(BS_OR(b_5, b_6)))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_2), BS_AND(b_3, b_6)), BS_NOT(BS_OR(b_4, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_AND(b_3, b_5)), BS_AND(BS_NOT(b_6), b_7)))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_7))), BS_AND(b_3, b_4)), BS_AND(BS_AND(BS_AND(b_1, b_4), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_5, BS_NOT(b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), b_6), BS_NOT(BS_OR(b_1, b_3))), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_AND(BS_NOT(b_0), b_6), BS_AND(b_1, b_2)), BS_AND(b_3, b_5)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_5)), b_3), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_4))), BS_AND(BS_NOT(b_6), b_7))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(b_6)), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_4, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_AND(b_1, b_2)), BS_AND(b_5, b_7))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(b_0), b_7), BS_AND(b_1, b_2)), BS_AND(b_3, b_5)), BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_4, b_6)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_4, b_5))), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_3, b_4)), BS_AND(b_6, BS_NOT(b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_2), BS_NOT(BS_OR(b_3, b_7))), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(b_6)), BS_NOT(BS_OR(b_1, b_5)))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), b_5), BS_AND(b_1, b_3)), BS_NOT(BS_OR(b_4, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_OR(b_1, b_3))), BS_AND(b_6, b_7)))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_4), BS_NOT(BS_OR(b_1, b_5))), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_4), b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_7))), BS_AND(b_3, b_6)), BS_AND(BS_AND(BS_AND(b_0, b_4), BS_NOT(BS_OR(b_1, b_2))), BS_AND(BS_NOT(b_5), b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_4), BS_AND(BS_NOT(b_3), b_7)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, b_3)), BS_AND(b_4, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), b_6), BS_AND(b_2, b_4)), BS_NOT(BS_OR(b_3, b_7))), BS_AND(BS_AND(BS_AND(b_1, b_5), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_6, b_7))))), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_NOT(b_6), b_7))));
      out[2] = BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_5)), BS_AND(BS_NOT(b_4), b_7)), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), b_6), BS_NOT(BS_OR(b_3, b_4))), BS_NOT(BS_OR(b_5, b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(b_6)), BS_AND(b_3, b_7)), BS_AND(BS_AND(BS_AND(b_1, b_2), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, b_6)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_NOT(b_7)), BS_AND(b_1, b_2)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), b_6), BS_AND(b_1, b_2)), BS_AND(b_3, b_4))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), b_4), BS_NOT(BS_OR(b_2, b_6))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_4)), BS_AND(b_5, BS_NOT(b_6)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_4), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_AND(b_1, b_2), BS_NOT(BS_OR(b_3, b_5))), BS_AND(b_6, BS_NOT(b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_2)), BS_AND(BS_NOT(b_4), b_5)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_6))), BS_AND(b_5, BS_NOT(b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_2, b_3), BS_AND(b_4, b_5)), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_AND(b_1, b_4), BS_NOT(BS_OR(b_3, b_5))), BS_AND(BS_NOT(b_6), b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_5)), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_7)), BS_NOT(BS_OR(b_2, b_5))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), b_4), BS_NOT(BS_OR(b_1, b_3))), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_AND(BS_NOT(b_3), b_7)), BS_AND(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_4), BS_NOT(BS_OR(b_2, b_5))), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_3, b_4)), BS_AND(b_5, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_AND(b_3, b_7)), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_AND(b_4, b_5)), BS_NOT(BS_OR(b_6, b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_4)), BS_NOT(b_6)), BS_AND(b_3, b_7)), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_3, b_7))), BS_AND(b_5, b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_4)), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_NOT(BS_OR(b_3, b_6))), BS_AND(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(b_5)), BS_AND(b_3, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_5, BS_NOT(b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_2), BS_NOT(BS_OR(b_3, b_6))), BS_AND(b_4, b_7)), BS_AND(BS_AND(BS_AND(b_1, b_3), BS_NOT(BS_OR(b_2, b_4))), BS_AND(b_5, b_6))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_6)), BS_AND(b_3, b_4)), BS_AND(b_5, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_5, b_6)))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_4))), BS_AND(b_6, BS_NOT(b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_2)), BS_AND(b_5, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_AND(b_3, b_7)), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_3, b_5)), BS_NOT(BS_OR(b_6, b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_OR(b_2, b_4))), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_7), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_4, b_5)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, b_7)), BS_NOT(BS_OR(b_3, b_6)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_5))), BS_AND(b_4, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_NOT(b_7)), BS_AND(b_2, b_4))), BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), b_6), BS_NOT(BS_OR(b_1, b_5))), BS_AND(b_3, b_4)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, b_5)), BS_NOT(BS_OR(b_3, b_6))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_6))), BS_AND(b_3, b_5)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_2)), BS_AND(BS_NOT(b_4), b_6))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_3)), BS_AND(b_4, BS_NOT(b_7))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_4, b_5)), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_4)), BS_AND(b_2, b_3)), BS_AND(b_5, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_OR(b_1, b_2))), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_AND(b_2, b_5), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_6, BS_NOT(b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_3), BS_AND(BS_NOT(b_2), b_7)), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_AND(b_2, b_3), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_NOT(b_6), b_7))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(b_6)), BS_AND(b_2, b_7)), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_AND(b_1, b_3), BS_NOT(BS_OR(b_2, b_4))), BS_AND(b_6, BS_NOT(b_7)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_2, b_7))), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(b_1, b_3), BS_NOT(BS_OR(b_2, b_4))), BS_AND(b_5, BS_NOT(b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_6), BS_NOT(BS_OR(b_2, b_4))), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_2)), BS_AND(b_3, b_5)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), b_3), BS_NOT(BS_OR(b_2, b_7))), BS_AND(BS_AND(BS_AND(b_1, b_3), BS_AND(BS_NOT(b_2), b_7)), BS_AND(b_5, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(b_6)), BS_NOT(BS_OR(b_1, b_5))), BS_AND(b_4, b_7)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_6)), BS_NOT(BS_OR(b_3, b_7))))))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_6), BS_NOT(BS_OR(b_1, b_2))), BS_AND(BS_NOT(b_5), b_7)), BS_AND(BS_AND(BS_AND(b_1, b_2), BS_NOT(BS_OR(b_3, b_6))), BS_AND(b_5, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_4, b_6))), BS_AND(b_5, b_7))));
      out[3] = BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(b_7)), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_2, b_4))), BS_NOT(BS_OR(b_6, b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), b_7), BS_NOT(BS_OR(b_1, b_5))), BS_AND(b_3, b_6)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_6)), BS_NOT(BS_OR(b_2, b_5))), BS_AND(b_4, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(b_1, BS_NOT(BS_OR(b_3, b_4))), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), b_5), BS_NOT(BS_OR(b_2, b_3)))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), b_7), BS_AND(b_2, b_3)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), b_7), BS_NOT(BS_OR(b_2, b_5))), BS_AND(b_3, b_4))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_6), BS_NOT(BS_OR(b_2, b_3))), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_AND(b_3, b_4)), BS_AND(b_6, b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(b_7)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_4))), BS_AND(b_6, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_AND(b_1, b_3)), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_AND(b_2, b_3), BS_NOT(BS_OR(b_4, b_6))), BS_AND(b_5, BS_NOT(b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_3)), BS_AND(b_2, b_4)), BS_AND(b_6, BS_NOT(b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(BS_NOT(b_2), b_7)), BS_AND(b_3, b_6)))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), b_7), BS_NOT(BS_OR(b_1, b_5))), BS_AND(b_3, b_4)), BS_AND(BS_AND(BS_AND(b_0, b_4), BS_NOT(BS_OR(b_1, b_3))), BS_AND(b_5, BS_NOT(b_6)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_3)), BS_NOT(b_7)), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_AND(b_2, b_3)), BS_NOT(BS_OR(b_6, b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(BS_NOT(b_3), b_6)), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_NOT(b_6)), BS_AND(b_1, b_2))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_4)), BS_NOT(BS_OR(b_3, b_5))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_3)), BS_AND(b_2, b_4)), BS_AND(b_5, b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(b_6)), BS_NOT(BS_OR(b_2, b_3))), BS_AND(BS_AND(BS_AND(b_1, b_2), BS_AND(b_3, b_5)), BS_AND(BS_NOT(b_4), b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_6), BS_NOT(BS_OR(b_2, b_3))), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_AND(b_1, b_2), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_NOT(b_6), b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_4), BS_AND(BS_NOT(b_3), b_7)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(BS_OR(b_4, b_5))), BS_AND(b_6, b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(b_6)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_AND(BS_AND(b_1, b_2), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_NOT(b_6), b_7))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_OR(b_2, b_3))), BS_AND(BS_NOT(b_4), b_6)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(BS_NOT(b_2), b_7)), BS_AND(b_3, b_4))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), b_5), BS_NOT(BS_OR(b_2, b_4))), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_1, b_3))), BS_AND(b_6, BS_NOT(b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(b_0, BS_NOT(BS_OR(b_3, b_4))), BS_NOT(BS_OR(b_5, b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_6)), BS_AND(b_2, b_3)), BS_AND(b_5, BS_NOT(b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(b_2)), BS_AND(b_5, b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_7)), b_6), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_4))), BS_AND(b_3, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_NOT(b_7)), BS_AND(b_1, b_3)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_4, b_6))), BS_AND(b_5, BS_NOT(b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_6)), BS_AND(b_1, b_2)), BS_AND(b_5, BS_NOT(b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_5)), BS_NOT(BS_OR(b_6, b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_2), BS_AND(b_3, b_5)), BS_NOT(BS_OR(b_4, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_3, b_4)), BS_NOT(b_6)), BS_AND(b_5, b_7)))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_3, b_4)), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(b_6)), BS_NOT(BS_OR(b_1, b_4))), BS_AND(b_5, b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_3)), BS_NOT(b_7)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), b_5), BS_NOT(BS_OR(b_4, b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_AND(BS_NOT(b_1), b_6)), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), b_5), BS_NOT(BS_OR(b_3, b_6)))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(b_0), b_7), BS_AND(b_1, b_2)), BS_AND(b_3, b_6)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(b_7)), BS_NOT(BS_OR(b_1, b_2))), BS_AND(b_4, b_6))))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(b_1)), BS_AND(b_3, b_4)), BS_AND(b_6, b_7)))));
      out[4] = BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_2)), BS_AND(b_3, b_4)), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_4, b_5)), BS_AND(b_6, BS_NOT(b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_3)), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_2, b_6)), BS_AND(BS_NOT(b_5), b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, BS_NOT(b_7))), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(BS_OR(b_3, b_5))), BS_AND(b_6, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_5)), BS_AND(b_2, b_3)), BS_AND(b_4, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_3)), BS_AND(b_4, b_7))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_3)), BS_AND(b_4, BS_NOT(b_6))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(b_5)), BS_AND(b_3, b_4)), BS_AND(b_6, b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, BS_NOT(b_6))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_OR(b_4, b_5))), BS_NOT(BS_OR(b_6, b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_4, b_5)), BS_AND(b_6, BS_NOT(b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_NOT(BS_OR(b_3, b_7))), BS_AND(b_4, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_2), BS_NOT(BS_OR(b_4, b_7))), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_AND(BS_NOT(b_3), b_6)), BS_AND(b_4, b_5)))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_OR(b_1, b_4))), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_4), b_6))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), b_7), BS_AND(b_1, b_4)), BS_NOT(BS_OR(b_3, b_6))), BS_AND(BS_AND(BS_AND(b_1, b_2), BS_AND(b_3, b_5)), BS_AND(b_6, BS_NOT(b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), b_7), BS_AND(b_2, b_5)), BS_NOT(BS_OR(b_3, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(BS_NOT(b_4), b_7)), BS_AND(b_5, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_OR(b_3, b_7))), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(b_1, b_4), BS_NOT(BS_OR(b_3, b_6))), BS_AND(b_5, b_7))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_2, b_4)), BS_AND(b_3, b_6)), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_AND(BS_NOT(b_4), b_7)), BS_AND(b_5, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_AND(b_4, b_5)), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_AND(b_2, b_3)), BS_AND(BS_NOT(b_6), b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), b_6), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_2, b_4))), BS_AND(b_5, BS_NOT(b_6)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_3), b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(b_5)), BS_AND(b_6, b_7))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_7)), b_5), BS_AND(b_3, b_4)), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(BS_OR(b_4, b_6))), BS_AND(b_5, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_4)), BS_AND(b_6, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_3, b_4))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), b_4), BS_NOT(BS_OR(b_3, b_5))), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_AND(BS_AND(b_1, b_3), b_7), BS_NOT(BS_OR(b_2, b_5))), BS_AND(b_4, b_6))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_3, b_4)), BS_AND(BS_NOT(b_5), b_7)), BS_AND(BS_AND(BS_AND(b_1, b_4), BS_NOT(BS_OR(b_2, b_7))), BS_AND(b_5, b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, b_4)), BS_AND(b_6, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_OR(b_3, b_5))), BS_NOT(BS_OR(b_6, b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_AND(b_2, b_5)), BS_AND(b_6, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_0, b_5), b_7), BS_NOT(BS_OR(b_2, b_6))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_2, b_7)), BS_AND(b_3, b_4)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_7), BS_NOT(b_6)), BS_NOT(BS_OR(b_1, b_3))), BS_NOT(BS_OR(b_4, b_5)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, b_3)), BS_AND(b_6, BS_NOT(b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_3, b_4))), BS_NOT(BS_OR(b_5, b_7))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), b_7), BS_AND(b_1, b_3)), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(b_7)), BS_NOT(BS_OR(b_1, b_2))), BS_AND(b_4, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_7), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_6)), BS_AND(b_2, b_3)), BS_AND(b_4, b_5)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_4)), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_AND(BS_NOT(b_0), b_6), BS_AND(b_2, b_3)), BS_AND(b_4, b_5))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_7)), BS_AND(b_2, b_3)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_AND(BS_NOT(b_3), b_7)), BS_AND(b_5, b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_6)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), b_7), BS_NOT(BS_OR(b_2, b_5))), BS_AND(b_4, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), b_5), BS_NOT(BS_OR(b_1, b_6))), BS_AND(BS_AND(BS_AND(b_0, b_7), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_3, b_4))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(BS_NOT(b_2), b_7)), BS_AND(b_3, b_5)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_4)), BS_AND(BS_NOT(b_5), b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_7), BS_NOT(BS_OR(b_2, b_3))), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_7))), BS_AND(b_4, b_5)))))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_5)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, BS_NOT(b_7))), BS_NOT(BS_OR(b_3, b_5)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_OR(b_2, b_5))), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_3)), BS_AND(BS_NOT(b_5), b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_7)), BS_AND(b_1, b_3)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_AND(b_2, b_3)), BS_NOT(BS_OR(b_5, b_6)))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_AND(b_2, b_3)), BS_AND(BS_NOT(b_5), b_7)))));
      out[5] = BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_4), BS_NOT(BS_OR(b_2, b_5))), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_2)), BS_AND(BS_NOT(b_6), b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, b_4)), BS_NOT(BS_OR(b_3, b_5))), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_AND(BS_NOT(b_1), b_5)), BS_AND(b_3, b_4)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_2), BS_AND(b_3, b_6)), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_7)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(b_1), b_7), BS_AND(b_2, b_3)), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_6))), BS_AND(b_4, b_5))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_6))), BS_AND(b_4, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_3)), BS_AND(BS_NOT(b_4), b_6))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_4)), BS_AND(b_2, BS_NOT(b_7))), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_4, b_5))), BS_AND(b_6, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_1, b_6))), BS_AND(b_3, b_4)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_3, b_6)), BS_AND(BS_NOT(b_5), b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(BS_NOT(b_3), b_7)), BS_AND(b_4, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_5, b_6)))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_4, b_6)), BS_AND(BS_NOT(b_5), b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_2, b_6)), BS_AND(b_3, b_4)), BS_AND(b_5, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_4), b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_AND(b_1, b_2)), BS_AND(b_3, BS_NOT(b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_3), BS_NOT(BS_OR(b_2, b_4))), BS_AND(b_5, BS_NOT(b_6))), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_1, b_5))), BS_AND(b_3, BS_NOT(b_6)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_4))), BS_AND(BS_NOT(b_5), b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_2)), BS_AND(b_4, b_7))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_4)), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_AND(b_0, b_4), BS_NOT(BS_OR(b_3, b_5))), BS_AND(b_6, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_AND(b_4, b_5)), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), b_7), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, b_6)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_7)), BS_AND(b_2, b_5)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_AND(b_1, b_2)), BS_AND(b_6, b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_2, b_6)), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_AND(b_1, b_2), BS_NOT(BS_OR(b_3, b_5))), BS_AND(b_4, BS_NOT(b_6)))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_2), BS_AND(BS_NOT(b_3), b_7)), BS_AND(b_4, b_6)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(BS_NOT(b_2), b_7)), BS_AND(b_3, b_4))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_2), BS_AND(b_4, b_6)), BS_AND(BS_NOT(b_5), b_7)), BS_AND(BS_AND(BS_AND(b_1, b_3), BS_NOT(BS_OR(b_2, b_6))), BS_AND(b_5, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_4, b_7)), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_AND(b_3, b_4)), BS_AND(b_5, BS_NOT(b_6)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_2))), BS_AND(BS_NOT(BS_OR(b_4, b_7)), BS_AND(b_5, b_6))), BS_AND(BS_AND(BS_AND(b_0, b_6), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_3, b_5)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_6)), BS_NOT(BS_OR(b_2, b_5))), BS_AND(b_3, b_4))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_6)), BS_NOT(BS_OR(b_4, b_7))), BS_AND(BS_AND(BS_AND(b_1, b_3), BS_AND(BS_NOT(b_2), b_6)), BS_AND(b_4, b_5)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_5)), BS_NOT(b_7)), BS_AND(b_4, b_6)), BS_AND(BS_AND(BS_AND(b_1, b_4), b_7), BS_NOT(BS_OR(b_3, b_5)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_NOT(b_6)), BS_NOT(BS_OR(b_3, b_5))), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_NOT(b_5), b_7)))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_3), BS_AND(BS_NOT(b_2), b_7)), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_AND(b_1, b_2), BS_AND(b_3, b_6)), BS_AND(BS_NOT(b_5), b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_3), BS_NOT(BS_OR(b_2, b_6))), BS_AND(b_4, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_3, b_5)), BS_AND(BS_NOT(b_6), b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(b_0, BS_NOT(BS_OR(b_1, b_3))), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_AND(b_1, b_2)), BS_AND(BS_NOT(b_6), b_7))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(b_7)), BS_NOT(BS_OR(b_2, b_5))), BS_AND(b_3, b_6)), BS_AND(BS_AND(BS_AND(b_1, BS_NOT(b_7)), BS_NOT(BS_OR(b_2, b_4))), BS_NOT(BS_OR(b_5, b_6)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, BS_NOT(b_7))), BS_NOT(BS_OR(b_3, b_5))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_NOT(b_7)), BS_NOT(BS_OR(b_3, b_6)))), BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), b_7), BS_NOT(BS_OR(b_1, b_6))), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_AND(b_1, b_3), BS_AND(b_4, b_5)), BS_AND(b_6, BS_NOT(b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_3, b_7))), BS_AND(b_4, b_6)), BS_AND(BS_AND(BS_AND(b_1, b_3), BS_NOT(BS_OR(b_2, b_4))), BS_NOT(BS_OR(b_6, b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_AND(b_2, b_6)), BS_AND(BS_NOT(b_5), b_7)), BS_AND(BS_AND(BS_AND(b_1, b_2), BS_NOT(BS_OR(b_4, b_5))), BS_AND(b_6, BS_NOT(b_7))))))))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_AND(b_1, b_2)), BS_AND(b_3, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(b_7)), BS_NOT(BS_OR(b_3, b_6)))));
      out[6] = BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_7)), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_6), b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_6)), BS_AND(b_2, b_4)), BS_AND(b_5, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_1, b_4))), BS_NOT(BS_OR(b_5, b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_2, b_3), b_6), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_4)), BS_NOT(BS_OR(b_5, b_7)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_4)), BS_NOT(b_7)), BS_AND(b_2, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_AND(b_1, b_3)), BS_AND(BS_NOT(b_5), b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_1, BS_NOT(b_7)), BS_NOT(BS_OR(b_2, b_3))), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_7))), BS_AND(b_4, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_2, b_4))), BS_AND(b_5, b_7)), BS_AND(BS_AND(BS_AND(b_2, b_3), BS_AND(b_4, b_5)), BS_NOT(BS_OR(b_6, b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_AND(b_3, b_6)), BS_AND(BS_NOT(b_5), b_7)), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_NOT(b_6)), BS_AND(b_1, b_2)), BS_AND(b_5, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), b_5), BS_AND(b_2, b_3)), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_2))), BS_AND(BS_NOT(b_4), b_5)))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_5)), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_AND(b_1, b_3), BS_AND(BS_NOT(b_2), b_7)), BS_AND(b_4, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_1, b_4))), BS_AND(b_3, BS_NOT(b_5))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_2)), BS_AND(b_4, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_4))), BS_AND(b_5, b_6))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_AND(BS_NOT(b_4), b_7)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, BS_NOT(b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), BS_AND(b_1, b_2)), BS_AND(BS_AND(b_3, b_4), BS_AND(BS_NOT(b_6), b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_2, b_3), b_6), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_7)), BS_AND(b_1, b_2)), BS_AND(b_4, b_5)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_6)), BS_AND(b_2, b_7)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_AND(BS_AND(b_0, b_4), BS_NOT(BS_OR(b_1, b_5))), BS_AND(BS_NOT(b_6), b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_3)), BS_AND(BS_NOT(b_5), b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, BS_NOT(b_6))), BS_NOT(BS_OR(b_3, b_4)))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_4)), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_2, b_4))), BS_AND(BS_NOT(b_6), b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_4)), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_4))), BS_AND(b_6, BS_NOT(b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_1, b_4))), BS_AND(b_3, b_7)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_3), b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(BS_OR(b_3, b_5))), BS_AND(b_4, b_7)), BS_AND(BS_AND(BS_AND(b_1, b_5), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_NOT(b_6), b_7))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_7)), BS_AND(b_1, b_2)), BS_AND(b_4, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, BS_NOT(b_7))), BS_NOT(BS_OR(b_4, b_6)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_3)), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_4), b_6)), BS_AND(BS_AND(BS_AND(b_0, b_6), BS_NOT(BS_OR(b_1, b_2))), BS_AND(BS_NOT(b_3), b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), BS_NOT(b_5)), BS_AND(b_1, b_2)), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_OR(b_2, b_3))), BS_NOT(BS_OR(b_4, b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_4, b_6)), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_7)), BS_AND(b_1, b_2)), BS_AND(b_3, b_6)))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_OR(b_1, b_3))), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_4)), BS_AND(b_5, BS_NOT(b_6)))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_5), BS_NOT(BS_OR(b_2, b_3))), BS_NOT(BS_OR(b_4, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_1), b_5), BS_NOT(BS_OR(b_3, b_4))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_3), b_6)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), b_7), BS_AND(b_3, b_4)), BS_AND(b_5, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_4), BS_NOT(BS_OR(b_3, b_5))), BS_AND(b_6, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_OR(b_2, b_5))), BS_AND(b_3, b_4))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(b_7)), BS_AND(b_3, b_5)), BS_NOT(BS_OR(b_4, b_6))), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_AND(BS_NOT(b_3), b_7)), BS_AND(b_5, b_6))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_4), BS_NOT(BS_OR(b_1, b_2))), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_4, BS_NOT(b_5))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_2))), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_4))), BS_AND(b_6, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_4, b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_NOT(BS_OR(b_3, b_5))), BS_AND(b_4, b_6)))))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_3)), BS_AND(b_4, b_5)), BS_NOT(BS_OR(b_6, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_OR(b_1, b_3))), BS_AND(b_6, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_2), BS_NOT(BS_OR(b_3, b_6))), BS_AND(b_5, b_7)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(BS_OR(b_2, b_5))), BS_AND(b_3, BS_NOT(b_6))))));
      out[7] = BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(b_7)), BS_NOT(BS_OR(b_1, b_6))), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_3, b_5))), BS_AND(b_6, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(b_7)), BS_NOT(BS_OR(b_1, b_3))), BS_AND(b_4, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_6)), BS_NOT(BS_OR(b_4, b_5))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(BS_OR(b_1, b_4))), BS_AND(b_3, BS_NOT(b_5))), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_AND(b_3, b_4)), BS_AND(b_5, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_4)), BS_AND(b_5, BS_NOT(b_6))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, b_7)), BS_NOT(BS_OR(b_4, b_5)))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_NOT(b_5)), BS_AND(b_1, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_5)), BS_NOT(BS_OR(b_4, b_6)))), BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_NOT(b_7)), BS_NOT(BS_OR(b_2, b_3))), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_AND(BS_NOT(b_1), b_7)), BS_AND(b_4, b_6)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_4, b_5)), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(b_3)), BS_AND(b_4, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_2), BS_NOT(BS_OR(b_4, b_7))), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_NOT(b_1), BS_AND(b_2, b_3)), BS_AND(b_4, b_5)))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), b_6), BS_NOT(BS_OR(b_1, b_7))), BS_AND(b_4, b_5)), BS_AND(BS_AND(BS_AND(b_2, b_5), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_6, BS_NOT(b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_4), b_6)), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_3, b_4)), BS_AND(b_5, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_1, b_3)), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_5)), BS_AND(b_6, b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_AND(b_1, b_4)), BS_AND(b_6, BS_NOT(b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_2, b_3)), BS_AND(BS_NOT(b_4), b_7)), BS_AND(b_5, b_6))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), b_7), BS_AND(b_2, b_6)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), b_7), BS_AND(b_2, b_4)), BS_NOT(BS_OR(b_3, b_6)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_4)), BS_AND(b_5, b_7)), BS_AND(BS_AND(BS_AND(b_1, b_2), BS_AND(b_3, b_5)), BS_AND(BS_NOT(b_4), b_6)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_7)), BS_NOT(BS_OR(b_3, b_5))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(BS_NOT(b_3), b_7)), BS_AND(b_5, b_6))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_2)), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), b_7), BS_NOT(BS_OR(b_2, b_3))), BS_NOT(BS_OR(b_4, b_6)))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_3, BS_NOT(b_6))), BS_NOT(BS_OR(b_4, b_5))), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), b_5), BS_AND(b_1, b_4)), BS_NOT(BS_OR(b_3, b_7)))), BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_3), BS_NOT(BS_OR(b_2, b_5))), BS_AND(b_6, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_2, b_5)), BS_NOT(BS_OR(b_4, b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_6), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_3, b_5))), BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), BS_NOT(b_7)), BS_AND(b_1, b_2)), BS_NOT(BS_OR(b_5, b_6)))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_4, b_6))), BS_AND(b_5, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_AND(b_4, b_5)), BS_AND(b_6, b_7))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_2, b_3)), BS_AND(b_4, b_6)), BS_AND(BS_NOT(b_5), b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_5)), b_6), BS_AND(b_1, b_4))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_4)), BS_AND(b_2, b_3)), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_1, b_4))), BS_NOT(BS_OR(b_5, b_7))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_3)), BS_AND(b_5, BS_NOT(b_6))), BS_AND(BS_AND(BS_AND(b_1, b_3), BS_NOT(BS_OR(b_2, b_4))), BS_AND(BS_NOT(b_5), b_6))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_3)), b_4), BS_AND(b_1, b_2)), BS_NOT(BS_OR(b_5, b_7))), BS_AND(BS_AND(BS_AND(b_0, b_1), BS_AND(b_3, b_5)), BS_NOT(BS_OR(b_4, b_7))))))), BS_OR(BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_4)), BS_AND(b_2, b_3)), BS_AND(BS_NOT(b_5), b_6)), BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_7)), BS_NOT(BS_OR(b_1, b_4))), BS_NOT(BS_OR(b_5, b_6)))), BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_4)), b_7), BS_AND(b_1, b_3)), BS_NOT(BS_OR(b_5, b_6))), BS_AND(BS_AND(BS_AND(b_0, BS_NOT(b_7)), BS_NOT(BS_OR(b_1, b_2))), BS_NOT(BS_OR(b_4, b_5))))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_1, b_2), BS_NOT(BS_OR(b_3, b_4))), BS_AND(b_5, BS_NOT(b_7))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(b_2, b_4)), BS_AND(BS_NOT(b_6), b_7))), BS_OR(BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_2), BS_NOT(b_7)), BS_NOT(BS_OR(b_1, b_6))), BS_AND(b_3, b_4)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), b_7), BS_AND(b_4, b_5))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_OR(b_1, b_4))), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_AND(b_0, b_2), BS_AND(b_3, b_4)), BS_AND(b_5, BS_NOT(b_6)))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_3), BS_NOT(BS_OR(b_2, b_4))), BS_AND(b_6, BS_NOT(b_7))), BS_AND(BS_AND(BS_AND(b_0, b_5), BS_NOT(BS_OR(b_2, b_3))), BS_AND(BS_NOT(b_4), b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), b_6), BS_NOT(BS_OR(b_2, b_4))), BS_AND(b_3, b_5)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_5, b_7))), BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_AND(BS_NOT(b_2), b_7)), BS_AND(b_5, b_6)), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_1)), BS_NOT(BS_OR(b_2, b_3))), BS_AND(b_4, BS_NOT(b_6))))))))), BS_OR(BS_OR(BS_OR(BS_AND(BS_AND(BS_AND(b_2, b_3), BS_AND(b_4, b_5)), BS_AND(b_6, b_7)), BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_6)), BS_AND(b_2, b_3)), BS_AND(b_5, b_7))), BS_OR(BS_AND(BS_AND(BS_AND(b_0, b_4), BS_NOT(BS_OR(b_1, b_2))), BS_AND(BS_NOT(b_5), b_7)), BS_AND(BS_AND(BS_AND(b_1, b_2), BS_AND(b_3, b_5)), BS_AND(b_6, b_7)))), BS_OR(BS_OR(BS_AND(BS_AND(BS_NOT(BS_OR(b_1, b_4)), BS_AND(b_2, b_5)), BS_AND(BS_NOT(b_6), b_7)), BS_AND(BS_AND(BS_AND(BS_AND(b_0, b_1), b_7), BS_NOT(BS_OR(b_2, b_6))), BS_AND(b_3, b_4))), BS_AND(BS_AND(BS_NOT(BS_OR(b_0, b_2)), BS_AND(b_3, b_4)), BS_NOT(BS_OR(b_5, b_7))))));
#else
#error undefined BOOLMODE
#endif
/* performance times in seconds for 56*512k*blocksbox calls in SSE2 config
mode  MSVC13   gcc
0     8,313    7,544
1     7,907    7,140
10    7,829    7,871
11    8,438    8,446

table 5M7      5M3
bool  5M7      5M4
*/
}


/**
   Nearly the same as dvbcsa_bs_block_decrypt_register. kkmulti is now 8 times in size.
   @parameter keys[in]  bit sliced key array [56][8][BS_BATCH_BYTES]
   @parameter r         ts input data ib0 + some free space left in front for virtual shift
   https://upload.wikimedia.org/wikipedia/commons/e/e4/Dvbcsa_Block_decrypt_shift.svg
*/
static inline void aycw_block_decrypt(const dvbcsa_bs_word_t* keys, dvbcsa_bs_word_t* r)
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

static inline int aycw_checkPESheader(dvbcsa_bs_word_t *data, dvbcsa_bs_word_t *candidates)
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
   ret  = BS_EXTLS32(c);
   *candidates = BS_NOT(c);
   return ~ret;
}

#endif  // AYCW_STREAM_32_H_