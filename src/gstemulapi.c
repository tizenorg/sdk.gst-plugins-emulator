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

void emul_codec_write_to_qemu (int ctx_index, int api_index, CodecDevice *dev)
{
  CodecIOParams ioparam;

//  memset(&ioparam, 0, sizeof(ioparam));
  ioparam.api_index = api_index;
  ioparam.ctx_index = ctx_index;
  ioparam.mem_offset = dev->mem_info.offset;
  if (write (dev->fd, &ioparam, 1) < 0) {
    printf ("[%s:%d] failed to copy data.\n", __func__, __LINE__);
  }
  CODEC_LOG (DEBUG, "[%s] mem_offset = 0x%x\n", __func__, ioparam.mem_offset);
}

extern int device_fd;
extern gpointer device_mem;

struct mem_info {
    gpointer start;
    uint32_t offset;
};

static struct mem_info secure_device_mem()
{
  uint32_t mem_offset = 0;
  struct mem_info info;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  ioctl(device_fd, CODEC_CMD_SECURE_MEMORY, &mem_offset);
  info.start = (gpointer)((uint32_t)device_mem + mem_offset);
  info.offset = mem_offset;

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
  CODEC_LOG (DEBUG, "caramis0 = 0x%x\n", mem_offset);

  return info;
}

