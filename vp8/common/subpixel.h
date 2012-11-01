/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef SUBPIXEL_H
#define SUBPIXEL_H

#define prototype_subpixel_predict(sym) \
  void sym(unsigned char *src, int src_pitch, int xofst, int yofst, \
           unsigned char *dst, int dst_pitch)

#if ARCH_X86 || ARCH_X86_64
#include "x86/subpixel_x86.h"
#endif

#if ARCH_ARM
#include "arm/subpixel_arm.h"
#endif

#ifndef vp9_subpix_sixtap16x16
#define vp9_subpix_sixtap16x16 vp9_sixtap_predict16x16_c
#endif
extern prototype_subpixel_predict(vp9_subpix_sixtap16x16);

#ifndef vp9_subpix_sixtap8x8
#define vp9_subpix_sixtap8x8 vp9_sixtap_predict8x8_c
#endif
extern prototype_subpixel_predict(vp9_subpix_sixtap8x8);

#ifndef vp9_subpix_sixtap_avg16x16
#define vp9_subpix_sixtap_avg16x16 vp9_sixtap_predict_avg16x16_c
#endif
extern prototype_subpixel_predict(vp9_subpix_sixtap_avg16x16);

#ifndef vp9_subpix_sixtap_avg8x8
#define vp9_subpix_sixtap_avg8x8 vp9_sixtap_predict_avg8x8_c
#endif
extern prototype_subpixel_predict(vp9_subpix_sixtap_avg8x8);
#ifndef vp9_subpix_sixtap8x4
#define vp9_subpix_sixtap8x4 vp9_sixtap_predict8x4_c
#endif
extern prototype_subpixel_predict(vp9_subpix_sixtap8x4);

#ifndef vp9_subpix_sixtap4x4
#define vp9_subpix_sixtap4x4 vp9_sixtap_predict_c
#endif
extern prototype_subpixel_predict(vp9_subpix_sixtap4x4);

#ifndef vp9_subpix_sixtap_avg4x4
#define vp9_subpix_sixtap_avg4x4 vp9_sixtap_predict_avg_c
#endif
extern prototype_subpixel_predict(vp9_subpix_sixtap_avg4x4);

#ifndef vp9_subpix_eighttap16x16
#define vp9_subpix_eighttap16x16 vp9_eighttap_predict16x16_c
#endif
extern prototype_subpixel_predict(vp9_subpix_eighttap16x16);

#ifndef vp9_subpix_eighttap8x8
#define vp9_subpix_eighttap8x8 vp9_eighttap_predict8x8_c
#endif
extern prototype_subpixel_predict(vp9_subpix_eighttap8x8);

#ifndef vp9_subpix_eighttap_avg16x16
#define vp9_subpix_eighttap_avg16x16 vp9_eighttap_predict_avg16x16_c
#endif
extern prototype_subpixel_predict(vp9_subpix_eighttap_avg16x16);

#ifndef vp9_subpix_eighttap_avg8x8
#define vp9_subpix_eighttap_avg8x8 vp9_eighttap_predict_avg8x8_c
#endif
extern prototype_subpixel_predict(vp9_subpix_eighttap_avg8x8);

#ifndef vp9_subpix_eighttap8x4
#define vp9_subpix_eighttap8x4 vp9_eighttap_predict8x4_c
#endif
extern prototype_subpixel_predict(vp9_subpix_eighttap8x4);

#ifndef vp9_subpix_eighttap4x4
#define vp9_subpix_eighttap4x4 vp9_eighttap_predict_c
#endif
extern prototype_subpixel_predict(vp9_subpix_eighttap4x4);

#ifndef vp9_subpix_eighttap_avg4x4
#define vp9_subpix_eighttap_avg4x4 vp9_eighttap_predict_avg4x4_c
#endif
extern prototype_subpixel_predict(vp9_subpix_eighttap_avg4x4);

#ifndef vp9_subpix_eighttap16x16_sharp
#define vp9_subpix_eighttap16x16_sharp vp9_eighttap_predict16x16_sharp_c
#endif
extern prototype_subpixel_predict(vp9_subpix_eighttap16x16_sharp);

#ifndef vp9_subpix_eighttap8x8_sharp
#define vp9_subpix_eighttap8x8_sharp vp9_eighttap_predict8x8_sharp_c
#endif
extern prototype_subpixel_predict(vp9_subpix_eighttap8x8_sharp);

#ifndef vp9_subpix_eighttap_avg16x16_sharp
#define vp9_subpix_eighttap_avg16x16_sharp vp9_eighttap_predict_avg16x16_sharp_c
#endif
extern prototype_subpixel_predict(vp9_subpix_eighttap_avg16x16_sharp);

#ifndef vp9_subpix_eighttap_avg8x8_sharp
#define vp9_subpix_eighttap_avg8x8_sharp vp9_eighttap_predict_avg8x8_sharp_c
#endif
extern prototype_subpixel_predict(vp9_subpix_eighttap_avg8x8_sharp);

