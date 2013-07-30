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
emul_avcodec_init_to (CodecContext *ctx,
                      CodecElement *codec,
                      uint8_t *device_buf)
{
  int size = 0, codec_size;

  CODEC_LOG (DEBUG, "[init] write data to qemu.\n");
  size = sizeof(size);
  codec_size =
    sizeof(CodecElement) - sizeof(codec->longname) - sizeof(codec->pix_fmts);

  memcpy (device_buf + size, codec, codec_size);
  size += codec_size;

  if (codec->media_type == AVMEDIA_TYPE_VIDEO) {
    CODEC_LOG (DEBUG, "before avcodec_open. pixel format: %d\n", ctx->video.pix_fmt);
    memcpy (device_buf + size, &ctx->video, sizeof(ctx->video));
//    *(VideoData *)(device_buf + size) = ctx->video;
    size += sizeof(ctx->video);
  } else if (codec->media_type == AVMEDIA_TYPE_AUDIO) {
    memcpy (device_buf + size, &ctx->audio, sizeof(ctx->audio));
//    *(AudioData *)(device_buf + size) = ctx->audio;
    size += sizeof(ctx->audio);
  } else {
    CODEC_LOG (ERR, "media type is unknown.\n");
    return;
  }

  memcpy (device_buf + size,
      &ctx->bit_rate, sizeof(ctx->bit_rate));
//  *(int *)(device_buf + size) = ctx->bit_rate;
  size += sizeof(ctx->bit_rate);
  memcpy (device_buf + size,
      &ctx->codec_tag, sizeof(ctx->codec_tag));
//  *(int *)(device_buf + size) = ctx->codec_tag;
  size += sizeof(ctx->codec_tag);
  memcpy (device_buf + size,
      &ctx->codecdata_size, sizeof(ctx->codecdata_size));
//  *(int *)(device_buf + size) = ctx->codecdata_size;
  size += sizeof(ctx->codecdata_size);
  if (ctx->codecdata_size > 0) {
    memcpy (device_buf + size, ctx->codecdata, ctx->codecdata_size);
    size += ctx->codecdata_size;
  }
  size -= sizeof(size);
  memcpy (device_buf, &size, sizeof(size));
//  *(int *)device_buf = size;

  CODEC_LOG (DEBUG, "[init] write data: %d\n", size);
}

int
emul_avcodec_init_from (CodecContext *ctx,
                        CodecElement *codec,
                        uint8_t *device_buf)
{
  int ret = 0, size = 0;

  CODEC_LOG (DEBUG, "[init] read data from qemu.\n");
  memcpy (&ret, (uint8_t *)device_buf, sizeof(ret));
//  ret = *(int *)device_buf;
  size = sizeof(ret);

  if (!ret) {
    if (codec->media_type == AVMEDIA_TYPE_AUDIO) {
      memcpy (&ctx->audio.sample_fmt,
          (uint8_t *)device_buf + size, sizeof(ctx->audio.sample_fmt));
//      ctx->audio.sample_fmt = *(int *)(device_buf + size);
      size += sizeof(ctx->audio.sample_fmt);
      memcpy (&ctx->audio.frame_size,
          (uint8_t *)device_buf + size, sizeof(ctx->audio.frame_size));
//      ctx->audio.frame_size = *(int *)(device_buf + size);
      size += sizeof(ctx->audio.frame_size);
      memcpy (&ctx->audio.bits_per_smp_fmt,
          (uint8_t *)device_buf + size, sizeof(ctx->audio.bits_per_smp_fmt));
//      ctx->audio.bits_per_smp_fmt = *(int *)(device_buf + size);
      size += sizeof(ctx->audio.bits_per_smp_fmt);

      CODEC_LOG (DEBUG, "[init] sample_fmt %d\n", ctx->audio.sample_fmt);
    }
    ctx->codec = codec;
  } else {
    CODEC_LOG (ERR, "failed to open codec context\n");
  }

  CODEC_LOG (DEBUG, "context index: %d\n", ctx->index);

  return ret;
}

void
emul_avcodec_decode_video_to (uint8_t *in_buf, int in_size,
                              int idx, int64_t in_offset,
                              uint8_t *device_buf)
{
  int size = 0;

  CODEC_LOG (DEBUG, "[decode_video] write data to qemu\n");
  size = sizeof(size);
  memcpy (device_buf + size, &in_size, sizeof(in_size));
//  *(int *)(device_buf + size) = in_size;
  size += sizeof(in_size);
  memcpy (device_buf + size, &idx, sizeof(idx));
//  *(int *)(device_buf + size) = idx;
  size += sizeof(idx);
  memcpy (device_buf + size, &in_offset, sizeof(in_offset));
//  *(int64_t *)(device_buf + size) = in_offset;
  size += sizeof(in_offset);
  if (in_size > 0) {
    memcpy (device_buf + size, in_buf, in_size);
    size += in_size;
  }

  size -= sizeof(size);
  CODEC_LOG (DEBUG, "[decode_video] total: %d, inbuf size: %d\n", size, in_size);
  memcpy(device_buf, &size, sizeof(size));
//  *(int *)device_buf = size;
  CODEC_LOG (DEBUG, "[decode_video] leave\n");
}

