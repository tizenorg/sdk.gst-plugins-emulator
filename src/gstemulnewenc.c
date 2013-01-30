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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <gst/gst.h>
#include "gstemulcommon.h"

#define GST_EMULENC_PARAMS_QDATA g_quark_from_static_string("emulenc-params"); 

typedef struct _GstEmulEnc
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

  GstAdapter *adapter;

  // TODO: needs a certain container such as AVCodecContext.
  guint extradata_size;
  guint8 *extradata;

  CodecDev codecbuf;
} GstEmulEnc;

typedef struct _GstEmulEncClass
{
  GstElementClass parent_class;

  CodecInfo *codecinfo;
  GstPadTemplate *sinktempl, *srctempl;
  GstCaps *sinkcaps;
} GstEmulEncClass;

static GstElementClass *parent_class = NULL;

static void gst_emulenc_base_init (GstEmulEncClass *klass);
static void gst_emulenc_class_init (GstEmulEncClass *klass);
static void gst_emulenc_init (GstEmulEnc *emulenc);
static void gst_emulenc_finalize (GObject *object);

static gboolean gst_emulenc_setcaps (GstPad *pad, GstCaps *caps);
static gboolean gst_emulenc_getcaps (GstPad *pad);

static GstFlowReturn gst_emulenc_chain_video (GstPad *pad, GstBuffer *buffer);
static GstFlowReturn gst_emulenc_chain_audio (GstPad *pad, GstBuffer *buffer);

static gboolean gst_emulenc_event_video (GstPad *pad, GstEvent *event);
static gboolean gst_emulenc_event_src (GstPad *pad, GstEvent *event);

static GstStateChangeReturn gst_emulenc_change_state (GstElement *element, GstStateChange transition);

int gst_emul_codec_init (GstEmulEnc *emulenc);
void gst_emul_codec_deinit (GstEmulEnc *emulenc);

#if 0
int gst_emul_codec_encode_video (GstEmulEnc *emulenc, guint8 *in_buf, guint in_size,
        GstBuffer **out_buf, GstClockTime in_timestamp);
int gst_emul_codec_encode_audio (GstEmulEnc *emulenc, guint8 *in_buf, guint in_size,
        GstBuffer **out_buf, GstClockTime in_timestamp);
#endif

/*
 * Implementation
 */
static void
gst_emulenc_base_init (GstEmulEncClass *klass)
{
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
    GstPadTemplate *sinktempl = NULL, *srctempl = NULL;
    GstCaps *sinkcaps = NULL, *srccaps = NULL;
    CodecInfo *info;
    gchar *longname, *classification;

    info =
        (CodecInfo *)g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
         GST_EMULENC_PARAMS_QDATA);

    longname = g_strdup_printf ("%s Encoder", info->codec_longname);

    classification = g_strdup_printf ("Codec/Encoder/%s",
            (info->media_type == AVMEDIA_TYPE_VIDEO) ? "Video" : "Audio");

    gst_element_class_set_details_simple (element_class,
            longname,
            classification,
            "accelerated codec for Tizen Emulator",
            "Kitae Kim <kt920.kim@samsung.com>");
    g_free (longname);
    g_free (classification);


  if (!(srccaps = gst_emul_codecname_to_caps (info, TRUE))) {
    GST_DEBUG ("Couldn't get source caps for encoder '%s'", info->codec_name);
    srccaps = gst_caps_new_simple ("unknown/unknown", NULL);
  }

  switch (info->media_type) {
  case AVMEDIA_TYPE_VIDEO:
    sinkcaps = gst_caps_from_string ("video/x-raw-rgb; video/x-raw-yuv; video/x-raw-gray");
    break;
  case AVMEDIA_TYPE_AUDIO:
    srccaps = gst_emul_codectype_to_audio_caps (info, TRUE);
    break;
  default:
    GST_LOG("unknown media type.\n");
    break;
  }

  if (!sinkcaps) {
      GST_DEBUG ("Couldn't get sink caps for encoder '%s'", info->codec_name);
      sinkcaps = gst_caps_new_simple ("unknown/unknown", NULL);
  }

  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
          GST_PAD_ALWAYS, sinkcaps);
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC,
          GST_PAD_ALWAYS, srccaps);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  klass->sinktempl = sinktempl;
  klass->srctempl = srctempl;
  klass->sinkcaps = NULL;
}

    static void
