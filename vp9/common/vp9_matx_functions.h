/*
 *  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed  by a BSD-style license that can be
 *  found in the LICENSE file in the root of the source tree. An additional
 *  intellectual property  rights grant can  be found in the  file PATENTS.
 *  All contributing  project authors may be  found in the AUTHORS  file in
 *  the root of the source tree.
 */

/*
 *  \file vp9_matx_functions.h
 *
 *  This file contains image processing functions for MATX class
 */

#ifndef VP9_COMMON_VP9_MATX_FUNCTIONS_H_
#define VP9_COMMON_VP9_MATX_FUNCTIONS_H_

#include <assert.h>
#include "vp9/common/vp9_matx_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MATX_SUFFIX 8u
#include "vp9/common/vp9_matx_functions_h.def"

struct MATX;


/*!\brief Copy one matx to another (reallocate if needed)
 *
 * \param    src    Source matrix to copy
 * \param    dst    Destination matrix
 */
void vp9_matx_copy_to(CONST_MATX_PTR src, MATX_PTR dst);

/*!\brief Fill in the matrix with zeros
 *
 * \param    image    Matrix to fill in
 */
void vp9_matx_zerofill(MATX_PTR image);

/*!\brief Assign all matrix elements to value
 *
 * \param    image    Source matrix
 * \param    value    Value to assign
 */
void vp9_matx_set_to(MATX_PTR image, int value);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VP9_COMMON_VP9_MATX_FUNCTIONS_H_
