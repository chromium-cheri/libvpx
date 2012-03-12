/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#include <math.h>
#include "vpx_mem/vpx_mem.h"

#include "onyx_int.h"
#include "quantize.h"
#include "vp8/common/quant_common.h"

#include "vp8/common/seg_common.h"

#ifdef ENC_DEBUG
extern int enc_debug;
#endif

#define EXACT_QUANT
void vp8_fast_quantize_b_c(BLOCK *b, BLOCKD *d)
{
    int i, rc, eob, nonzeros;
    int x, y, z, sz;
    short *coeff_ptr   = b->coeff;
    short *round_ptr   = b->round;
    short *quant_ptr   = b->quant_fast;
    short *qcoeff_ptr  = d->qcoeff;
    short *dqcoeff_ptr = d->dqcoeff;
    short *dequant_ptr = d->dequant;

    vpx_memset(qcoeff_ptr, 0, 32);
    vpx_memset(dqcoeff_ptr, 0, 32);

    eob = -1;
    for (i = 0; i < 16; i++)
    {
        rc   = vp8_default_zig_zag1d[i];
        z    = coeff_ptr[rc];

        sz = (z >> 31);                                 // sign of z
        x  = (z ^ sz) - sz;                             // x = abs(z)

        y  = ((x + round_ptr[rc]) * quant_ptr[rc]) >> 16; // quantize (x)
        x  = (y ^ sz) - sz;                         // get the sign back
        qcoeff_ptr[rc] = x;                          // write to destination
        dqcoeff_ptr[rc] = x * dequant_ptr[rc];        // dequantized value

        if (y)
        {
            eob = i;                                // last nonzero coeffs
        }
    }
    d->eob = eob + 1;
}



#ifdef EXACT_QUANT
void vp8_regular_quantize_b(BLOCK *b, BLOCKD *d)
{
    int i, rc, eob;
    int zbin;
    int x, y, z, sz;
    short *zbin_boost_ptr  = b->zrun_zbin_boost;
    short *coeff_ptr       = b->coeff;
    short *zbin_ptr        = b->zbin;
    short *round_ptr       = b->round;
    short *quant_ptr       = b->quant;
    unsigned char *quant_shift_ptr = b->quant_shift;
    short *qcoeff_ptr      = d->qcoeff;
    short *dqcoeff_ptr     = d->dqcoeff;
    short *dequant_ptr     = d->dequant;
    short zbin_oq_value    = b->zbin_extra;

    vpx_memset(qcoeff_ptr, 0, 32);
    vpx_memset(dqcoeff_ptr, 0, 32);

    eob = -1;

    for (i = 0; i < b->eob_max_offset; i++)
    {
        rc   = vp8_default_zig_zag1d[i];
        z    = coeff_ptr[rc];

        zbin = zbin_ptr[rc] + *zbin_boost_ptr + zbin_oq_value;

        zbin_boost_ptr ++;
        sz = (z >> 31);                                 // sign of z
        x  = (z ^ sz) - sz;                             // x = abs(z)

        if (x >= zbin)
        {
            x += round_ptr[rc];
            y  = (((x * quant_ptr[rc]) >> 16) + x)
                 >> quant_shift_ptr[rc];                // quantize (x)
            x  = (y ^ sz) - sz;                         // get the sign back
            qcoeff_ptr[rc]  = x;                        // write to destination
            dqcoeff_ptr[rc] = x * dequant_ptr[rc];      // dequantized value

            if (y)
            {
                eob = i;                                // last nonzero coeffs
                zbin_boost_ptr = b->zrun_zbin_boost;    // reset zero runlength
            }
        }
    }

    d->eob = eob + 1;
}

/* Perform regular quantization, with unbiased rounding and no zero bin. */
void vp8_strict_quantize_b(BLOCK *b, BLOCKD *d)
{
    int i;
    int rc;
    int eob;
    int x;
    int y;
    int z;
    int sz;
    short *coeff_ptr;
    short *quant_ptr;
    unsigned char *quant_shift_ptr;
    short *qcoeff_ptr;
    short *dqcoeff_ptr;
    short *dequant_ptr;

    coeff_ptr       = b->coeff;
    quant_ptr       = b->quant;
    quant_shift_ptr = b->quant_shift;
    qcoeff_ptr      = d->qcoeff;
    dqcoeff_ptr     = d->dqcoeff;
    dequant_ptr     = d->dequant;
    eob = - 1;
    vpx_memset(qcoeff_ptr, 0, 32);
    vpx_memset(dqcoeff_ptr, 0, 32);
    for (i = 0; i < 16; i++)
    {
        int dq;
        int round;

        /*TODO: These arrays should be stored in zig-zag order.*/
        rc = vp8_default_zig_zag1d[i];
        z = coeff_ptr[rc];
        dq = dequant_ptr[rc];
        round = dq >> 1;
        /* Sign of z. */
        sz = -(z < 0);
        x = (z + sz) ^ sz;
        x += round;
        if (x >= dq)
        {
            /* Quantize x. */
            y  = (((x * quant_ptr[rc]) >> 16) + x) >> quant_shift_ptr[rc];
            /* Put the sign back. */
            x = (y + sz) ^ sz;
            /* Save the coefficient and its dequantized value. */
            qcoeff_ptr[rc] = x;
            dqcoeff_ptr[rc] = x * dq;
            /* Remember the last non-zero coefficient. */
            if (y)
                eob = i;
        }
    }

    d->eob = eob + 1;
}