gst_emulenc_class_init (GstEmulEncClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

#if 0
  gobject_class->set_property = gst_emulenc_set_property
  gobject_class->get_property = gst_emulenc_get_property
#endif

  gobject_class->finalize = gst_emulenc_finalize;

  gstelement_class->change_state = gst_emulenc_change_state; 
}

    static void
gst_emulenc_init (GstEmulEnc *emulenc)
{
  GstEmulEncClass *oclass;
  oclass = (GstEmulEncClass*) (G_OBJECT_GET_CLASS(emulenc));

  emulenc->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_pad_set_setcaps_function (emulenc->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_emulenc_setcaps));
  gst_pad_set_getcaps_function (emulenc->sinkpad,
                                GST_DEBUG_FUNCPTR(gst_emulenc_getcaps));

  emulenc->srcpad = gst_pad_new_from_template (oclass->srctempl, "src");
  gst_pad_use_fixed_caps (emulenc->srcpad);

  switch (oclass->codecinfo->media_type) {
  case AVMEDIA_TYPE_VIDEO:
    gst_pad_set_chain_function (emulenc->sinkpad, gst_emulenc_chain_video);
    gst_pad_set_event_function (emulenc->sinkpad, gst_emulenc_event_video);
    gst_pad_set_event_function (emulenc->srckpad, gst_emulenc_event_src);

    break;
  case AVMEDIA_TYPE_AUDIO:
    gst_pad_set_chain_function (emuldec->sinkpad, gst_emulenc_chain_audio);    
    break;
  default:
    break;
  }

  gst_element_add_pad (GST_ELEMENT (emulenc), emuldenc->sinkpad);
  gst_element_add_pad (GST_ELEMENT (emulenc), emuldenc->srcpad);

  // need to know what adapter does.
  emulenc->adapter = gst_adapter_new ();
}

    static void
gst_emulenc_finalize (GObject *object)
{
    // Deinit Decoder
    GstEmulEnc *emulenc = (GstEmulEnc *) object;

    G_OBJECT_CLASS (parent_class)->finalize (object);
}

    static gboolean
gst_emulenc_src_event (GstPad *pad, GstEvent *event)
{
    return 0;
}

    static void
gst_emulenc_get_caps (GstEmulEnc *emulenc, GstCaps *caps)
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
        emulenc->extradata_size = GST_BUFFER_SIZE (buf);
        emulenc->extradata = GST_BUFFER_DATA (buf);
    } else {
        CODEC_LOG (2, "no codec data\n");
        emulenc->extradata_size = 0;
        emulenc->extradata = NULL;
    }

#if 1 /* video type */
    /* Common Properites, width, height and etc. */
    gst_structure_get_int (structure, "width", &width);
    gst_structure_get_int (structure, "height", &height);
    gst_structure_get_int (structure, "bpp", &bits_per_coded_sample);

    emulenc->format.video.width = width;
    emulenc->format.video.height = height;

    fps = gst_structure_get_value (structure, "framerate");
    if (fps) {
        emulenc->format.video.framerate_den = gst_value_get_fraction_numerator (fps);
        emulenc->format.video.framerate_num = gst_value_get_fraction_denominator (fps);
    }

#if 0
    par = gst_structure_get_value (structure, "pixel-aspect-ratio");
    if (par) {
        sample_aspect_ratio.num = gst_structure_get_fraction_numerator (par);
        sample_aspect_ratio.den = gst_structure_get_fraction_denominator (par);
    }
#endif
#endif

#if 0 /* audio type */
    gst_structure_get_int (structure, "channels", &channels);
    gst_structure_get_int (structure, "rate", &sample_rate);
    gst_structure_get_int (structure, "block_align", &block_align);
    gst_structure_get_int (structure, "bitrate", &bit_rate);

    emulenc->format.audio.channels = channels;
    emulenc->format.audio.samplerate = sample_rate;
#endif

}

    static gboolean
