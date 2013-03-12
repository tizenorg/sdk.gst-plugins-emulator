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
#include "gstemulapi2.h"
#include "gstemuldev.h"

enum {
  CODEC_USER_FROM = 0,
  CODEC_USER_TO,
};

int
emul_avcodec_init (CodecContext *ctx, CodecElement *codec, CodecDevice *dev)
{
  int fd;
  uint8_t *mmapbuf;
  int size = 0, ret = 0;
  int usable, copyback;
  CodecIOParams params;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd.\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = (uint8_t *)dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address.\n");
    return -1;
  }

  if (dev->mem_info.type == CODEC_FIXED_DEVICE_MEM) {
    emul_avcodec_init_to (ctx, codec, mmapbuf);    
  }	else {
    copyback = CODEC_USER_FROM;
    ioctl (fd, CODEC_CMD_ADD_TASK_QUEUE, &copyback);

    while (1) {
      ioctl (fd, CODEC_CMD_COPY_TO_DEVICE_MEM, &usable);
      if (usable) {
        CODEC_LOG (DEBUG, "[init][%d] failure.\n", __LINE__);
        continue;
      }

      emul_avcodec_init_to (ctx, codec, mmapbuf);    

#if 0
      CODEC_LOG (DEBUG, "[init] write data to qemu.\n");
      size = sizeof(size);
      memcpy (mmapbuf + size,
          &codec->media_type, sizeof(codec->media_type));
      size += sizeof(codec->media_type);
      memcpy (mmapbuf + size, &codec->codec_type, sizeof(codec->codec_type));
      size += sizeof(codec->codec_type);
      memcpy (mmapbuf + size, codec->name, sizeof(codec->name));
      size += sizeof(codec->name);

      if (codec->media_type == AVMEDIA_TYPE_VIDEO) {
        memcpy (mmapbuf + size, &ctx->video, sizeof(ctx->video));
        size += sizeof(ctx->video);
      } else if (codec->media_type == AVMEDIA_TYPE_AUDIO) {
        memcpy (mmapbuf + size, &ctx->audio, sizeof(ctx->audio));
        size += sizeof(ctx->audio);
      } else {
        GST_ERROR ("media type is unknown.\n");
        ret = -1;
        break;;
      }

      memcpy (mmapbuf + size,
          &ctx->codecdata_size, sizeof(ctx->codecdata_size));
      size += sizeof(ctx->codecdata_size);
      if (ctx->codecdata_size) {
        memcpy (mmapbuf + size, ctx->codecdata, ctx->codecdata_size);
        size += ctx->codecdata_size;
      }
      size -= sizeof(size);
      memcpy (mmapbuf, &size, sizeof(size));

      CODEC_LOG (DEBUG, "[init] write data: %d\n", size);
#endif
      break;
    }

#if 0
    if (ret < 0) {
      return ret;
    }
#endif
  }

  CODEC_PARAM_INIT (params);
  params.api_index = CODEC_INIT;
  params.mem_offset = dev->mem_info.offset;
  CODEC_WRITE_TO_QEMU (fd, &params, 1);

  if (dev->mem_info.type == CODEC_FIXED_DEVICE_MEM) {
    ret = emul_avcodec_init_from (ctx, codec, mmapbuf);
  } else {
    while (1) {
      ioctl (fd, CODEC_CMD_COPY_FROM_DEVICE_MEM, &usable);
      if (usable) {
        CODEC_LOG (DEBUG, "[init][%d] failure.\n", __LINE__);
        continue;
      }

      ret = emul_avcodec_init_from (ctx, codec, mmapbuf);
#if 0
      CODEC_LOG (DEBUG, "[init] read data from qemu.\n");
      if (codec->media_type == AVMEDIA_TYPE_AUDIO) {
        memcpy (&ctx->audio.sample_fmt,
            (uint8_t *)mmapbuf, sizeof(ctx->audio.sample_fmt));
        size += sizeof(ctx->audio.sample_fmt);
        CODEC_LOG (DEBUG, "[init] AUDIO sample_fmt: %d\n", ctx->audio.sample_fmt);
      }
      CODEC_LOG (DEBUG, "[init] %s\n", codec->media_type ? "AUDIO" : "VIDEO");
      memcpy (&ret, (uint8_t *)mmapbuf + size, sizeof(ret));
      size += sizeof(ret);
      memcpy (&ctx->index, (uint8_t *)mmapbuf + size, sizeof(ctx->index));
      ctx->codec = codec;
      CODEC_LOG (DEBUG, "context index: %d\n", ctx->index);
#endif
      break;
    }
    ioctl (fd, CODEC_CMD_REMOVE_TASK_QUEUE, &copyback);
  }

  CODEC_LOG (DEBUG, "leave: %s, ret: %d\n", __func__, ret);
  return ret;
}