#else

void vp8_regular_quantize_b(BLOCK *b, BLOCKD *d)
{
    int i, rc, eob;
    int zbin;
    int x, y, z, sz;
    short *zbin_boost_ptr = b->zrun_zbin_boost;
    short *coeff_ptr      = b->coeff;
    short *zbin_ptr       = b->zbin;
    short *round_ptr      = b->round;
    short *quant_ptr      = b->quant;
    short *qcoeff_ptr     = d->qcoeff;
    short *dqcoeff_ptr    = d->dqcoeff;
    short *dequant_ptr    = d->dequant;
    short zbin_oq_value   = b->zbin_extra;

    vpx_memset(qcoeff_ptr, 0, 32);
    vpx_memset(dqcoeff_ptr, 0, 32);

    eob = -1;

    for (i = 0; i < 16; i++)
    {
        rc   = vp8_default_zig_zag1d[i];
        z    = coeff_ptr[rc];

        //if ( i == 0 )
        //    zbin = zbin_ptr[rc] + *zbin_boost_ptr + zbin_oq_value/2;
        //else
        zbin = zbin_ptr[rc] + *zbin_boost_ptr + zbin_oq_value;

        zbin_boost_ptr ++;
        sz = (z >> 31);                                 // sign of z
        x  = (z ^ sz) - sz;                             // x = abs(z)

        if (x >= zbin)
        {
            y  = ((x + round_ptr[rc]) * quant_ptr[rc]) >> 16; // quantize (x)
            x  = (y ^ sz) - sz;                         // get the sign back
            qcoeff_ptr[rc]  = x;                         // write to destination
            dqcoeff_ptr[rc] = x * dequant_ptr[rc];        // dequantized value

            if (y)
            {
                eob = i;                                // last nonzero coeffs
                zbin_boost_ptr = &b->zrun_zbin_boost[0];    // reset zero runlength
            }
        }
    }

    d->eob = eob + 1;
}

#endif
//EXACT_QUANT


void vp8_quantize_mby_c(MACROBLOCK *x)
{
    int i;
    int has_2nd_order = (x->e_mbd.mode_info_context->mbmi.mode != B_PRED
        && x->e_mbd.mode_info_context->mbmi.mode != I8X8_PRED
        && x->e_mbd.mode_info_context->mbmi.mode != SPLITMV);

    for (i = 0; i < 16; i++)
        x->quantize_b(&x->block[i], &x->e_mbd.block[i]);

    if(has_2nd_order)
        x->quantize_b(&x->block[24], &x->e_mbd.block[24]);
}

void vp8_quantize_mb_c(MACROBLOCK *x)
{
    int i;
    int has_2nd_order=(x->e_mbd.mode_info_context->mbmi.mode != B_PRED
        && x->e_mbd.mode_info_context->mbmi.mode != I8X8_PRED
        && x->e_mbd.mode_info_context->mbmi.mode != SPLITMV);

    for (i = 0; i < 24+has_2nd_order; i++)
        x->quantize_b(&x->block[i], &x->e_mbd.block[i]);
}


void vp8_quantize_mbuv_c(MACROBLOCK *x)
{
    int i;

    for (i = 16; i < 24; i++)
        x->quantize_b(&x->block[i], &x->e_mbd.block[i]);
}




