#define MIN_VAL     0
#define MAX_VAL     255

#define SUBPEL_BITS 4
#define SUBPEL_MASK ((1 << SUBPEL_BITS) - 1)
#define FILTER_BITS 7
#define SUBPEL_SHIFTS (1 << SUBPEL_BITS)
#define SUBPEL_TAPS 8
#define INTER_FILTER_COUNT 512
#define ROUND_POWER_OF_TWO(value, n) \
    (((value) + (1 << ((n) - 1))) >> (n))

typedef uchar uint8_t;
typedef short int16_t;

typedef struct inter_pred_param {
  int src_mv;
  int src_stride;

  int dst_mv;
  int dst_stride;
  int filter_x_mv;
  int filter_y_mv;
}INTER_PRED_PARAM;

typedef struct inter_param_index {
  int param_num;
  int x_mv;
  int y_mv;
}INTER_PARAM_INDEX;

static const float4 filter_bits_add_f4 = {64, 64, 64, 64};

static const ushort4 round_bit4 = (ushort4){(ushort)1, (ushort)1, (ushort)1,(ushort)1};

#define ROUND_POWER_OF_TWO_VEC4(value, n) \
    (((value) + ( round_bit4 << ((n) - round_bit4))) >> (n))

static const int16_t vp9_filters[INTER_FILTER_COUNT] = {
  0, 0, 0, 128, 0, 0, 0, 0, 0, 1, -5, 126, 8, -3, 1, 0,
  -1, 3, -10, 122, 18, -6, 2, 0, -1, 4, -13, 118, 27, -9, 3, -1,
  -1, 4, -16, 112, 37, -11, 4, -1, -1, 5, -18, 105, 48, -14, 4, -1,
  -1, 5, -19,  97, 58, -16, 5, -1, -1, 6, -19,  88, 68, -18, 5, -1,
  -1,  6, -19,  78,  78, -19,   6, -1,  -1,   5, -18,  68,  88, -19,   6, -1,
  -1,  5, -16,  58,  97, -19,   5, -1,  -1,   4, -14,  48, 105, -18,   5, -1,
  -1,  4, -11,  37, 112, -16,   4, -1,  -1,   3,  -9,  27, 118, -13,   4, -1,
   0,  2,  -6,  18, 122, -10,   3, -1,   0,   1,  -3,   8, 126,  -5,   1,  0,
   0,  0,   0, 128,   0,   0,   0,  0,  -3,  -1,  32,  64,  38,   1,  -3,  0,
  -2, -2,  29,  63,  41,   2,  -3,  0,  -2,  -2,  26,  63,  43,   4,  -4,  0,
  -2, -3,  24,  62,  46,   5,  -4,  0,  -2,  -3,  21,  60,  49,   7,  -4,  0,
  -1, -4,  18,  59,  51,   9,  -4,  0,  -1,  -4,  16,  57,  53,  12,  -4, -1,
  -1, -4,  14,  55,  55,  14,  -4, -1,  -1,  -4,  12,  53,  57,  16,  -4, -1,
   0, -4,   9,  51,  59,  18,  -4, -1,   0,  -4,   7,  49,  60,  21,  -3, -2,
   0, -4,   5,  46,  62,  24,  -3, -2,   0,  -4,   4,  43,  63,  26,  -2, -2,
   0, -3,   2,  41,  63,  29,  -2, -2,   0,  -3,   1,  38,  64,  32,  -1, -3,
   0,  0,   0, 128,   0,   0,   0,  0,  -1,   3,  -7, 127,   8,  -3,   1,  0,
  -2,  5, -13, 125,  17,  -6,   3, -1,  -3,   7, -17, 121,  27, -10,   5, -2,
  -4,  9, -20, 115,  37, -13,   6, -2,  -4,  10, -23, 108,  48, -16,   8, -3,
  -4, 10, -24, 100,  59, -19,   9, -3,  -4,  11, -24,  90,  70, -21,  10, -4,
  -4, 11, -23,  80,  80, -23,  11, -4,  -4,  10, -21,  70,  90, -24,  11, -4,
  -3,  9, -19,  59, 100, -24,  10, -4,  -3,   8, -16,  48, 108, -23,  10, -4,
  -2,  6, -13,  37, 115, -20,   9, -4,  -2,   5, -10,  27, 121, -17,   7, -3,
  -1,  3,  -6,  17, 125, -13,   5, -2,   0,   1,  -3,   8, 127,  -7,   3, -1,
   0,  0,   0, 128,   0,   0,   0,  0,   0,   0,   0, 120,   8,   0,   0,  0,
   0,  0,   0, 112,  16,   0,   0,  0,   0,   0,   0, 104,  24,   0,   0,  0,
   0,  0,   0,  96,  32,   0,   0,  0,   0,   0,   0,  88,  40,   0,   0,  0,
   0,  0,   0,  80,  48,   0,   0,  0,   0,   0,   0,  72,  56,   0,   0,  0,
   0,  0,   0,  64,  64,   0,   0,  0,   0,   0,   0,  56,  72,   0,   0,  0,
   0,  0,   0,  48,  80,   0,   0,  0,   0,   0,   0,  40,  88,   0,   0,  0,
   0,  0,   0,  32,  96,   0,   0,  0,   0,   0,   0,  24, 104,   0,   0,  0,
   0,  0,   0,  16, 112,   0,   0,  0,   0,   0,   0,   8, 120,   0,   0,  0
};
static int min_val = 0, max_val = 255;
static uint8_t clip_pixel(int value) {
  int temp;
  temp = clamp(value, min_val, max_val);
  return (uint8_t)temp;
}

static float min_f = 0.0f;
static float max_f = 255.0f;
static uint8_t clip_pixel_f(float value) {
  return (uint8_t)(clamp(value, min_f, max_f));
}

static void clip_pixel_f4(float4 value1, float4 value2, uchar *dst) {
  uchar4 tmp = convert_uchar4(clamp(value1, (float4)min_f, (float4)max_f));
  dst[0] = tmp.x;
  dst[1] = tmp.y;
  dst[2] = tmp.z;
  dst[3] = tmp.w;
  tmp = convert_uchar4(clamp(value2, (float4)min_f, (float4)max_f));
  dst[4] = tmp.x;
  dst[5] = tmp.y;
  dst[6] = tmp.z;
  dst[7] = tmp.w;
}