void
emul_avcodec_deinit (CodecContext *ctx, CodecDevice *dev)
{
  int fd;
  int copyback, usable;
  void *mmapbuf = NULL;
  CodecIOParams params;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

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

#if 0
  copyback = CODEC_USER_FROM;
  ioctl (fd, CODEC_CMD_ADD_TASK_QUEUE, &copyback);

  while (1) {
    ioctl (fd, CODEC_CMD_COPY_TO_DEVICE_MEM, &usable);
    if (usable) {
      CODEC_LOG (DEBUG, "[deinit][%d] failure.\n", __LINE__);
      continue;
    }
  } 
#endif

  CODEC_PARAM_INIT (params);
  params.api_index = CODEC_DEINIT;
  params.ctx_index = ctx->index;
  params.mem_offset = dev->mem_info.offset;
  CODEC_WRITE_TO_QEMU (fd, &params, 1);

//  ioctl (fd, CODEC_CMD_REMOVE_TASK_QUEUE, &copyback);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

int
emul_avcodec_decode_video (CodecContext *ctx, uint8_t *in_buf, int in_size,
    GstBuffer **out_buf, int *got_picture_ptr, CodecDevice *dev)
{
  int fd;
  uint8_t *mmapbuf = NULL;
  int len = 0, size = 0;
  int copyback, usable;
  CodecIOParams params;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = (uint8_t *)dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address\n");
    return -1;
  }

  if (dev->mem_info.type == CODEC_FIXED_DEVICE_MEM) {
    emul_avcodec_decode_video_to (in_buf, in_size, mmapbuf);  
  } else {
    copyback = CODEC_USER_FROM;
    ioctl (fd, CODEC_CMD_ADD_TASK_QUEUE, &copyback);

    while (1) {
      ioctl (fd, CODEC_CMD_COPY_TO_DEVICE_MEM, &usable);
      if (usable) {
        CODEC_LOG (DEBUG, "[decode_video] wait 1.\n");
        continue;
      }

      emul_avcodec_decode_video_to (in_buf, in_size, mmapbuf);  
#if 0
      CODEC_LOG (DEBUG, "[decode_video] write data to qemu\n");
      size = sizeof(size);
      memcpy (mmapbuf + size, &in_size, sizeof(in_size));
      size += sizeof(in_size);
      if (in_size > 0) {
        memcpy (mmapbuf + size, in_buf, in_size);
        size += in_size;
      }

      size -= sizeof(size);
      CODEC_LOG (DEBUG, "[decode_video] total: %d, inbuf size: %d\n", size, in_size);
      memcpy(mmapbuf, &size, sizeof(size));
#endif
      break;
    }
  }

  /* provide raw image for decoding to qemu */
  CODEC_PARAM_INIT (params);
  params.api_index = CODEC_DECODE_VIDEO;
  params.ctx_index = ctx->index;
  params.mem_offset = dev->mem_info.offset;
  CODEC_WRITE_TO_QEMU (fd, &params, 1);

  if (dev->mem_info.type == CODEC_FIXED_DEVICE_MEM) {
    len = emul_avcodec_decode_video_from (ctx, got_picture_ptr, mmapbuf);
  } else {
    while (1) {
      ioctl (fd, CODEC_CMD_COPY_FROM_DEVICE_MEM, &usable);
      if (usable) {
        CODEC_LOG (DEBUG, "[decode_video] wait 2.\n");
        continue;
      }

      len = emul_avcodec_decode_video_from (ctx, got_picture_ptr, mmapbuf);
#if 0
      CODEC_LOG (DEBUG, "[decode_video] read data from qemu.\n");
      memcpy (&len, (uint8_t *)mmapbuf, sizeof(len));
      size = sizeof(len);
      memcpy (got_picture_ptr,
          (uint8_t *)mmapbuf + size, sizeof(*got_picture_ptr));
      size += sizeof(*got_picture_ptr);
      memcpy (&ctx->video, (uint8_t *)mmapbuf + size, sizeof(ctx->video));

      CODEC_LOG (DEBUG, "[decode_video] len: %d, have_date: %d\n", len, *got_picture_ptr);
#endif
      break;
    }
    ioctl (fd, CODEC_CMD_REMOVE_TASK_QUEUE, &copyback);
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
  return len;
}

void
emul_av_picture_copy (CodecContext *ctx, uint8_t *pict, uint32_t pict_size, CodecDevice *dev)
{
  int fd;
  void *mmapbuf = NULL;
  int copyback, usable;
  CodecIOParams params;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd\n", CODEC_DEV);
    return;
  }

  mmapbuf = dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address\n");
    return;
  }

  if (dev->mem_info.type == CODEC_FIXED_DEVICE_MEM) {
  } else {
#if 1 
    copyback = CODEC_USER_FROM;
    ioctl (fd, CODEC_CMD_ADD_TASK_QUEUE, &copyback);

    while (1) {
      ioctl (fd, CODEC_CMD_COPY_TO_DEVICE_MEM, &usable);
      if (usable) {
        CODEC_LOG (DEBUG, "[decode_video] wait 1.\n");
        continue;
      }
      break;
    }
  }
