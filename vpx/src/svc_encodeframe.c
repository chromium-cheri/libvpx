/**
 * @file
 * VP9 SVC encoding support via libvpx
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VPX_DISABLE_CTRL_TYPECHECKS 1
#define VPX_CODEC_DISABLE_COMPAT 1
#include "vpx/svc_context.h"
#include "vpx/vp8cx.h"
#include "vpx/vpx_encoder.h"

#define SVC_REFERENCE_FRAMES 8

static const char *DEFAULT_QUANTIZER_VALUES = "60,53,39,33,27";
static const char *DEFAULT_SCALE_FACTORS = "4/16,5/16,7/16,11/16,16/16";
static char *colors[VPX_SS_MAX_LAYERS] = {"\x1B[35m", "\x1B[32m", "\x1B[33m",
                                          "\x1B[34m", "\x1B[36m"};
static char *normal_color = "\x1B[0m";

typedef struct SvcInternal {
  // values extracted from options
  int scaling_factor_num[VPX_SS_MAX_LAYERS];
  int scaling_factor_den[VPX_SS_MAX_LAYERS];
  int quantizer[VPX_SS_MAX_LAYERS];

  // accumulated statistics
  double psnr_in_layer[VPX_SS_MAX_LAYERS];
  int bytes_in_layer[VPX_SS_MAX_LAYERS];

  // codec encoding values
  int width;
  int height;

  // state variables
  int encode_frame_count;
  int frame_within_gop;
  vpx_enc_frame_flags_t enc_frame_flags;
  int layers;
  int layer;
  int is_keyframe;

  size_t frame_size;
  size_t buffer_size;
  void *buffer;

  char message_buffer[2048];
  vpx_codec_ctx_t *codec_ctx;
} SvcInternal;

// One encoded frame layer
struct LayerData {
  void *buf;    // compressed data buffer
  size_t size;  // length of compressed data
  struct LayerData *next;
};

#ifdef _WIN32
#define strtok_r strtok_s
char *strtok_s(char *strToken, const char *strDelimit, char **context);
#endif

// create LayerData from encoder output
static struct LayerData *ld_create(void *buf, size_t size) {
  struct LayerData *layer_data;

  layer_data = malloc(sizeof(struct LayerData));
  if (layer_data == NULL) {
    return NULL;
  }
  layer_data->buf = malloc(size);
  if (layer_data->buf == NULL) {
    return NULL;
  }
  memcpy(layer_data->buf, buf, size);
  layer_data->size = size;
  return layer_data;
}

// free LayerData
static void ld_free(struct LayerData *layer_data) {
  if (layer_data->buf) {
    free(layer_data->buf);
    layer_data->buf = NULL;
  }
  free(layer_data);
}

// add layer data to list
static void ld_list_add(struct LayerData **list, struct LayerData *layer_data) {
  struct LayerData **p = list;

  while (*p != NULL) p = &(*p)->next;
  *p = layer_data;
  layer_data->next = NULL;
}

// get accumulated size of layer data
static size_t ld_list_get_buffer_size(struct LayerData *list) {
  struct LayerData *p;
  size_t size = 0;

  for (p = list; p != NULL; p = p->next) {
    size += p->size;
  }
  return size;
}

// copy layer data to buffer
static void ld_list_copy_to_buffer(struct LayerData *list, uint8_t *buffer) {
  struct LayerData *p;

  for (p = list; p != NULL; p = p->next) {
    buffer[0] = 1;
    memcpy(buffer, p->buf, p->size);
    buffer += p->size;
  }
}

// free layer data list
static void ld_list_free(struct LayerData *list) {
  struct LayerData *p = list;

  while (p) {
    list = list->next;
    ld_free(p);
    p = list;
  }
}

// Superframe Index
#define SUPERFRAME_SLOTS (8)
#define SUPERFRAME_BUFFER_SIZE (SUPERFRAME_SLOTS * sizeof(uint32_t) + 2)

struct Superframe {
  int count;
  uint32_t sizes[SUPERFRAME_SLOTS];
  uint32_t magnitude;
  uint8_t buffer[SUPERFRAME_BUFFER_SIZE];
  size_t index_size;
};

static void sf_create_index(struct Superframe *sf) {
  uint8_t marker = 0xc0;
  int mag, mask;
  uint8_t *bufp;
  int i, j;
  int this_sz;

  if (sf->count == 0 || sf->count >= 8) return;

  // Add the number of frames to the marker byte
  marker |= sf->count - 1;

  // Choose the magnitude
  for (mag = 0, mask = 0xff; mag < 4; mag++) {
    if (sf->magnitude < mask) break;
    mask <<= 8;
    mask |= 0xff;
  }
  marker |= mag << 3;

  // Write the index
  sf->index_size = 2 + (mag + 1) * sf->count;
  bufp = sf->buffer;

  *bufp++ = marker;
  for (i = 0; i < sf->count; i++) {
    this_sz = sf->sizes[i];

    for (j = 0; j <= mag; j++) {
      *bufp++ = this_sz & 0xff;
      this_sz >>= 8;
    }
  }
  *bufp++ = marker;
}

static void svc_log_reset(SvcContext *svc_ctx) {
  SvcInternal *si = (SvcInternal *)svc_ctx->internal;
  si->message_buffer[0] = '\0';
}

static int svc_log(SvcContext *svc_ctx, int level, const char *fmt, ...) {
  char buf[512];
  int retval = 0;
  va_list ap;
  SvcInternal *si = (SvcInternal *)svc_ctx->internal;

  if (level > svc_ctx->log_level) {
    return retval;
  }

  va_start(ap, fmt);
  retval = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (svc_ctx->log_print) {
    printf("%s", buf);
  } else {
    strncat(si->message_buffer, buf,
            sizeof(si->message_buffer) - strlen(si->message_buffer) - 1);
  }

  if (level == SVC_LOG_ERROR) {
    si->codec_ctx->err_detail = si->message_buffer;
  }
  return retval;
}

static vpx_codec_err_t set_option_encoding_mode(SvcContext *svc_ctx,
                                                const char *value_str) {
  if (strcmp(value_str, "i") == 0) {
    svc_ctx->encoding_mode = INTER_LAYER_PREDICTION_I;
  } else if (strcmp(value_str, "alt-ip") == 0) {
    svc_ctx->encoding_mode = ALT_INTER_LAYER_PREDICTION_IP;
  } else if (strcmp(value_str, "ip") == 0) {
    svc_ctx->encoding_mode = INTER_LAYER_PREDICTION_IP;
  } else if (strcmp(value_str, "gf") == 0) {
    svc_ctx->encoding_mode = USE_GOLDEN_FRAME;
  } else {
    svc_log(svc_ctx, SVC_LOG_ERROR, "invalid encoding mode: %s", value_str);
    return VPX_CODEC_INVALID_PARAM;
  }
  return VPX_CODEC_OK;
}

/**
 * Parse SVC encoding options
 * Format: encoding-mode=<svc_mode>,layers=<layer_count>
 *         scale-factors=<n1>/<d1>,<n2>/<d2>,...
 *         quantizers=<q1>,<q2>,...
 * svc_mode = [i|ip|alt_ip|gf]
 */
