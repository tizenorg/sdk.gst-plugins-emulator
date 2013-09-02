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

#include "gstemulutils.h"
#include "gstemulapi.h"
#include "gstemuldev.h"
#include <gst/base/gstadapter.h>

#define GST_EMULENC_PARAMS_QDATA g_quark_from_static_string("maruenc-params")

typedef struct _GstEmulEnc
{
  GstElement element;

  GstPad *srcpad;
  GstPad *sinkpad;

  CodecContext *context;
  CodecDevice *dev;
  gboolean opened;
  GstClockTime adapter_ts;
  guint64 adapter_consumed;
  GstAdapter *adapter;
  gboolean discont;

  // cache
  gulong bitrate;
  gint gop_size;
  gulong buffer_size;

  guint8 *working_buf;
  gulong working_buf_size;

  GQueue *delay;

} GstEmulEnc;

typedef struct _GstEmulEncClass
{
  GstElementClass parent_class;

  CodecElement *codec;
  GstPadTemplate *sinktempl;
  GstPadTemplate *srctempl;
  GstCaps *sinkcaps;
} GstEmulEncClass;

static GstElementClass *parent_class = NULL;

static void gst_emulenc_base_init (GstEmulEncClass *klass);
static void gst_emulenc_class_init (GstEmulEncClass *klass);
static void gst_emulenc_init (GstEmulEnc *emulenc);
static void gst_emulenc_finalize (GObject *object);

static gboolean gst_emulenc_setcaps (GstPad *pad, GstCaps *caps);
static GstCaps *gst_emulenc_getcaps (GstPad *pad);

static GstCaps *gst_emulenc_get_possible_sizes (GstEmulEnc *emulenc,
  GstPad *pad, const GstCaps *caps);

static GstFlowReturn gst_emulenc_chain_video (GstPad *pad, GstBuffer *buffer);
static GstFlowReturn gst_emulenc_chain_audio (GstPad *pad, GstBuffer *buffer);

static gboolean gst_emulenc_event_video (GstPad *pad, GstEvent *event);
static gboolean gst_emulenc_event_src (GstPad *pad, GstEvent *event);

GstStateChangeReturn gst_emulenc_change_state (GstElement *element, GstStateChange transition);

#define DEFAULT_VIDEO_BITRATE   300000
#define DEFAULT_VIDEO_GOP_SIZE  15
#define DEFAULT_AUDIO_BITRATE   128000

#define DEFAULT_WIDTH 352
#define DEFAULT_HEIGHT 288

/*
 * Implementation
 */
static void
gst_emulenc_base_init (GstEmulEncClass *klass)
{
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
    GstPadTemplate *sinktempl = NULL, *srctempl = NULL;
    GstCaps *sinkcaps = NULL, *srccaps = NULL;
    CodecElement *codec;
    gchar *longname, *classification, *description;

    codec =
        (CodecElement *)g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
         GST_EMULENC_PARAMS_QDATA);

    longname = g_strdup_printf ("%s Encoder", codec->longname);
    classification = g_strdup_printf ("Codec/Encoder/%s",
            (codec->media_type == AVMEDIA_TYPE_VIDEO) ? "Video" : "Audio");
    description = g_strdup_printf ("%s Encoder", codec->name);

    gst_element_class_set_details_simple (element_class,
            longname,
            classification,
            description,
//            "accelerated codec for Tizen Emulator",
            "Kitae Kim <kt920.kim@samsung.com>");

    g_free (longname);
    g_free (classification);


  if (!(srccaps = gst_emul_codecname_to_caps (codec->name, NULL, TRUE))) {
    GST_DEBUG ("Couldn't get source caps for encoder '%s'", codec->name);
    srccaps = gst_caps_new_simple ("unknown/unknown", NULL);
  }

  switch (codec->media_type) {
  case AVMEDIA_TYPE_VIDEO:
    sinkcaps = gst_caps_from_string ("video/x-raw-rgb; video/x-raw-yuv; video/x-raw-gray");
    break;
  case AVMEDIA_TYPE_AUDIO:
    sinkcaps = gst_emul_codectype_to_audio_caps (NULL, codec->name, TRUE, codec);
    break;
  default:
    GST_LOG("unknown media type.\n");
    break;
  }

  if (!sinkcaps) {
      GST_DEBUG ("Couldn't get sink caps for encoder '%s'", codec->name);
      sinkcaps = gst_caps_new_simple ("unknown/unknown", NULL);
  }

  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
          GST_PAD_ALWAYS, sinkcaps);
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC,
          GST_PAD_ALWAYS, srccaps);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  klass->codec = codec;
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

  gstelement_class->change_state = gst_emulenc_change_state;

  gobject_class->finalize = gst_emulenc_finalize;
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

  if (oclass->codec->media_type == AVMEDIA_TYPE_VIDEO) {
    gst_pad_set_chain_function (emulenc->sinkpad, gst_emulenc_chain_video);
    gst_pad_set_event_function (emulenc->sinkpad, gst_emulenc_event_video);
    gst_pad_set_event_function (emulenc->srcpad, gst_emulenc_event_src);

    emulenc->bitrate = DEFAULT_VIDEO_BITRATE;
    emulenc->buffer_size = 512 * 1024;
    emulenc->gop_size = DEFAULT_VIDEO_GOP_SIZE;
#if 0
    emulenc->lmin = 2;
    emulenc->lmax = 31;
#endif
  } else if (oclass->codec->media_type == AVMEDIA_TYPE_AUDIO){
    gst_pad_set_chain_function (emulenc->sinkpad, gst_emulenc_chain_audio);
    emulenc->bitrate = DEFAULT_AUDIO_BITRATE;
  }

  gst_element_add_pad (GST_ELEMENT (emulenc), emulenc->sinkpad);
  gst_element_add_pad (GST_ELEMENT (emulenc), emulenc->srcpad);

  emulenc->context = g_malloc0 (sizeof(CodecContext));
  emulenc->context->video.pix_fmt = PIX_FMT_NONE;
  emulenc->context->audio.sample_fmt = SAMPLE_FMT_NONE;

  emulenc->opened = FALSE;

