/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// Simple Encoder
// ==============
//
// This is an example of a simple encoder loop. It takes an input file in
// YV12 format, passes it through the encoder, and writes the compressed
// frames to disk in IVF format. Other decoder examples build upon this
// one.
//
// The details of the IVF format have been elided from this example for
// simplicity of presentation, as IVF files will not generally be used by
// your application. In general, an IVF file consists of a file header,
// followed by a variable number of frames. Each frame consists of a frame
// header followed by a variable length payload. The length of the payload
// is specified in the first four bytes of the frame header. The payload is
// the raw compressed data.
//
// Standard Includes
// -----------------
// For encoders, you only have to include `vpx_encoder.h` and then any
// header files for the specific codecs you use. In this case, we're using
// vp8. The `VPX_CODEC_DISABLE_COMPAT` macro can be defined to ensure
// strict compliance with the latest SDK by disabling some backwards
// compatibility features. Defining this macro is encouraged.
//
// Getting The Default Configuration
// ---------------------------------
// Encoders have the notion of "usage profiles." For example, an encoder
// may want to publish default configurations for both a video
// conferencing appliction and a best quality offline encoder. These
// obviously have very different default settings. Consult the
// documentation for your codec to see if it provides any default
// configurations. All codecs provide a default configuration, number 0,
// which is valid for material in the vacinity of QCIF/QVGA.
//
// Updating The Configuration
// ---------------------------------
// Almost all applications will want to update the default configuration
// with settings specific to their usage. Here we set the width and height
// of the video file to that specified on the command line. We also scale
// the default bitrate based on the ratio between the default resolution
// and the resolution specified on the command line.
//
// Initializing The Codec
// ----------------------
// The encoder is initialized by the following code.
//
// Encoding A Frame
// ----------------
// The frame is read as a continuous block (size width * height * 3 / 2)
// from the input file. If a frame was read (the input file has not hit
// EOF) then the frame is passed to the encoder. Otherwise, a NULL
// is passed, indicating the End-Of-Stream condition to the encoder. The
// `frame_cnt` is reused as the presentation time stamp (PTS) and each
// frame is shown for one frame-time in duration. The flags parameter is
// unused in this example. The deadline is set to VPX_DL_REALTIME to
// make the example run as quickly as possible.
//
// Processing The Encoded Data
// ---------------------------
// Each packet of type `VPX_CODEC_CX_FRAME_PKT` contains the encoded data
// for this frame. We write a IVF frame header, followed by the raw data.
//
// Cleanup
// -------
// The `vpx_codec_destroy` call frees any memory allocated by the codec.
//
// Error Handling
// --------------
// This example does not special case any error return codes. If there was
// an error, a descriptive message is printed and the program exits. With
// few exeptions, vpx_codec functions return an enumerated error status,
// with the value `0` indicating success.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VPX_CODEC_DISABLE_COMPAT 1
#include "vpx/vpx_encoder.h"

#include "./tools_common.h"
#include "./video_writer.h"

static const char *exec_name;

void usage_exit() {
  fprintf(stderr, "Usage: %s <codec> <width> <height> <infile> <outfile>\n",
          exec_name);
  exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
  FILE *infile = NULL;
  vpx_codec_ctx_t codec;
  vpx_codec_enc_cfg_t cfg;
  int frame_count = 0;
  vpx_image_t raw;
  vpx_codec_err_t res;
  VpxVideoInfo info = {0};
  VpxVideoWriter *writer = NULL;
  const VpxInterface *encoder = NULL;
  const int fps = 30;        // TODO(dkovalev) add command line argument
  const int bitrate = 200;   // kbit/s TODO(dkovalev) add command line argument
  const char *const codec_arg = argv[1];
  const char *const width_arg = argv[2];
  const char *const height_arg = argv[3];
  const char *const infile_arg = argv[4];
  const char *const outfile_arg = argv[5];

  exec_name = argv[0];

  if (argc != 6)
    die("Invalid number of arguments");

  encoder = get_vpx_encoder_by_name(codec_arg);
  if (!encoder)
     die("Unsupported codec.");

  info.codec_fourcc = encoder->fourcc;
  info.frame_width = strtol(width_arg, NULL, 0);
  info.frame_height = strtol(height_arg, NULL, 0);
  info.time_base.numerator = 1;
  info.time_base.denominator = fps;

  if (info.frame_width <= 0 ||
      info.frame_height <= 0 ||
      (info.frame_width % 2) != 0 ||
      (info.frame_height % 2) != 0) {
    die("Invalid frame size: %dx%d", info.frame_width, info.frame_height);
  }

  if (!vpx_img_alloc(&raw, VPX_IMG_FMT_I420, info.frame_width,
                                             info.frame_height, 1)) {
    die("Failed to allocate image.");
  }

  printf("Using %s\n", vpx_codec_iface_name(encoder->interface()));

  res = vpx_codec_enc_config_default(encoder->interface(), &cfg, 0);
  if (res)
    die_codec(&codec, "Failed to get default codec config.");

  cfg.g_w = info.frame_width;
  cfg.g_h = info.frame_height;
  cfg.g_timebase.num = info.time_base.numerator;
  cfg.g_timebase.den = info.time_base.denominator;
  cfg.rc_target_bitrate = bitrate;

  writer = vpx_video_writer_open(outfile_arg, kContainerIVF, &info);
  if (!writer)
    die("Failed to open %s for writing.", outfile_arg);

  if (!(infile = fopen(infile_arg, "rb")))
    die("Failed to open %s for reading.", infile_arg);

  if (vpx_codec_enc_init(&codec, encoder->interface(), &cfg, 0))
    die_codec(&codec, "Failed to initialize encoder");

  while (vpx_img_read(&raw, 0, infile)) {
    vpx_codec_iter_t iter = NULL;
    const vpx_codec_cx_pkt_t *pkt = NULL;

    ++frame_count;

    res = vpx_codec_encode(&codec, &raw, frame_count, 1, 0,
                           VPX_DL_GOOD_QUALITY);
    if (res != VPX_CODEC_OK)
      die_codec(&codec, "Failed to encode frame");

    while ((pkt = vpx_codec_get_cx_data(&codec, &iter)) != NULL) {
      if (pkt->kind == VPX_CODEC_CX_FRAME_PKT) {
        const int keyframe = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
        if (!vpx_video_writer_write_frame(writer,
                                          pkt->data.frame.buf,
                                          pkt->data.frame.sz,
                                          pkt->data.frame.pts))
          die_codec(&codec, "Failed to write compressed frame.");
        printf(keyframe ? "K" : ".");
        fflush(stdout);
      }
    }
  }
  printf("\n");
  fclose(infile);
  printf("Processed %d frames.\n", frame_count);

  vpx_img_free(&raw);
  if (vpx_codec_destroy(&codec))
    die_codec(&codec, "Failed to destroy codec.");

  vpx_video_writer_close(writer);

  return EXIT_SUCCESS;
}