void vp8_fast_quantize_b_2x2_c(BLOCK *b, BLOCKD *d)
{
  int i, rc, eob;
  int zbin;
  int x, y, z, sz;
  short *coeff_ptr  = b->coeff;
  short *zbin_ptr   = b->zbin;
  short *round_ptr  = b->round;
  short *quant_ptr  = b->quant;
  short *qcoeff_ptr = d->qcoeff;
  short *dqcoeff_ptr = d->dqcoeff;
  short *dequant_ptr = d->dequant;
  //double q2nd = 4;
  vpx_memset(qcoeff_ptr, 0, 32);
  vpx_memset(dqcoeff_ptr, 0, 32);

  eob = -1;

  for (i = 0; i < 4; i++)
  {
    rc   = vp8_default_zig_zag1d[i];
    z    = coeff_ptr[rc];
    //zbin = zbin_ptr[rc]/q2nd;
    zbin = zbin_ptr[rc];

    sz = (z >> 31);                                 // sign of z
    x  = (z ^ sz) - sz;                             // x = abs(z)

    if (x >= zbin)
    {
      //y  = ((int)((x + round_ptr[rc]/q2nd) * quant_ptr[rc] * q2nd)) >> 16; // quantize (x)
      y  = ((int)((x + round_ptr[rc]) * quant_ptr[rc])) >> 16; // quantize (x)
      x  = (y ^ sz) - sz;                         // get the sign back
      qcoeff_ptr[rc] = x;                          // write to destination
      //dqcoeff_ptr[rc] = x * dequant_ptr[rc] / q2nd;        // dequantized value
      dqcoeff_ptr[rc] = x * dequant_ptr[rc];        // dequantized value
      dqcoeff_ptr[rc] = (dqcoeff_ptr[rc]+2)>>2;

      if (y)
      {
        eob = i;                                // last nonzero coeffs
      }
    }
  }
  d->eob = eob + 1;
  //if (d->eob > 4) printf("Flag Fast 2 (%d)\n", d->eob);
}

void vp8_fast_quantize_b_8x8_c(BLOCK *b, BLOCKD *d)
{
    int i, rc, eob;
    int zbin;
    int x, y, z, sz;
    short *coeff_ptr  = b->coeff;
    short *zbin_ptr   = b->zbin;
    short *round_ptr  = b->round;
    short *quant_ptr  = b->quant;
    short *qcoeff_ptr = d->qcoeff;
    short *dqcoeff_ptr = d->dqcoeff;
    short *dequant_ptr = d->dequant;
    //double q1st = 2;
    vpx_memset(qcoeff_ptr, 0, 64*sizeof(short));
    vpx_memset(dqcoeff_ptr, 0, 64*sizeof(short));

    eob = -1;



    for (i = 0; i < 64; i++)
    {

        rc   = vp8_default_zig_zag1d_8x8[i];
        z    = coeff_ptr[rc];
        //zbin = zbin_ptr[rc!=0]/q1st ;
        zbin = zbin_ptr[rc!=0] ;

        sz = (z >> 31);                                 // sign of z
        x  = (z ^ sz) - sz;                             // x = abs(z)

        if (x >= zbin)
        {
            //y  = ((int)((x + round_ptr[rc!=0] / q1st) * quant_ptr[rc!=0] * q1st)) >> 16;
            y  = ((int)((x + round_ptr[rc!=0]) * quant_ptr[rc!=0])) >> 16;
            x  = (y ^ sz) - sz;                         // get the sign back
            qcoeff_ptr[rc] = x;                         // write to destination
            //dqcoeff_ptr[rc] = x * dequant_ptr[rc!=0] / q1st;        // dequantized value
            dqcoeff_ptr[rc] = x * dequant_ptr[rc!=0];        // dequantized value
            dqcoeff_ptr[rc] = (dqcoeff_ptr[rc]+2)>>2;

            if (y)
            {
                eob = i;                                // last nonzero coeffs
            }
        }
    }
    d->eob = eob + 1;
}




void vp8_regular_quantize_b_2x2(BLOCK *b, BLOCKD *d)
{
  int i, rc, eob;
  int zbin;
  int x, y, z, sz;
  short *zbin_boost_ptr = b->zrun_zbin_boost;
  short *coeff_ptr  = b->coeff;
  short *zbin_ptr   = b->zbin;
  short *round_ptr  = b->round;
  short *quant_ptr  = b->quant;
  unsigned char *quant_shift_ptr = b->quant_shift;
  short *qcoeff_ptr = d->qcoeff;
  short *dqcoeff_ptr = d->dqcoeff;
  short *dequant_ptr = d->dequant;
  short zbin_oq_value = b->zbin_extra;
  //double q2nd = 4;
  vpx_memset(qcoeff_ptr, 0, 32);
  vpx_memset(dqcoeff_ptr, 0, 32);

  eob = -1;

  for (i = 0; i < b->eob_max_offset_8x8; i++)
  {
    rc   = vp8_default_zig_zag1d[i];
    z    = coeff_ptr[rc];

    //zbin = (zbin_ptr[rc] + *zbin_boost_ptr + zbin_oq_value)/q2nd;
    zbin = (zbin_ptr[rc] + *zbin_boost_ptr + zbin_oq_value);

    zbin_boost_ptr ++;
    sz = (z >> 31);                                 // sign of z
    x  = (z ^ sz) - sz;                             // x = abs(z)

    if (x >= zbin)
    {
      //x += (round_ptr[rc]/q2nd);
      x += (round_ptr[rc]);
      y  = ((int)((int)(x * quant_ptr[rc]) >> 16) + x)
           >> quant_shift_ptr[rc];                // quantize (x)
      x  = (y ^ sz) - sz;                         // get the sign back
      qcoeff_ptr[rc]  = x;                         // write to destination
      //dqcoeff_ptr[rc] = x * dequant_ptr[rc]/q2nd;        // dequantized value
      dqcoeff_ptr[rc] = x * dequant_ptr[rc];        // dequantized value


      if (y)
      {
        eob = i;                                // last nonzero coeffs
        zbin_boost_ptr = &b->zrun_zbin_boost[0];    // reset zero runlength
      }
    }
  }

  d->eob = eob + 1;
}

