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
// #include <semaphore.h>
#include <glib.h>
#include <gst/gst.h>
#include "pixfmt.h"

GST_DEBUG_CATEGORY_EXTERN (emul_debug);
#define GST_CAT_DEFAULT emul_debug

G_BEGIN_DECLS

enum codec_log_level {
  ERR,
  WARN,
  INFO,
  DEBUG,
};

#define CODEC_DEV   "/dev/newcodec"
#define CODEC_VER   1

#define CODEC_LOG(level, fmt, ...) \
  do { \
    if (level <= INFO) \
      printf("[gst-emul][%d] " fmt, __LINE__, ##__VA_ARGS__); \
  } while (0)

#define FF_INPUT_BUFFER_PADDING_SIZE  8
#define FF_MAX_AUDIO_FRAME_SIZE     192000 // 1 second of 48khz 32bit audio
#define FF_MIN_BUFFER_SIZE        16384

#define GEN_MASK(x) ((1<<(x))-1)
#define ROUND_UP_X(v, x) (((v) + GEN_MASK(x)) & ~GEN_MASK(x))
#define ROUND_UP_2(x) ROUND_UP_X(x, 1)
#define ROUND_UP_4(x) ROUND_UP_X(x, 2)
#define ROUND_UP_8(x) ROUND_UP_X(x, 3)
#define DIV_ROUND_UP_X(v, x) (((v) + GEN_MASK(x)) >> (x))

typedef struct _CodecIOParams {
  int32_t   api_index;
  int32_t   ctx_index;
  uint32_t  mem_offset;
  uint32_t  mem_type;
} CodecIOParams;

typedef struct _CodecDeviceMem {
  uint32_t  index;
  uint32_t  offset;
} CodecDeviceMem;

typedef struct _CodecDevice {
  int       fd;
  uint8_t   *buf;
  uint32_t  buf_size;
  CodecDeviceMem mem_info;
} CodecDevice;

typedef struct _CodecElement {
  int32_t codec_type;
  int32_t media_type;
  gchar name[32];
  gchar longname[64];
  union {
    int32_t pix_fmts[4];
    int32_t sample_fmts[4];
  };
} CodecElement;

typedef struct _VideoData {
  int width, height;
  int fps_n, fps_d;
  int par_n, par_d;
  int pix_fmt, bpp;
  int ticks_per_frame;
} VideoData;

typedef struct _AudioData {
  int channels, sample_rate;
  int block_align, depth;
  int sample_fmt, frame_size;
  int bits_per_smp_fmt;
  int64_t channel_layout;
} AudioData;

typedef struct _CodecContext {
  CodecElement *codec;
  int index;

  int bit_rate;
  int codec_tag;	
  int codecdata_size;
  uint8_t *codecdata;

  VideoData video;
  AudioData audio;
} CodecContext;

enum CODEC_FUNC_TYPE {
//  CODEC_ELEMENT_INIT = 1,
  CODEC_INIT = 0,
  CODEC_DECODE_VIDEO,
  CODEC_ENCODE_VIDEO,
  CODEC_DECODE_AUDIO,
  CODEC_ENCODE_AUDIO,
  CODEC_PICTURE_COPY,
  CODEC_DEINIT,
};

enum CODEC_IO_CMD {
//  CODEC_CMD_ACQUIRE_DEVICE_MEM = 0,
  CODEC_CMD_RELEASE_DEVICE_MEM = 5,
  CODEC_CMD_REMOVE_TASK_QUEUE,
  CODEC_CMD_COPY_TO_DEVICE_MEM,
  CODEC_CMD_COPY_FROM_DEVICE_MEM,
  CODEC_CMD_GET_VERSION = 20,
  CODEC_CMD_GET_ELEMENT,
  CODEC_CMD_GET_CONTEXT_INDEX,
  CODEC_CMD_SECURE_MEMORY = 30,
  CODEC_CMD_RELEASE_MEMORY,
  CODEC_CMD_COPY_FROM_DEVICE_MEM2,
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
