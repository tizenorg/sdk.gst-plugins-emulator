/*
 * GStreamer codec plugin for Tizen Emulator.
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * KiTae Kim <kt920.kim@samsung.com>
 * SeokYeon Hwang <syeon.hwang@samsung.com>
 * YeongKyoon Lee <yeongkyoon.lee@samsung.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Contributors:
 * - S-Core Co., Ltd
 *
 */

#include "gstemulcommon.h"
#include "gstemulapi.h"
#include "gstemuldev.h"

gboolean
emul_avcodec_init (CodecContext *ctx, CodecElement *codec, CodecDevice *dev)
{
  int fd;
  void *mmapbuf;
  int size = 0;
  gboolean ret = TRUE;
  CodecIOParams params;

  printf ("enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd.\n", CODEC_DEV);
    return FALSE;
  }

  mmapbuf = dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address.\n");
    return FALSE;
  }

//  printf("codec_init media_type: %d, codec_type: %d, name: %s\n",
//    media_type, codec_type, name);

  /* copy basic info to initialize codec on the host side.
   * e.g. width, height, FPS ant etc. */
  memcpy ((uint8_t *)mmapbuf, &codec->media_type, sizeof(codec->media_type));
  size = sizeof(codec->media_type);
  memcpy ((uint8_t *)mmapbuf + size, &codec->codec_type, sizeof(codec->codec_type));
  size += sizeof(codec->codec_type);
  memcpy ((uint8_t *)mmapbuf + size, codec->name, sizeof(codec->name));
  size += sizeof(codec->name);

  switch (codec->media_type) {
  case AVMEDIA_TYPE_VIDEO:
    memcpy ((uint8_t *)mmapbuf + size, &ctx->video, sizeof(ctx->video));
    size += sizeof(ctx->video);
    break;
  case AVMEDIA_TYPE_AUDIO:
    memcpy ((uint8_t *)mmapbuf + size, &ctx->audio, sizeof(ctx->audio));
    size += sizeof(ctx->audio);
    break;
  default:
    GST_ERROR ("media type is unknown.\n");
    ret = FALSE;
    break;
  }

  memcpy ((uint8_t *)mmapbuf + size, &ctx->codecdata_size, sizeof(ctx->codecdata_size));
  size += sizeof(ctx->codecdata_size);
  if (ctx->codecdata_size) {
    memcpy ((uint8_t *)mmapbuf + size, ctx->codecdata, ctx->codecdata_size);
  }

  CODEC_PARAM_INIT (params);
  params.api_index = CODEC_INIT;
  params.ctx_index = 0;
  CODEC_WRITE_TO_QEMU (fd, &params, 1);

  if (codec->media_type == AVMEDIA_TYPE_AUDIO) {
    memcpy (&ctx->audio.sample_fmt,
		  (uint8_t *)mmapbuf, sizeof(ctx->audio.sample_fmt));
  }
  ctx->codec = codec;

  printf ("leave: %s\n", __func__);

  return ret;
}

void
emul_avcodec_deinit (CodecContext *ctx, CodecDevice *dev)
{
  int fd;
  void *mmapbuf;
  CodecIOParams params;

  printf ("enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd.\n", CODEC_DEV);
    return;
  }

  mmapbuf = dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address.\n");
    return;
  }

  CODEC_PARAM_INIT (params);
  params.api_index = CODEC_DEINIT;
  params.ctx_index = 0;
  CODEC_WRITE_TO_QEMU (fd, &params, 1);

#if 0
  /* close device fd and release mapped memory region */
  gst_emul_codec_device_close (dev);
#endif

  printf ("leave: %s\n", __func__);
}

int
emul_avcodec_decode_video (CodecContext *ctx, guint8 *in_buf, guint in_size,
        GstBuffer **out_buf, int *got_picture_ptr, CodecDevice *dev)
{
  int fd;
  void *mmapbuf;
  int len = 0, size = 0;
  guint out_size;
  CodecIOParams params;

  *out_buf = NULL;

  printf ("enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address\n");
    return -1;
  }

  memcpy ((uint8_t *)mmapbuf, &in_size, sizeof(guint));
  size = sizeof(guint);
//  memcpy ((uint8_t *)mmapbuf + size, &dec_info->timestamp, sizeof(GstClockTime));
//  size += sizeof(GstClockTime);
  memcpy ((uint8_t *)mmapbuf + size, in_buf, in_size);

  /* provide raw image for decoding to qemu */
  CODEC_PARAM_INIT (params);
  params.api_index = CODEC_DECODE_VIDEO;
  params.ctx_index = 0;
  CODEC_WRITE_TO_QEMU (fd, &params, 1);

  memcpy (&len, (uint8_t *)mmapbuf, sizeof(len));
  size = sizeof(len);


  printf ("leave: %s\n", __func__);

  return len;
}