static vpx_codec_err_t svc_parse_options(SvcContext *svc_ctx) {
  char *input_string;
  char *option_name;
  char *option_value;
  char *input_ptr;
  int res;

  if (svc_ctx->options == NULL) return VPX_CODEC_OK;
  input_string = strdup(svc_ctx->options);

  // parse option name
  option_name = strtok_r(input_string, "=", &input_ptr);
  while (option_name != NULL) {
    // parse option value
    option_value = strtok_r(NULL, " ", &input_ptr);
    if (option_value == NULL) {
      svc_log(svc_ctx, SVC_LOG_ERROR, "option missing value: %s\n",
              option_name);
      return VPX_CODEC_INVALID_PARAM;
    }
    if (strcmp("encoding-mode", option_name) == 0) {
      res = set_option_encoding_mode(svc_ctx, option_value);
      if (res != VPX_CODEC_OK) return res;
    } else if (strcmp("layers", option_name) == 0) {
      svc_ctx->spatial_layers = atoi(option_value);
    } else if (strcmp("scale-factors", option_name) == 0) {
      svc_ctx->scale_factors = option_value;
    } else if (strcmp("quantizers", option_name) == 0) {
      svc_ctx->quantizer_values = option_value;
    } else {
      svc_log(svc_ctx, SVC_LOG_ERROR, "invalid option: %s\n", option_name);
      return VPX_CODEC_INVALID_PARAM;
    }
    option_name = strtok_r(NULL, "=", &input_ptr);
  }
  return VPX_CODEC_OK;
}

