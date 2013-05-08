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

#ifndef __GST_EMUL_API_H__
#define __GST_EMUL_API_H__

#include "gstemulcommon.h"

int emul_avcodec_init (CodecContext *ctx, CodecElement *codec,
  CodecDevice *dev);

void emul_avcodec_deinit (CodecContext *ctx, CodecDevice *dev);

int emul_avcodec_decode_video (CodecContext *ctx, uint8_t *in_buf, int in_size,
    gint idx, gint64 in_offset, GstBuffer **out_buf,
    int *got_picture_ptr, CodecDevice *dev);


int emul_avcodec_decode_audio (CodecContext *ctx, int16_t *samples,
  int *frame_size_ptr, uint8_t *in_buf,
  int in_size, CodecDevice *dev);

int emul_avcodec_encode_video (CodecContext *ctx, uint8_t*out_buf, int out_size,
		uint8_t *in_buf, int in_size, int64_t in_timestamp, CodecDevice *dev);

int emul_avcodec_encode_audio (CodecContext *ctx, uint8_t *out_buf,
                          int out_size, uint8_t *in_buf,
                          int in_size, CodecDevice *dev);

void emul_av_picture_copy (CodecContext *ctx, uint8_t *pict, uint32_t pict_size, CodecDevice *dev);

void emul_codec_write_to_qemu (int ctx_index, int api_index, CodecDevice *dev);

#endif /* __GST_EMUL_API_H__ */