void vp8_regular_quantize_b_8x8(BLOCK *b, BLOCKD *d)
{
  int i, rc, eob;
  int zbin;
  int x, y, z, sz;
  short *zbin_boost_ptr = b->zrun_zbin_boost;
  short *coeff_ptr  = b->coeff;
  short *zbin_ptr   = b->zbin;
  short *round_ptr  = b->round;
  short *quant_ptr  = b->quant;
  unsigned char *quant_shift_ptr = b->quant_shift;
  short *qcoeff_ptr = d->qcoeff;
  short *dqcoeff_ptr = d->dqcoeff;
  short *dequant_ptr = d->dequant;
  short zbin_oq_value = b->zbin_extra;
  //double q1st = 2;

  vpx_memset(qcoeff_ptr, 0, 64*sizeof(short));
  vpx_memset(dqcoeff_ptr, 0, 64*sizeof(short));

  eob = -1;

  for (i = 0; i < b->eob_max_offset_8x8; i++)
  {

    rc   = vp8_default_zig_zag1d_8x8[i];
    z    = coeff_ptr[rc];

    //zbin = (zbin_ptr[rc!=0] + *zbin_boost_ptr + zbin_oq_value)/q1st;
    zbin = (zbin_ptr[rc!=0] + *zbin_boost_ptr + zbin_oq_value);
    //TODO: 8x8 zbin boost needs be done properly
    if(zbin_boost_ptr < &b->zrun_zbin_boost[15])
        zbin_boost_ptr ++;

    sz = (z >> 31);                                 // sign of z
    x  = (z ^ sz) - sz;                             // x = abs(z)

    if (x >= zbin)
    {
      //x += (round_ptr[rc!=0]/q1st);
      //y  = ((int)(((int)(x * quant_ptr[rc!=0] * q1st) >> 16) + x))
      //    >> quant_shift_ptr[rc!=0];                // quantize (x)
      x += (round_ptr[rc!=0]);
      y  = ((int)(((int)(x * quant_ptr[rc!=0]) >> 16) + x))
          >> quant_shift_ptr[rc!=0];                // quantize (x)
      x  = (y ^ sz) - sz;                         // get the sign back
      qcoeff_ptr[rc]  = x;                         // write to destination
      //dqcoeff_ptr[rc] = x * dequant_ptr[rc!=0] / q1st;        // dequantized value
      dqcoeff_ptr[rc] = x * dequant_ptr[rc!=0];        // dequantized value

      if (y)
      {
        eob = i;                                // last nonzero coeffs
        zbin_boost_ptr = &b->zrun_zbin_boost[0];    // reset zero runlength
      }
    }
  }

  d->eob = eob + 1;
}