static vpx_codec_err_t parse_quantizer_values(SvcContext *svc_ctx) {
  char *input_string;
  char *token;
  const char *delim = ",";
  char *save_ptr;
  int found = 0;
  int i, q;
  SvcInternal *si = (SvcInternal *)svc_ctx->internal;

  if (svc_ctx->quantizer_values == NULL ||
      strlen(svc_ctx->quantizer_values) == 0) {
    input_string = strdup(DEFAULT_QUANTIZER_VALUES);
  } else {
    input_string = strdup(svc_ctx->quantizer_values);
  }

  token = strtok_r(input_string, delim, &save_ptr);
  for (i = 0; i < si->layers; ++i) {
    if (token != NULL) {
      q = atoi(token);
      if (q <= 0 || q > 100) {
        svc_log(svc_ctx, SVC_LOG_ERROR,
                "svc-quantizer-values: invalid value %s\n", token);
        return VPX_CODEC_INVALID_PARAM;
      }
      token = strtok_r(NULL, delim, &save_ptr);
      found = i + 1;
    } else {
      q = 0;
    }
    si->quantizer[i + VPX_SS_MAX_LAYERS - si->layers] = q;
  }
  if (found != si->layers) {
    svc_log(svc_ctx, SVC_LOG_ERROR,
            "svc: quantizers: %d values required, but only %d specified\n",
            si->layers, found);
    return VPX_CODEC_INVALID_PARAM;
  }
  free(input_string);
  return VPX_CODEC_OK;
}

static vpx_codec_err_t svc_invalid_scale_factor(SvcContext *svc_ctx,
                                                char *value) {
  svc_log(svc_ctx, SVC_LOG_ERROR, "svc scale-factors: invalid value %s\n",
          value);
  return VPX_CODEC_INVALID_PARAM;
}

static vpx_codec_err_t parse_scale_factors(SvcContext *svc_ctx) {
  char *input_string;
  char *token;
  const char *delim = ",";
  char *save_ptr;
  int found = 0;
  int i;
  int64_t num, den;
  SvcInternal *si = (SvcInternal *)svc_ctx->internal;

  if (svc_ctx->scale_factors == NULL || strlen(svc_ctx->scale_factors) == 0) {
    input_string = strdup(DEFAULT_SCALE_FACTORS);
  } else {
    input_string = strdup(svc_ctx->scale_factors);
  }

  token = strtok_r(input_string, delim, &save_ptr);
  for (i = 0; i < si->layers; i++) {
    num = den = 1;
    if (token != NULL) {
      num = strtol(token, &token, 10);
      if (num <= 0) return svc_invalid_scale_factor(svc_ctx, token);
      if (*token++ != '/') return svc_invalid_scale_factor(svc_ctx, token);
      den = strtol(token, &token, 10);
      if (den <= 0) return svc_invalid_scale_factor(svc_ctx, token);

      token = strtok_r(NULL, delim, &save_ptr);
      found = i + 1;
    }
    si->scaling_factor_num[i + VPX_SS_MAX_LAYERS - si->layers] = (int)num;
    si->scaling_factor_den[i + VPX_SS_MAX_LAYERS - si->layers] = (int)den;
  }
  if (found != si->layers) {
    svc_log(svc_ctx, SVC_LOG_ERROR,
            "svc: scale-factors: %d values required, but only %d specified\n",
            si->layers, found);
    return VPX_CODEC_INVALID_PARAM;
  }
  free(input_string);
  return VPX_CODEC_OK;
}