static void release_device_mem(gpointer start)
{
  uint32_t offset = start - device_mem;
  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  ioctl(device_fd, CODEC_CMD_RELEASE_MEMORY, &offset);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

static void emul_buffer_free(gpointer start)
{
  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  release_device_mem(start);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

GstFlowReturn emul_buffer_alloc(GstPad *pad, guint64 offset, guint size, GstCaps *caps, GstBuffer **buf)
{
  struct mem_info info;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  *buf = gst_buffer_new ();

  info = secure_device_mem();

  CODEC_LOG (DEBUG, "[%s] start: 0x%x, offset 0x%x\n", __func__, info.start, info.offset);

  GST_BUFFER_DATA (*buf) = GST_BUFFER_MALLOCDATA (*buf) = info.start;
  GST_BUFFER_SIZE (*buf) = size;
  GST_BUFFER_FREE_FUNC (*buf) = emul_buffer_free;

  GST_BUFFER_OFFSET (*buf) = offset;

  if (caps) {
    gst_buffer_set_caps (*buf, caps);
  }

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
  return GST_FLOW_OK;
}

int
emul_avcodec_init (CodecContext *ctx, CodecElement *codec, CodecDevice *dev)
{
  int fd;
  uint8_t *mmapbuf;
  int ret = 0;
  uint32_t mem_offset = 0;

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

  ioctl(fd, CODEC_CMD_GET_CONTEXT_INDEX, &ctx->index);
  CODEC_LOG (DEBUG, "recv context index: %d\n", ctx->index);

  ioctl (fd, CODEC_CMD_COPY_TO_DEVICE_MEM, &mem_offset);
  emul_avcodec_init_to (ctx, codec, mmapbuf + mem_offset);
  dev->mem_info.offset = mem_offset;
  emul_codec_write_to_qemu (ctx->index, CODEC_INIT, dev);


  ioctl (fd, CODEC_CMD_COPY_FROM_DEVICE_MEM, &mem_offset);
  ret = emul_avcodec_init_from (ctx, codec, mmapbuf + mem_offset);

  ioctl (fd, CODEC_CMD_REMOVE_TASK_QUEUE, &mem_offset);

  CODEC_LOG (DEBUG, "leave: %s, ret: %d\n", __func__, ret);
  return ret;
}

void
emul_avcodec_deinit (CodecContext *ctx, CodecDevice *dev)
{
  int fd;
  void *mmapbuf = NULL;

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

  emul_codec_write_to_qemu (ctx->index, CODEC_DEINIT, dev);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

int
emul_avcodec_decode_video (CodecContext *ctx, uint8_t *in_buf, int in_size,
                          gint idx, gint64 in_offset, GstBuffer **out_buf,
                          int *got_picture_ptr, CodecDevice *dev)
{
  int fd;
  uint8_t *mmapbuf = NULL;
  int len = 0;
//  int copyback, usable;
  uint32_t mem_offset = 0;

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

  ioctl (fd, CODEC_CMD_COPY_TO_DEVICE_MEM, &mem_offset);
  CODEC_LOG (DEBUG, "[%s] mem_offset = 0x%x\n", __func__, mem_offset);
  emul_avcodec_decode_video_to (in_buf, in_size, idx, in_offset, mmapbuf + mem_offset);

  dev->mem_info.offset = mem_offset;
  emul_codec_write_to_qemu (ctx->index, CODEC_DECODE_VIDEO, dev);

  ioctl (fd, CODEC_CMD_COPY_FROM_DEVICE_MEM, &mem_offset);
  CODEC_LOG (DEBUG, "[%s] mem_offset = 0x%x\n", __func__, mem_offset);
  len = emul_avcodec_decode_video_from (ctx, got_picture_ptr, mmapbuf + mem_offset);

  ioctl (fd, CODEC_CMD_REMOVE_TASK_QUEUE, &mem_offset);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
  return len;
}

void
emul_av_picture_copy (CodecContext *ctx, uint8_t *pict,
                      uint32_t pict_size, CodecDevice *dev)
{
  int fd;
  void *mmapbuf = NULL;
//  int copyback, usable;
  uint32_t mem_offset = 0;

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

#if 0
  ioctl (fd, CODEC_CMD_ADD_TASK_QUEUE, NULL);

  while (1) {
    ioctl (fd, CODEC_CMD_COPY_TO_DEVICE_MEM, &usable);
    if (usable) {
      CODEC_LOG (DEBUG, "[copy_frame] waiting before write.\n");
      usleep (500);
      continue;
    }
    break;
  }

  emul_codec_write_to_qemu (ctx->index, CODEC_PICTURE_COPY, dev);

  while (1) {
    ioctl (fd, CODEC_CMD_COPY_FROM_DEVICE_MEM, &usable);
    if (usable) {
      CODEC_LOG (DEBUG, "[copy_frame] waiting after write.\n");
      usleep (500);
      continue;
    }
    memcpy (pict, mmapbuf, pict_size);
    break;
  }
  ioctl (fd, CODEC_CMD_REMOVE_TASK_QUEUE, &copyback);
#endif

  dev->mem_info.offset = (uint32_t)pict - (uint32_t)device_mem;
  CODEC_LOG (DEBUG, "[%s] mem_offset = 0x%x\n", __func__, dev->mem_info.offset);
  emul_codec_write_to_qemu (ctx->index, CODEC_PICTURE_COPY, dev);
//  ioctl (fd, CODEC_CMD_COPY_FROM_DEVICE_MEM, &mem_offset);
  ioctl (fd, CODEC_CMD_COPY_FROM_DEVICE_MEM2, &(dev->mem_info.offset));

  CODEC_LOG (DEBUG, "caramis1 = 0x%x\n", __func__, mem_offset);
#if 0
  memcpy (pict, mmapbuf, pict_size);

  ioctl (fd, CODEC_CMD_REMOVE_TASK_QUEUE, &mem_offset);
#endif

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
}

int
emul_avcodec_decode_audio (CodecContext *ctx, int16_t *samples,
                          int *frame_size_ptr, uint8_t *in_buf,
                          int in_size, CodecDevice *dev)
{
  int fd;
  uint8_t *mmapbuf = NULL;
  int len;
//  int copyback, usable;
  uint32_t mem_offset = 0;

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

#if 0
  ioctl (fd, CODEC_CMD_ADD_TASK_QUEUE, NULL);

  while (1) {
    ioctl (fd, CODEC_CMD_COPY_TO_DEVICE_MEM, &usable);
    if (usable) {
      CODEC_LOG (DEBUG, "[decode_audio] waiting before write.\n");
      usleep (500);
      continue;
    }

    emul_avcodec_decode_audio_to (in_buf, in_size, mmapbuf);
    break;
  }

  emul_codec_write_to_qemu (ctx->index, CODEC_DECODE_AUDIO, dev);

  while (1) {
    ioctl (fd, CODEC_CMD_COPY_FROM_DEVICE_MEM, &usable);
    if (usable) {
      CODEC_LOG (DEBUG, "[decode_audio] waiting after write.\n");
      usleep (500);
      continue;
    }

    len =
      emul_avcodec_decode_audio_from (ctx, frame_size_ptr, samples, mmapbuf);
    break;
  }
  ioctl (fd, CODEC_CMD_REMOVE_TASK_QUEUE, &copyback);
#endif

  ioctl (fd, CODEC_CMD_COPY_TO_DEVICE_MEM, &mem_offset);
  emul_avcodec_decode_audio_to (in_buf, in_size, mmapbuf + mem_offset);

  dev->mem_info.offset = mem_offset;
  emul_codec_write_to_qemu (ctx->index, CODEC_DECODE_AUDIO, dev);

  ioctl (fd, CODEC_CMD_COPY_FROM_DEVICE_MEM, &mem_offset);
  len =
    emul_avcodec_decode_audio_from (ctx, frame_size_ptr, samples, mmapbuf + mem_offset);

  ioctl (fd, CODEC_CMD_REMOVE_TASK_QUEUE, &mem_offset);

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);

  return len;
}

int
emul_avcodec_encode_video (CodecContext *ctx, uint8_t *out_buf,
                        int out_size, uint8_t *in_buf,
                        int in_size, int64_t in_timestamp, CodecDevice *dev)
{
  int fd;
  void *mmapbuf;
  int len = 0;
  int copyback, usable;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd.\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address.\n");
    return -1;
  }

//  ioctl (fd, CODEC_CMD_ADD_TASK_QUEUE, NULL);
#if 0
  while (1) {
    ioctl (fd, CODEC_CMD_COPY_TO_DEVICE_MEM, &usable);
    if (usable) {
      CODEC_LOG (DEBUG, "[encode_video] waiting before write.\n");
      usleep (500);
      continue;
    }

    emul_avcodec_encode_video_to (in_buf, in_size, in_timestamp, mmapbuf);
    break;
  }

  emul_codec_write_to_qemu (ctx->index, CODEC_ENCODE_VIDEO, dev);

  while (1) {
    ioctl (fd, CODEC_CMD_COPY_FROM_DEVICE_MEM, &usable);
    if (usable) {
      CODEC_LOG (DEBUG, "[encode_video] waiting after write.\n");
      usleep (500);
      continue;
    }

    len = emul_avcodec_encode_video_from (out_buf, out_size, mmapbuf);
    break;
  }
  ioctl (fd, CODEC_CMD_REMOVE_TASK_QUEUE, &copyback);
#endif

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
  return len;
}

int
emul_avcodec_encode_audio (CodecContext *ctx, uint8_t *out_buf,
                          int out_size, uint8_t *in_buf,
                          int in_size, CodecDevice *dev)
{
  int fd;
  void *mmapbuf;
  int len = 0;
  int copyback, usable;

  CODEC_LOG (DEBUG, "enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR ("failed to get %s fd.\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = dev->buf;
  if (!mmapbuf) {
    GST_ERROR ("failed to get mmaped memory address.\n");
    return -1;
  }

//  ioctl (fd, CODEC_CMD_ADD_TASK_QUEUE, NULL);
#if 0
  while (1) {
    ioctl (fd, CODEC_CMD_COPY_TO_DEVICE_MEM, &usable);
    if (usable) {
      CODEC_LOG (DEBUG, "[encode_audio] waiting before write.\n");
      usleep (500);
      continue;
    }

    emul_avcodec_encode_audio_to (out_size, in_size, in_buf, mmapbuf);
    break;
  }

  emul_codec_write_to_qemu (ctx->index, CODEC_ENCODE_AUDIO, dev);

  while (1) {
    ioctl (fd, CODEC_CMD_COPY_FROM_DEVICE_MEM, &usable);
    if (usable) {
      CODEC_LOG (DEBUG, "[encode_audio] waiting after write.\n");
      usleep (500);
      continue;
    }
    len = emul_avcodec_encode_audio_from (out_buf, out_size, mmapbuf);
    break;
  }
  ioctl (fd, CODEC_CMD_REMOVE_TASK_QUEUE, &copyback);
#endif

  CODEC_LOG (DEBUG, "leave: %s\n", __func__);
  return len;
}
