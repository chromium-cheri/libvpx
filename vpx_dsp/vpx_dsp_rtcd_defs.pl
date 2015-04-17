sub vpx_dsp_forward_decls() {
print <<EOF
/*
 * DSP
 */

#include "vpx/vpx_integer.h"

EOF
}
forward_decls qw/vpx_dsp_forward_decls/;

# x86inc.asm doesn't work if pic is enabled on 32 bit platforms so no assembly.
if (vpx_config("CONFIG_USE_X86INC") eq "yes") {
  $mmx_x86inc = 'mmx';
  $sse_x86inc = 'sse';
  $sse2_x86inc = 'sse2';
  $ssse3_x86inc = 'ssse3';
  $avx_x86inc = 'avx';
  $avx2_x86inc = 'avx2';
} else {
  $mmx_x86inc = $sse_x86inc = $sse2_x86inc = $ssse3_x86inc =
  $avx_x86inc = $avx2_x86inc = '';
}

# this variable is for functions that are 64 bit only.
if ($opts{arch} eq "x86_64") {
  $mmx_x86_64 = 'mmx';
  $sse2_x86_64 = 'sse2';
  $ssse3_x86_64 = 'ssse3';
  $avx_x86_64 = 'avx';
  $avx2_x86_64 = 'avx2';
} else {
  $mmx_x86_64 = $sse2_x86_64 = $ssse3_x86_64 =
  $avx_x86_64 = $avx2_x86_64 = '';
}

