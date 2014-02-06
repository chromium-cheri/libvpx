/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "vp9/common/vp9_frame_buffers.h"

#include <stdlib.h>

#include "vpx_mem/vpx_mem.h"

int vp9_init_internal_frame_buffers(struct InternalFrameBufferList* list) {
  if (list == NULL)
    return 0;

  vp9_deinit_internal_frame_buffers(list);

  list->num_internal_frame_buffers =
      VP9_MAXIMUM_REF_BUFFERS +VPX_MAXIMUM_WORK_BUFFERS;
  list->int_fb = vpx_calloc(list->num_internal_frame_buffers,
                            sizeof(*list->int_fb));
  if (list->int_fb == NULL)
    return 0;
  return 1;
}

void vp9_deinit_internal_frame_buffers(struct InternalFrameBufferList *list) {
  int i;

  if (list == NULL)
      return;

  for (i = 0; i < list->num_internal_frame_buffers; ++i) {
    vpx_free(list->int_fb[i].data);
    list->int_fb[i].data = NULL;
  }
  vpx_free(list->int_fb);
  list->int_fb = NULL;
}

int vp9_get_frame_buffer(void *cb_priv, size_t min_size,
                         vpx_codec_frame_buffer_t *fb) {
  int i;
  struct InternalFrameBufferList *const int_fb_list =
      (struct InternalFrameBufferList*)cb_priv;
  if (int_fb_list == NULL || fb == NULL)
    return -1;

  // Find a free frame buffer.
  for (i = 0; i < int_fb_list->num_internal_frame_buffers; ++i) {
    if (!int_fb_list->int_fb[i].in_use)
      break;
  }

  if (i == int_fb_list->num_internal_frame_buffers)
    return -1;

  if (int_fb_list->int_fb[i].size < min_size) {
    vpx_free(int_fb_list->int_fb[i].data);
    int_fb_list->int_fb[i].data = (uint8_t*)vpx_malloc(min_size);
    if (!int_fb_list->int_fb[i].data)
      return -1;

    int_fb_list->int_fb[i].size = min_size;
  }

  fb->data = int_fb_list->int_fb[i].data;
  fb->size = int_fb_list->int_fb[i].size;
  int_fb_list->int_fb[i].in_use = 1;

  // Set the frame buffer's private data to point at the internal frame buffer.
  fb->frame_priv = (void*)(&int_fb_list->int_fb[i]);
  return 0;
}

int vp9_release_frame_buffer(void *cb_priv, vpx_codec_frame_buffer_t *fb) {
  struct InternalFrameBuffer *int_fb;
  (void)cb_priv;
  if (!fb)
    return -1;

  int_fb = (struct InternalFrameBuffer*)fb->frame_priv;
  int_fb->in_use = 0;
  return 0;
}