vpx_codec_err_t vpx_svc_init(SvcContext *svc_ctx, vpx_codec_ctx_t *codec_ctx,
                             vpx_codec_iface_t *iface,
                             vpx_codec_enc_cfg_t *enc_cfg) {
  int max_intra_size_pct;
  vpx_codec_err_t res;
  SvcInternal *si;

  if (svc_ctx->internal == NULL) {
    svc_ctx->internal = malloc(sizeof(SvcInternal));
  }
  si = (SvcInternal *)svc_ctx->internal;
  memset(si, 0, sizeof(SvcInternal));
  si->codec_ctx = codec_ctx;

  si->width = enc_cfg->g_w;
  si->height = enc_cfg->g_h;

  // parse any supplied command line options
  res = svc_parse_options(svc_ctx);
  if (res != VPX_CODEC_OK) return res;

  if (svc_ctx->spatial_layers < 1 ||
      svc_ctx->spatial_layers > VPX_SS_MAX_LAYERS) {
    svc_log(svc_ctx, SVC_LOG_ERROR, "spatial layers: invalid value: %d\n",
            svc_ctx->spatial_layers);
    return VPX_CODEC_INVALID_PARAM;
  }
  // use SvcInternal value for number of layers to enable forcing single layer
  // for first frame
  si->layers = svc_ctx->spatial_layers;

  if (svc_ctx->gop_size < 2) {
    svc_log(svc_ctx, SVC_LOG_ERROR, "gop_size too small: %d\n",
            svc_ctx->gop_size);
    return VPX_CODEC_INVALID_PARAM;
  }
  res = parse_quantizer_values(svc_ctx);
  if (res != VPX_CODEC_OK) return res;

  res = parse_scale_factors(svc_ctx);
  if (res != VPX_CODEC_OK) return res;

  // initialize encoder configuration
  enc_cfg->ss_number_layers = si->layers;
  // force single pass
  enc_cfg->g_pass = VPX_RC_ONE_PASS;
  // Lag in frames not currently supported
  enc_cfg->g_lag_in_frames = 0;

  // TODO(ivanmaltz): determine if these values need to be set explicitly for
  // svc, or if the normal default/override mechanism can be used
  enc_cfg->rc_dropframe_thresh = 0;
  enc_cfg->rc_end_usage = VPX_CBR;
  enc_cfg->rc_resize_allowed = 0;
  enc_cfg->rc_min_quantizer = 33;
  enc_cfg->rc_max_quantizer = 33;
  enc_cfg->rc_undershoot_pct = 100;
  enc_cfg->rc_overshoot_pct = 15;
  enc_cfg->rc_buf_initial_sz = 500;
  enc_cfg->rc_buf_optimal_sz = 600;
  enc_cfg->rc_buf_sz = 1000;

  enc_cfg->g_error_resilient = 1;
  enc_cfg->kf_mode = VPX_KF_DISABLED;
  enc_cfg->kf_min_dist = enc_cfg->kf_max_dist = 3000;

  // Initialize codec
  res = vpx_codec_enc_init(codec_ctx, iface, enc_cfg, VPX_CODEC_USE_PSNR);
  if (res != VPX_CODEC_OK) {
    svc_log(svc_ctx, SVC_LOG_ERROR, "svc_enc_init error\n");
    return res;
  }

  vpx_codec_control(codec_ctx, VP9E_SET_SVC, 1);
  vpx_codec_control(codec_ctx, VP8E_SET_CPUUSED, 1);
  vpx_codec_control(codec_ctx, VP8E_SET_STATIC_THRESHOLD, 1);
  vpx_codec_control(codec_ctx, VP8E_SET_NOISE_SENSITIVITY, 1);
  vpx_codec_control(codec_ctx, VP8E_SET_TOKEN_PARTITIONS, 1);

  max_intra_size_pct =
      (int)(((double)enc_cfg->rc_buf_optimal_sz * 0.5) *
            ((double)enc_cfg->g_timebase.den / enc_cfg->g_timebase.num) / 10.0);
  vpx_codec_control(codec_ctx, VP8E_SET_MAX_INTRA_BITRATE_PCT,
                    max_intra_size_pct);
  return VPX_CODEC_OK;
}

// SVC Algorithm flags - these get mapped to VP8_EFLAG_* defined in vp8cx.h

// encoder should reference the last frame
#define USE_LAST (1 << 0)

// encoder should reference the alt ref frame
#define USE_ARF (1 << 1)

// encoder should reference the golden frame
#define USE_GF (1 << 2)

// encoder should copy current frame to the last frame buffer
#define UPDATE_LAST (1 << 3)

// encoder should copy current frame to the alt ref frame buffer
#define UPDATE_ARF (1 << 4)

// encoder should copy current frame to the golden frame
#define UPDATE_GF (1 << 5)