void vp8_strict_quantize_b_2x2(BLOCK *b, BLOCKD *d)
{
  int i;
  int rc;
  int eob;
  int x;
  int y;
  int z;
  int sz;
  short *coeff_ptr;
  short *quant_ptr;
  unsigned char *quant_shift_ptr;
  short *qcoeff_ptr;
  short *dqcoeff_ptr;
  short *dequant_ptr;
  //double q2nd = 4;
  coeff_ptr = b->coeff;
  quant_ptr = b->quant;
  quant_shift_ptr = b->quant_shift;
  qcoeff_ptr = d->qcoeff;
  dqcoeff_ptr = d->dqcoeff;
  dequant_ptr = d->dequant;
  eob = - 1;
  vpx_memset(qcoeff_ptr, 0, 32);
  vpx_memset(dqcoeff_ptr, 0, 32);
  for (i = 0; i < 4; i++)
  {
    int dq;
    int round;

    /*TODO: These arrays should be stored in zig-zag order.*/
    rc = vp8_default_zig_zag1d[i];
    z = coeff_ptr[rc];
    //z = z * q2nd;
    //dq = dequant_ptr[rc]/q2nd;
    dq = dequant_ptr[rc];
    round = dq >> 1;
    /* Sign of z. */
    sz = -(z < 0);
    x = (z + sz) ^ sz;
    x += round;
    if (x >= dq)
    {
      /* Quantize x */
      y  = (((x * quant_ptr[rc]) >> 16) + x) >> quant_shift_ptr[rc];
      /* Put the sign back. */
      x = (y + sz) ^ sz;
      /* Save * the * coefficient and its dequantized value. */
      qcoeff_ptr[rc] = x;
      dqcoeff_ptr[rc] = x * dq;
      /* Remember the last non-zero coefficient. */
      if (y)
        eob = i;
    }
  }
  d->eob = eob + 1;
}

void vp8_strict_quantize_b_8x8(BLOCK *b, BLOCKD *d)
{
  int i;
  int rc;
  int eob;
  int x;
  int y;
  int z;
  int sz;
  short *coeff_ptr;
  short *quant_ptr;
  unsigned char *quant_shift_ptr;
  short *qcoeff_ptr;
  short *dqcoeff_ptr;
  short *dequant_ptr;
  //double q1st = 2;
  printf("call strict quantizer\n");
  coeff_ptr = b->coeff;
  quant_ptr = b->quant;
  quant_shift_ptr = b->quant_shift;
  qcoeff_ptr = d->qcoeff;
  dqcoeff_ptr = d->dqcoeff;
  dequant_ptr = d->dequant;
  eob = - 1;
  vpx_memset(qcoeff_ptr, 0, 64*sizeof(short));
  vpx_memset(dqcoeff_ptr, 0, 64*sizeof(short));
  for (i = 0; i < 64; i++)
  {
    int dq;
    int round;

    /*TODO: These arrays should be stored in zig-zag order.*/
    rc = vp8_default_zig_zag1d_8x8[i];
    z = coeff_ptr[rc];
    //z = z * q1st;
    //dq = dequant_ptr[rc!=0]/q1st;
    dq = dequant_ptr[rc!=0];
    round = dq >> 1;
    /* Sign of z. */
    sz = -(z < 0);
    x = (z + sz) ^ sz;
    x += round;
    if (x >= dq)
    {
      /* Quantize x. */
      y  = ((int)(((int)((x * quant_ptr[rc!=0])) >> 16) + x)) >> quant_shift_ptr[rc!=0];
      /* Put the sign back. */
      x = (y + sz) ^ sz;
      /* Save the coefficient and its dequantized value.  * */
      qcoeff_ptr[rc] = x;
      dqcoeff_ptr[rc] = x * dq;
      /* Remember the last non-zero coefficient. */
      if (y)
        eob = i;
    }
  }
  d->eob = eob + 1;
}



void vp8_quantize_mby_8x8(MACROBLOCK *x)
{
  int i;
  int has_2nd_order=(x->e_mbd.mode_info_context->mbmi.mode != B_PRED
                     && x->e_mbd.mode_info_context->mbmi.mode != SPLITMV);
  for(i = 0; i < 16; i ++)
  {
    x->e_mbd.block[i].eob = 0;
  }
  x->e_mbd.block[24].eob = 0;
  for (i = 0; i < 16; i+=4)
    x->quantize_b_8x8(&x->block[i], &x->e_mbd.block[i]);

  if (has_2nd_order)
    x->quantize_b_2x2(&x->block[24], &x->e_mbd.block[24]);

}

void vp8_quantize_mb_8x8(MACROBLOCK *x)
{
  int i;
  int has_2nd_order=(x->e_mbd.mode_info_context->mbmi.mode != B_PRED
                     && x->e_mbd.mode_info_context->mbmi.mode != SPLITMV);
  for(i = 0; i < 25; i ++)
  {
    x->e_mbd.block[i].eob = 0;
  }
  for (i = 0; i < 24; i+=4)
    x->quantize_b_8x8(&x->block[i], &x->e_mbd.block[i]);

  if (has_2nd_order)
    x->quantize_b_2x2(&x->block[24], &x->e_mbd.block[24]);
}

void vp8_quantize_mbuv_8x8(MACROBLOCK *x)
{
  int i;

  for(i = 16; i < 24; i ++)
  {
    x->e_mbd.block[i].eob = 0;
  }
  for (i = 16; i < 24; i+=4)
    x->quantize_b_8x8(&x->block[i], &x->e_mbd.block[i]);
}



