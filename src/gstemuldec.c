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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <gst/gst.h>
//#include <glib.h>
//#include <glib/gprintf.h>
#include "gstemulcommon.h"

#define GST_EMULDEC_PARAMS_QDATA g_quark_from_static_string("emuldec-params")

typedef struct
{
  gint idx;
  GstClockTime timestamp;
  GstClockTime duration;
  gint64 offset;
} GstTSInfo;

#define GST_TS_INFO_NONE &ts_info_none
static const GstTSInfo ts_info_none = { -1, -1, -1, -1 };

#define MAX_TS_MASK 0xff

typedef struct _GstEmulDec
{
  GstElement element;

  GstPad *srcpad;
  GstPad *sinkpad;

  union {
    struct {
      gint width;
      gint height;
      gint framerate_num;
      gint framerate_den;
      gint pix_fmt;
    } video;
    struct {
      gint channels;
      gint samplerate;
      gint depth;
    } audio;
  } format;

  guint extradata_size;
  guint8 *extradata;

  GstTSInfo ts_info[MAX_TS_MASK + 1];
  gint ts_idx;

  CodecDev codecbuf;
  CodecInfo *codecinfo;
} GstEmulDec;

typedef struct _GstEmulDecClass
{
  GstElementClass parent_class;

  CodecInfo *codecinfo;
  GstPadTemplate *sinktempl;
  GstPadTemplate *srctempl;
} GstEmulDecClass;


static GstElementClass *parent_class = NULL;

static void gst_emuldec_base_init (GstEmulDecClass *klass);
static void gst_emuldec_class_init (GstEmulDecClass *klass);
static void gst_emuldec_init (GstEmulDec *emuldec);
static void gst_emuldec_finalize (GObject *object);

static gboolean gst_emuldec_setcaps (GstPad *pad, GstCaps *caps);

// sinkpad
static gboolean gst_emuldec_sink_event (GstPad *pad, GstEvent *event);
static GstFlowReturn gst_emuldec_chain (GstPad *pad, GstBuffer *buffer);

// srcpad
static gboolean gst_emuldec_src_event (GstPad *pad, GstEvent *event);
static GstStateChangeReturn gst_emuldec_change_state (GstElement *element,
                                                GstStateChange transition);

int gst_emul_codec_init (GstEmulDec *emuldec, CodecInfo *codec_info);
void gst_emul_codec_deinit (GstEmulDec *emuldec);
int gst_emul_decode_video (GstEmulDec *emuldec, guint8 *in_buf, guint in_size,
                            GstBuffer **out_buf, GstTSInfo *dec_info);
int gst_emul_decode_audio (GstEmulDec *emuldec, guint8 *in_buf, guint in_size,
                            GstBuffer **outb_buf, GstTSInfo *dec_info);
int gst_emul_codec_device_open (GstEmulDec *emuldec);
int gst_emul_codec_device_close (GstEmulDec *emuldec);

GstCaps *gst_emul_codecname_to_caps (CodecInfo *info);
GstCaps *gst_emul_codectype_to_video_caps (CodecInfo *info);
GstCaps *gst_emul_codectype_to_audio_caps (CodecInfo *info);
static GstCaps * gst_emul_smpfmt_to_caps (int8_t sample_fmt, CodecInfo *info);

static gboolean gst_emuldec_negotiate (GstEmulDec *dec, gboolean force);


static CodecInfo *codec;

static const GstTSInfo *
gst_ts_info_store (GstEmulDec *dec, GstClockTime timestamp,
    GstClockTime duration, gint64 offset)
{
  gint idx = dec->ts_idx;
  dec->ts_info[idx].idx = idx;
  dec->ts_info[idx].timestamp = timestamp;
  dec->ts_info[idx].duration = duration;
  dec->ts_info[idx].offset = offset;
  dec->ts_idx = (idx + 1) & MAX_TS_MASK;

  return &dec->ts_info[idx];
}

static const GstTSInfo *
gst_ts_info_get (GstEmulDec *dec, gint idx)
{
  if (G_UNLIKELY (idx < 0 || idx > MAX_TS_MASK))
    return GST_TS_INFO_NONE;

  return &dec->ts_info[idx];
}

/*
 * Implementation
 */
static void
gst_emuldec_base_init (GstEmulDecClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstCaps *sinkcaps, *srccaps;
  GstPadTemplate *sinktempl, *srctempl;
  CodecInfo *info;
  gchar *longname, *classification;

  info =
      (CodecInfo *)g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
                                      GST_EMULDEC_PARAMS_QDATA);

  longname = g_strdup_printf ("%s Decoder", info->codec_longname);

  classification = g_strdup_printf ("Codec/Decoder/%s",
                    (info->media_type == AVMEDIA_TYPE_VIDEO) ?
                    "Video" : "Audio");

  gst_element_class_set_details_simple (element_class,
            longname,                        // longname
            classification,                  // classification
            "accelerated codec for Tizen Emulator",       // description
            "Kitae Kim <kt920.kim@samsung.com>");   // author

  g_free (longname);
  g_free (classification);

#if 0
  sinkcaps = gst_caps_new_simple (mimetype,
          "width", GST_TYPE_INT_RANGE, 16, 4096,
          "height", GST_TYPE_INT_RANGE, 16, 4096,
          "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
#endif
  sinkcaps = gst_emul_codecname_to_caps (info);
  if (!sinkcaps) {
    sinkcaps = gst_caps_from_string ("unknown/unknown");
  }

  // if type is video
  switch (info->media_type) {
	case AVMEDIA_TYPE_VIDEO:
    srccaps = gst_caps_from_string ("video/x-raw-rgb; video/x-raw-yuv");
		break;
  case AVMEDIA_TYPE_AUDIO:
    srccaps = gst_emul_codectype_to_audio_caps (info);
		break;
	default:
		GST_LOG("unknown media type.\n");
		break;
  }

  if (!srccaps) {
    srccaps = gst_caps_from_string ("unknown/unknown");
  }

  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
                GST_PAD_ALWAYS, sinkcaps);
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC,
                GST_PAD_ALWAYS, srccaps);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  klass->codecinfo = info;

  klass->sinktempl = sinktempl;
  klass->srctempl = srctempl;
}

static void
gst_emuldec_class_init (GstEmulDecClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

#if 0
  gobject_class->set_property = gst_emuldec_set_property
  gobject_class->get_property = gst_emuldec_get_property
#endif

  gobject_class->finalize = gst_emuldec_finalize;
  gstelement_class->change_state = gst_emuldec_change_state;
}

