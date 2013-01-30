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

#ifndef __GST_EMUL_H__
#define __GST_EMUL_H__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <glib.h>
#include <gst/gst.h>
#include "pixfmt.h"

GST_DEBUG_CATEGORY_EXTERN (emul_debug);
#define GST_CAT_DEFAULT emul_debug

G_BEGIN_DECLS

#define CODEC_DEV   "/dev/newcodec"
#define CODEC_VER   10

#define CODEC_PARAM_INIT(var) \
  memset (&var, 0x00, sizeof(var))

#define CODEC_WRITE_TO_QEMU(fd, var, size) \
  if (write (fd, var, size) < 0) { \
    printf ("[%d] failed to copy data.\n", __LINE__); \
  }

#define FF_INPUT_BUFFER_PADDING_SIZE  8
#define FF_MAX_AUDIO_FRAME_SIZE     192000 // 1 second of 48khz 32bit audio
#define FF_MIN_BUFFER_SIZE        16384

typedef struct _CodecIOParams {
  uint32_t  api_index;
  uint32_t  ctx_index;
  uint32_t  file_index;
  uint32_t  mem_index;
  uint32_t  device_mem_offset;
} CodecIOParams;

typedef struct _CodecDevice {
  int   fd;
  void    *buf;
  uint32_t  buf_size;
} CodecDevice;

typedef struct _CodecElement {
  gchar   name[32];
  gchar   longname[64];
  uint16_t  codec_type;
  uint16_t  media_type;
#if 0
  union {
    struct {
      int8_t pix_fmts[8];
    } video;
    struct {
      int8_t sample_fmts[8];
    } audio;
  } format;
#endif
} CodecElement;

typedef struct _VideoData {
  int width, height;
  int fps_n, fps_d;
  int par_n, par_d;
  int pix_fmt, bpp;
} VideoData;

typedef struct _AudioData {
  int channels, sample_rate;
  int bit_rate, block_align;
  int depth, sample_fmt;
  int64_t channel_layout;
} AudioData;

typedef struct _CodecContext {
  uint8_t *codecdata;
  int codecdata_size;

  VideoData video;
  AudioData audio;

  CodecElement *codec;
} CodecContext;

enum CODEC_FUNC_TYPE {
  CODEC_ELEMENT_INIT = 1,
  CODEC_INIT,
  CODEC_DEINIT,
  CODEC_DECODE_VIDEO,
  CODEC_ENCODE_VIDEO,
  CODEC_DECODE_AUDIO,
  CODEC_ENCODE_AUDIO,
};

enum CODEC_IO_CMD {
  CODEC_CMD_GET_VERSION = 5,
  CODEC_CMD_GET_DEVICE_MEM,
  CODEC_CMD_SET_DEVICE_MEM,
  CODEC_CMD_GET_MMAP_OFFSET,
  CODEC_CMD_SET_MMAP_OFFSET,
};

enum CODEC_MEDIA_TYPE {
  AVMEDIA_TYPE_UNKNOWN = -1,
  AVMEDIA_TYPE_VIDEO,
  AVMEDIA_TYPE_AUDIO,
};

enum CODEC_TYPE {
  CODEC_TYPE_UNKNOWN = -1,
  CODEC_TYPE_DECODE,
  CODEC_TYPE_ENCODE,
};

enum SAMPLT_FORMAT {
  SAMPLE_FMT_NONE = -1,
  SAMPLE_FMT_U8,
  SAMPLE_FMT_S16,
  SAMPLE_FMT_S32,
  SAMPLE_FMT_FLT,
  SAMPLE_FMT_DBL,
  SAMPLE_FMT_NB
};

/* Define codec types.
 * e.g. FFmpeg, x264, libvpx and etc.
 */
enum {
  FFMPEG_TYPE = 1,
};

G_END_DECLS

#endif