/* quantize_b_pair function pointer in MACROBLOCK structure is set to one of
 * these two C functions if corresponding optimized routine is not available.
 * NEON optimized version implements currently the fast quantization for pair
 * of blocks. */
void vp8_regular_quantize_b_pair(BLOCK *b1, BLOCK *b2, BLOCKD *d1, BLOCKD *d2)
{
    vp8_regular_quantize_b(b1, d1);
    vp8_regular_quantize_b(b2, d2);
}

void vp8_fast_quantize_b_pair_c(BLOCK *b1, BLOCK *b2, BLOCKD *d1, BLOCKD *d2)
{
    vp8_fast_quantize_b_c(b1, d1);
    vp8_fast_quantize_b_c(b2, d2);
}


#define EXACT_QUANT
#ifdef EXACT_QUANT
static void invert_quant(int improved_quant, short *quant,
                               unsigned char *shift, short d)
{
    if(improved_quant)
    {
        unsigned t;
        int l;
        t = d;
        for(l = 0; t > 1; l++)
            t>>=1;
        t = 1 + (1<<(16+l))/d;
        *quant = (short)(t - (1<<16));
        *shift = l;
    }
    else
    {
        *quant = (1 << 16) / d;
        *shift = 0;
    }
}


void vp8cx_init_quantizer(VP8_COMP *cpi)
{
    int i;
    int quant_val;
    int Q;
    int zbin_boost[16] = { 0,  0,  8, 10, 12, 14, 16, 20,
                          24, 28, 32, 36, 40, 44, 44, 44};
    int qrounding_factor = 48;

    for (Q = 0; Q < QINDEX_RANGE; Q++)
    {
        int qzbin_factor = (vp8_dc_quant(Q,0) < 148) ? 84 : 80;

        // dc values
        quant_val = vp8_dc_quant(Q, cpi->common.y1dc_delta_q);
        cpi->Y1quant_fast[Q][0] = (1 << 16) / quant_val;
        invert_quant(cpi->sf.improved_quant, cpi->Y1quant[Q] + 0,
                     cpi->Y1quant_shift[Q] + 0, quant_val);
        cpi->Y1zbin[Q][0] = ((qzbin_factor * quant_val) + 64) >> 7;
        cpi->Y1round[Q][0] = (qrounding_factor * quant_val) >> 7;
        cpi->common.Y1dequant[Q][0] = quant_val;
        cpi->zrun_zbin_boost_y1[Q][0] = (quant_val * zbin_boost[0]) >> 7;

        quant_val = vp8_dc2quant(Q, cpi->common.y2dc_delta_q);
        cpi->Y2quant_fast[Q][0] = (1 << 16) / quant_val;
        invert_quant(cpi->sf.improved_quant, cpi->Y2quant[Q] + 0,
                     cpi->Y2quant_shift[Q] + 0, quant_val);
        cpi->Y2zbin[Q][0] = ((qzbin_factor * quant_val) + 64) >> 7;
        cpi->Y2round[Q][0] = (qrounding_factor * quant_val) >> 7;
        cpi->common.Y2dequant[Q][0] = quant_val;
        cpi->zrun_zbin_boost_y2[Q][0] = (quant_val * zbin_boost[0]) >> 7;

        quant_val = vp8_dc_uv_quant(Q, cpi->common.uvdc_delta_q);
        cpi->UVquant_fast[Q][0] = (1 << 16) / quant_val;
        invert_quant(cpi->sf.improved_quant, cpi->UVquant[Q] + 0,
                     cpi->UVquant_shift[Q] + 0, quant_val);
        cpi->UVzbin[Q][0] = ((qzbin_factor * quant_val) + 64) >> 7;;
        cpi->UVround[Q][0] = (qrounding_factor * quant_val) >> 7;
        cpi->common.UVdequant[Q][0] = quant_val;
        cpi->zrun_zbin_boost_uv[Q][0] = (quant_val * zbin_boost[0]) >> 7;

        // all the ac values = ;
        for (i = 1; i < 16; i++)
        {
            int rc = vp8_default_zig_zag1d[i];

            quant_val = vp8_ac_yquant(Q);
            cpi->Y1quant_fast[Q][rc] = (1 << 16) / quant_val;
            invert_quant(cpi->sf.improved_quant, cpi->Y1quant[Q] + rc,
                         cpi->Y1quant_shift[Q] + rc, quant_val);
            cpi->Y1zbin[Q][rc] = ((qzbin_factor * quant_val) + 64) >> 7;
            cpi->Y1round[Q][rc] = (qrounding_factor * quant_val) >> 7;
            cpi->common.Y1dequant[Q][rc] = quant_val;
            cpi->zrun_zbin_boost_y1[Q][i] = (quant_val * zbin_boost[i]) >> 7;

            quant_val = vp8_ac2quant(Q, cpi->common.y2ac_delta_q);
            cpi->Y2quant_fast[Q][rc] = (1 << 16) / quant_val;
            invert_quant(cpi->sf.improved_quant, cpi->Y2quant[Q] + rc,
                         cpi->Y2quant_shift[Q] + rc, quant_val);
            cpi->Y2zbin[Q][rc] = ((qzbin_factor * quant_val) + 64) >> 7;
            cpi->Y2round[Q][rc] = (qrounding_factor * quant_val) >> 7;
            cpi->common.Y2dequant[Q][rc] = quant_val;
            cpi->zrun_zbin_boost_y2[Q][i] = (quant_val * zbin_boost[i]) >> 7;

            quant_val = vp8_ac_uv_quant(Q, cpi->common.uvac_delta_q);
            cpi->UVquant_fast[Q][rc] = (1 << 16) / quant_val;
            invert_quant(cpi->sf.improved_quant, cpi->UVquant[Q] + rc,
                         cpi->UVquant_shift[Q] + rc, quant_val);
            cpi->UVzbin[Q][rc] = ((qzbin_factor * quant_val) + 64) >> 7;
            cpi->UVround[Q][rc] = (qrounding_factor * quant_val) >> 7;
            cpi->common.UVdequant[Q][rc] = quant_val;
            cpi->zrun_zbin_boost_uv[Q][i] = (quant_val * zbin_boost[i]) >> 7;
        }
    }
}
#endif