static void
gst_emuldec_init (GstEmulDec *emuldec)
{
  GstEmulDecClass *oclass;

  oclass = (GstEmulDecClass*) (G_OBJECT_GET_CLASS(emuldec));

  emuldec->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_pad_set_setcaps_function (emuldec->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_emuldec_setcaps));
  gst_pad_set_event_function (emuldec->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_emuldec_sink_event));
  gst_pad_set_chain_function (emuldec->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_emuldec_chain));
  gst_element_add_pad (GST_ELEMENT(emuldec), emuldec->sinkpad);

  emuldec->srcpad = gst_pad_new_from_template (oclass->srctempl, "src") ;
  gst_pad_use_fixed_caps (emuldec->srcpad);
  gst_pad_set_event_function (emuldec->srcpad,
                              GST_DEBUG_FUNCPTR(gst_emuldec_src_event));
  gst_element_add_pad (GST_ELEMENT(emuldec), emuldec->srcpad);
}

static void
gst_emuldec_finalize (GObject *object)
{
  // Deinit Decoder
  GstEmulDec *emuldec = (GstEmulDec *) object;

  if (emuldec->extradata) {
    printf("free extradata.\n");
    g_free(emuldec->extradata);
    emuldec->extradata = NULL;
  }

  if (codec) {
    printf("free CodecInfo.\n");
    g_free(codec);
    codec = NULL;
  }
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_emuldec_src_event (GstPad *pad, GstEvent *event)
{
  GstEmulDec *emuldec;
  gboolean res; 

  emuldec = (GstEmulDec *) gst_pad_get_parent (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
    {    
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);
      GST_LOG_OBJECT (emuldec, "update QOS: %f, %" GST_TIME_FORMAT,
                      proportion, GST_TIME_ARGS (timestamp));
      /* update our QoS values */
//      gst_ffmpegdec_update_qos (ffmpegdec, proportion, timestamp + diff);

      /* forward upstream */
      res = gst_pad_push_event (emuldec->sinkpad, event);
      break;
    }    
    default:
      /* forward upstream */
      res = gst_pad_push_event (emuldec->sinkpad, event);
      break;
  }

  gst_object_unref (emuldec);

  return 0;
}

#if 0
GstCaps*
gst_emul_codectype_to_video_caps (CodecInfo *info)
{
	GstCaps *caps;

	return caps;
}
#endif

static GstCaps*
gst_emul_codectype_to_caps (GstEmulDec *emuldec, CodecInfo *codec_info)
{
  GstCaps *caps;
  guint32 fmt = 0;

 	switch (codec_info->media_type) {	
	case AVMEDIA_TYPE_VIDEO:
	{
	  const char *mimetype = "video/x-raw-yuv";
	  gint num, denom;


		// YUV420P or YUVJ420P
		fmt = GST_MAKE_FOURCC ('I', '4', '2', '0');

		caps = gst_caps_new_simple (mimetype,
						"width", G_TYPE_INT, emuldec->format.video.width,
						"height", G_TYPE_INT, emuldec->format.video.height, NULL);

		num = emuldec->format.video.framerate_den;
		denom = emuldec->format.video.framerate_num;

		if (!denom) {
			GST_LOG ("invalid framerate: %d/0, -> %d/1", num, num);
			denom = 1;
		}

		if (gst_util_fraction_compare (num, denom, 1000, 1) > 0) {
			GST_LOG ("excessive framerate: %d/%d, -> 0/1", num, denom);
			num = 0;
			denom = 1;
		}

    GST_LOG ("setting framerate: %d/%d", num, denom);
		gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, num, denom, NULL);

    // default unfixed setting
		if (!caps) {
			caps = gst_caps_new_simple (mimetype,
							"width", GST_TYPE_INT_RANGE, 16, 4096,
							"height", GST_TYPE_INT_RANGE, 16, 4096,
							"framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
		}

		gst_caps_set_simple (caps, "format", GST_TYPE_FOURCC, fmt, NULL);
	}
		break;
 	case AVMEDIA_TYPE_AUDIO:
  {
    const char *mimetype="audio/x-raw-int";

    caps = gst_caps_new_simple (mimetype,
            "rate", G_TYPE_INT, emuldec->format.audio.samplerate,
            "channels", G_TYPE_INT, emuldec->format.audio.channels,
            "signed", G_TYPE_BOOLEAN, TRUE,
            "endianness", G_TYPE_INT, G_BYTE_ORDER,
            "width", G_TYPE_INT, 16,
            "depth", G_TYPE_INT, 16, NULL);
  }
		break;
	default:
		break;
	}

  return caps;
}

static void
gst_emul_caps_with_codecid (GstEmulDec *emuldec,
														CodecInfo *codec_info,
														GstCaps *caps)
{
  GstStructure *structure;

  int width, height, bits_per_coded_sample;
  const GValue *fps;
  //    const GValue *par;
  //    guint extradata_size;
  //    guint8 *extradata;

  /* FFmpeg Specific Values */
  const GstBuffer *buf;
  const GValue *value;

  structure = gst_caps_get_structure (caps, 0);

  value = gst_structure_get_value (structure, "codec_data");
  if (value) {
    buf = GST_BUFFER_CAST (gst_value_get_mini_object (value));
    emuldec->extradata_size = GST_BUFFER_SIZE (buf);
    emuldec->extradata = GST_BUFFER_DATA (buf);
		printf("extradata: %p, size: %d\n", emuldec->extradata, emuldec->extradata_size);
  } else {
//    CODEC_LOG(2, "no codec data\n");
    GST_DEBUG_OBJECT (emuldec, "no extra data.\n");
    emuldec->extradata_size = 0;
    emuldec->extradata = g_malloc0 (GST_ROUND_UP_16(8));
  }

	printf("caps_with_codecid, media_type: %d, codec_name: %s\n", codec_info->media_type, codec_info->codec_name);

  if (!gst_caps_is_fixed (caps)) {
    return;
  }

	switch (codec_info->media_type) {
	case AVMEDIA_TYPE_VIDEO:
  /* Common Properites, width, height and etc. */
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  gst_structure_get_int (structure, "bpp", &bits_per_coded_sample);

  emuldec->format.video.width = width;
  emuldec->format.video.height = height;

  fps = gst_structure_get_value (structure, "framerate");
  if (fps) {
    emuldec->format.video.framerate_den = gst_value_get_fraction_numerator (fps);
    emuldec->format.video.framerate_num = gst_value_get_fraction_denominator (fps);
    printf ("framerate: %d/%d.\n", emuldec->format.video.framerate_num, emuldec->format.video.framerate_den);

  }

#if 0
  par = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if (par) {
    sample_aspect_ratio.num = gst_structure_get_fraction_numerator (par);
    sample_aspect_ratio.den = gst_structure_get_fraction_denominator (par);
  }
#endif
		break;
  /* audio type */
	case AVMEDIA_TYPE_AUDIO:
	{
		GstStructure *structure;
		int channels = 0;
		int sample_rate = 0;
		int block_align = 0;
		int bit_rate = 0;
		
		g_return_if_fail (gst_caps_get_size (caps) == 1);
		structure = gst_caps_get_structure (caps, 0);

		gst_structure_get_int (structure, "channels", &channels);
		gst_structure_get_int (structure, "rate", &sample_rate);
		gst_structure_get_int (structure, "block_align", &block_align);
		gst_structure_get_int (structure, "bitrate", &bit_rate);

		printf("channels: %d, sample_rate: %d, block_align: %d, bit_rate: %d\n",
						channels, sample_rate, block_align, bit_rate);

		emuldec->format.audio.channels = channels;
		emuldec->format.audio.samplerate = sample_rate;
	}
		break;
	default:
		break;
	}

}