int
emul_avcodec_decode_audio (CodecContext *ctx, int16_t *samples,
		gint *frame_size_ptr, guint8 *in_buf, guint in_size, CodecDevice *dev)
{
  int fd;
  void *mmapbuf = NULL;
  int size = 0;
  gint out_size = 0, len;
  CodecIOParams params;

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR("failed to get %s fd\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = dev->buf;
  if (!mmapbuf) {
    GST_ERROR("failed to get mmaped memory address\n");
    return -1;
  }

  memcpy ((uint8_t *)mmapbuf, &in_size, sizeof(guint));
  size = sizeof(guint);
  if (in_size > 0) {
    memcpy ((uint8_t *)mmapbuf + size, in_buf, in_size);
  }

  CODEC_PARAM_INIT (params);
  params.api_index = CODEC_DECODE_AUDIO;
  params.ctx_index = 0;
  params.device_mem_offset = 0;
  CODEC_WRITE_TO_QEMU (fd, &params, 1);

  memcpy (&ctx->audio.channel_layout,
		  (uint8_t *)mmapbuf, sizeof(ctx->audio.channel_layout));
  size = sizeof(ctx->audio.channel_layout);
  memcpy (&len, (uint8_t *)mmapbuf + size, sizeof(len));
  size += sizeof(len);
  memcpy (frame_size_ptr, (uint8_t *)mmapbuf + size, sizeof(*frame_size_ptr));
  size += sizeof(*frame_size_ptr);
  if (len > 0) {
    memcpy (samples, (uint8_t *)mmapbuf + size, FF_MAX_AUDIO_FRAME_SIZE);
  }

  return len;
}

int
emul_avcodec_encode_video (CodecContext *ctx, guint8 *in_buf, guint in_size,
        GstBuffer **out_buf, CodecDevice *dev)
{
  int fd;
  void *mmapbuf;
  int len = 0, outbuf_size, size = 0;
  CodecIOParams params;

  printf ("enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd.\n", CODEC_DEV);
    return FALSE;
  }

  mmapbuf = dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address.\n");
    return FALSE;
  }

  memcpy ((uint8_t *)mmapbuf + size, &in_size, sizeof(guint));
  size += sizeof(guint);
#if 0
  memcpy ((uint8_t *)mmapbuf + size, &in_timestamp, sizeof(GstClockTime));
  size += sizeof(GstClockTime);
#endif
  memcpy ((uint8_t *)mmapbuf + size, in_buf, in_size);

  CODEC_PARAM_INIT (params);
  params.api_index = CODEC_DECODE_AUDIO;
  params.ctx_index = 0;
  CODEC_WRITE_TO_QEMU (fd, &params, 1);

#if 0
  size = 0;
  memcpy (&out_size, (uint8_t *)mmapbuf + size, sizeof(uint));
  size += sizeof(guint);

  ret = gst_pad_alloc_buffer_and_set_caps (emulenc->srcpad,
          GST_BUFFER_OFFSET_NONE, out_size,
          GST_PAD_CAPS (emulenc->srcpad), out_buf);

  gst_buffer_set_caps (*out_buf, GST_PAD_CAPS (emulenc->srcpad));

  if (GST_BUFFER_DATA(*out_buf)) {
      memcpy (GST_BUFFER_DATA(*out_buf), (uint8_t *)mmapbuf + size, out_size);
  } else {
      pritnf ("failed to allocate output buffer\n");
  }
#endif

  printf ("leave: %s\n", __func__);

  return len;
}

int
emul_avcodec_encode_audio (CodecContext *ctx, CodecDevice *dev)
{
  int fd;
  void *mmapbuf;
  int len = 0, size = 0;
  CodecIOParams params;

  printf ("enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd.\n", CODEC_DEV);
    return FALSE;
  }

  mmapbuf = dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address.\n");
    return FALSE;
  }

#if 0
  memcpy ((uint8_t *)mmapbuf + size, &in_size, sizeof(guint));
  size += sizeof(guint);
  memcpy ((uint8_t *)mmapbuf + size, &in_timestamp, sizeof(GstClockTime));
  size += sizeof(GstClockTime);
  memcpy ((uint8_t *)mmapbuf + size, in_buf, in_size);
#endif

  CODEC_PARAM_INIT (params);
  params.api_index = CODEC_DECODE_AUDIO;
  params.ctx_index = 0;
  CODEC_WRITE_TO_QEMU (fd, &params, 1);

#if 0
  size = 0;
  memcpy (&out_size, (uint8_t *)mmapbuf + size, sizeof(uint));
  size += sizeof(guint);

  *out_buf = gst_buffer_new();
  GST_BUFFER_DATA (out_buf) = GST_BUFFER_MALLOCDATA (out_buf) = av_malloc (out_size);
  GST_BUFFER_SIZE (out_buf) = out_size;
  //  GST_BUFFER_FREE_FUNC (out_buf) = g_free;
  if (GST_PAD_CAPS(emulenc->srcpad)) {
      gst_buffer_set_caps (*out_buf, GST_PAD_CAPS (emulenc->srcpad));
  }

  if (GST_BUFFER_DATA(*out_buf)) {
      memcpy (GST_BUFFER_DATA(*out_buf), (uint8_t *)mmapbuf + size, out_size);
  } else {
      pritnf ("failed to allocate output buffer\n");
  }
#endif

  printf ("leave: %s\n", __func__);

  return len;
}