if (vpx_config("CONFIG_ENCODERS") eq "yes") {
#
# Single block SAD
#
add_proto qw/unsigned int vpx_sad4x4/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride";
specialize qw/vpx_sad4x4 mmx sse neon/;

add_proto qw/unsigned int vpx_sad8x8/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride";
specialize qw/vpx_sad8x8 mmx sse2 neon/;

add_proto qw/unsigned int vpx_sad8x16/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride";
specialize qw/vpx_sad8x16 mmx sse2 neon/;

add_proto qw/unsigned int vpx_sad16x8/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride";
specialize qw/vpx_sad16x8 mmx sse2 neon/; #"$sse2_x86inc";

add_proto qw/unsigned int vpx_sad16x16/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride";
specialize qw/vpx_sad16x16 mmx sse2 sse3 media neon/; #"$sse2_x86inc";
$vpx_sad16x16_media=vpx_sad16x16_armv6;

add_proto qw/unsigned int vpx_sad64x64/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride";
#specialize qw/vpx_sad64x64 neon avx2/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad32x64/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
#specialize qw/vpx_sad32x64 avx2/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad64x32/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
#specialize qw/vpx_sad64x32 avx2/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad32x16/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
#specialize qw/vpx_sad32x16 avx2/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad16x32/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
specialize qw/vpx_sad16x32/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad32x32/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride";
#specialize qw/vpx_sad32x32 neon avx2/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad8x4/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
specialize qw/vpx_sad8x4/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad4x8/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
specialize qw/vpx_sad4x8/, "$sse_x86inc";

#
# Multi-block SAD, comparing a reference to N blocks 1 pixel apart horizontally
#
add_proto qw/void vpx_sad4x4x3/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad4x4x3 sse3/;

add_proto qw/void vpx_sad8x8x3/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad8x8x3 sse3/;

add_proto qw/void vpx_sad8x16x3/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad8x16x3 sse3/;

add_proto qw/void vpx_sad16x8x3/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad16x8x3 sse3 ssse3/;

add_proto qw/void vpx_sad16x16x3/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
#specialize qw/vpx_sad16x16x3 sse3 ssse3/;

# Note the only difference in the following prototypes is that they return into
# an array of int
add_proto qw/void vpx_sad4x4x8/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad4x4x8 sse4_1/;

add_proto qw/void vpx_sad8x8x8/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad8x8x8 sse4_1/;

add_proto qw/void vpx_sad8x16x8/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad8x16x8 sse4_1/;

add_proto qw/void vpx_sad16x8x8/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad16x8x8 sse4_1/;

add_proto qw/void vpx_sad16x16x8/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad16x16x8 sse4_1/;

#
# Multi-block SAD, comparing a reference to N independent blocks
#
add_proto qw/void vpx_sad4x4x4d/, "const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[], int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad4x4x4d sse3/;

add_proto qw/void vpx_sad8x8x4d/, "const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[], int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad8x8x4d sse3/;

add_proto qw/void vpx_sad8x16x4d/, "const uint8_t *src_ptr, int src_stride, const uint8_t * const ref_ptr[], int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad8x16x4d sse3/;

add_proto qw/void vpx_sad16x8x4d/, "const unsigned char *src_ptr, int src_stride, const unsigned char * const ref_ptr[], int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad16x8x4d sse3/;

add_proto qw/void vpx_sad16x16x4d/, "const unsigned char *src_ptr, int src_stride, const unsigned char * const ref_ptr[], int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad16x16x4d sse2 sse3 neon/;

add_proto qw/unsigned int vpx_sad64x64/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride";
specialize qw/vpx_sad64x64 neon avx2/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad32x64/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
specialize qw/vpx_sad32x64 avx2/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad64x32/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
specialize qw/vpx_sad64x32 avx2/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad32x16/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
specialize qw/vpx_sad32x16 avx2/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad32x32/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride";
specialize qw/vpx_sad32x32 neon avx2/, "$sse2_x86inc";


add_proto qw/unsigned int vpx_sad8x16/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride";
specialize qw/vpx_sad8x16/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad8x8/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride";
specialize qw/vpx_sad8x8 neon/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad8x4/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
specialize qw/vpx_sad8x4/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad4x8/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
specialize qw/vpx_sad4x8/, "$sse_x86inc";

add_proto qw/unsigned int vpx_sad4x4/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride";
specialize qw/vpx_sad4x4/, "$sse_x86inc";

add_proto qw/unsigned int vpx_sad64x64_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred";
specialize qw/vpx_sad64x64_avg avx2/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad32x64_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred";
specialize qw/vpx_sad32x64_avg avx2/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad64x32_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred";
specialize qw/vpx_sad64x32_avg avx2/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad32x16_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred";
specialize qw/vpx_sad32x16_avg avx2/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad16x32_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred";
specialize qw/vpx_sad16x32_avg/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad32x32_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred";
specialize qw/vpx_sad32x32_avg avx2/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad16x16_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred";
specialize qw/vpx_sad16x16_avg/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad16x8_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred";
specialize qw/vpx_sad16x8_avg/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad8x16_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred";
specialize qw/vpx_sad8x16_avg/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad8x8_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred";
specialize qw/vpx_sad8x8_avg/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad8x4_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred";
specialize qw/vpx_sad8x4_avg/, "$sse2_x86inc";

add_proto qw/unsigned int vpx_sad4x8_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred";
specialize qw/vpx_sad4x8_avg/, "$sse_x86inc";

add_proto qw/unsigned int vpx_sad4x4_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred";
specialize qw/vpx_sad4x4_avg/, "$sse_x86inc";

add_proto qw/void vpx_sad64x64x3/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad64x64x3/;

add_proto qw/void vpx_sad32x32x3/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad32x32x3/;

add_proto qw/void vpx_sad16x16x3/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad16x16x3 sse3 ssse3/;

add_proto qw/void vpx_sad16x8x3/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad16x8x3 sse3 ssse3/;

add_proto qw/void vpx_sad8x16x3/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad8x16x3 sse3/;

add_proto qw/void vpx_sad8x8x3/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad8x8x3 sse3/;

add_proto qw/void vpx_sad4x4x3/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad4x4x3 sse3/;

add_proto qw/void vpx_sad64x64x8/, "const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array";
specialize qw/vpx_sad64x64x8/;

add_proto qw/void vpx_sad32x32x8/, "const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array";
specialize qw/vpx_sad32x32x8/;

add_proto qw/void vpx_sad16x16x8/, "const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array";
specialize qw/vpx_sad16x16x8 sse4/;

add_proto qw/void vpx_sad16x8x8/, "const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array";
specialize qw/vpx_sad16x8x8 sse4/;

add_proto qw/void vpx_sad8x16x8/, "const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array";
specialize qw/vpx_sad8x16x8 sse4/;

add_proto qw/void vpx_sad8x8x8/, "const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array";
specialize qw/vpx_sad8x8x8 sse4/;

add_proto qw/void vpx_sad8x4x8/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, uint32_t *sad_array";
specialize qw/vpx_sad8x4x8/;

add_proto qw/void vpx_sad4x8x8/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, uint32_t *sad_array";
specialize qw/vpx_sad4x8x8/;

add_proto qw/void vpx_sad4x4x8/, "const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array";
specialize qw/vpx_sad4x4x8 sse4/;

add_proto qw/void vpx_sad64x64x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad64x64x4d sse2 avx2 neon/;

add_proto qw/void vpx_sad32x64x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad32x64x4d sse2/;

add_proto qw/void vpx_sad64x32x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad64x32x4d sse2/;

add_proto qw/void vpx_sad32x16x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad32x16x4d sse2/;

add_proto qw/void vpx_sad16x32x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad16x32x4d sse2/;

add_proto qw/void vpx_sad32x32x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad32x32x4d sse2 avx2 neon/;

add_proto qw/void vpx_sad16x8x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad16x8x4d sse2/;

add_proto qw/void vpx_sad8x16x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad8x16x4d sse2/;

add_proto qw/void vpx_sad8x8x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad8x8x4d sse2/;

# TODO(jingning): need to convert these 4x8/8x4 functions into sse2 form
add_proto qw/void vpx_sad8x4x4d/, "const uint8_t *src_ptr, int src_stride, const uint8_t* const ref_ptr[], int ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad8x4x4d sse2/;

add_proto qw/void vpx_sad4x8x4d/, "const uint8_t *src_ptr, int src_stride, const uint8_t* const ref_ptr[], int ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad4x8x4d sse/;

add_proto qw/void vpx_sad4x4x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
specialize qw/vpx_sad4x4x4d sse/;

if (vpx_config("CONFIG_VP9_HIGHBITDEPTH") eq "yes") {

  add_proto qw/unsigned int vpx_highbd_sad64x64/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride";
  specialize qw/vpx_highbd_sad64x64/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad32x64/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
  specialize qw/vpx_highbd_sad32x64/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad64x32/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
  specialize qw/vpx_highbd_sad64x32/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad32x16/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
  specialize qw/vpx_highbd_sad32x16/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad16x32/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
  specialize qw/vpx_highbd_sad16x32/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad32x32/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride";
  specialize qw/vpx_highbd_sad32x32/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad16x16/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride";
  specialize qw/vpx_highbd_sad16x16/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad16x8/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride";
  specialize qw/vpx_highbd_sad16x8/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad8x16/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride";
  specialize qw/vpx_highbd_sad8x16/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad8x8/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride";
  specialize qw/vpx_highbd_sad8x8/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad8x4/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
  specialize qw/vpx_highbd_sad8x4/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad4x8/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride";
  specialize qw/vpx_highbd_sad4x8/;

  add_proto qw/unsigned int vpx_highbd_sad4x4/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride";
  specialize qw/vpx_highbd_sad4x4/;

  add_proto qw/unsigned int vpx_highbd_sad64x64_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred";
  specialize qw/vpx_highbd_sad64x64_avg/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad32x64_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred";
  specialize qw/vpx_highbd_sad32x64_avg/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad64x32_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred";
  specialize qw/vpx_highbd_sad64x32_avg/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad32x16_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred";
  specialize qw/vpx_highbd_sad32x16_avg/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad16x32_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred";
  specialize qw/vpx_highbd_sad16x32_avg/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad32x32_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred";
  specialize qw/vpx_highbd_sad32x32_avg/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad16x16_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred";
  specialize qw/vpx_highbd_sad16x16_avg/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad16x8_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred";
  specialize qw/vpx_highbd_sad16x8_avg/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad8x16_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred";
  specialize qw/vpx_highbd_sad8x16_avg/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad8x8_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred";
  specialize qw/vpx_highbd_sad8x8_avg/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad8x4_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred";
  specialize qw/vpx_highbd_sad8x4_avg/, "$sse2_x86inc";

  add_proto qw/unsigned int vpx_highbd_sad4x8_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred";
  specialize qw/vpx_highbd_sad4x8_avg/;

  add_proto qw/unsigned int vpx_highbd_sad4x4_avg/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred";
  specialize qw/vpx_highbd_sad4x4_avg/;

  add_proto qw/void vpx_highbd_sad64x64x3/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad64x64x3/;

  add_proto qw/void vpx_highbd_sad32x32x3/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad32x32x3/;

  add_proto qw/void vpx_highbd_sad16x16x3/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad16x16x3/;

  add_proto qw/void vpx_highbd_sad16x8x3/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad16x8x3/;

  add_proto qw/void vpx_highbd_sad8x16x3/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad8x16x3/;

  add_proto qw/void vpx_highbd_sad8x8x3/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad8x8x3/;

  add_proto qw/void vpx_highbd_sad4x4x3/, "const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad4x4x3/;

  add_proto qw/void vpx_highbd_sad64x64x8/, "const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array";
  specialize qw/vpx_highbd_sad64x64x8/;

  add_proto qw/void vpx_highbd_sad32x32x8/, "const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array";
  specialize qw/vpx_highbd_sad32x32x8/;

  add_proto qw/void vpx_highbd_sad16x16x8/, "const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array";
  specialize qw/vpx_highbd_sad16x16x8/;

  add_proto qw/void vpx_highbd_sad16x8x8/, "const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array";
  specialize qw/vpx_highbd_sad16x8x8/;

  add_proto qw/void vpx_highbd_sad8x16x8/, "const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array";
  specialize qw/vpx_highbd_sad8x16x8/;

  add_proto qw/void vpx_highbd_sad8x8x8/, "const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array";
  specialize qw/vpx_highbd_sad8x8x8/;

  add_proto qw/void vpx_highbd_sad8x4x8/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, uint32_t *sad_array";
  specialize qw/vpx_highbd_sad8x4x8/;

  add_proto qw/void vpx_highbd_sad4x8x8/, "const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, uint32_t *sad_array";
  specialize qw/vpx_highbd_sad4x8x8/;

  add_proto qw/void vpx_highbd_sad4x4x8/, "const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array";
  specialize qw/vpx_highbd_sad4x4x8/;

  add_proto qw/void vpx_highbd_sad64x64x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad64x64x4d sse2/;

  add_proto qw/void vpx_highbd_sad32x64x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad32x64x4d sse2/;

  add_proto qw/void vpx_highbd_sad64x32x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad64x32x4d sse2/;

  add_proto qw/void vpx_highbd_sad32x16x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad32x16x4d sse2/;

  add_proto qw/void vpx_highbd_sad16x32x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad16x32x4d sse2/;

  add_proto qw/void vpx_highbd_sad32x32x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad32x32x4d sse2/;

  add_proto qw/void vpx_highbd_sad16x16x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad16x16x4d sse2/;

  add_proto qw/void vpx_highbd_sad16x8x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad16x8x4d sse2/;

  add_proto qw/void vpx_highbd_sad8x16x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad8x16x4d sse2/;

  add_proto qw/void vpx_highbd_sad8x8x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad8x8x4d sse2/;

  add_proto qw/void vpx_highbd_sad8x4x4d/, "const uint8_t *src_ptr, int src_stride, const uint8_t* const ref_ptr[], int ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad8x4x4d sse2/;

  add_proto qw/void vpx_highbd_sad4x8x4d/, "const uint8_t *src_ptr, int src_stride, const uint8_t* const ref_ptr[], int ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad4x8x4d sse2/;

  add_proto qw/void vpx_highbd_sad4x4x4d/, "const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array";
  specialize qw/vpx_highbd_sad4x4x4d sse2/;

}  # CONFIG_VP9_HIGHBITDEPTH
}  # CONFIG_ENCODERS

1;
