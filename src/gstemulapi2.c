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

#include "gstemulapi2.h"

void
emul_avcodec_init_to (CodecContext *ctx, CodecElement *codec, uint8_t *device_buf)
{
  int size = 0, codec_size;

  CODEC_LOG (DEBUG, "[init] write data to qemu.\n");
  size = sizeof(size);
  codec_size =
    sizeof(codec->media_type) + sizeof(codec->codec_type) + sizeof(codec->name);

#if 0
  memcpy (device_buf + size,
      &codec->media_type, sizeof(codec->media_type));
  size += sizeof(codec->media_type);
  memcpy (device_buf + size, &codec->codec_type, sizeof(codec->codec_type));
  size += sizeof(codec->codec_type);
  memcpy (device_buf + size, codec->name, sizeof(codec->name));
  size += sizeof(codec->name);
#endif
  memcpy (device_buf + size, codec, codec_size);
  size += codec_size;

  if (codec->media_type == AVMEDIA_TYPE_VIDEO) {
    memcpy (device_buf + size, &ctx->video, sizeof(ctx->video));
    size += sizeof(ctx->video);
  } else if (codec->media_type == AVMEDIA_TYPE_AUDIO) {
    memcpy (device_buf + size, &ctx->audio, sizeof(ctx->audio));
    size += sizeof(ctx->audio);
  } else {
    GST_ERROR ("media type is unknown.\n");
    return;
  }

  memcpy (device_buf + size,
      &ctx->codecdata_size, sizeof(ctx->codecdata_size));
  size += sizeof(ctx->codecdata_size);
  if (ctx->codecdata_size) {
    memcpy (device_buf + size, ctx->codecdata, ctx->codecdata_size);
    size += ctx->codecdata_size;
  }
  size -= sizeof(size);
  memcpy (device_buf, &size, sizeof(size));

  CODEC_LOG (DEBUG, "[init] write data: %d\n", size);
}

int
emul_avcodec_init_from (CodecContext *ctx, CodecElement *codec, uint8_t *device_buf)
{
  int ret = 0, size = 0;

  CODEC_LOG (DEBUG, "[init] read data from qemu.\n");
  if (codec->media_type == AVMEDIA_TYPE_AUDIO) {
    memcpy (&ctx->audio.sample_fmt,
        (uint8_t *)device_buf, sizeof(ctx->audio.sample_fmt));
    size += sizeof(ctx->audio.sample_fmt);
    CODEC_LOG (DEBUG, "[init] AUDIO sample_fmt: %d\n", ctx->audio.sample_fmt);
  }
  CODEC_LOG (DEBUG, "[init] %s\n", codec->media_type ? "AUDIO" : "VIDEO");
  memcpy (&ret, (uint8_t *)device_buf + size, sizeof(ret));
  size += sizeof(ret);
  memcpy (&ctx->index, (uint8_t *)device_buf + size, sizeof(ctx->index));
  ctx->codec = codec;
  CODEC_LOG (DEBUG, "context index: %d\n", ctx->index);

  return ret;
}

void
emul_avcodec_decode_video_to (uint8_t *in_buf, int in_size, uint8_t *device_buf)
{
  int ret = 0, size = 0;

  CODEC_LOG (DEBUG, "[decode_video] write data to qemu\n");
  size = sizeof(size);
  memcpy (device_buf + size, &in_size, sizeof(in_size));
  size += sizeof(in_size);
  if (in_size > 0) {
    memcpy (device_buf + size, in_buf, in_size);
    size += in_size;
  }

  size -= sizeof(size);
  CODEC_LOG (DEBUG, "[decode_video] total: %d, inbuf size: %d\n", size, in_size);
  memcpy(device_buf, &size, sizeof(size));
}

int
emul_avcodec_decode_video_from (CodecContext *ctx, int *got_picture_ptr, uint8_t *device_buf)
{
  int len = 0, size = 0;

  CODEC_LOG (DEBUG, "[decode_video] read data from qemu.\n");
  memcpy (&len, (uint8_t *)device_buf, sizeof(len));
  size = sizeof(len);
  memcpy (got_picture_ptr,
      (uint8_t *)device_buf + size, sizeof(*got_picture_ptr));
  size += sizeof(*got_picture_ptr);
  memcpy (&ctx->video, (uint8_t *)device_buf + size, sizeof(ctx->video));

  CODEC_LOG (DEBUG, "[decode_video] len: %d, have_date: %d\n", len, *got_picture_ptr);

  return len;
}

void
emul_avcodec_decode_audio_to (uint8_t *in_buf, int in_size, uint8_t *device_buf)
{
  int size = 0;

  size = sizeof(size);
  memcpy (device_buf + size, &in_size, sizeof(in_size));
  size += sizeof(in_size);
  if (in_size > 0) {
    memcpy (device_buf + size, in_buf, in_size);
    size += in_size;
  }

  size -= sizeof(size);
  memcpy (device_buf, &size, sizeof(size));
  CODEC_LOG (DEBUG, "[decode_audio] write size: %d, inbuf_size: %d\n", size, in_size);
}

int
emul_avcodec_decode_audio_from (CodecContext *ctx, int *frame_size_ptr, int16_t *samples, uint8_t *device_buf)
{
  int len = 0, size = 0;

  CODEC_LOG (DEBUG, "[decode_audio] read data\n");
  memcpy (&ctx->audio.channel_layout,
      (uint8_t *)device_buf, sizeof(ctx->audio.channel_layout));
  size = sizeof(ctx->audio.channel_layout);
  memcpy (&len, (uint8_t *)device_buf + size, sizeof(len));
  size += sizeof(len);
  memcpy (frame_size_ptr, (uint8_t *)device_buf + size, sizeof(*frame_size_ptr));
  size += sizeof(*frame_size_ptr);
  CODEC_LOG (DEBUG, "[decode_audio] len: %d, channel_layout: %lld\n",
      len, ctx->audio.channel_layout);
  if (len > 0) {
    memcpy (samples, (uint8_t *)device_buf + size, FF_MAX_AUDIO_FRAME_SIZE);
  }

  return len;
}
