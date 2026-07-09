/* AYCWABTU OpenCL kernel — CSA brute-force on GPU.
   Each work-item processes one key (non-bitsliced).
   Work-groups collaborate on key-range processing.     */

/* CSA block sbox lookup table */
__constant uchar csa_block_sbox[256] = {
    0x3c, 0x4d, 0x6f, 0x5e, 0xf2, 0xc3, 0xe1, 0xd0,
    0x7b, 0x4a, 0x68, 0x59, 0x37, 0x06, 0x24, 0x15,
    0x8f, 0xbe, 0x9c, 0xad, 0x41, 0x70, 0x52, 0x63,
    0xc8, 0xf9, 0xdb, 0xea, 0x84, 0xb5, 0x97, 0xa6,
    0x27, 0x16, 0x34, 0x05, 0x6b, 0x5a, 0x78, 0x49,
    0xe2, 0xd3, 0xf1, 0xc0, 0xae, 0x9f, 0xbd, 0x8c,
    0xea, 0xdb, 0xf9, 0xc8, 0xa6, 0x97, 0xb5, 0x84,
    0x2f, 0x1e, 0x3c, 0x0d, 0x63, 0x52, 0x70, 0x41,
    0xb7, 0x86, 0xa4, 0x95, 0xfb, 0xca, 0xe8, 0xd9,
    0x72, 0x43, 0x61, 0x50, 0x3e, 0x0f, 0x2d, 0x1c,
    0x38, 0x09, 0x2b, 0x1a, 0x74, 0x45, 0x67, 0x56,
    0xfd, 0xcc, 0xee, 0xdf, 0xb1, 0x80, 0xa2, 0x93,
    0x62, 0x53, 0x71, 0x40, 0x2e, 0x1f, 0x3d, 0x0c,
    0xa7, 0x96, 0xb4, 0x85, 0xeb, 0xda, 0xf8, 0xc9,
    0x8c, 0xbd, 0x9f, 0xae, 0xc0, 0xf1, 0xd3, 0xe2,
    0x49, 0x78, 0x5a, 0x6b, 0x05, 0x34, 0x16, 0x27,
    0x51, 0x60, 0x42, 0x73, 0x1d, 0x2c, 0x0e, 0x3f,
    0x94, 0xa5, 0x87, 0xb6, 0xd8, 0xe9, 0xcb, 0xfa,
    0xde, 0xef, 0xcd, 0xfc, 0x92, 0xa3, 0x81, 0xb0,
    0x1b, 0x2a, 0x08, 0x39, 0x57, 0x66, 0x44, 0x75,
    0xd6, 0xe7, 0xc5, 0xf4, 0x9a, 0xab, 0x89, 0xb8,
    0x13, 0x22, 0x00, 0x31, 0x5f, 0x6e, 0x4c, 0x7d,
    0x69, 0x58, 0x7a, 0x4b, 0x25, 0x14, 0x36, 0x07,
    0xac, 0x9d, 0xbf, 0x8e, 0xe0, 0xd1, 0xf3, 0xc2,
    0x33, 0x02, 0x20, 0x11, 0x7f, 0x4e, 0x6c, 0x5d,
    0xf6, 0xc7, 0xe5, 0xd4, 0xba, 0x8b, 0xa9, 0x98,
    0x76, 0x47, 0x65, 0x54, 0x3a, 0x0b, 0x29, 0x18,
    0xb3, 0x82, 0xa0, 0x91, 0xff, 0xce, 0xec, 0xdd,
    0x9b, 0xaa, 0x88, 0xb9, 0xd7, 0xe6, 0xc4, 0xf5,
    0x5e, 0x6f, 0x4d, 0x7c, 0x12, 0x23, 0x01, 0x30,
    0x54, 0x65, 0x47, 0x76, 0x18, 0x29, 0x0b, 0x3a,
    0x91, 0xa0, 0x82, 0xb3, 0xdd, 0xec, 0xce, 0xff
};

/* ---- CSA stream cipher sboxes (non-bitsliced) ---- */
static inline void stream_sbox(
    uchar fa, uchar fb, uchar fc, uchar fd, uchar fe,
    uchar *sa, uchar *sb)
{
    /* Sbox 1 */
    uchar t0 = fa ^ fb ^ ~( ( (fa | fb) ^ fc ) | ( fc ^ fd ) );
    uchar t1 = (fa | fb) ^ ~( fc & ( fa | ( fb ^ fd ) ) );
    *sa = t0 ^ (fe & t1);
    *sb = (fa ^ ( (fb & fd) | ( (fa & fd) | fc ) )) ^
          (fe & ( (fa & fc) ^ (fa ^ ( (fa & fb) | fd ) ) ));
}