#if 0
  emulenc->file = NULL;
#endif
  emulenc->delay = g_queue_new ();

  emulenc->dev = g_malloc0 (sizeof(CodecDevice));
  if (!emulenc->dev) {
    printf("failed to allocate memory.\n");
  }

  // need to know what adapter does.
  emulenc->adapter = gst_adapter_new ();
}

static void
gst_emulenc_finalize (GObject *object)
{
  // Deinit Decoder
  GstEmulEnc *emulenc = (GstEmulEnc *) object;

  if (emulenc->opened) {
    gst_emul_avcodec_close (emulenc->context, emulenc->dev);
    emulenc->opened = FALSE;
  }

  if (emulenc->context) {
    g_free (emulenc->context);
    emulenc->context = NULL;
  }

  g_queue_free (emulenc->delay);
#if 0
  g_free (emulenc->filename);
#endif

  g_object_unref (emulenc->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_emulenc_get_possible_sizes (GstEmulEnc *emulenc, GstPad *pad,
  const GstCaps *caps)
{
  GstCaps *othercaps = NULL;
  GstCaps *tmpcaps = NULL;
  GstCaps *intersect = NULL;
  guint i;

  othercaps = gst_pad_peer_get_caps (emulenc->srcpad);

  if (!othercaps) {
    return gst_caps_copy (caps);
  }

  intersect = gst_caps_intersect (othercaps,
    gst_pad_get_pad_template_caps (emulenc->srcpad));
  gst_caps_unref (othercaps);

  if (gst_caps_is_empty (intersect)) {
    return intersect;
  }

  if (gst_caps_is_any (intersect)) {
    return gst_caps_copy (caps);
  }

  tmpcaps = gst_caps_new_empty ();

  for (i = 0; i <gst_caps_get_size (intersect); i++) {
    GstStructure *s = gst_caps_get_structure (intersect, i);
    const GValue *height = NULL;
    const GValue *width = NULL;
    const GValue *framerate = NULL;
    GstStructure *tmps;

    height = gst_structure_get_value (s, "height");
    width = gst_structure_get_value (s, "width");
    framerate = gst_structure_get_value (s, "framerate");

    tmps = gst_structure_new ("video/x-rwa-rgb", NULL);
    if (width) {
      gst_structure_set_value (tmps, "width", width);
    }
    if (height) {
      gst_structure_set_value (tmps, "height", height);
    }
    if (framerate) {
      gst_structure_set_value (tmps, "framerate", framerate);
    }
    gst_caps_merge_structure (tmpcaps, gst_structure_copy (tmps));

    gst_structure_set_name (tmps, "video/x-raw-yuv");
    gst_caps_merge_structure (tmpcaps, gst_structure_copy (tmps));

    gst_structure_set_name (tmps, "video/x-raw-gray");
    gst_caps_merge_structure (tmpcaps, tmps);
  }
  gst_caps_unref (intersect);

  intersect = gst_caps_intersect (caps, tmpcaps);
  gst_caps_unref (tmpcaps);

  return intersect;
}

static GstCaps *
gst_emulenc_getcaps (GstPad *pad)
{
  GstEmulEnc *emulenc = (GstEmulEnc *) GST_PAD_PARENT (pad);
  GstEmulEncClass *oclass =
    (GstEmulEncClass *) G_OBJECT_GET_CLASS (emulenc);
  CodecContext *ctx = NULL;
  enum PixelFormat pixfmt;
  GstCaps *caps = NULL;
  GstCaps *finalcaps = NULL;
  gint i;

  GST_DEBUG_OBJECT (emulenc, "getting caps");

  if (!oclass->codec) {
    GST_ERROR_OBJECT (emulenc, "codec element is null.");
    return NULL;
  }

  if (oclass->codec->media_type == AVMEDIA_TYPE_AUDIO) {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

    GST_DEBUG_OBJECT (emulenc, "audio caps, return template %" GST_PTR_FORMAT,
      caps);
    return caps;
  }

  // cached
  if (oclass->sinkcaps) {
    caps = gst_emulenc_get_possible_sizes (emulenc, pad, oclass->sinkcaps);
    GST_DEBUG_OBJECT (emulenc, "return cached caps %" GST_PTR_FORMAT, caps);
    return caps;
  }

  GST_DEBUG_OBJECT (emulenc, "probing caps");
  i = pixfmt = 0;

  for (pixfmt = 0;; pixfmt++) {
    GstCaps *tmpcaps;

    if ((pixfmt = oclass->codec->pix_fmts[i++]) == PIX_FMT_NONE) {
      GST_DEBUG_OBJECT (emulenc,
          "At the end of official pixfmt for this codec, breaking out");
      break;
    }

    GST_DEBUG_OBJECT (emulenc,
        "Got an official pixfmt [%d], attempting to get caps", pixfmt);
    tmpcaps = gst_emul_pixfmt_to_caps (pixfmt, NULL, oclass->codec->name);
    if (tmpcaps) {
      GST_DEBUG_OBJECT (emulenc, "Got caps, breaking out");
      if (!caps) {
        caps = gst_caps_new_empty ();
      }
      gst_caps_append (caps, tmpcaps);
      continue;
    }

    GST_DEBUG_OBJECT (emulenc,
        "Couldn't figure out caps without context, trying again with a context");

    GST_DEBUG_OBJECT (emulenc, "pixfmt: %d", pixfmt);
    if (pixfmt >= PIX_FMT_NB) {
      GST_WARNING ("Invalid pixfmt, breaking out");
      break;
    }

    ctx = g_malloc0 (sizeof(CodecContext));
    if (!ctx) {
      GST_DEBUG_OBJECT (emulenc, "no context");
      break;
    }

    ctx->video.width = DEFAULT_WIDTH;
    ctx->video.height = DEFAULT_HEIGHT;
    ctx->video.fps_n = 1;
    ctx->video.fps_d = 25;
    ctx->video.ticks_per_frame = 1;
    ctx->bit_rate = DEFAULT_VIDEO_BITRATE;

//  ctx->strict_std_compliance = -1;
    ctx->video.pix_fmt = pixfmt;

    GST_DEBUG ("Attempting to open codec");
    if (gst_emul_avcodec_open (ctx, oclass->codec, emulenc->dev) >= 0
        && ctx->video.pix_fmt == pixfmt) {
      ctx->video.width = -1;
      if (!caps) {
        caps = gst_caps_new_empty ();
      }
      tmpcaps = gst_emul_codectype_to_caps (oclass->codec->media_type, ctx,
          oclass->codec->name, TRUE);
      if (tmpcaps) {
        gst_caps_append (caps, tmpcaps);
      } else {
        GST_LOG_OBJECT (emulenc,
            "Couldn't get caps for codec: %s", oclass->codec->name);
      }
      gst_emul_avcodec_close (ctx, emulenc->dev);
    } else {
      GST_DEBUG_OBJECT (emulenc, "Opening codec failed with pixfmt: %d", pixfmt);
    }

    gst_emul_avcodec_close (ctx, emulenc->dev);
#if 0
    if (ctx->priv_data) {
      gst_emul_avcodec_close (ctx, emulenc->dev);
    }
#endif
    g_free (ctx);
  }

  if (!caps) {
    caps = gst_emulenc_get_possible_sizes (emulenc, pad,
      gst_pad_get_pad_template_caps (pad));
    GST_DEBUG_OBJECT (emulenc, "probing gave nothing, "
      "return template %" GST_PTR_FORMAT, caps);
    return caps;
  }

  GST_DEBUG_OBJECT (emulenc, "probed caps gave %" GST_PTR_FORMAT, caps);
  oclass->sinkcaps = gst_caps_copy (caps);

  finalcaps = gst_emulenc_get_possible_sizes (emulenc, pad, caps);
  gst_caps_unref (caps);

  return finalcaps;
}

static gboolean
gst_emulenc_setcaps (GstPad *pad, GstCaps *caps)
{
  GstEmulEnc *emulenc;
  GstEmulEncClass *oclass;
  GstCaps *other_caps;
  GstCaps *allowed_caps;
  GstCaps *icaps;
  enum PixelFormat pix_fmt;
  int32_t buf_size;

  emulenc = (GstEmulEnc *) (gst_pad_get_parent (pad));
  oclass = (GstEmulEncClass *) (G_OBJECT_GET_CLASS (emulenc));

  if (emulenc->opened) {
    gst_emul_avcodec_close (emulenc->context, emulenc->dev);
    emulenc->opened = FALSE;

    gst_pad_set_caps (emulenc->srcpad, NULL);
  }

  emulenc->context->bit_rate = emulenc->bitrate;
  GST_DEBUG_OBJECT (emulenc, "Setting context to bitrate %lu, gop_size %d",
      emulenc->bitrate, emulenc->gop_size);

#if 0

  // user defined properties
  emulenc->context->gop_size = emulenc->gop_size;
  emulenc->context->lmin = (emulenc->lmin * FF_QP2LAMBDA + 0.5);
  emulenc->context->lmax = (emulenc->lmax * FF_QP2LAMBDA + 0.5);

  // some other defaults
  emulenc->context->b_frame_strategy = 0;
  emulenc->context->coder_type = 0;
  emulenc->context->context_model = 0;
  emulenc->context->scenechange_threshold = 0;
  emulenc->context->inter_threshold = 0;

  if (emulenc->interlaced) {
    emulenc->context->flags |=
      CODEC_FLAG_INTERLACED_DCT | CODEC_FLAG_INTERLACED_ME;
    emulenc->picture->interlaced_frame = TRUE;

    emulenc->picture->top_field_first = TRUE;
  }
#endif

  gst_emul_caps_with_codectype (oclass->codec->media_type, caps, emulenc->context);

  if (!emulenc->context->video.fps_d) {
    emulenc->context->video.fps_d = 25;
    emulenc->context->video.fps_n = 1;
  } else if (!strcmp(oclass->codec->name ,"mpeg4")
      && (emulenc->context->video.fps_d > 65535)) {
      emulenc->context->video.fps_n =
        (gint) gst_util_uint64_scale_int (emulenc->context->video.fps_n,
            65535, emulenc->context->video.fps_d);
      emulenc->context->video.fps_d = 65535;
      GST_LOG_OBJECT (emulenc, "MPEG4 : scaled down framerate to %d / %d",
          emulenc->context->video.fps_d, emulenc->context->video.fps_n);
  }

  pix_fmt = emulenc->context->video.pix_fmt;

  {
    switch (oclass->codec->media_type) {
    case AVMEDIA_TYPE_VIDEO:
    {
      int width, height;

      width = emulenc->context->video.width;
      height = emulenc->context->video.height;
      buf_size = width * height * 6 + FF_MIN_BUFFER_SIZE + 100;
      break;
    }
    case AVMEDIA_TYPE_AUDIO:
        buf_size = FF_MAX_AUDIO_FRAME_SIZE + 100;
        break;
    default:
        buf_size = -1;
        break;
    }
  }

  emulenc->dev->buf_size = gst_emul_align_size(buf_size);

  // open codec
  if (gst_emul_avcodec_open (emulenc->context,
      oclass->codec, emulenc->dev) < 0) {
    GST_DEBUG_OBJECT (emulenc, "maru_%senc: Failed to open codec",
        oclass->codec->name);
    return FALSE;
  }

  if (pix_fmt != emulenc->context->video.pix_fmt) {
    gst_emul_avcodec_close (emulenc->context, emulenc->dev);
    GST_DEBUG_OBJECT (emulenc,
      "maru_%senc: AV wants different colorspace (%d given, %d wanted)",
      oclass->codec->name, pix_fmt, emulenc->context->video.pix_fmt);
    return FALSE;
  }

  if (oclass->codec->media_type == AVMEDIA_TYPE_VIDEO
    && pix_fmt == PIX_FMT_NONE) {
    GST_DEBUG_OBJECT (emulenc, "maru_%senc: Failed to determine input format",
      oclass->codec->name);
    return FALSE;
  }

  GST_DEBUG_OBJECT (emulenc, "picking an output format.");
  allowed_caps = gst_pad_get_allowed_caps (emulenc->srcpad);
  if (!allowed_caps) {
    GST_DEBUG_OBJECT (emulenc, "but no peer, using template caps");
    allowed_caps =
      gst_caps_copy (gst_pad_get_pad_template_caps (emulenc->srcpad));
  }

  GST_DEBUG_OBJECT (emulenc, "chose caps %" GST_PTR_FORMAT, allowed_caps);
  gst_emul_caps_with_codecname (oclass->codec->name,
    oclass->codec->media_type, allowed_caps, emulenc->context);

  other_caps =
  gst_emul_codecname_to_caps (oclass->codec->name, emulenc->context, TRUE);
  if (!other_caps) {
  GST_DEBUG("Unsupported codec - no caps found");
    gst_emul_avcodec_close (emulenc->context, emulenc->dev);
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
    gst_caps_unref (icaps);
    icaps = newcaps;
  }

  if (!gst_pad_set_caps (emulenc->srcpad, icaps)) {
    gst_emul_avcodec_close (emulenc->context, emulenc->dev);
    gst_caps_unref (icaps);
    return FALSE;
  }
  gst_object_unref (emulenc);

  emulenc->opened = TRUE;

  return TRUE;
}

static void
gst_emulenc_setup_working_buf (GstEmulEnc *emulenc)
{
  guint wanted_size =
      emulenc->context->video.width * emulenc->context->video.height * 6 +
      FF_MIN_BUFFER_SIZE;

  if (emulenc->working_buf == NULL ||
    emulenc->working_buf_size != wanted_size) {
    if (emulenc->working_buf) {
      g_free (emulenc->working_buf);
    }
    emulenc->working_buf_size = wanted_size;
    emulenc->working_buf = g_malloc0 (emulenc->working_buf_size);
  }
  emulenc->buffer_size = wanted_size;
}

GstFlowReturn
gst_emulenc_chain_video (GstPad *pad, GstBuffer *buffer)
{
  GstEmulEnc *emulenc = (GstEmulEnc *) (GST_PAD_PARENT (pad));
  GstBuffer *outbuf;
  gint ret_size = 0, frame_size;

  GST_DEBUG_OBJECT (emulenc,
      "Received buffer of time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

#if 0
  GST_OBJECT_LOCK (emulenc);
  force_keyframe = emulenc->force_keyframe;
  emulenc->force_keyframe = FALSE;
  GST_OBJECT_UNLOCK (emulenc);

  if (force_keyframe) {
    emulenc->picture->pict_type = FF_I_TYPE;
  }
#endif

  frame_size = gst_emul_avpicture_size (emulenc->context->video.pix_fmt,
      emulenc->context->video.width, emulenc->context->video.height);
  g_return_val_if_fail (frame_size == GST_BUFFER_SIZE (buffer),
      GST_FLOW_ERROR);

#if 0
  pts = gst_emul_time_gst_to_ff (GST_BUFFER_TIMESTAMP (buffer) /
    emulenc->context.video.ticks_per_frame,
    emulenc->context.video.fps_n, emulen->context.video.fps_d);
#endif

  // TODO: check whether this func needs or not.
  gst_emulenc_setup_working_buf (emulenc);

  ret_size =
    codec_encode_video (emulenc->context, emulenc->working_buf,
                emulenc->working_buf_size, GST_BUFFER_DATA (buffer),
                GST_BUFFER_SIZE (buffer), GST_BUFFER_TIMESTAMP (buffer),
                emulenc->dev);

  if (ret_size < 0) {
    GstEmulEncClass *oclass =
      (GstEmulEncClass *) (G_OBJECT_GET_CLASS (emulenc));
    GST_ERROR_OBJECT (emulenc,
        "maru_%senc: failed to encode buffer", oclass->codec->name);
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  g_queue_push_tail (emulenc->delay, buffer);
  if (ret_size) {
    buffer = g_queue_pop_head (emulenc->delay);
  } else {
    return GST_FLOW_OK;
  }

#if 0
  if (emulenc->file && emulenc->context->stats_out) {
    if (fprintf (emulenc->file, "%s", emulenc->context->stats_out) < 0) {
      GST_ELEMENT_ERROR (emulenc, RESOURCE, WRITE,
        (("Could not write to file \"%s\"."), emulenc->filename),
        GST_ERROR_SYSTEM);
    }
  }
#endif
#if 1
  {
    int ret;
    uint32_t mem_offset;
    uint8_t *working_buf = NULL;

    mem_offset = emulenc->dev->mem_info.offset;
    working_buf = emulenc->dev->buf + mem_offset;
    if (!working_buf) {
    } else {
      CODEC_LOG (INFO,
          "encoded video. mem_offset = 0x%x\n",  mem_offset);

      outbuf = gst_buffer_new_and_alloc (ret_size);
//    memcpy (GST_BUFFER_DATA (outbuf), emulenc->working_buf, ret_size);
      memcpy (GST_BUFFER_DATA (outbuf), working_buf, ret_size);
      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);
      GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buffer);
    }

    ret = ioctl(emulenc->dev->fd, CODEC_CMD_RELEASE_MEMORY, &mem_offset);
    if (ret < 0) {
      CODEC_LOG (ERR, "failed to release used buffer\n");
    }
  }
#endif

#if 0
  if (emulenc->context->coded_frame) {
    if (!emulenc->context->coded_frame->key_frame) {
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
    }
  } else {
    GST_WARNING_OBJECT (emulenc, "codec did not provide keyframe info");
  }
#endif
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (emulenc->srcpad));

  gst_buffer_unref (buffer);

#if 0

  if (force_keyframe) {
    gst_pad_push_event (emulenc->srcpad,
      gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
      gst_structure_new ("GstForceKeyUnit", "timestamp",
                         G_TYPE_UINT64, GST_BUFFER_TIMESTAMP (outbuf), NULL)));
  }
#endif

  return gst_pad_push (emulenc->srcpad, outbuf);
}