static int map_vp8_flags(int svc_flags) {
  int flags = 0;

  if (!(svc_flags & USE_LAST)) flags |= VP8_EFLAG_NO_REF_LAST;

  if (!(svc_flags & USE_ARF)) flags |= VP8_EFLAG_NO_REF_ARF;

  if (!(svc_flags & USE_GF)) flags |= VP8_EFLAG_NO_REF_GF;

  if (svc_flags & UPDATE_LAST) {
    // last is updated automatically
  } else {
    flags |= VP8_EFLAG_NO_UPD_LAST;
  }
  if (svc_flags & UPDATE_ARF) {
    flags |= VP8_EFLAG_FORCE_ARF;
  } else {
    flags |= VP8_EFLAG_NO_UPD_ARF;
  }
  if (svc_flags & UPDATE_GF) {
    flags |= VP8_EFLAG_FORCE_GF;
  } else {
    flags |= VP8_EFLAG_NO_UPD_GF;
  }
  return flags;
}

/**
 * Helper to check if the current frame is the first, full resolution dummy.
 */
static int vpx_svc_dummy_frame(SvcContext *svc_ctx, SvcInternal *si) {
  return svc_ctx->first_frame_full_size == 1 && si->encode_frame_count == 0;
}

static void calculate_enc_frame_flags(SvcContext *svc_ctx) {
  vpx_enc_frame_flags_t flags = VPX_EFLAG_FORCE_KF;
  SvcInternal *si = (SvcInternal *)svc_ctx->internal;

  int is_keyframe = (si->frame_within_gop == 0);

  // keyframe layer zero is identical for all modes
  if ((is_keyframe && si->layer == 0) || vpx_svc_dummy_frame(svc_ctx, si)) {
    si->enc_frame_flags = VPX_EFLAG_FORCE_KF;
    return;
  }

  switch (svc_ctx->encoding_mode) {
    case ALT_INTER_LAYER_PREDICTION_IP:
      if (si->layer == 0) {
        flags = map_vp8_flags(USE_LAST | UPDATE_LAST);
      } else if (is_keyframe) {
        if (si->layer == si->layers - 1) {
          flags = map_vp8_flags(USE_ARF | UPDATE_LAST);
        } else {
          flags = map_vp8_flags(USE_ARF | UPDATE_LAST | UPDATE_GF);
        }
      } else {
        flags = map_vp8_flags(USE_LAST | USE_ARF | UPDATE_LAST);
      }
      break;
    case INTER_LAYER_PREDICTION_I:
      if (si->layer == 0) {
        flags = map_vp8_flags(USE_LAST | UPDATE_LAST);
      } else if (is_keyframe) {
        flags = map_vp8_flags(USE_ARF | UPDATE_LAST);
      } else {
        flags = map_vp8_flags(USE_LAST | UPDATE_LAST);
      }
      break;
    case INTER_LAYER_PREDICTION_IP:
      if (si->layer == 0) {
        flags = map_vp8_flags(USE_LAST | UPDATE_LAST);
      } else if (is_keyframe) {
        flags = map_vp8_flags(USE_ARF | UPDATE_LAST);
      } else {
        flags = map_vp8_flags(USE_LAST | USE_ARF | UPDATE_LAST);
      }
      break;
    case USE_GOLDEN_FRAME:
      if (2 * si->layers - SVC_REFERENCE_FRAMES <= si->layer) {
        if (si->layer == 0) {
          flags = map_vp8_flags(USE_LAST | USE_GF | UPDATE_LAST);
        } else if (is_keyframe) {
          flags = map_vp8_flags(USE_ARF | UPDATE_LAST | UPDATE_GF);
        } else {
          flags = map_vp8_flags(USE_LAST | USE_ARF | USE_GF | UPDATE_LAST);
        }
      } else {
        if (si->layer == 0) {
          flags = map_vp8_flags(USE_LAST | UPDATE_LAST);
        } else if (is_keyframe) {
          flags = map_vp8_flags(USE_ARF | UPDATE_LAST);
        } else {
          flags = map_vp8_flags(USE_LAST | UPDATE_LAST);
        }
      }
      break;
    default:
      svc_log(svc_ctx, SVC_LOG_ERROR, "unexpected encoding mode: %d\n",
              svc_ctx->encoding_mode);
      break;
  }
  si->enc_frame_flags = flags;
}