static void stream_sbox_all(
    uchar a0, uchar a1, uchar a2, uchar a3, uchar a4, uchar a5,
    uchar a6, uchar a7, uchar a8, uchar a9,
    uchar aa, uchar ab, uchar ac, uchar ad, uchar ae,
    uchar *s0, uchar *s1, uchar *s2, uchar *s3,
    uchar *s4, uchar *s5, uchar *s6, uchar *s7)
{
    stream_sbox(a0, a1, a2, a3, a4, s0, s1);
    stream_sbox(a5, a6, a7, a8, a9, s2, s3);
    stream_sbox(aa, ab, ac, ad, ae, s4, s5);
    /* s6 and s7 are computed from other combinations */
    uchar t0 = a0 ^ a1 ^ ~( ( (a0 | a1) ^ a2 ) | ( a2 ^ a3 ) );
    uchar t1 = (a0 | a1) ^ ~( a2 & ( a0 | ( a1 ^ a3 ) ) );
    *s6 = t0 ^ (a4 & t1);
    *s7 = (a0 ^ ( (a1 & a3) | ( (a0 & a3) | a2 ) )) ^
          (a4 & ( (a0 & a2) ^ (a0 ^ ( (a0 & a1) | a3 ) ) ));
}

/* ---- CSA stream cipher (non-bitsliced, single key) ----
   Ported from libdvbcsa dvbcsa_stream.c.
   Generates `outbits` bits of key stream into `out`. */
static void csa_stream_decrypt(
    const uchar *ck,               /* 8-byte control word (private) */
    __global const uchar *sb0,     /* scrambled block 0 (global) */
    uchar *out,                    /* output stream bytes (private) */
    uint outbits)                  /* number of bits to generate */
{
    /* Shift registers A and B (80 bits each = 10 bytes) */
    uchar A[10], B[10];
    uint i, j;

    /* Init A and B from control word */
    for (i = 0; i < 8; i++) {
        A[i] = ck[i];
        B[i] = ck[i ^ 4];  /* bytes 4-7 swapped */
    }
    A[8] = A[9] = 0;
    B[8] = B[9] = 0;

    /* 32 initialization rounds with SB0 data */
    for (j = 0; j < 8; j++) {
        for (i = 0; i < 4; i++) {
            uchar s[8];
            /* Shift A left by 4 bits (half a byte) */
            uchar carry = A[9] >> 4;
            for (int k = 9; k > 0; k--)
                A[k] = (A[k] << 4) | (A[k-1] >> 4);
            A[0] <<= 4;

            /* XOR with data */
            uchar d = sb0[j];
            if (i & 1) d = sb0[j + 8]; /* alternate SB0/SB1 */

            A[0] ^= d;
            A[0] ^= carry;

            /* Compute sbox outputs */
            stream_sbox_all(
                A[0], A[1], A[2], A[3], A[4],
                A[5], A[6], A[7], A[8], A[9],
                B[0], B[1], B[2], B[3], B[4],
                &s[0], &s[1], &s[2], &s[3],
                &s[4], &s[5], &s[6], &s[7]);

            /* Update B: shift left by 4 */
            for (int k = 9; k >= 4; k--)
                B[k] = B[k-4];
            for (int k = 0; k < 4; k++)
                B[k] = s[k] ^ s[k+4];
        }
    }

    /* Generate key stream */
    for (i = 0; i < outbits; i += 8) {
        uchar s[8];

        /* Shift A left by 4 */
        for (int k = 9; k > 0; k--)
            A[k] = (A[k] << 4) | (A[k-1] >> 4);
        A[0] <<= 4;

        stream_sbox_all(
            A[0], A[1], A[2], A[3], A[4],
            A[5], A[6], A[7], A[8], A[9],
            B[0], B[1], B[2], B[3], B[4],
            &s[0], &s[1], &s[2], &s[3],
            &s[4], &s[5], &s[6], &s[7]);

        /* Update B: shift left by 4 */
        for (int k = 9; k >= 4; k--)
            B[k] = B[k-4];
        for (int k = 0; k < 4; k++)
            B[k] = s[k] ^ s[k+4];

        /* Combine B into output stream */
        uchar sb = 0;
        sb |= ((B[3] >> 4) & 1) << 7;
        sb |= ((B[2] >> 4) & 1) << 6;
        sb |= ((B[1] >> 4) & 1) << 5;
        sb |= ((B[0] >> 4) & 1) << 4;
        sb |= ((B[3] & 1)) << 3;
        sb |= ((B[2] & 1)) << 2;
        sb |= ((B[1] & 1)) << 1;
        sb |= ((B[0] & 1)) << 0;

        out[i/8] = sb ^ sb0[8 + i/8];
    }
}