GstFlowReturn
gst_emulenc_encode_audio (GstEmulEnc *emulenc, guint8 *audio_in,
  guint in_size, guint max_size, GstClockTime timestamp,
  GstClockTime duration, gboolean discont)
{
  GstBuffer *outbuf;
  guint8 *audio_out;
  gint res;
  GstFlowReturn ret;

  outbuf = gst_buffer_new_and_alloc (max_size + FF_MIN_BUFFER_SIZE);
  audio_out = GST_BUFFER_DATA (outbuf);

  GST_LOG_OBJECT (emulenc, "encoding buffer of max size %d", max_size);
  if (emulenc->buffer_size != max_size) {
    emulenc->buffer_size = max_size;
  }

  res = codec_encode_audio (emulenc->context, audio_out, max_size,
                                  audio_in, in_size, emulenc->dev);

  if (res < 0) {
    GST_ERROR_OBJECT (emulenc, "Failed to encode buffer: %d", res);
    gst_buffer_unref (outbuf);
    return GST_FLOW_OK;
  }
  GST_LOG_OBJECT (emulenc, "got output size %d", res);

  GST_BUFFER_SIZE (outbuf) = res;
  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  GST_BUFFER_DURATION (outbuf) = duration;
  if (discont) {
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
  }
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (emulenc->srcpad));

  GST_LOG_OBJECT (emulenc, "pushing size %d, timestamp %",
      GST_TIME_FORMAT, res, GST_TIME_ARGS (timestamp));

  ret = gst_pad_push (emulenc->srcpad, outbuf);

  return ret;
}

