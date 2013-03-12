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

void emul_avcodec_init_to (CodecContext *ctx, CodecElement *codec, uint8_t *device_buf);

int emul_avcodec_init_from (CodecContext *ctx, CodecElement *codec, uint8_t *device_buf);

void emul_avcodec_decode_video_to (uint8_t *in_buf, int in_size, uint8_t *device_buf);

int emul_avcodec_decode_video_from (CodecContext *ctx, int *got_picture_ptr, uint8_t *device_buf);

void emul_avcodec_decode_audio_to (uint8_t *in_buf, int in_size, uint8_t *device_buf);

int emul_avcodec_decode_audio_from (CodecContext *ctx, int *frame_size_ptr, int16_t *samples, uint8_t *device_buf);