void vp8cx_mb_init_quantizer(VP8_COMP *cpi, MACROBLOCK *x)
{
    int i;
    int QIndex;
    MACROBLOCKD *xd = &x->e_mbd;
    int zbin_extra;
    int segment_id = xd->mode_info_context->mbmi.segment_id;

    // Select the baseline MB Q index allowing for any segment level change.
    if ( segfeature_active( xd, segment_id, SEG_LVL_ALT_Q ) )
    {
        // Abs Value
        if (xd->mb_segment_abs_delta == SEGMENT_ABSDATA)
            QIndex = get_segdata( xd, segment_id, SEG_LVL_ALT_Q );

        // Delta Value
        else
        {
            QIndex = cpi->common.base_qindex +
                     get_segdata( xd, segment_id, SEG_LVL_ALT_Q );

            // Clamp to valid range
            QIndex = (QIndex >= 0) ? ((QIndex <= MAXQ) ? QIndex : MAXQ) : 0;
        }
    }
    else
        QIndex = cpi->common.base_qindex;

    // Y
    zbin_extra = ( cpi->common.Y1dequant[QIndex][1] *
                   ( cpi->zbin_over_quant +
                     cpi->zbin_mode_boost +
                     x->act_zbin_adj ) ) >> 7;

    for (i = 0; i < 16; i++)
    {
        x->block[i].quant = cpi->Y1quant[QIndex];
        x->block[i].quant_fast = cpi->Y1quant_fast[QIndex];
        x->block[i].quant_shift = cpi->Y1quant_shift[QIndex];
        x->block[i].zbin = cpi->Y1zbin[QIndex];
        x->block[i].round = cpi->Y1round[QIndex];
        x->e_mbd.block[i].dequant = cpi->common.Y1dequant[QIndex];
        x->block[i].zrun_zbin_boost = cpi->zrun_zbin_boost_y1[QIndex];
        x->block[i].zbin_extra = (short)zbin_extra;

        // Segment max eob offset feature.
        if ( segfeature_active( xd, segment_id, SEG_LVL_EOB ) )
        {
            x->block[i].eob_max_offset =
                get_segdata( xd, segment_id, SEG_LVL_EOB );
            x->block[i].eob_max_offset_8x8 =
                get_segdata( xd, segment_id, SEG_LVL_EOB );
        }
        else
        {
            x->block[i].eob_max_offset = 16;
            x->block[i].eob_max_offset_8x8 = 64;
        }
    }

    // UV
    zbin_extra = ( cpi->common.UVdequant[QIndex][1] *
                   ( cpi->zbin_over_quant +
                     cpi->zbin_mode_boost +
                     x->act_zbin_adj ) ) >> 7;

    for (i = 16; i < 24; i++)
    {
        x->block[i].quant = cpi->UVquant[QIndex];
        x->block[i].quant_fast = cpi->UVquant_fast[QIndex];
        x->block[i].quant_shift = cpi->UVquant_shift[QIndex];
        x->block[i].zbin = cpi->UVzbin[QIndex];
        x->block[i].round = cpi->UVround[QIndex];
        x->e_mbd.block[i].dequant = cpi->common.UVdequant[QIndex];
        x->block[i].zrun_zbin_boost = cpi->zrun_zbin_boost_uv[QIndex];
        x->block[i].zbin_extra = (short)zbin_extra;

        // Segment max eob offset feature.
        if ( segfeature_active( xd, segment_id, SEG_LVL_EOB ) )
        {
            x->block[i].eob_max_offset =
                get_segdata( xd, segment_id, SEG_LVL_EOB );
            x->block[i].eob_max_offset_8x8 =
                get_segdata( xd, segment_id, SEG_LVL_EOB );
        }
        else
        {
            x->block[i].eob_max_offset = 16;
            x->block[i].eob_max_offset_8x8 = 64;
        }
    }

    // Y2
    zbin_extra = ( cpi->common.Y2dequant[QIndex][1] *
                   ( (cpi->zbin_over_quant / 2) +
                     cpi->zbin_mode_boost +
                     x->act_zbin_adj ) ) >> 7;

    x->block[24].quant_fast = cpi->Y2quant_fast[QIndex];
    x->block[24].quant = cpi->Y2quant[QIndex];
    x->block[24].quant_shift = cpi->Y2quant_shift[QIndex];
    x->block[24].zbin = cpi->Y2zbin[QIndex];
    x->block[24].round = cpi->Y2round[QIndex];
    x->e_mbd.block[24].dequant = cpi->common.Y2dequant[QIndex];
    x->block[24].zrun_zbin_boost = cpi->zrun_zbin_boost_y2[QIndex];
    x->block[24].zbin_extra = (short)zbin_extra;

    // TBD perhaps not use for Y2
    // Segment max eob offset feature.
    if ( segfeature_active( xd, segment_id, SEG_LVL_EOB ) )
    {
        x->block[24].eob_max_offset =
            get_segdata( xd, segment_id, SEG_LVL_EOB );
        x->block[24].eob_max_offset_8x8 =
            get_segdata( xd, segment_id, SEG_LVL_EOB );
    }
    else
    {
        x->block[24].eob_max_offset = 16;
        x->block[24].eob_max_offset_8x8 = 4;
    }

    /* save this macroblock QIndex for vp8_update_zbin_extra() */
    x->q_index = QIndex;
}


