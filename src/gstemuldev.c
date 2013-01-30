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

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "gstemulcommon.h"
#include "gstemuldev.h"


int
gst_emul_codec_device_open (CodecDevice *dev)
{
  int fd;
  void *mmapbuf;

  printf("enter: %s\n", __func__);

  if ((fd = open(CODEC_DEV, O_RDWR)) < 0) {
    perror("Failed to open codec device.");
    return -1;
  }
  GST_DEBUG("succeeded to open %s.\n", CODEC_DEV);

  mmapbuf = mmap (NULL, dev->buf_size, PROT_READ | PROT_WRITE,
                  MAP_SHARED, fd, 0);
  if (!mmapbuf) {
    perror("Failed to map device memory of codec.");
    close(fd);
    return -1;
  }
  GST_DEBUG("succeeded to map device memory.\n");

  dev->fd = fd;
  dev->buf = mmapbuf;

  return 0;
}

int
gst_emul_codec_device_close (CodecDevice *dev)
{
  int fd = 0;
  void *mmapbuf = NULL;

  printf("enter: %s\n", __func__);

  fd = dev->fd;
  if (fd < 0) {
    GST_ERROR("Failed to get %s fd.\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = dev->buf;
  if (!mmapbuf) {
    GST_ERROR("Failed to get mmaped memory address.\n");
    return -1;
  }

  GST_DEBUG("Release memory region of %s.\n", CODEC_DEV);
  if (munmap(mmapbuf, dev->buf_size) != 0) {
    GST_ERROR("Failed to release memory region of %s.\n", CODEC_DEV);
  }

  GST_DEBUG("close %s.\n", CODEC_DEV);
  if (close(fd) != 0) {
    GST_ERROR("Failed to close %s. fd: %d\n", CODEC_DEV, fd);
  }

  printf("leave: %s\n", __func__);

  return 0;
}
