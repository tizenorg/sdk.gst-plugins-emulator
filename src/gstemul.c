/*
 * Emulator
 *
 * Copyright (C) 2011 - 2012 Samsung Electronics Co., Ltd. All rights reserved.
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
 */

/* First, include the header file for the plugin, to bring in the
 * object definition and other useful things.
 */

/*
 *
 * Contributors:
 * - S-Core Co., Ltd
 *
 */

#include <gst/gst.h>

GST_DEBUG_CATEGORY (emul_debug);

#define GST_TYPE_EMULDEC \
  (gst_emul_dec_get_type())
#define GST_EMULDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_EMULDEC,GstEmulDec))
#define GST_EMULDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_EMULDEC,GstEmulDecClass))
#define GST_IS_EMULDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_EMULDEC))
#define GST_IS_EMULDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_EMULDEC))

extern gboolean gst_emuldec_register (GstPlugin *plugin);
extern gboolean gst_emulenc_register (GstPlugin *plugin);

static gboolean
plugin_init (GstPlugin *plugin)
{
	GST_DEBUG_CATEGORY_INIT (emul_debug, "tizen-emul", 0, "Tizen Emulator Codec Elements");
//	gst_emulenc_register (plugin);	
	gst_emuldec_register (plugin);	

	return TRUE;
}

#ifndef PACKAGE
#define PACKAGE "GST-EMUL-CODEC"
#endif

GST_PLUGIN_DEFINE (
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	"tizen-sdk",
	"Codecs for Tizen Emulator",
	plugin_init,
	"0.1.1",
	"LGPL",
	"Gst-Emul-Codec",
	"http://tizen.org"
)