static GstFlowReturn
gst_emulenc_chain_audio (GstPad *pad, GstBuffer *buffer)
{
  GstEmulEnc *emulenc;
  GstEmulEncClass *oclass;
  GstClockTime timestamp, duration;
  guint in_size, frame_size;
  gint osize;
  GstFlowReturn ret;
  gint out_size = 0;
  gboolean discont;
  guint8 *in_data;
  CodecContext *ctx;

  emulenc = (GstEmulEnc *) (GST_OBJECT_PARENT (pad));
  oclass = (GstEmulEncClass *) G_OBJECT_GET_CLASS (emulenc);

  ctx = emulenc->context;

  in_size = GST_BUFFER_SIZE (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  duration = GST_BUFFER_DURATION (buffer);
  discont = GST_BUFFER_IS_DISCONT (buffer);

  GST_DEBUG_OBJECT (emulenc,
    "Received time %" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT
    ", size %d", GST_TIME_ARGS (timestamp), GST_TIME_ARGS (duration), in_size);

  frame_size = ctx->audio.frame_size;
  osize = ctx->audio.bits_per_sample_fmt;

  if (frame_size > 1) {
    guint avail, frame_bytes;

    if (discont) {
      GST_LOG_OBJECT (emulenc, "DISCONT, clear adapter");
      gst_adapter_clear (emulenc->adapter);
      emulenc->discont = TRUE;
    }

    if (gst_adapter_available (emulenc->adapter) == 0) {
      GST_LOG_OBJECT (emulenc, "taking buffer timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (timestamp));
      emulenc->adapter_ts = timestamp;
      emulenc->adapter_consumed = 0;
    } else {
      GstClockTime upstream_time;
      GstClockTime consumed_time;
      guint64 bytes;

      consumed_time =
        gst_util_uint64_scale (emulenc->adapter_consumed, GST_SECOND,
            ctx->audio.sample_rate);
      timestamp = emulenc->adapter_ts + consumed_time;
      GST_LOG_OBJECT (emulenc, "taking adapter timestamp %" GST_TIME_FORMAT
        " and adding consumed time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (emulenc->adapter_ts), GST_TIME_ARGS (consumed_time));

      upstream_time = gst_adapter_prev_timestamp (emulenc->adapter, &bytes);
      if (GST_CLOCK_TIME_IS_VALID (upstream_time)) {
        GstClockTimeDiff diff;

        upstream_time +=
          gst_util_uint64_scale (bytes, GST_SECOND,
            ctx->audio.sample_rate * osize * ctx->audio.channels);
        diff = upstream_time - timestamp;

        if (diff > GST_SECOND / 10 || diff < -GST_SECOND / 10) {
          GST_DEBUG_OBJECT (emulenc, "adapter timestamp drifting, "
            "taking upstream timestamp %" GST_TIME_FORMAT,
            GST_TIME_ARGS (upstream_time));
          timestamp = upstream_time;

          emulenc->adapter_consumed = bytes / (osize * ctx->audio.channels);
          emulenc->adapter_ts =
            upstream_time - gst_util_uint64_scale (emulenc->adapter_consumed,
                GST_SECOND, ctx->audio.sample_rate);
          emulenc->discont = TRUE;
        }
      }
    }

    GST_LOG_OBJECT (emulenc, "pushing buffer in adapter");
    gst_adapter_push (emulenc->adapter, buffer);

    frame_bytes = frame_size * osize * ctx->audio.channels;
    avail = gst_adapter_available (emulenc->adapter);

    GST_LOG_OBJECT (emulenc, "frame_bytes %u, avail %u", frame_bytes, avail);

    while (avail >= frame_bytes) {
      GST_LOG_OBJECT (emulenc, "taking %u bytes from the adapter", frame_bytes);

      in_data = (guint8 *) gst_adapter_peek (emulenc->adapter, frame_bytes);
      emulenc->adapter_consumed += frame_size;

      duration =
        gst_util_uint64_scale (emulenc->adapter_consumed, GST_SECOND,
          ctx->audio.sample_rate);
      duration -= (timestamp - emulenc->adapter_ts);

      out_size = frame_bytes * 4;

      ret =
        gst_emulenc_encode_audio (emulenc, in_data, frame_bytes, out_size,
          timestamp, duration, emulenc->discont);

      gst_adapter_flush (emulenc->adapter, frame_bytes);
      if (ret != GST_FLOW_OK) {
        GST_DEBUG_OBJECT (emulenc, "Failed to push buffer %d (%s)", ret,
          gst_flow_get_name (ret));
      }

      timestamp += duration;

      emulenc->discont = FALSE;
      avail = gst_adapter_available (emulenc->adapter);
    }
    GST_LOG_OBJECT (emulenc, "%u bytes left in the adapter", avail);
  } else {
#if 0
    int coded_bps = av_get_bits_per_sample (oclass->codec->name);

    GST_LOG_OBJECT (emulenc, "coded bps %d, osize %d", coded_bps, osize);

    out_size = in_size / osize;
    if (coded_bps) {
      out_size = (out_size * coded_bps) / 8;
    }
#endif
    in_data = (guint8 *) GST_BUFFER_DATA (buffer);
    ret = gst_emulenc_encode_audio (emulenc, in_data, in_size, out_size,
      timestamp, duration, discont);
    gst_buffer_unref (buffer);
    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (emulenc, "Failed to push buffer %d (%s)", ret,
        gst_flow_get_name (ret));
    }
  }

  return GST_FLOW_OK;
}

static void
gst_emulenc_flush_buffers (GstEmulEnc *emulenc, gboolean send)
{
  GstBuffer *outbuf, *inbuf;
  gint ret_size = 0;

  GST_DEBUG_OBJECT (emulenc, "flushing buffers with sending %d", send);

  if (!emulenc->opened) {
    while (!g_queue_is_empty (emulenc->delay)) {
      gst_buffer_unref (g_queue_pop_head (emulenc->delay));
    }
  }

#if 0
  while (!g_queue_is_empty (emulenc->delay)) {
    emulenc_setup_working_buf (emulenc);

    ret_size = codec_encode_video (emulenc->context,
      emulenc->working_buf, emulenc->working_buf_size, NULL, NULL, 0,
      emulenc->dev);

    if (ret_size < 0) {
      GstEmulEncClass *oclass =
        (GstEmulEncClass *) (G_OBJECT_GET_CLASS (emulenc));
      GST_WARNING_OBJECT (emulenc,
        "maru_%senc: failed to flush buffer", oclass->codec->name);
      break;
    }

    if (emulenc->file && emulenc->context->stats_out) {
      if (fprintf (emulenc->file, "%s", emulenc->context->stats_out) < 0) {
        GST_ELEMENT_ERROR (emeulenc, RESOURCE, WRITE,
          (("Could not write to file \"%s\"."), emulenc->filename),
          GST_ERROR_SYSTEM);
      }
    }

    inbuf = g_queue_pop_head (emulenc->delay);

    outbuf = gst_buffer_new_and_alloc (ret_size);
    memcpy (GST_BUFFER_DATA (outbuf), emulenc->working_buf, ret_size);
    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (inbuf);
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);

    if (!emulenc->context->coded_frame->key_frame) {
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
    }
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (emulenc->srcpad));

    gst_buffer_unref (inbuf);

    if (send) {
      gst_pad_push (emulenc->srcpad, outbuf);
    } else {
      gst_buffer_unref (outbuf);
    }
  }

  while (!g_queue_is_empty (emulenc->delay)) {
    gst_buffer_unref (g_queue_pop_head (emulenc->delay));
  }
#endif
}