gst_emulenc_setcaps (GstPad *pad, GstCaps *caps)
{
  GstEmulEnc *emulenc;
  GstEmulEncClass *oclass;
  gboolean ret = TRUE;

  emulenc = (GstEmulEnc *) (gst_pad_get_parent (pad));
  oclass = (GstEmulEncClass *) (G_OBJECT_GET_CLASS (emulenc));

  if (emulenc->opend) {
    gst_emul_deinit ();
    emulenc->opened = FALSE;

    gst_pad_set_caps (emulenc->srcpad, NULL);
  }

  gst_emul_caps_with_codectype (codec_type, caps);

  if (!time_base.den) {
    time_base.den = 25;
    time_base.num = 1;
  } else if (strcmp(  ,"mpeg4") == 0) {
  }

  // open codec

  if (gst_emul_codec_dev_open (emulenc) < 0) {
    CODEC_LOG(1, "failed to access %s or mmap operation\n", CODEC_DEV);
    gst_object_unref (emulenc);
    return FALSE;
  }

  if (gst_emul_codec_init (emulenc) < 0) {
    CODEC_LOG(1, "cannot initialize codec\n");
    gst_object_unref (emulenc);
    return FALSE;
  }

  allowed_caps = gst_pad_get_allowed_caps (emulenc->srcpad);
  if (!allowed_caps) {
    allowed_caps =
      gst_caps_copy (gst_pad_get_pad_template_caps (emulenc->srcpad));
  }

  gst_emul_caps_with_codecid (codec_id, codec_type, allowed_caps);

  other_caps = gst_emul_codecname_to_caps (codec_id, TRUE);

  if (!other_caps) {
     // deinit
    return FALSE;
  }

  icaps = gst_caps_intersect (allowed_caps, other_caps);
  gst_caps_unref (allowed_caps);
  gst_caps_unref (other_caps);
  if (gst_caps_is_empty (icaps)) {
    gst_caps_unref (icaps);
    return FALSE;
  }

  if (gst_caps_get_size (icaps) > 1) {
    GstCaps *newcaps;
  
    newcaps =
      gst_caps_new_full (gst_structure_copy (gst_caps_get_structure (icaps,
              0)), NULL);

  if (!gst_pad_set_caps (emulenc->srcpad, icaps)) {
    // deinit
    gst_caps_unref (icaps);
    return FALSE;
  }
  gst_object_unref (emulenc);

  // emulenc->opened = TRUE;

  return TRUE;
}

static gboolean
gst_emulenc_event_video (GstPad *pad, GstEvent *event)
{
  GstEmulEnc *emulenc;
  emulenc = (GstEmulEnc *) gst_pad_get_parent (pad);

  switch (GST_TYPE_EVENT (event)) {
  case GST_EVENT_EOS:
    CODEC_LOG(2, "received GST_EVENT_EOS\n");
    // flush_buffers
    break;
  case GST_EVENT_CUSTOM_DOWNSTREAM:
    CODEC_LOG(2, "received GST_EVENT_CUSTOM_DOWNSTREAM\n");
    break;
  }

  return gst_pad_push_event (emulenc->srcpad, event);
}

static gboolean
gst_emulenc_event_src (GstPad *pad, GstEvent *event)
{
  switch (GST_EVENT_TYPE (event)) {
  default:
    break    
  }

  return TRUE;
}

// minimum encoding buffer size.
// Used to avoid some checks during header writing.
#define FF_MIN_BUFFER_SIZE 16384

static inst
gst_emul_encode_video (uint8_t *outbuf, int buf_size,
                      uint8_t *pict_buf, uint32_t pict_buf_size)
{
  GstEmulEnc *emulenc = (GetEmulEnc *) (GST_PAD_PARENT (pad));
  int size = 0, api_index = CODEC_ENCODE_VIDEO;
  int ret;

  ret = write (); 

  return ret;
}

static GstFlowReturn
gst_emulenc_encode_audio (GstEmulEnc *emulenc, guint8 *audio_in,
  guint in_size, guint max_size, GstClockTime timestamp,
  GstClockTime duration, gboolean discont)
{
  GstBuffer *outbuf;
  guint8_t *audio_out;
  gint res;
  GstFlowReturn ret;
  int size = 0, api_index = CODEC_ENCODE_AUDIO;

  outbuf = gst_buffer_new_alloc (max_size + FF_MIN_BUFFER_SIZE);
  audio_out = GST_BUFFER_DATA (outbuf);

  // copy params to qemu

  res = write();

  // copy output and out params from qemu

  if (res < 0) {
    GST_ERROR_OBJECT (emulenc, "Failed to encode buffer: %d", res);
    gst_buffer_unref (outbuf);
    return GST_FLOW_OK;
  }

  GST_BUFFER_SIZE (outbuf) = res;
  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  GST_BUFFER_DURATION (outbuf) = duration

  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (emulenc->srcpad));

  ret = gst_pad_push (emulenc->srcpad, outbuf);

  return ret;
}