static gboolean
gst_emuldec_setcaps (GstPad *pad, GstCaps *caps)
{
  GstEmulDec *emuldec;
  GstEmulDecClass *oclass;
  gboolean ret = TRUE;

  emuldec = (GstEmulDec *) (gst_pad_get_parent (pad));
  oclass = (GstEmulDecClass *) (G_OBJECT_GET_CLASS (emuldec));

  GST_OBJECT_LOCK (emuldec);

	emuldec->codecinfo = oclass->codecinfo;
	printf("setcaps, codec_type: %d, codec_name: %s\n", emuldec->codecinfo->codec_type, emuldec->codecinfo->codec_name);

  gst_emul_caps_with_codecid (emuldec, oclass->codecinfo, caps);

#if 0
  if (!emuldec->format.video.framerate_den ||
      !emuldec->format.video.framerate_num) {
    emuldec->format.video.framerate_num = 1;
    emuldec->format.video.framerate_den = 25;
  }
#endif

  if (gst_emul_codec_device_open (emuldec) < 0) {
//    CODEC_LOG(1, "failed to access %s or mmap operation\n", CODEC_DEV);
    GST_LOG_OBJECT (emuldec, "failed to access %s or mmap operation\n", CODEC_DEV);
    GST_OBJECT_UNLOCK (emuldec);
    gst_object_unref (emuldec);
    return FALSE;
  }

  if (gst_emul_codec_init (emuldec, oclass->codecinfo) < 0) {
//    CODEC_LOG(1, "cannot initialize codec\n");
    GST_LOG_OBJECT (emuldec, "cannot initialize codec.\n");
    GST_OBJECT_UNLOCK (emuldec);
    gst_object_unref (emuldec);
    return FALSE;
  }

#if 0 /* open a parser */
  gst_emul_codec_parser (emuldec);
#endif

  GST_OBJECT_UNLOCK (emuldec);

  gst_object_unref (emuldec);

  return ret;
}

static gboolean
gst_emuldec_sink_event (GstPad *pad, GstEvent *event)
{
  GstEmulDec *emuldec;
  gboolean ret = FALSE;

  emuldec = (GstEmulDec *) gst_pad_get_parent (pad);
#if 1 
    switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_EOS:
//            CODEC_LOG(1, "received GST_EVENT_EOS\n");
            GST_LOG_OBJECT (emuldec, "received GST_EVENT_EOS\n");
            break;
        case GST_EVENT_NEWSEGMENT:
//            CODEC_LOG(1, "received GST_EVENT_NEWSEGMENT\n");
            GST_LOG_OBJECT (emuldec, "received GST_EVENT_NEWSEGMENT\n");
            break;
    }
    ret = gst_pad_push_event (emuldec->srcpad, event);
#endif

  gst_object_unref (emuldec);

  return ret;
}

static GstFlowReturn
gst_emuldec_chain (GstPad *pad, GstBuffer *buffer)
{
  GstEmulDec *emuldec;
	CodecInfo *codec_info;
  guint8 *in_buf, *aud_buf;
  GstBuffer *out_buf = NULL;
  gint in_size = 0, aud_size = 0, len = 0;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime in_timestamp;
  GstClockTime in_duration;
  gint64 in_offset;
  gboolean discont;
  const GstTSInfo *in_info;
  GstTSInfo *dec_info;

  emuldec = (GstEmulDec *) (GST_PAD_PARENT (pad));
	if (!emuldec) {
		GST_ERROR ("failed to get GstEmulDec.\n");
		return GST_FLOW_ERROR;
	}

  discont = GST_BUFFER_IS_DISCONT (buffer);
  if (G_UNLIKELY (discont)) {
    printf("received DISCONT.\n");
    // TODO
    // drain
    // flush pcache
    // flush buffers
    // reset timestamp
  }

	codec_info = emuldec->codecinfo;

  aud_size = in_size = GST_BUFFER_SIZE (buffer);
  aud_buf = in_buf = GST_BUFFER_DATA (buffer);

  in_timestamp = GST_BUFFER_TIMESTAMP (buffer);
  in_duration = GST_BUFFER_DURATION (buffer);
  in_offset = GST_BUFFER_OFFSET (buffer);

  GST_LOG_OBJECT(emuldec, "offset %" G_GUINT64_FORMAT ", ts: %." GST_TIME_FORMAT "\n",
                in_offset, GST_TIME_ARGS(in_timestamp));
  in_info = gst_ts_info_store (emuldec, in_timestamp, in_duration, in_offset);

  // TODO
  dec_info = in_info;

  do { 
	printf("media_type: %d, name: %s\n", codec_info->media_type, codec_info->codec_name);
  switch (codec_info->media_type) {
	case AVMEDIA_TYPE_VIDEO:
#if 1
    {
        /* Caps negotiation */
        GstCaps *caps;

        caps = gst_emul_codectype_to_caps (emuldec, codec_info);

        if (!gst_pad_set_caps (emuldec->srcpad, caps)) {
            GST_ERROR_OBJECT(emuldec, "failed to set caps for srcpad.\n");
            gst_buffer_unref (buffer);
            return GST_FLOW_NOT_NEGOTIATED;
        }
    }
#endif
      printf("before decode\n");
      len = gst_emul_decode_video (emuldec, in_buf, in_size, &out_buf, dec_info);
      printf("after decode\n");
      break;
	case AVMEDIA_TYPE_AUDIO:
      len = gst_emul_decode_audio (emuldec, in_buf, in_size, &out_buf, dec_info);
      aud_size -= len;
      aud_buf += len; 
			break;
	default:
			break;
	}
  } while (aud_size > 0);

  GST_DEBUG_OBJECT(emuldec, "out_buf:%p, len:%d\n", out_buf, len);

#if 0
  idx = picture->reordered_opaque;
  out_info = gst_ts_info_get(emuldec, idx);
  out_pts = out_info->timestamp;
  out_duration = out_info->duration;
  out_offset = out_info->offset;
  GST_LOG_OBJECT (emuldec, "offset %" G_GUINT64_FORMAT ", ts: %." G_GINT64_FORMAT "\n",
                  out_offset, out_duration);
#endif
  ret = gst_pad_push (emuldec->srcpad, out_buf);

//  g_free (out_buf);
  gst_buffer_unref (buffer);

  return ret;
}