#ifndef vp9_subpix_eighttap8x4_sharp
#define vp9_subpix_eighttap8x4_sharp vp9_eighttap_predict8x4_sharp_c
#endif
extern prototype_subpixel_predict(vp9_subpix_eighttap8x4_sharp);

#ifndef vp9_subpix_eighttap4x4_sharp
#define vp9_subpix_eighttap4x4_sharp vp9_eighttap_predict_sharp_c
#endif
extern prototype_subpixel_predict(vp9_subpix_eighttap4x4_sharp);

#ifndef vp9_subpix_eighttap_avg4x4_sharp
#define vp9_subpix_eighttap_avg4x4_sharp vp9_eighttap_predict_avg4x4_sharp_c
#endif
extern prototype_subpixel_predict(vp9_subpix_eighttap_avg4x4_sharp);

#ifndef vp9_subpix_bilinear16x16
#define vp9_subpix_bilinear16x16 vp9_bilinear_predict16x16_c
#endif
extern prototype_subpixel_predict(vp9_subpix_bilinear16x16);

#ifndef vp9_subpix_bilinear8x8
#define vp9_subpix_bilinear8x8 vp9_bilinear_predict8x8_c
#endif
extern prototype_subpixel_predict(vp9_subpix_bilinear8x8);

#ifndef vp9_subpix_bilinear_avg16x16
#define vp9_subpix_bilinear_avg16x16 vp9_bilinear_predict_avg16x16_c
#endif
extern prototype_subpixel_predict(vp9_subpix_bilinear_avg16x16);

#ifndef vp9_subpix_bilinear_avg8x8
#define vp9_subpix_bilinear_avg8x8 vp9_bilinear_predict_avg8x8_c
#endif
extern prototype_subpixel_predict(vp9_subpix_bilinear_avg8x8);

#ifndef vp9_subpix_bilinear8x4
#define vp9_subpix_bilinear8x4 vp9_bilinear_predict8x4_c
#endif
extern prototype_subpixel_predict(vp9_subpix_bilinear8x4);

#ifndef vp9_subpix_bilinear4x4
#define vp9_subpix_bilinear4x4 vp9_bilinear_predict4x4_c
#endif
extern prototype_subpixel_predict(vp9_subpix_bilinear4x4);

#ifndef vp9_subpix_bilinear_avg4x4
#define vp9_subpix_bilinear_avg4x4 vp9_bilinear_predict_avg4x4_c
#endif
extern prototype_subpixel_predict(vp9_subpix_bilinear_avg4x4);

typedef prototype_subpixel_predict((*vp9_subpix_fn_t));
typedef struct {
  vp9_subpix_fn_t  eighttap16x16;
  vp9_subpix_fn_t  eighttap8x8;
  vp9_subpix_fn_t  eighttap_avg16x16;
  vp9_subpix_fn_t  eighttap_avg8x8;
  vp9_subpix_fn_t  eighttap_avg4x4;
  vp9_subpix_fn_t  eighttap8x4;
  vp9_subpix_fn_t  eighttap4x4;
  vp9_subpix_fn_t  eighttap16x16_sharp;
  vp9_subpix_fn_t  eighttap8x8_sharp;
  vp9_subpix_fn_t  eighttap_avg16x16_sharp;
  vp9_subpix_fn_t  eighttap_avg8x8_sharp;
  vp9_subpix_fn_t  eighttap_avg4x4_sharp;
  vp9_subpix_fn_t  eighttap8x4_sharp;
  vp9_subpix_fn_t  eighttap4x4_sharp;
  vp9_subpix_fn_t  sixtap16x16;
  vp9_subpix_fn_t  sixtap8x8;
  vp9_subpix_fn_t  sixtap_avg16x16;
  vp9_subpix_fn_t  sixtap_avg8x8;
  vp9_subpix_fn_t  sixtap8x4;
  vp9_subpix_fn_t  sixtap4x4;
  vp9_subpix_fn_t  sixtap_avg4x4;
  vp9_subpix_fn_t  bilinear16x16;
  vp9_subpix_fn_t  bilinear8x8;
  vp9_subpix_fn_t  bilinear_avg16x16;
  vp9_subpix_fn_t  bilinear_avg8x8;
  vp9_subpix_fn_t  bilinear8x4;
  vp9_subpix_fn_t  bilinear4x4;
  vp9_subpix_fn_t  bilinear_avg4x4;
} vp9_subpix_rtcd_vtable_t;

#if CONFIG_RUNTIME_CPU_DETECT
#define SUBPIX_INVOKE(ctx,fn) (ctx)->fn
#else
#define SUBPIX_INVOKE(ctx,fn) vp9_subpix_##fn
#endif

#endif