#if 0
static void
emulenc_setup_working_buf (GstEmulEnc * emulenc)
{
  guint wanted_size =
    ffmpegenc->context->width * ffmpegenc->context->height * 6 +
    FF_MIN_BUFFER_SIZE;
 
  /* Above is the buffer size used by ffmpeg/ffmpeg.c */
  if (ffmpegenc->working_buf == NULL ||
    ffmpegenc->working_buf_size != wanted_size) {
    if (ffmpegenc->working_buf)
      g_free (ffmpegenc->working_buf);
    ffmpegenc->working_buf_size = wanted_size;
    ffmpegenc->working_buf = g_malloc (ffmpegenc->working_buf_size);
  }
  ffmpegenc->buffer_size = wanted_size;
}
#endif

static GstFlowReturn
gst_emulenc_chain_video (GstPad *pad, GstBuffer *buffer)
{
  GstEmulEnc *emulenc = (GetEmulEnc *) (GST_PAD_PARENT (pad));
  GstBuffer *outbuf;
  gint in_size = 0;

  // setup_working_buf
  // width * height * 6 + FF_MIN_BUFFER_SIZE
  // 

//  gst_emul_encode_video ();

  outbuf = gst_buffer_new_and_alloc (ret_size);
  memcpy (GST_BUFFER_DATA(outbuf), , ret_size);
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (outbuf);

  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (emulenc->srcpad));

  gst_buffer_unref (buffer);
  
  return gst_pad_push (emulenc->srcpad, outbuf);
}

static GstFlowReturn
gst_emulenc_chain_audio (GstPad *pad, GstBuffer *buffer)
{
  GstEmulEnc *emulenc;
  GstEmulEncClass *oclass;
  GstClockTime timestamp, duration;
  guint size;
  GstFlowReturn ret;
  gboolean discont;

  emulenc = (GstEmulEnc *) (GST_OBJECT_PARENT (pad));
  oclass = (GstEmulEncClass *) G_OBJECT_GET_CLASS (emulenc);

  size = GST_BUFFER_SIZE (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  duration = GST_BUFFER_DURATION (buffer);
  discont = GST_BUFFER_IS_DISCONT (buffer);

  if (discont) {
    gst_adapter_clear (emulenc->adapter);
  }
  
//  gst_adapter_push (emulenc->adapter, buffer);

// TODO
// gst_emul_encode_audio

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_emulenc_change_state (GstElement *element, GstStateChange transition)
{
    GstEmulEnc *emulenc = (GstEmulEnc*)element;
    GstStateChangeReturn ret;

    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

    switch (transition) {
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            // flush buffer
            gst_emul_codec_deinit (emulenc);
        default:
            break;
    }

    return ret;
}

gboolean
gst_emulenc_register (GstPlugin *plugin, GList *element)
{
  GTypeInfo typeinfo = {
      sizeof (GstEmulEncClass),
      (GBaseInitFunc) gst_emulenc_base_init,
      NULL,
      (GClassInitFunc) gst_emulenc_class_init,
      NULL,
      NULL,
      sizeof (GstEmulEnc),
      0,
      (GInstanceInitFunc) gst_emulenc_init,
  };

  GType type;
  gchar *type_name;
  gint rank = GST_RANK_PRIMARY;
  gboolean ret = TRUE;
  GList *elem = NULL;
  CodecElement *codec = NULL;

  /* register element */
  while ((elem = g_list_next (element))) {
    codec = (CodecElement *)elem->data;
    if (!codec) {
      ret = FALSE;
      break;
    }

    if (codec->codec_type != CODEC_TYPE_ENCODE) {
      continue;
    }

    type_name = g_strdup_printf ("tzenc_%s", codec->name);
    type = g_type_from_name (type_name);
    if (!type) {
      type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
      g_type_set_qdata (type, GST_EMULDEC_PARAMS_QDATA, (gpointer) codec);
    }

    if (!gst_element_register (plugin, type_name, rank, type)) {
      g_free (type_name);
      return FALSE;
    }
    g_free (type_name);
  }

  return ret;
}