vpx_codec_err_t vpx_svc_get_layer_resolution(SvcContext *svc_ctx, int layer,
                                             unsigned int *width,
                                             unsigned int *height) {
  int w, h, index, num, den;
  SvcInternal *si = (SvcInternal *)svc_ctx->internal;

  if (layer < 0 || layer >= si->layers) return VPX_CODEC_INVALID_PARAM;

  index = layer + VPX_SS_MAX_LAYERS - si->layers;
  num = si->scaling_factor_num[index];
  den = si->scaling_factor_den[index];
  if (num == 0 || den == 0) return VPX_CODEC_INVALID_PARAM;

  w = si->width * num / den;
  h = si->height * num / den;

  // make height and width even to make chrome player happy
  w += w % 2;
  h += h % 2;

  *width = w;
  *height = h;

  return VPX_CODEC_OK;
}

static void set_svc_parameters(SvcContext *svc_ctx,
                               vpx_codec_ctx_t *codec_ctx) {
  int layer, layer_index;
  vpx_svc_parameters_t svc_params;
  int use_higher_layer;
  SvcInternal *si = (SvcInternal *)svc_ctx->internal;

  memset(&svc_params, 0, sizeof(svc_params));
  svc_params.layer = si->layer;
  svc_params.flags = si->enc_frame_flags;

  layer = si->layer;
  if (svc_ctx->encoding_mode == ALT_INTER_LAYER_PREDICTION_IP &&
      si->frame_within_gop == 0) {
    // layers 1 & 3 don't exist in this mode, use the higher one
    if (layer == 0 || layer == 2) {
      layer += 1;
    }
  }
  if (VPX_CODEC_OK != vpx_svc_get_layer_resolution(svc_ctx, layer,
                                                   &svc_params.width,
                                                   &svc_params.height)) {
    svc_log(svc_ctx, SVC_LOG_ERROR, "vpx_svc_get_layer_resolution failed\n");
  }
  layer_index = layer + VPX_SS_MAX_LAYERS - si->layers;
  svc_params.min_quantizer = si->quantizer[layer_index];
  svc_params.max_quantizer = si->quantizer[layer_index];
  svc_params.distance_from_i_frame = si->frame_within_gop;

  // Use buffer i for layer i LST
  svc_params.lst_fb_idx = si->layer;

  // Use buffer i-1 for layer i Alt (Inter-layer prediction)
  if (si->layer != 0) {
    use_higher_layer =
        svc_ctx->encoding_mode == ALT_INTER_LAYER_PREDICTION_IP &&
        si->frame_within_gop == 0;
    svc_params.alt_fb_idx = use_higher_layer ? si->layer - 2 : si->layer - 1;
  }

  if (svc_ctx->encoding_mode == ALT_INTER_LAYER_PREDICTION_IP) {
    svc_params.gld_fb_idx = si->layer + 1;
  } else {
    if (si->layer < 2 * si->layers - SVC_REFERENCE_FRAMES)
      svc_params.gld_fb_idx = svc_params.lst_fb_idx;
    else
      svc_params.gld_fb_idx = 2 * si->layers - 1 - si->layer;
  }

  svc_log(svc_ctx, SVC_LOG_DEBUG, "%sSVC frame: %d, layer: %d, %dx%d, q: %d\n",
          svc_ctx->log_print ? colors[si->layer] : "",  //
          si->encode_frame_count, si->layer, svc_params.width,
          svc_params.height, svc_params.min_quantizer);

  if (svc_params.flags == VPX_EFLAG_FORCE_KF) {
    svc_log(svc_ctx, SVC_LOG_DEBUG, "flags == VPX_EFLAG_FORCE_KF\n");
  } else {
    svc_log(
        svc_ctx, SVC_LOG_DEBUG, "Using:    LST/GLD/ALT [%2d|%2d|%2d]\n",
        svc_params.flags & VP8_EFLAG_NO_REF_LAST ? -1 : svc_params.lst_fb_idx,
        svc_params.flags & VP8_EFLAG_NO_REF_GF ? -1 : svc_params.gld_fb_idx,
        svc_params.flags & VP8_EFLAG_NO_REF_ARF ? -1 : svc_params.alt_fb_idx);
    svc_log(
        svc_ctx, SVC_LOG_DEBUG, "Updating: LST/GLD/ALT [%2d|%2d|%2d]\n",
        svc_params.flags & VP8_EFLAG_NO_UPD_LAST ? -1 : svc_params.lst_fb_idx,
        svc_params.flags & VP8_EFLAG_NO_UPD_GF ? -1 : svc_params.gld_fb_idx,
        svc_params.flags & VP8_EFLAG_NO_UPD_ARF ? -1 : svc_params.alt_fb_idx);
  }

  vpx_codec_control(codec_ctx, VP9E_SET_SVC_PARAMETERS, &svc_params);
}