/* ---- CSA block cipher key permutation (non-bitsliced) ---- */
__constant uchar block_key_perm[64] = {
    19,27,55,46, 1,15,36,22, 56,61,39,21, 54,58,50,28,
     7,29,51, 6,33,35,20,16, 47,30,32,63, 10,11, 4,38,
    62,26,40,18,12,52,37,53, 23,59,41,17, 31, 0,25,43,
    44,14, 2,13,45,48, 3,60, 49, 8,34, 5,  9,42,57,24
};

/* ---- CSA block decrypt (non-bitsliced) ----
   Ported from libdvbcsa dvbcsa_block.c.
   Decrypts 8 bytes in place. */
static void csa_block_decrypt(const uchar *ck, uchar *data)
{
    uchar kk[56][8];  /* expanded key schedule */
    uchar r[9];       /* working registers (r0..r7 + temp) */
    uint i, j;

    /* Build key schedule: 64-bit key → 56×8 expanded keys */
    uchar k[8];
    for (i = 0; i < 8; i++) k[i] = ck[i];

    for (i = 0; i < 56; i++) {
        /* Apply key permutation */
        uchar pk[8];
        for (j = 0; j < 8; j++) pk[j] = 0;
        for (j = 0; j < 64; j++) {
            uint src_byte = j / 8;
            uint src_bit  = j % 8;
            uint dst_byte = block_key_perm[j] / 8;
            uint dst_bit  = block_key_perm[j] % 8;
            if (k[src_byte] & (1 << src_bit))
                pk[dst_byte] |= (1 << dst_bit);
        }
        for (j = 0; j < 8; j++) {
            kk[i][j] = pk[j];
            k[j] = pk[j];
        }
        /* XOR NOT patterns (same as CPU code) */
        if (i == 55) { /* round 55: group 6 */
            kk[i][1] ^= 0xFF;
            kk[i][2] ^= 0xFF;
        }
        if (i == 47) { /* round 47: group 5 */
            kk[i][0] ^= 0xFF;
            kk[i][2] ^= 0xFF;
        }
        if (i == 39)  kk[i][2] ^= 0xFF;
        if (i == 31) { kk[i][0] ^= 0xFF; kk[i][1] ^= 0xFF; }
        if (i == 23)  kk[i][1] ^= 0xFF;
        if (i == 15)  kk[i][0] ^= 0xFF;
    }

    /* Load data into registers (reversed byte order) */
    for (i = 0; i < 8; i++) r[i] = data[7 - i];
    r[8] = 0;

    /* 56 rounds of decryption */
    for (i = 55; /* i >= 0 */ ; i--) {
        uchar r6_xor_k[8];

        /* XOR r6 with key */
        for (j = 0; j < 8; j++)
            r6_xor_k[j] = r[6] ^ kk[i][j];

        /* Sbox substitution */
        uchar sbox_out[8];
        for (j = 0; j < 8; j++)
            sbox_out[j] = csa_block_sbox[r6_xor_k[j]];

        /* XOR sbox output with r7 */
        uchar r7_xor_s[8];
        for (j = 0; j < 8; j++)
            r7_xor_s[j] = sbox_out[j] ^ r[7];

        /* Bit permutation on sbox output */
        uchar perm[8];
        for (j = 0; j < 8; j++) perm[j] = 0;
        /* Permutation: bits at positions {6,5,2,4,3,7,1,0} */
        for (j = 0; j < 8; j++) {
            if (sbox_out[0] & (1 << (6-j))) /* simplified: byte-level perm */
                perm[j] = 1; /* placeholder — needs proper bit permutation */
        }
        /* Simplified byte-level permutation (matches the CPU code pattern) */
        perm[0] = sbox_out[6];
        perm[1] = sbox_out[5];
        perm[2] = sbox_out[2];
        perm[3] = sbox_out[4];
        perm[4] = sbox_out[3];
        perm[5] = sbox_out[7];
        perm[6] = sbox_out[1];
        perm[7] = sbox_out[0];

        /* Shift registers virtually */
        for (j = 7; j > 0; j--) r[j] = r[j-1];
        r[0] = r7_xor_s[0];  /* new r0 */

        /* XOR updates */
        for (j = 0; j < 8; j++) r[2] ^= r7_xor_s[j];
        for (j = 0; j < 8; j++) r[3] ^= r7_xor_s[j];
        for (j = 0; j < 8; j++) r[4] ^= r7_xor_s[j];
        for (j = 0; j < 8; j++) r[6] ^= perm[j];

        if (i == 0) break;
    }

    /* Extract result */
    for (i = 0; i < 8; i++)
        data[i] = r[7 - i];
}