static gboolean
gst_emulenc_event_video (GstPad *pad, GstEvent *event)
{
  GstEmulEnc *emulenc;
  emulenc = (GstEmulEnc *) gst_pad_get_parent (pad);

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_EOS:
    gst_emulenc_flush_buffers (emulenc, TRUE);
    break;
  case GST_EVENT_CUSTOM_DOWNSTREAM:
  {
    const GstStructure *s;
    s = gst_event_get_structure (event);

    if (gst_structure_has_name (s, "GstForceKeyUnit")) {
#if 0
      emulenc->picture->pict_type = FF_I_TYPE;
#endif
    }
  }
    break;
  default:
    break;
  }

  return gst_pad_push_event (emulenc->srcpad, event);
}

static gboolean
gst_emulenc_event_src (GstPad *pad, GstEvent *event)
{
  GstEmulEnc *emulenc = (GstEmulEnc *) (GST_PAD_PARENT (pad));
  gboolean forward = TRUE;

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_CUSTOM_UPSTREAM:
  {
    const GstStructure *s;
    s = gst_event_get_structure (event);

    if (gst_structure_has_name (s, "GstForceKeyUnit")) {
#if 0
      GST_OBJECT_LOCK (emulenc);
      emulenc->force_keyframe = TRUE;
      GST_OBJECT_UNLOCK (emulenc);
#endif
      forward = FALSE;
      gst_event_unref (event);
    }
  }
    break;
  default:
    break;
  }

  if (forward) {
    return gst_pad_push_event (emulenc->sinkpad, event);
  }

  return TRUE;
}