static GstStateChangeReturn
gst_emuldec_change_state (GstElement *element, GstStateChange transition)
{
  GstEmulDec *emuldec = (GstEmulDec*)element;
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_emul_codec_deinit (emuldec);
      default:
        break;
  }

  return ret;
}

gboolean
gst_emuldec_register (GstPlugin *plugin)
{
  GTypeInfo typeinfo = {
    sizeof (GstEmulDecClass),
    (GBaseInitFunc) gst_emuldec_base_init,
    NULL,
    (GClassInitFunc) gst_emuldec_class_init,
    NULL,
    NULL,
    sizeof (GstEmulDec),
    0,
    (GInstanceInitFunc) gst_emuldec_init,
  };

  GType type;
  gchar *type_name;
  gint rank = GST_RANK_PRIMARY;

  /* register element */
  {
    int codec_fd, codec_cnt = 0;
    int func_type = CODEC_QUERY;
    int index = 0, size = 0;
    uint16_t codec_name_len, codec_longname_len;
    void *buf;
    CodecInfo *codec_info;

    codec_fd = open(CODEC_DEV, O_RDWR);
    if (codec_fd < 0) {
      perror("[gst-emul-codec] failed to open codec device");
      return FALSE;
    }

    buf = mmap (NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, codec_fd, 0);
    if (!buf) {
      perror("[gst-emul-codec] failed to mmap");
    }

    write (codec_fd, &func_type, sizeof(int));
    memcpy (&codec_cnt, buf, sizeof(int));
    size = sizeof(uint32_t);

    codec_info = g_malloc0 (codec_cnt * sizeof(CodecInfo));
    codec = codec_info;
    for (; index < codec_cnt; index++) {
      memcpy(&codec_info[index].media_type, (uint8_t *)buf + size, sizeof(uint16_t));
      size += sizeof(uint16_t);
      memcpy(&codec_info[index].codec_type, (uint8_t *)buf + size, sizeof(uint16_t));
      size += sizeof(uint16_t);
      memcpy(&codec_name_len, (uint8_t *)buf + size, sizeof(uint16_t));
      size += sizeof(uint16_t);
      memcpy(&codec_longname_len, (uint8_t *)buf + size, sizeof(uint16_t));
      size += sizeof(uint16_t);
      memcpy(&codec_info[index].codec_name, (uint8_t *)buf + size, codec_name_len);
      size += 32;
      memcpy(&codec_info[index].codec_longname, (uint8_t *)buf + size, codec_longname_len);
      size += 64;
      memcpy (&codec_info[index].sample_fmts, (uint8_t *)buf + size, 8);
      size += 8;
    }

    for (index = 0; index < codec_cnt; index++) {
      if (!codec_info[index].codec_type) {
        continue;
      }

      type_name = g_strdup_printf ("tzdec_%s", codec_info[index].codec_name);
      type = g_type_from_name (type_name);
      if (!type) {
        type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
        g_type_set_qdata (type, GST_EMULDEC_PARAMS_QDATA, (gpointer) &codec_info[index]);
      }

      if (!gst_element_register (plugin, type_name, rank, type)) {
        g_free (type_name);
        return FALSE;
      }
      g_free (type_name);
    }

    munmap (buf, 4096);
    close (codec_fd);
  }

  return TRUE;
}

#if 0
void *gst_emul_codec_query (GstEmulDec *emuldec)
{
  int fd, codec_cnt = 0;
  int size = 0, i;
  void *mmapbuf;
  int func_type = CODEC_QUERY;
  CodecInfo *codec_info;

  CODEC_LOG(2, "enter: %s\n", __func__);

  fd = emuldec->codecbuf.fd;
  mmapbuf = emuldec->codecbuf.mmapbuf;
  if (fd < 0) {
//    CODEC_LOG(1, "failed to get %s fd\n", CODEC_DEV);
    GST_ERROR_OBJECT(emuldec, "failed to get %s fd.\n", CODEC_DEV);
    return NULL;
  }

  if (!mmapbuf) {
//    CODEC_LOG(1, "failed to get mmaped memory address\n");
    GST_ERROR_OBJECT(emuldec, "failed to get mmaped memory address.\n");
    return NULL;
  }

  write (fd, &func_type, sizeof(func_type));

  memcpy (&codec_cnt, mmapbuf, sizeof(uint32_t));
  size += sizeof(uint32_t);
  codec_info = g_malloc0 (codec_cnt * sizeof(CodecInfo));

  for (i = 0; i < codec_cnt; i++) {
    uint16_t codec_name_len, codec_longname_len;

    memcpy (&codec_info[i].media_type, (uint8_t *)mmapbuf + size, sizeof(uint16_t));
    size += sizeof(uint16_t);
    memcpy (&codec_info[i].codec_type, (uint8_t *)mmapbuf + size, sizeof(uint16_t));
    size += sizeof(uint16_t);
    memcpy (&codec_name_len, (uint8_t *)mmapbuf + size, sizeof(uint16_t));
    size += sizeof(uint16_t);
    memcpy (&codec_longname_len, (uint8_t *)mmapbuf + size, sizeof(uint16_t));
    size += sizeof(uint16_t);
    memcpy (codec_info[i].codec_name, (uint8_t *)mmapbuf + size, codec_name_len);
    size += 32;
    memcpy (codec_info[i].codec_longname, (uint8_t *)mmapbuf + size, codec_longname_len);
    size += 64;
    memcpy (&codec_info[i].sample_fmts, (uint8_t *)mmapbuf + size, 8);
    size += 8;
  }

  CODEC_LOG(2, "leave: %s\n", __func__);

  return codec_info;
}
#endif