/**
 * Encode a frame into multiple layers
 * Create a superframe containing the individual layers
 */
vpx_codec_err_t vpx_svc_encode(SvcContext *svc_ctx, vpx_codec_ctx_t *codec_ctx,
                               struct vpx_image *rawimg, vpx_codec_pts_t pts,
                               int64_t duration, int deadline) {
  vpx_codec_err_t res;
  vpx_codec_iter_t iter;
  const vpx_codec_cx_pkt_t *cx_pkt;
  struct LayerData *cx_layer_list = NULL;
  struct LayerData *layer_data;
  size_t frame_pkt_size;
  struct Superframe superframe;
  SvcInternal *si = (SvcInternal *)svc_ctx->internal;

  memset(&superframe, 0, sizeof(superframe));
  svc_log_reset(svc_ctx);

  si->layers = vpx_svc_dummy_frame(svc_ctx, si) ? 1 : svc_ctx->spatial_layers;
  if (si->frame_within_gop >= svc_ctx->gop_size ||
      si->encode_frame_count == 0 ||
      (si->encode_frame_count == 1 && svc_ctx->first_frame_full_size == 1)) {
    si->frame_within_gop = 0;
  }
  si->is_keyframe = (si->frame_within_gop == 0);
  si->frame_size = 0;

  svc_log(svc_ctx, SVC_LOG_DEBUG,
          "vpx_svc_encode  layers: %d, frame_count: %d, frame_within_gop: %d\n",
          si->layers, si->encode_frame_count, si->frame_within_gop);

  // encode each layer
  for (si->layer = 0; si->layer < si->layers; si->layer++) {
    if (svc_ctx->encoding_mode == ALT_INTER_LAYER_PREDICTION_IP &&
        si->is_keyframe && (si->layer == 1 || si->layer == 3)) {
      svc_log(svc_ctx, SVC_LOG_DEBUG, "Skip encoding layer %d\n", si->layer);
      continue;
    }
    calculate_enc_frame_flags(svc_ctx);

    if (vpx_svc_dummy_frame(svc_ctx, si)) {
      // do not set svc parameters, use normal encode
      svc_log(svc_ctx, SVC_LOG_DEBUG, "encoding full size first frame\n");
    } else {
      set_svc_parameters(svc_ctx, codec_ctx);
    }
    res = vpx_codec_encode(codec_ctx, rawimg, pts, duration,
                           si->enc_frame_flags, deadline);
    if (res != VPX_CODEC_OK) {
      return res;
    }
    // save compressed data
    iter = NULL;
    while ((cx_pkt = vpx_codec_get_cx_data(codec_ctx, &iter))) {
      switch (cx_pkt->kind) {
        case VPX_CODEC_CX_FRAME_PKT:
          frame_pkt_size = cx_pkt->data.frame.sz;
          if (!vpx_svc_dummy_frame(svc_ctx, si)) {
            si->bytes_in_layer[si->layer] += frame_pkt_size;
            svc_log(svc_ctx, SVC_LOG_DEBUG,
                    "SVC frame: %d, layer: %d, size: %d%s\n",
                    si->encode_frame_count, si->layer, (int)frame_pkt_size,
                    svc_ctx->log_print ? normal_color : "");
          }
          layer_data = ld_create(cx_pkt->data.frame.buf, frame_pkt_size);
          if (layer_data == NULL) {
            svc_log(svc_ctx, SVC_LOG_ERROR, "Error allocating LayerData\n");
            return 0;
          }
          ld_list_add(&cx_layer_list, layer_data);

          // save layer size in superframe index
          superframe.sizes[superframe.count++] = frame_pkt_size;
          superframe.magnitude |= frame_pkt_size;
          break;
        case VPX_CODEC_PSNR_PKT:
          if (!vpx_svc_dummy_frame(svc_ctx, si)) {
            svc_log(svc_ctx, SVC_LOG_DEBUG,
                    "%sSVC frame: %d, layer: %d, PSNR(Total/Y/U/V): "
                    "%2.3f  %2.3f  %2.3f  %2.3f \n",
                    svc_ctx->log_print ? colors[si->layer] : "",
                    si->encode_frame_count, si->layer,
                    cx_pkt->data.psnr.psnr[0], cx_pkt->data.psnr.psnr[1],
                    cx_pkt->data.psnr.psnr[2], cx_pkt->data.psnr.psnr[3]);
            si->psnr_in_layer[si->layer] += cx_pkt->data.psnr.psnr[0];
          }
          break;
        default:
          break;
      }
    }
  }
  // add superframe index to layer data list
  if (!vpx_svc_dummy_frame(svc_ctx, si)) {
    sf_create_index(&superframe);
    layer_data = ld_create(superframe.buffer, superframe.index_size);
    ld_list_add(&cx_layer_list, layer_data);
  }
  // get accumulated size of layer data
  si->frame_size = ld_list_get_buffer_size(cx_layer_list);
  if (si->frame_size == 0) return VPX_CODEC_ERROR;

  // all layers encoded, create single buffer with concatenated layers
  if (si->frame_size > si->buffer_size) {
    free(si->buffer);
    si->buffer = malloc(si->frame_size);
    si->buffer_size = si->frame_size;
  }
  // copy layer data into packet
  ld_list_copy_to_buffer(cx_layer_list, si->buffer);

  ld_list_free(cx_layer_list);

  svc_log(svc_ctx, SVC_LOG_DEBUG, "SVC frame: %d, kf: %d, size: %d, pts: %d\n",
          si->encode_frame_count, si->is_keyframe, (int)si->frame_size,
          (int)pts);
  si->frame_within_gop++;
  si->encode_frame_count++;

  return VPX_CODEC_OK;
}