GstStateChangeReturn
gst_emulenc_change_state (GstElement *element, GstStateChange transition)
{
  GstEmulEnc *emulenc = (GstEmulEnc*)element;
  GstStateChangeReturn ret;

  switch (transition) {
  default:
    break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
  case GST_STATE_CHANGE_PAUSED_TO_READY:
    gst_emulenc_flush_buffers (emulenc, FALSE);
    if (emulenc->opened) {
      gst_emul_avcodec_close (emulenc->context, emulenc->dev);
      emulenc->opened = FALSE;
    }
    gst_adapter_clear (emulenc->adapter);

#if 0
    if (emulenc->flie) {
      fclose (emulenc->file);
      emulenc->file = NULL;
    }
#endif

    if (emulenc->working_buf) {
      g_free (emulenc->working_buf);
      emulenc->working_buf = NULL;
    }
    break;
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
  gint rank = GST_RANK_NONE;
  GList *elem = element;
  CodecElement *codec = NULL;

  if (!elem) {
    return FALSE;
  }

  /* register element */
  do {
    codec = (CodecElement *)(elem->data);
    if (!codec) {
      return FALSE;
    }

    if (codec->codec_type != CODEC_TYPE_ENCODE) {
      continue;
    }

    type_name = g_strdup_printf ("maru_%senc", codec->name);
    type = g_type_from_name (type_name);
    if (!type) {
      type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
      g_type_set_qdata (type, GST_EMULENC_PARAMS_QDATA, (gpointer) codec);
    }

    if (!gst_element_register (plugin, type_name, rank, type)) {
      g_free (type_name);
      return FALSE;
    }
    g_free (type_name);
  } while ((elem = elem->next));

  return TRUE;
}