int
emul_avcodec_decode_video_from (CodecContext *ctx,
                                int *got_picture_ptr,
                                uint8_t *device_buf)
{
  int len = 0, size = 0;

  CODEC_LOG (DEBUG, "[decode_video] read data from qemu.\n");
  memcpy (&len, (uint8_t *)device_buf, sizeof(len));
//  len = *(int *)device_buf;
  size = sizeof(len);
  memcpy (got_picture_ptr,
      (uint8_t *)device_buf + size, sizeof(*got_picture_ptr));
//  *got_picture_ptr = *(int *)(device_buf + size);
  size += sizeof(*got_picture_ptr);
  memcpy (&ctx->video, (uint8_t *)device_buf + size, sizeof(ctx->video));
//  ctx->video = *(VideoData *)(device_buf + size);

  CODEC_LOG (DEBUG, "[decode_video] len: %d, have_data: %d\n", len, *got_picture_ptr);

  return len;
}

void
emul_avcodec_decode_audio_to (uint8_t *in_buf,
                              int in_size,
                              uint8_t *device_buf)
{
  int size = 0;

  size = sizeof(size);
  memcpy (device_buf + size, &in_size, sizeof(in_size));
//  *(int *)(device_buf + size) = in_size;
  size += sizeof(in_size);
  if (in_size > 0) {
    memcpy (device_buf + size, in_buf, in_size);
    size += in_size;
  }

  size -= sizeof(size);
  memcpy (device_buf, &size, sizeof(size));
//  *(int *)device_buf = size;

  CODEC_LOG (DEBUG, "[decode_audio] write size: %d, inbuf_size: %d\n", size, in_size);
}

int
emul_avcodec_decode_audio_from (CodecContext *ctx, int *frame_size_ptr,
                                int16_t *samples, uint8_t *device_buf)
{
  int len = 0, size = 0;

  CODEC_LOG (DEBUG, "[decode_audio] read data\n");
  memcpy (&ctx->audio.channel_layout,
    (uint8_t *)device_buf, sizeof(ctx->audio.channel_layout));
//  ctx->audio.channel_layout = *(int64_t *)device_buf;
  size = sizeof(ctx->audio.channel_layout);
  memcpy (&len, (uint8_t *)device_buf + size, sizeof(len));
//  len = *(int *)(device_buf + size);
  size += sizeof(len);
  memcpy (frame_size_ptr,
    (uint8_t *)device_buf + size, sizeof(*frame_size_ptr));
//  frame_size_ptr = *(int *)(device_buf + size);
  size += sizeof(*frame_size_ptr);
  CODEC_LOG (DEBUG, "[decode_audio] len: %d, frame_size: %d\n",
          len, (*frame_size_ptr));
#if 0
  if (len > 0) {
    memcpy (samples,
      (uint8_t *)device_buf + size, FF_MAX_AUDIO_FRAME_SIZE);
  }
#endif

  return len;
}

void
emul_avcodec_encode_video_to (uint8_t *in_buf, int in_size,
                              int64_t in_timestamp, uint8_t *device_buf)
{
  int size = 0;

  size = sizeof(size);

  CODEC_LOG (DEBUG, "[encode_video] write data to qemu\n");
  memcpy ((uint8_t *)device_buf + size, &in_size, sizeof(in_size));
  size += sizeof(in_size);
  memcpy ((uint8_t *)device_buf + size, &in_timestamp, sizeof(in_timestamp));
  size += sizeof(in_timestamp);
  if (in_size > 0) {
    memcpy ((uint8_t *)device_buf + size, in_buf, in_size);
    size += in_size;
  }

  size -= sizeof(size);
  memcpy (device_buf, &size, sizeof(size));

  CODEC_LOG (DEBUG, "[encode_video] write data: %d\n", size);
}

int
emul_avcodec_encode_video_from (uint8_t *out_buf,
                                int out_size,
                                uint8_t *device_buf)
{
  int len, size;

  CODEC_LOG (DEBUG, "[encode_video] read data\n");
  memcpy (&len, (uint8_t *)device_buf, sizeof(len));
  size = sizeof(len);
  memcpy (out_buf, (uint8_t *)device_buf + size, out_size);

  return len;
}

void
emul_avcodec_encode_audio_to (int out_size, int in_size,
                              uint8_t *in_buf, uint8_t *device_buf)
{
  int size = 0;

  size = sizeof(size);
  CODEC_LOG (DEBUG, "[encode_audio] write data to qemu\n");

  memcpy (device_buf + size, &in_size, sizeof(in_size));
  size += sizeof(in_size);
  memcpy (device_buf + size, &out_size, sizeof(out_size));
  size += sizeof(out_size);
  if (in_size > 0) {
    memcpy (device_buf + size, in_buf, in_size);
    size += in_size;
  }
  size -= sizeof(size);
  memcpy (device_buf, &size, sizeof(size));

  CODEC_LOG (DEBUG, "[encode_audio] write data: %d\n", size);
}

int
emul_avcodec_encode_audio_from (uint8_t *out_buf,
                                int out_size,
                                uint8_t *device_buf)
{
  int len, size;

  CODEC_LOG (DEBUG, "[encode_audio] read data\n");
  memcpy (&len, (uint8_t *)device_buf, sizeof(len));
  size = sizeof(len);
  memcpy (out_buf, (uint8_t *)device_buf + size, out_size);

  return len;
}