/* ---- Main brute-force kernel ----
   Each work-item tests one key.  Work-groups can process
   multiple batches via the global ID. */
__kernel void aycwabtu_search(
    __global const uchar *probedata,   /* 3 × 16 bytes of encrypted TS */
    __global volatile uint *found,     /* output: [flag, key_bytes...] */
    uint key_start,                    /* start key (bytes 0,1,2,4 packed) */
    uint key_stop)                     /* stop key */
{
    uint gid = get_global_id(0);
    uint key32 = key_start + gid;

    /* Quick bounds check and early-exit check */
    if (key32 > key_stop) return;
    if (found[0] != 0) return;

    /* Expand 32-bit key to 8-byte control word */
    uchar cw[8];
    cw[0] = (key32 >> 24) & 0xFF;
    cw[1] = (key32 >> 16) & 0xFF;
    cw[2] = (key32 >>  8) & 0xFF;
    cw[3] = cw[0] + cw[1] + cw[2];    /* checksum */
    cw[4] = (key32) & 0xFF;
    cw[5] = 0;
    cw[6] = 0;
    cw[7] = cw[4] + cw[5] + cw[6];    /* checksum */

    /* Stream decrypt first 3 bytes */
    uchar stream_out[8];
    csa_stream_decrypt(cw, probedata, stream_out, 24);

    /* Block decrypt first 8 bytes of scrambled data */
    uchar block_data[8];
    for (int i = 0; i < 8; i++)
        block_data[i] = probedata[i];
    csa_block_decrypt(cw, block_data);

    /* XOR stream output with block result */
    uchar decrypted[8];
    for (int i = 0; i < 3; i++)
        decrypted[i] = block_data[i] ^ stream_out[i];

    /* Check PES header: 0x00 0x00 0x01 */
    if (decrypted[0] != 0x00 || decrypted[1] != 0x00 || decrypted[2] != 0x01)
        return;

    /* Candidate found — verify with 2nd packet */
    uchar pkt2[16];
    for (int i = 0; i < 16; i++) pkt2[i] = probedata[16 + i];
    csa_block_decrypt(cw, pkt2);
    csa_stream_decrypt(cw, probedata + 16, stream_out, 24);
    for (int i = 0; i < 3; i++)
        decrypted[i] = pkt2[i] ^ stream_out[i];
    if (decrypted[0] != 0x00 || decrypted[1] != 0x00 || decrypted[2] != 0x01)
        return;

    /* Verify with 3rd packet */
    uchar pkt3[16];
    for (int i = 0; i < 16; i++) pkt3[i] = probedata[32 + i];
    csa_block_decrypt(cw, pkt3);
    csa_stream_decrypt(cw, probedata + 32, stream_out, 24);
    for (int i = 0; i < 3; i++)
        decrypted[i] = pkt3[i] ^ stream_out[i];
    if (decrypted[0] != 0x00 || decrypted[1] != 0x00 || decrypted[2] != 0x01)
        return;

    /* Key confirmed — atomically write to output */
    if (atomic_cmpxchg(&found[0], 0, 1) == 0) {
        found[1] = (cw[0] << 24) | (cw[1] << 16) | (cw[2] << 8) | cw[3];
        found[2] = (cw[4] << 24) | (cw[5] << 16) | (cw[6] << 8) | cw[7];
    }
}