char *vpx_svc_get_message(SvcContext *svc_ctx) {
  SvcInternal *si = (SvcInternal *)svc_ctx->internal;
  return si->message_buffer;
}

void *vpx_svc_get_buffer(SvcContext *svc_ctx) {
  SvcInternal *si = (SvcInternal *)svc_ctx->internal;
  return si->buffer;
}

int vpx_svc_get_frame_size(SvcContext *svc_ctx) {
  SvcInternal *si = (SvcInternal *)svc_ctx->internal;
  return si->frame_size;
}

int vpx_svc_get_encode_frame_count(SvcContext *svc_ctx) {
  SvcInternal *si = (SvcInternal *)svc_ctx->internal;
  return si->encode_frame_count;
}

int vpx_svc_is_keyframe(SvcContext *svc_ctx) {
  SvcInternal *si = (SvcInternal *)svc_ctx->internal;
  return (si->is_keyframe);
}

void vpx_svc_set_keyframe(SvcContext *svc_ctx) {
  SvcInternal *si = (SvcInternal *)svc_ctx->internal;
  si->frame_within_gop = 0;
}

void vpx_svc_dump_statistics(SvcContext *svc_ctx) {
  int number_of_frames, number_of_keyframes, encode_frame_count;
  int i;
  int bytes_total = 0;
  SvcInternal *si = (SvcInternal *)svc_ctx->internal;

  svc_log_reset(svc_ctx);

  encode_frame_count = si->encode_frame_count;
  if (svc_ctx->first_frame_full_size) encode_frame_count--;
  if (si->encode_frame_count <= 0) return;

  svc_log(svc_ctx, SVC_LOG_INFO, "\n");
  number_of_keyframes = encode_frame_count / svc_ctx->gop_size + 1;
  for (i = 0; i < si->layers; i++) {
    number_of_frames = encode_frame_count;

    if (svc_ctx->encoding_mode == ALT_INTER_LAYER_PREDICTION_IP &&
        (i == 1 || i == 3)) {
      number_of_frames -= number_of_keyframes;
    }
    svc_log(svc_ctx, SVC_LOG_INFO, "Layer %d PSNR=[%2.3f], Bytes=[%d]\n", i,
            (double)si->psnr_in_layer[i] / number_of_frames,
            si->bytes_in_layer[i]);
    bytes_total += si->bytes_in_layer[i];
  }

  // only display statistics once
  si->encode_frame_count = 0;

  svc_log(svc_ctx, SVC_LOG_INFO, "Total Bytes=[%d]\n", bytes_total);
}