void vp8_update_zbin_extra(VP8_COMP *cpi, MACROBLOCK *x)
{
    int i;
    int QIndex = x->q_index;
    int zbin_extra;

    // Y
    zbin_extra = ( cpi->common.Y1dequant[QIndex][1] *
                   ( cpi->zbin_over_quant +
                     cpi->zbin_mode_boost +
                     x->act_zbin_adj ) ) >> 7;
    for (i = 0; i < 16; i++)
    {
        x->block[i].zbin_extra = (short)zbin_extra;
    }

    // UV
    zbin_extra = ( cpi->common.UVdequant[QIndex][1] *
                   ( cpi->zbin_over_quant +
                     cpi->zbin_mode_boost +
                     x->act_zbin_adj ) ) >> 7;

    for (i = 16; i < 24; i++)
    {
        x->block[i].zbin_extra = (short)zbin_extra;
    }

    // Y2
    zbin_extra = ( cpi->common.Y2dequant[QIndex][1] *
                   ( (cpi->zbin_over_quant / 2) +
                     cpi->zbin_mode_boost +
                     x->act_zbin_adj ) ) >> 7;

    x->block[24].zbin_extra = (short)zbin_extra;
}


void vp8cx_frame_init_quantizer(VP8_COMP *cpi)
{
    // Clear Zbin mode boost for default case
    cpi->zbin_mode_boost = 0;

    // MB level quantizer setup
    vp8cx_mb_init_quantizer(cpi, &cpi->mb);
}


void vp8_set_quantizer(struct VP8_COMP *cpi, int Q)
{
    VP8_COMMON *cm = &cpi->common;

    cm->base_qindex = Q;

    // if any of the delta_q values are changing update flag will
    // have to be set.
    cm->y1dc_delta_q = 0;
    cm->y2ac_delta_q = 0;
    cm->uvdc_delta_q = 0;
    cm->uvac_delta_q = 0;
    cm->y2dc_delta_q = 0;

    // quantizer has to be reinitialized if any delta_q changes.
    // As there are not any here for now this is inactive code.
    //if(update)
    //    vp8cx_init_quantizer(cpi);
}