int gst_emul_codec_device_open (GstEmulDec *emuldec)
{
  int fd;
  int width, height;
  void *mmapbuf;
  uint32_t bufsize;

  CODEC_LOG(2, "enter: %s\n", __func__);

  width = emuldec->format.video.width;
  height = emuldec->format.video.height;
  bufsize = ((width * height * 3) / 2) + 32;

  if ((fd = open(CODEC_DEV, O_RDWR)) < 0) {
    perror("[gst-emul-codec] failed to open codec device");
//    CODEC_LOG(1, "failed to open %s, fd:%d, err:%d\n", CODEC_DEV, fd, errno);
    GST_ERROR("failed to open %s, fd:%d, err:%d\n", CODEC_DEV, fd, errno);

    return -1;
  }
//  CODEC_LOG(2, "succeeded to open %s, ret:%d\n", CODEC_DEV, fd);
  GST_DEBUG("succeeded to open %s.\n", CODEC_DEV);

  mmapbuf = mmap (NULL, bufsize, PROT_READ | PROT_WRITE,
                  MAP_SHARED, fd, 0);
  if (!mmapbuf) {
//    CODEC_LOG(1, "failure mmap() func\n");
    perror("[gst-emul-codec] failed to map memory with codec device.");
    GST_ERROR("failure mmap() func.\n");
    return -1;
  }

  emuldec->codecbuf.fd = fd;
  emuldec->codecbuf.buffer = mmapbuf;

  return 0;
}

