;
;  Copyright (c) 2016 The WebM project authors. All Rights Reserved.
;
;  Use of this source code is governed by a BSD-style license
;  that can be found in the LICENSE file in the root of the source
;  tree. An additional intellectual property rights grant can be found
;  in the file PATENTS.  All contributing project authors may
;  be found in the AUTHORS file in the root of the source tree.
;

; TODO(johannkoenig): Add the necessary include guards to vpx_config.asm.
; vpx_config.asm is not guarded so can not be included twice. Because this will
; be used in conjunction with x86_abi_support.asm or x86inc.asm, it must be
; included after those files.

; Increment register by sizeof() tran_low_t
%macro INCREMENT_TRAN_LOW 1
%if CONFIG_VP9_HIGHBITDEPTH
  add r3, 32
%else
  add r3, 16
%endif
%endmacro

; Increment %1 by sizeof() tran_low_t * %2.
%macro INCREMENT_STRIDE_TRAN_LOW 2
%if CONFIG_VP9_HIGHBITDEPTH
  lea %1, [%1 + %2 * 32]
%else
  lea %1, [%1 + %2 * 16]
%endif
%endmacro

; Load %2 + %3 into m%1.
; %3 is the offset in elements, not bits.
; If tran_low_t is 16 bits (low bit depth configuration) then load the value
; directly. If tran_low_t is 32 bits (high bit depth configuration) then pack
; the values down to 16 bits.
%macro LOAD_TRAN_LOW 3
%if CONFIG_VP9_HIGHBITDEPTH
  mova     m%1, [%2 + %3 * 32]
  packssdw m%1, [%2 + %3 * 32 + 16]
%else
  mova     m%1, [%2 + %3 * 16]
%endif
%endmacro

; Store m%1 to %2 + %3.
; %3 is the offset in elements, not bits.
; If tran_low_t is 16 bits (low bit depth configuration) then store the value
; directly. If tran_low_t is 32 bits (high bit depth configuration) then sign
; extend the values first.
; Uses m%4-m%6 as scratch registers for high bit depth.
%macro STORE_TRAN_LOW 6
%if CONFIG_VP9_HIGHBITDEPTH
  pxor                      m%4, m%4
  mova                      m%5, m%1
  mova                      m%6, m%1
  pcmpgtw                   m%4, m%1
  punpcklwd                 m%5, m%4
  punpckhwd                 m%6, m%4
  mova      [%2 + %3 * 32 +  0], m%5
  mova      [%2 + %3 * 32 + 16], m%6
%else
  mova           [%2 + %3 * 16], m%1
%endif
%endmacro