#endif

//  printf("before av_picture_copy. ctx: %d\n", ctx->index);

  CODEC_PARAM_INIT (params);
  params.api_index = CODEC_PICTURE_COPY;
  params.ctx_index = ctx->index;
  params.mem_offset = dev->mem_info.offset;
  CODEC_WRITE_TO_QEMU (fd, &params, 1);

  CODEC_LOG (DEBUG, "[copy_frame] after write.\n");

  if (dev->mem_info.type == CODEC_FIXED_DEVICE_MEM) {
    CODEC_LOG (DEBUG, "[copy_frame] read data from qemu.\n");
    memcpy (pict, mmapbuf, pict_size);
  } else {
    while (1) {
      ioctl (fd, CODEC_CMD_COPY_FROM_DEVICE_MEM, &usable);
      if (usable) {
        CODEC_LOG (DEBUG, "[copy_frame] wait 2.\n");
        continue;
      }

      CODEC_LOG (DEBUG, "[copy_frame] read data from qemu.\n");
      memcpy (pict, mmapbuf, pict_size);
      break;
    }
    ioctl (fd, CODEC_CMD_REMOVE_TASK_QUEUE, &copyback);
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

int
emul_avcodec_decode_audio (CodecContext *ctx, int16_t *samples,
    int *frame_size_ptr, uint8_t *in_buf, int in_size, CodecDevice *dev)
{
  int fd;
  uint8_t *mmapbuf = NULL;
  int size = 0, len;
  int copyback, usable;
  CodecIOParams params;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR("failed to get %s fd\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = (uint8_t *)dev->buf;
  if (!mmapbuf) {
    GST_ERROR("failed to get mmaped memory address\n");
    return -1;
  }

  if (dev->mem_info.type == CODEC_FIXED_DEVICE_MEM) {
    emul_avcodec_decode_audio_to (in_buf, in_size, mmapbuf);    
  } else {
    copyback = CODEC_USER_FROM;
    ioctl (fd, CODEC_CMD_ADD_TASK_QUEUE, &copyback);

    while (1) {
      ioctl (fd, CODEC_CMD_COPY_TO_DEVICE_MEM, &usable);
      if (usable) {
        CODEC_LOG (DEBUG, "[decode_audio][%d] wait 1.\n", __LINE__);
        continue;
      }

      emul_avcodec_decode_audio_to (in_buf, in_size, mmapbuf);    
#if 0
      size = sizeof(size);
      memcpy (mmapbuf + size, &in_size, sizeof(in_size));
      size += sizeof(in_size);
      if (in_size > 0) {
        memcpy (mmapbuf + size, in_buf, in_size);
        size += in_size;
      }

      size -= sizeof(size);
      memcpy (mmapbuf, &size, sizeof(size));
      CODEC_LOG (DEBUG, "[decode_audio] write size: %d, inbuf_size: %d\n", size, in_size);
#endif
      break;
    }
  }

  CODEC_PARAM_INIT (params);
  params.api_index = CODEC_DECODE_AUDIO;
  params.ctx_index = ctx->index;
  params.mem_offset = dev->mem_info.offset;
  CODEC_WRITE_TO_QEMU (fd, &params, 1);

  if (dev->mem_info.type == CODEC_FIXED_DEVICE_MEM) {
    len = emul_avcodec_decode_audio_from (ctx, frame_size_ptr, samples, mmapbuf);
  } else {
    while (1) {
      ioctl (fd, CODEC_CMD_COPY_FROM_DEVICE_MEM, &usable);
      if (usable) {
        CODEC_LOG (DEBUG, "[decode_audio][%d] wait 2.\n", __LINE__);
        continue;
      }

      len = emul_avcodec_decode_audio_from (ctx, frame_size_ptr, samples, mmapbuf);
#if 0
      CODEC_LOG (DEBUG, "[decode_audio] read data\n");
      memcpy (&ctx->audio.channel_layout,
          (uint8_t *)mmapbuf, sizeof(ctx->audio.channel_layout));
      size = sizeof(ctx->audio.channel_layout);
      memcpy (&len, (uint8_t *)mmapbuf + size, sizeof(len));
      size += sizeof(len);
      memcpy (frame_size_ptr, (uint8_t *)mmapbuf + size, sizeof(*frame_size_ptr));
      size += sizeof(*frame_size_ptr);
      CODEC_LOG (DEBUG, "[decode_audio] len: %d, channel_layout: %lld\n",
          len, ctx->audio.channel_layout);
      if (len > 0) {
        memcpy (samples, (uint8_t *)mmapbuf + size, FF_MAX_AUDIO_FRAME_SIZE);
      }
#endif
      break;
    }
    ioctl (fd, CODEC_CMD_REMOVE_TASK_QUEUE, &copyback);
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
  return len;
}

int
emul_avcodec_encode_video (CodecContext *ctx, uint8_t*out_buf, int out_size,
    uint8_t *in_buf, int in_size, CodecDevice *dev)
{
  int fd;
  void *mmapbuf;
  int len = 0, outbuf_size, size = 0;
  int copyback, usable;
  CodecIOParams params;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

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

  copyback = CODEC_USER_FROM;
  ioctl (fd, CODEC_CMD_ADD_TASK_QUEUE, &copyback);

  while (1) {
    ioctl (fd, CODEC_CMD_COPY_TO_DEVICE_MEM, &usable);
    if (usable) {
      CODEC_LOG (DEBUG, "[init][%d] failure.\n", __LINE__);
//      sleep(1);
      continue;
    }

    CODEC_LOG (DEBUG, "[encode_video] write data to qemu\n");
    memcpy ((uint8_t *)mmapbuf + size, &in_size, sizeof(guint));
    size += sizeof(guint);
    memcpy ((uint8_t *)mmapbuf + size, in_buf, in_size);
    break;
  }

  CODEC_PARAM_INIT (params);
  params.api_index = CODEC_ENCODE_VIDEO;
  params.ctx_index = ctx->index;
  params.mem_offset = dev->mem_info.offset;
  CODEC_WRITE_TO_QEMU (fd, &params, 1);

  size = 0;
  while (1) {
    copyback = CODEC_USER_TO;
    ioctl (fd, CODEC_CMD_COPY_FROM_DEVICE_MEM, &copyback);

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
    break;
  }
  ioctl (fd, CODEC_CMD_REMOVE_TASK_QUEUE, &copyback);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}

int
emul_avcodec_encode_audio (CodecContext *ctx, uint8_t *outbuf, int outbuf_size,
    const short *inbuf, int inbuf_size, CodecDevice *dev)
{
  int fd;
  void *mmapbuf;
  int len = 0, size = 0;
  int copyback, usable;
  CodecIOParams params;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

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

  copyback = CODEC_USER_FROM;
  ioctl (fd, CODEC_CMD_ADD_TASK_QUEUE, &copyback);

  while (1) {
    ioctl (fd, CODEC_CMD_COPY_TO_DEVICE_MEM, &usable);
    if (usable) {
      CODEC_LOG (DEBUG, "[decode_video] wait.\n");
//      sleep(1);
      continue;
    }

    CODEC_LOG (DEBUG, "[encode_audio] write data to qemu\n");
    memcpy ((uint8_t *)mmapbuf + size, &inbuf_size, sizeof(inbuf_size));
    size += sizeof(inbuf_size);
    memcpy ((uint8_t *)mmapbuf + size, inbuf, inbuf_size);
    break;
  }

  CODEC_PARAM_INIT (params);
  params.api_index = CODEC_ENCODE_AUDIO;
  params.ctx_index = ctx->index;
  params.mem_offset = dev->mem_info.offset;
  CODEC_WRITE_TO_QEMU (fd, &params, 1);

  while (1) {
    ioctl (fd, CODEC_CMD_COPY_FROM_DEVICE_MEM, &usable);
    if (usable) {
      CODEC_LOG (DEBUG, "[decode_video] wait. 2\n");
//      sleep(1);
      continue;
    }

    CODEC_LOG (DEBUG, "[encode_audio] read data from qemu\n");
    memcpy (outbuf, (uint8_t *)mmapbuf, outbuf_size);
    break;
  }
  ioctl (fd, CODEC_CMD_REMOVE_TASK_QUEUE, NULL);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}