int gst_emul_codec_init (GstEmulDec *emuldec, CodecInfo *codecinfo)
{
  int fd;
  int size = 0, ret;
  guint extradata_size = 0;
  guint8 *extradata = NULL;
  void *mmapbuf;
  int func_type = CODEC_INIT;

  CODEC_LOG(2, "enter: %s\n", __func__);

  fd = emuldec->codecbuf.fd;
  if (fd < 0) {
    GST_ERROR_OBJECT(emuldec, "failed to get %s fd.\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = emuldec->codecbuf.buffer;
  if (!mmapbuf) {
    GST_ERROR_OBJECT(emuldec, "failed to get mmaped memory address.\n");
    return -1;
  }

  extradata_size = emuldec->extradata_size;
  extradata = emuldec->extradata;

  GST_DEBUG_OBJECT(emuldec, "extradata: %p, extradata_size: %d\n", extradata, extradata_size);

  /* copy basic info to initialize codec on the host side.
   * e.g. width, height, FPS ant etc. */
  memcpy ((uint8_t *)mmapbuf, &codecinfo->media_type, sizeof(codecinfo->media_type));
  size = sizeof(codecinfo->media_type);
  memcpy ((uint8_t *)mmapbuf + size, &codecinfo->codec_type, sizeof(codecinfo->codec_type));
  size += sizeof(codecinfo->codec_type);
  memcpy ((uint8_t *)mmapbuf + size, codecinfo->codec_name, sizeof(codecinfo->codec_name));
  size += sizeof(codecinfo->codec_name);

  if (codecinfo->media_type == AVMEDIA_TYPE_VIDEO) {
	  memcpy ((uint8_t *)mmapbuf + size, &emuldec->format.video, sizeof(emuldec->format.video));
		size += sizeof(emuldec->format.video);
  } else if (codecinfo->media_type == AVMEDIA_TYPE_AUDIO) {
	  memcpy ((uint8_t *)mmapbuf + size, &emuldec->format.audio, sizeof(emuldec->format.audio));
		size += sizeof(emuldec->format.audio);
  } else {
    GST_ERROR_OBJECT(emuldec, "media type is unknown.\n");
		return -1;
	}

  if (extradata) {
    memcpy ((uint8_t *)mmapbuf + size, &extradata_size, sizeof(extradata_size));
    size += sizeof(extradata_size);
    memcpy ((uint8_t *)mmapbuf + size, extradata, extradata_size);
  }

  ret = write (fd, &func_type, sizeof(func_type));

  CODEC_LOG(2, "leave: %s\n", __func__);

  return ret;
}

int gst_emul_codec_device_close (GstEmulDec *emuldec)
{
  int width, height;
  uint32_t bufsize;
  int fd, ret = 0;
  void *mmapbuf;

  CODEC_LOG(2, "enter: %s\n", __func__);

  fd = emuldec->codecbuf.fd;
  if (fd < 0) {
//    CODEC_LOG(1, "failed to get %s fd\n", CODEC_DEV);
    GST_ERROR("failed to get %s fd.\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = emuldec->codecbuf.buffer;
  if (!mmapbuf) {
//    CODEC_LOG(1, "failed to get mmaped memory address\n");
    GST_ERROR("failed to get mmaped memory address.\n");
    return -1;
  }

  width = emuldec->format.video.width;
  height = emuldec->format.video.height;
  bufsize = ((width * height * 3) / 2) + 32;

//  CODEC_LOG(2, "release mmaped memory region of %s\n", CODEC_DEV);
  GST_DEBUG("release mmaped memory region of %s.\n", CODEC_DEV);
  ret = munmap (mmapbuf, bufsize);
  if (ret != 0) {
//    CODEC_LOG(1, "failed to release mmaped memory region of %s\n", CODEC_DEV);
    GST_ERROR("failed to release memory mapped region of %s.\n", CODEC_DEV);
  }

//  CODEC_LOG(2, "close %s fd\n", CODEC_DEV);

  GST_DEBUG("close %s fd.\n", CODEC_DEV);
  ret = close(fd);
  if (ret != 0) {
//    CODEC_LOG(1, "failed to close %s\n", CODEC_DEV);
    GST_ERROR("failed to close %s\n", CODEC_DEV);
  }

  CODEC_LOG(2, "leave: %s\n", __func__);

  return 0;
}

void gst_emul_codec_deinit (GstEmulDec *emuldec)
{
  int fd, ret;
  int func_type = CODEC_DEINIT;
  void *mmapbuf;

  CODEC_LOG(2, "enter: %s\n", __func__);

  fd = emuldec->codecbuf.fd;
  mmapbuf = emuldec->codecbuf.buffer;

  if (fd < 0) {
//    CODEC_LOG(1, "failed to get %s fd\n", CODEC_DEV);
    GST_ERROR("failed to get %s fd.\n", CODEC_DEV);
    return;
  }

  if (!mmapbuf) {
//    CODEC_LOG(1, "failed to get mmaped memory address\n");
    GST_ERROR("failed to get mmaped memory address.\n");
    return;
  }

  ret = write (fd, &func_type, sizeof(func_type));

  /* close device fd and release mapped memory region */
  gst_emul_codec_device_close (emuldec);

  CODEC_LOG(2, "leave: %s\n", __func__);
}

int gst_emul_decode_video (GstEmulDec *emuldec, guint8 *in_buf, guint in_size,
        GstBuffer **out_buf, GstTSInfo *dec_info)
{
  int fd, size = 0, ret;
  guint out_size;
  int api_index = CODEC_DECODE_VIDEO;
  void *mmapbuf;
  *out_buf = NULL;

  CODEC_LOG(2, "enter: %s\n", __func__);

#if 0
  GstCaps *caps;
  caps = gst_emul_codectype_to_caps (emuldec, emuldec->codecinfo);
  if (!gst_pad_set_caps (emuldec->srcpad, caps)) {
      GST_ERROR_OBJECT(emuldec, "failed to set caps for srcpad.\n");
//      gst_buffer_unref (buffer);
//     return GST_FLOW_NOT_NEGOTIATED;
  }
#endif

  fd = emuldec->codecbuf.fd;
  if (fd < 0) {
    GST_ERROR_OBJECT(emuldec, "failed to get %s fd\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = emuldec->codecbuf.buffer;
  if (!mmapbuf) {
    GST_ERROR_OBJECT(emuldec, "failed to get mmaped memory address\n");
    return -1;
  }

  memcpy ((uint8_t *)mmapbuf + size, &in_size, sizeof(guint));
  size += sizeof(guint);
  memcpy ((uint8_t *)mmapbuf + size, &dec_info->timestamp, sizeof(GstClockTime));
  size += sizeof(GstClockTime);
  memcpy ((uint8_t *)mmapbuf + size, in_buf, in_size);

  /* provide raw image for decoding to qemu */
  ret = write (fd, &api_index, sizeof(api_index));

  memcpy (&out_size, (uint8_t *)mmapbuf, sizeof(out_size));
  size = sizeof(out_size);
  printf("outbuf size: %d\n", out_size);

  ret = gst_pad_alloc_buffer_and_set_caps (emuldec->srcpad,
            GST_BUFFER_OFFSET_NONE, out_size,
            GST_PAD_CAPS (emuldec->srcpad), out_buf);

  gst_buffer_set_caps (*out_buf, GST_PAD_CAPS (emuldec->srcpad));

  if (GST_BUFFER_DATA(*out_buf)) {
    GST_DEBUG_OBJECT(emuldec, "copy decoded frame into out_buf.\n");
    memcpy (GST_BUFFER_DATA(*out_buf), (uint8_t *)mmapbuf + size, out_size);
  } else {
    GST_ERROR_OBJECT(emuldec, "failed to allocate output buffer.\n");
  }

  CODEC_LOG(2, "leave: %s\n", __func__);

  return ret;
}

int gst_emul_decode_audio (GstEmulDec *emuldec, guint8 *in_buf, guint in_size,
    GstBuffer **out_buf, GstTSInfo *dec_info)
{
  int fd, size = 0;
  guint out_size = 0;
  gint len = -1, have_data = 0;
  int api_index = CODEC_DECODE_AUDIO;
  void *mmapbuf = NULL;

  fd = emuldec->codecbuf.fd;
  if (fd < 0) {
    GST_ERROR_OBJECT(emuldec, "failed to get %s fd\n", CODEC_DEV);
    return -1;
  }

  mmapbuf = emuldec->codecbuf.buffer;
  if (!mmapbuf) {
    GST_ERROR_OBJECT(emuldec, "failed to get mmaped memory address\n");
    return -1;
  }

  memcpy ((uint8_t *)mmapbuf + size, &in_size, sizeof(guint));
  size += sizeof(guint);
  memcpy ((uint8_t *)mmapbuf + size, in_buf, in_size);

  write (fd, &api_index, sizeof(api_index));

  memcpy (&out_size, (uint8_t *)mmapbuf + size, sizeof(out_size));
  size = sizeof(out_size);
  memcpy (&len, (uint8_t *)mmapbuf + size, sizeof(len));
  size += sizeof(len);
  memcpy (&have_data, (uint8_t *)mmapbuf + size, sizeof(have_data));
	size += sizeof(have_data);

  printf("decode audio, out_size: %d, len: %d, have_data: %d\n", out_size, len, have_data);

  *out_buf = gst_buffer_new();
  GST_BUFFER_DATA (*out_buf) = GST_BUFFER_MALLOCDATA (*out_buf) = g_malloc0 (out_size);
  GST_BUFFER_SIZE (*out_buf) = out_size;
  GST_BUFFER_FREE_FUNC (*out_buf) = g_free;
  if (GST_PAD_CAPS(emuldec->srcpad)) {
    gst_buffer_set_caps (*out_buf, GST_PAD_CAPS (emuldec->srcpad));
  }

  if (GST_BUFFER_DATA(*out_buf)) {
    CODEC_LOG(2, "copy decoding audio data.\n");
//    memcpy (GST_BUFFER_DATA(*out_buf), (uint8_t *)mmapbuf + size, have_data);
    memcpy (GST_BUFFER_DATA(*out_buf), (uint8_t *)mmapbuf + size, out_size);
  } else {
    CODEC_LOG(1, "failed to allocate output buffer\n");
  }

  if (len >= 0 && have_data > 0) {
    GstCaps *caps = NULL;

    caps = gst_emul_codectype_to_caps (emuldec, emuldec->codecinfo);
    if (!gst_pad_set_caps (emuldec->srcpad, caps)) {
      CODEC_LOG(1, "failed to set caps to srcpad.\n");
    } else {
      gst_caps_unref (caps);
    }

    GstClockTime out_timestamp, out_duration;
    gint64 out_offset;

    GST_BUFFER_SIZE (*out_buf) = have_data;

    if (GST_CLOCK_TIME_IS_VALID (dec_info->timestamp)) {
      out_timestamp = dec_info->timestamp;
    } else {
      // TODO
    }

    emuldec->format.audio.depth = 2;

    CODEC_LOG(1, "depth: %d, channels: %d, sample_rate: %d\n",
              emuldec->format.audio.depth,
              emuldec->format.audio.channels,
              emuldec->format.audio.samplerate);

    out_duration = gst_util_uint64_scale (have_data, GST_SECOND,
              emuldec->format.audio.depth * emuldec->format.audio.channels *
              emuldec->format.audio.samplerate);
    out_offset = dec_info->offset;

    GST_BUFFER_TIMESTAMP (*out_buf) = out_timestamp;
    GST_BUFFER_DURATION (*out_buf) = out_duration;
    GST_BUFFER_OFFSET (*out_buf) = out_offset;
    gst_buffer_set_caps (*out_buf, GST_PAD_CAPS (emuldec->srcpad));

    if (GST_CLOCK_TIME_IS_VALID (out_timestamp)) {
      // TODO
//       printf("out timestamp is valid.\n");
    }

    GST_LOG_OBJECT (emuldec,
            "timestamp:%" GST_TIME_FORMAT ", duration:%" GST_TIME_FORMAT
            ", size %u", GST_TIME_ARGS (out_timestamp), GST_TIME_ARGS (out_duration),
            GST_BUFFER_SIZE (*out_buf));

#if 0    
    {
      GstClockTime stop;
      gint64 diff, ctime, cstop;

      stop = out_timestamp + out_duration;
    }
#endif

  } else {
    CODEC_LOG(1, "failed to decode audio.\n");
    gst_buffer_unref (*out_buf);
    *out_buf = NULL;
  }
#if 0
    /* the next timestamp we'll use when interpolating */
    if (GST_CLOCK_TIME_IS_VALID (out_timestamp))
        emuldec->next_out = out_timestamp + out_duration;

    /* now see if we need to clip the buffer against the segment boundaries. */
    if (G_UNLIKELY (!clip_audio_buffer (emuldec, *out_buf, out_timestamp,
                    out_duration)))
        goto clipped;
    } else {
      gst_buffer_unref (*out_buf);
      *out_buf = NULL;
    }

    /* If we don't error out after the first failed read with the AAC decoder,
     * we must *not* carry on pushing data, else we'll cause segfaults... */
    if (len == -1 && (in_plugin->id == CODEC_ID_AAC
              || in_plugin->id == CODEC_ID_AAC_LATM)) {
        GST_ELEMENT_ERROR (emuldec, STREAM, DECODE, (NULL),
              ("Decoding of AAC stream by FFMPEG failed."));
        *ret = GST_FLOW_ERROR;
    }

beach:
  GST_DEBUG_OBJECT (emuldec, "return flow %d, out %p, len %d",
      *ret, *out_buf, len);
  return len;

  /* ERRORS */
clipped:
  {
    GST_DEBUG_OBJECT (emuldec, "buffer clipped");
    gst_buffer_unref (*out_buf);
    *out_buf = NULL;
    goto beach;
  }
#endif

  return len;
}

GstCaps *
gst_emul_video_caps_new (CodecInfo *info, const char *mimetype,
        const char *fieldname, ...)
{
  GstStructure *structure = NULL;
  GstCaps *caps = NULL;
  gint i;
  va_list var_args;

  if (g_str_has_prefix(info->codec_name, "h263")) {
    /* 128x96, 176x144, 352x288, 704x576, and 1408x1152. slightly reordered
     * because we want automatic negotiation to go as close to 320x240 as
     * possible. */
    const static gint widths[] = { 352, 704, 176, 1408, 128 };
    const static gint heights[] = { 288, 576, 144, 1152, 96 };
    GstCaps *temp;
    gint n_sizes = G_N_ELEMENTS (widths);

    caps = gst_caps_new_empty ();
    for (i = 0; i < n_sizes; i++) {
      temp = gst_caps_new_simple (mimetype,
                    "width", G_TYPE_INT, widths[i],
                    "height", G_TYPE_INT, heights[i],
                    "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

      gst_caps_append (caps, temp);
    }
  }

  /* no fixed caps or special restrictions applied;
   * default unfixed setting */
  if (!caps) {
    GST_DEBUG ("Creating default caps");
    caps = gst_caps_new_simple (mimetype,
                "width", GST_TYPE_INT_RANGE, 16, 4096,
                "height", GST_TYPE_INT_RANGE, 16, 4096,
                "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    va_start (var_args, fieldname);
    structure = gst_caps_get_structure (caps, i);
    gst_structure_set_valist (structure, fieldname, var_args);
    va_end (var_args);
  }

  return caps;
}

GstCaps *
gst_emul_audio_caps_new (CodecInfo *info, const char *mimetype,
        const char *fieldname, ...)
{
  GstStructure *structure = NULL;
  GstCaps *caps = NULL;
  gint i;
  va_list var_args;

  gint maxchannels = 2;
  const gint *rates = NULL;
  gint n_rates = 0;

  if (strcmp(info->codec_name, "aac") == 0) {
    maxchannels = 6;
  } else if (g_str_has_prefix(info->codec_name, "ac3")) {  
    const static gint l_rates[] = { 48000, 44100, 32000 };
    maxchannels = 6;
    n_rates = G_N_ELEMENTS (l_rates);
    rates = l_rates;
  }
 
  caps = gst_caps_new_simple(mimetype,
          "channels", GST_TYPE_INT_RANGE, 1, maxchannels, NULL);
  if (n_rates) {
    GValue list = { 0, };
    GstStructure *structure;

    g_value_init(&list, GST_TYPE_LIST);
    for (i = 0; i < n_rates; i++) {
      GValue v = { 0, };
    
      g_value_init(&v, G_TYPE_INT);
      g_value_set_int(&v, rates[i]);
      gst_value_list_append_value(&list, &v);
      g_value_unset(&v);
    }
    structure = gst_caps_get_structure(caps, 0);
    gst_structure_set_value(structure, "rate", &list);
    g_value_unset(&list);
  } else {
    gst_caps_set_simple(caps, "rate", GST_TYPE_INT_RANGE, 4000, 96000, NULL);
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    va_start (var_args, fieldname);
    structure = gst_caps_get_structure (caps, i);
    gst_structure_set_valist (structure, fieldname, var_args);
    va_end (var_args);
  }

  return caps;
}

static GstCaps *
gst_emul_smpfmt_to_caps (int8_t sample_fmt, CodecInfo *info)
{
  GstCaps *caps = NULL;

  int bpp = 0;
  gboolean integer = TRUE;
  gboolean signedness = FALSE;

  switch (sample_fmt) {
  case SAMPLE_FMT_S16:
    signedness = TRUE;
    bpp = 16;
    break;
  case SAMPLE_FMT_S32:
    signedness = TRUE;
    bpp = 32;
    break;
  case SAMPLE_FMT_FLT:
    integer = FALSE;
    bpp = 32;
    break;
  case SAMPLE_FMT_DBL:
    integer = FALSE;
    bpp = 64;
    break;
  }

  if (bpp) {
    if (integer) {
      caps = gst_emul_audio_caps_new (info, "audio/x-raw-int",
          "signed", G_TYPE_BOOLEAN, signedness,
          "endianness", G_TYPE_INT, G_BYTE_ORDER,
          "width", G_TYPE_INT, bpp, "depth", G_TYPE_INT, bpp, NULL);
    } else {
      caps = gst_emul_audio_caps_new (info, "audio/x-raw-float",
          "endianness", G_TYPE_INT, G_BYTE_ORDER,
          "width", G_TYPE_INT, bpp, NULL);
    }
  }
  
  if (caps != NULL) {
    GST_LOG ("caps for sample_fmt=%d: %" GST_PTR_FORMAT, sample_fmt, caps);
  } else {
    GST_LOG ("No caps found for sample_fmt=%d", sample_fmt);
  }

  return caps;
}

GstCaps *
gst_emul_codectype_to_audio_caps (CodecInfo *info)
{
  GstCaps *caps = NULL;

  if (info->sample_fmts[0] != 0) {
    GstCaps *temp;
    int i;
    caps = gst_caps_new_empty ();
    for (i = 0; info->sample_fmts[i] != -1; i++) {
 	  	temp = gst_emul_smpfmt_to_caps (info->sample_fmts[i], info);
   		if (temp != NULL) {
	  		gst_caps_append (caps, temp);
  		}
    }
  } else {
    GstCaps *temp;
    int i;

    caps = gst_caps_new_empty ();
    for (i = 0; i <= SAMPLE_FMT_DBL; i++) {
      temp = gst_emul_smpfmt_to_caps (i, info);
      if (temp != NULL) {
        gst_caps_append (caps, temp);
      }
    }
  }
  
  return caps;
}

GstCaps *
gst_emul_codecname_to_caps (CodecInfo *info)
{
  GstCaps *caps = NULL;
  gchar *mime_type = NULL;

  if (strcmp(info->codec_name, "mpegvideo") == 0) {

    caps = gst_emul_video_caps_new (info, "video/mpeg",
                "mpegversion", G_TYPE_INT, 1,
                "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);

  } else if (strcmp(info->codec_name, "mpeg4") == 0) {

    caps = gst_emul_video_caps_new (info, "video/mpeg",
                "mpegversion", G_TYPE_INT, 4,
                "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
    if (info->codec_type) { // decode
      gst_caps_append (caps, gst_emul_video_caps_new (info, "video/x-divx",
                        "divxversion", GST_TYPE_INT_RANGE, 4, 5, NULL));
      gst_caps_append (caps, gst_emul_video_caps_new
                    (info, "video/x-xvid", NULL));
      gst_caps_append (caps, gst_emul_video_caps_new
                    (info, "video/x-3ivx", NULL));
    } else { // encode
      gst_caps_append (caps, gst_emul_video_caps_new (info, "video/x-divx",
                        "divxversion", G_TYPE_INT, 5, NULL));
    }

  } else if (strcmp(info->codec_name, "h263") == 0) {

    if (info->codec_type) {
      caps = gst_emul_video_caps_new (info, "video/x-h263",
                    "variant", G_TYPE_STRING, "itu", NULL);
    } else {
      caps = gst_emul_video_caps_new (info, "video/x-h263",
                    "variant", G_TYPE_STRING, "itu",
                    "h263version", G_TYPE_STRING, "h263", NULL);
    }

  } else if (strcmp(info->codec_name, "h263p") == 0) {

      caps = gst_emul_video_caps_new (info, "video/x-h263",
                "variant", G_TYPE_STRING, "itu",
                "h263version", G_TYPE_STRING, "h263p", NULL);

  } else if (strcmp(info->codec_name, "h264") == 0) {

      caps = gst_emul_video_caps_new (info, "video/x-h264", NULL);

  } else if (g_str_has_prefix(info->codec_name, "msmpeg4")) { // msmpeg4v1,m msmpeg4v2, msmpeg4

    gint version;

    if (strcmp(info->codec_name, "msmpeg4v1") == 0) {
      version = 41;
    } else if (strcmp(info->codec_name, "msmpeg4v2") == 0) {
      version = 42;
    } else {
      version = 43;
    }

    caps = gst_emul_video_caps_new (info, "video/x-msmpeg",
                "msmpegversion", G_TYPE_INT, version, NULL);
    if (info->codec_type && !strcmp(info->codec_name, "msmpeg4")) {
       gst_caps_append (caps, gst_emul_video_caps_new (info, "video/x-divx",
                        "divxversion", G_TYPE_INT, 3, NULL));
    }

  } else if (strcmp(info->codec_name, "wmv3") == 0) {

    caps = gst_emul_video_caps_new (info, "video/x-wmv",
                "wmvversion", G_TYPE_INT, 3, NULL);

  } else if (strcmp(info->codec_name, "vc1") == 0) {

    caps = gst_emul_video_caps_new (info, "video/x-wmv",
                "wmvversion", G_TYPE_INT, 3, "format", GST_TYPE_FOURCC,
                GST_MAKE_FOURCC ('W', 'V', 'C', '1'),  NULL);
#if 0
  } else if (strcmp(info->codec_name, "vp3") == 0) {

    mime_type = g_strdup ("video/x-vp3");

  } else if (strcmp(info->codec_name, "vp8") == 0) {

    mime_type = g_strdup ("video/x-vp8");
#endif
  } else if (strcmp(info->codec_name, "mp3") == 0) {

    mime_type = g_strdup ("audio/mpeg");
    if (info->codec_type) {
      caps = gst_caps_new_simple(mime_type, "mpegversion",
              G_TYPE_INT, 1, "layer", GST_TYPE_INT_RANGE, 1, 3, NULL);
    }
  } else if (strcmp(info->codec_name, "mp3adu") == 0) {

    mime_type = g_strdup_printf ("audio/x-gst_ff-%s", info->codec_name);
    caps = gst_emul_audio_caps_new (info, mime_type, NULL);
  } else if (strcmp(info->codec_name, "aac") == 0) {

    mime_type = g_strdup ("audio/mpeg");
    caps = gst_emul_audio_caps_new (info, mime_type, NULL);
    if (info->codec_type) {
        GValue arr = { 0, };
        GValue item = { 0, };

        g_value_init (&arr, GST_TYPE_LIST);
        g_value_init (&item, G_TYPE_INT);
        g_value_set_int (&item, 2);
        gst_value_list_append_value (&arr, &item);
        g_value_set_int (&item, 4);
        gst_value_list_append_value (&arr, &item);
        g_value_unset (&item);

        gst_caps_set_value (caps, "mpegversion", &arr);
        g_value_unset (&arr);

        g_value_init (&arr, GST_TYPE_LIST);
        g_value_init (&item, G_TYPE_STRING);
        g_value_set_string (&item, "raw");
        gst_value_list_append_value (&arr, &item);
        g_value_set_string (&item, "adts");
        gst_value_list_append_value (&arr, &item);
        g_value_set_string (&item, "adif");
        gst_value_list_append_value (&arr, &item);
        g_value_unset (&item);

        gst_caps_set_value (caps, "stream-format", &arr);
        g_value_unset (&arr);
    }

  } else if (strcmp(info->codec_name, "ac3") == 0) {

    mime_type = g_strdup ("audio/x-ac3");
    caps = gst_emul_audio_caps_new (info, mime_type, NULL);

  } else if (g_str_has_prefix(info->codec_name, "wmav")) {
    gint version = 1;
    if (strcmp(info->codec_name, "wmav2") == 0) {
      version = 2;
    }
    mime_type = g_strdup ("audio/x-wma");
    caps = gst_emul_audio_caps_new (info, mime_type, "wmaversion",
          G_TYPE_INT, version, "block_align", GST_TYPE_INT_RANGE, 0, G_MAXINT,
          "bitrate", GST_TYPE_INT_RANGE, 0, G_MAXINT, NULL);

  } else {
    GST_ERROR("failed to new caps for %s.\n", info->codec_name);
  }
  
  if (mime_type) {
    g_free(mime_type);
  }

  return caps;
}
