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
#include "gstemulutils.h"
#include "gstemulapi.h"

#define GST_EMULDEC_PARAMS_QDATA g_quark_from_static_string("emuldec-params")

/* indicate dts, pts, offset in the stream */
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

  CodecContext *context;
  CodecDevice *dev;

  union {
    struct {
      gint width, height;
      gint clip_width, clip_height;
      gint par_n, par_d;
      gint fps_n, fps_d;
      gint old_fps_n, old_fps_d;
      gboolean interlaced;

      enum PixelFormat pix_fmt;
    } video;
    struct {
      gint channels;
      gint samplerate;
      gint depth;
    } audio;
  } format;

  gboolean discont;
  gboolean clear_ts;

  /* tracking DTS/PTS */
  GstClockTime next_out;

  /* Qos stuff */
  gdouble proportion;
  GstClockTime earliest_time;
  gint64 processed;
  gint64 dropped;


  /* GstSegment can be used for two purposes:
   * 1. performing seeks (handling seek events)
   * 2. tracking playback regions (handling newsegment events)
   */
  GstSegment segment;

  GstTSInfo ts_info[MAX_TS_MASK + 1];
  gint ts_idx;

  /* reverse playback queue */
  GList *queued;

} GstEmulDec;

typedef struct _GstEmulDecClass
{
  GstElementClass parent_class;

  CodecElement *codec;
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

static gboolean gst_emuldec_negotiate (GstEmulDec *dec, gboolean force);

static gint gst_emuldec_frame (GstEmulDec *emuldec, guint8 *data,
                              guint size, gint *got_data,
                              const GstTSInfo *dec_info, GstFlowReturn *ret);

static gboolean gst_emuldec_open (GstEmulDec *emuldec);
static gboolean gst_emuldec_close (GstEmulDec *emuldec);



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

static void
gst_emuldec_reset_ts (GstEmulDec *emuldec)
{
  emuldec->next_out = GST_CLOCK_TIME_NONE;
}

static void
gst_emuldec_update_qos (GstEmulDec *emuldec, gdouble proportion,
  GstClockTime timestamp)
{
  GST_LOG_OBJECT (emuldec, "update QOS: %f, %" GST_TIME_FORMAT,
      proportion, GST_TIME_ARGS (timestamp));

  GST_OBJECT_LOCK (emuldec);
  emuldec->proportion = proportion;
  emuldec->earliest_time = timestamp;
  GST_OBJECT_UNLOCK (emuldec);
}

static void
gst_emuldec_reset_qos (GstEmulDec *emuldec)
{
  gst_emuldec_update_qos (emuldec, 0.5, GST_CLOCK_TIME_NONE);
  emuldec->processed = 0;
  emuldec->dropped = 0;
}

static gboolean
gst_emuldec_do_qos (GstEmulDec *emuldec, GstClockTime timestamp,
  gboolean *mode_switch)
{
  GstClockTimeDiff diff;
  gdouble proportion;
  GstClockTime qostime, earliest_time;
  gboolean res = TRUE;

  *mode_switch = FALSE;

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (timestamp))) {
    emuldec->processed++;
    return TRUE;
  }

  proportion = emuldec->proportion;
  earliest_time = emuldec->earliest_time;

  qostime = gst_segment_to_running_time (&emuldec->segment, GST_FORMAT_TIME,
    timestamp);
  

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (qostime))) {
    emuldec->processed++;
    return TRUE;
  }

  diff = GST_CLOCK_DIFF (qostime, earliest_time);

  if (proportion < 0.4 && diff < 0 ){
    emuldec->processed++;
    return TRUE;
  } else {
    if (diff >= 0) {
//      if (emuldec->waiting_for_key) {
      if (0) {
        res = FALSE;
      } else {
      }

      GstClockTime stream_time, jitter;
      GstMessage *qos_msg;

      emuldec->dropped++;
      stream_time =
          gst_segment_to_stream_time (&emuldec->segment, GST_FORMAT_TIME,
                  timestamp);
      jitter = GST_CLOCK_DIFF (qostime, earliest_time);
      qos_msg =
          gst_message_new_qos (GST_OBJECT_CAST (emuldec), FALSE, qostime,
                  stream_time, timestamp, GST_CLOCK_TIME_NONE);
      gst_message_set_qos_values (qos_msg, jitter, proportion, 1000000);
      gst_message_set_qos_stats (qos_msg, GST_FORMAT_BUFFERS,
              emuldec->processed, emuldec->dropped);
      gst_element_post_message (GST_ELEMENT_CAST (emuldec), qos_msg);

      return res;
    }
  }
}

static void
clear_queued (GstEmulDec *emuldec)
{
  g_list_foreach (emuldec->queued, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (emuldec->queued);
  emuldec->queued = NULL;
}

static GstFlowReturn
flush_queued (GstEmulDec *emuldec)
{
  GstFlowReturn res = GST_FLOW_OK;

  printf("flush queued\n");

  while (emuldec->queued) {
    GstBuffer *buf = GST_BUFFER_CAST (emuldec->queued->data);

    GST_LOG_OBJECT (emuldec, "pushing buffer %p, offset %"
      G_GUINT64_FORMAT ", timestamp %"
      GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT, buf,
      GST_BUFFER_OFFSET (buf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

    res = gst_pad_push (emuldec->srcpad, buf);

    emuldec->queued =
      g_list_delete_link (emuldec->queued, emuldec->queued);
  }

  return res;
}

static void
gst_emuldec_drain (GstEmulDec *emuldec)
{
  GstEmulDecClass *oclass;

  oclass = (GstEmulDecClass *) (G_OBJECT_GET_CLASS (emuldec));

  // TODO: drain
#if 1 
  {
    gint have_data, len, try = 0;

    do {
      GstFlowReturn ret;

      len =
        gst_emuldec_frame (emuldec, NULL, 0, &have_data, &ts_info_none, &ret);
      if (len < 0 || have_data == 0) {
        printf("drain. try: %d\n", try);

        break;
      }
    } while (try++ < 10);
  }
#endif

  if (emuldec->segment.rate < 0.0) {
    flush_queued (emuldec);
  }
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
  CodecElement *codec;
  gchar *longname, *classification;

  codec =
      (CodecElement *)g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
                                      GST_EMULDEC_PARAMS_QDATA);

  longname = g_strdup_printf ("%s Decoder", codec->longname);
  classification = g_strdup_printf ("Codec/Decoder/%s",
                    (codec->media_type == AVMEDIA_TYPE_VIDEO) ?
                    "Video" : "Audio");

  gst_element_class_set_details_simple (element_class,
            longname,                                     // longname
            classification,                               // classification
            "accelerated codec for Tizen Emulator",       // description
            "Kitae Kim <kt920.kim@samsung.com>");         // author

  g_free (longname);
  g_free (classification);

  sinkcaps = gst_emul_codecname_to_caps (codec->name, NULL, FALSE);
  if (!sinkcaps) {
    sinkcaps = gst_caps_from_string ("unknown/unknown");
  }

  switch (codec->media_type) {
  case AVMEDIA_TYPE_VIDEO:
    srccaps = gst_caps_from_string ("video/x-raw-rgb; video/x-raw-yuv");
    break;
  case AVMEDIA_TYPE_AUDIO:
    srccaps = gst_emul_codectype_to_audio_caps (NULL, codec->name, FALSE, codec);
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

  klass->codec = codec;
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

  emuldec->srcpad = gst_pad_new_from_template (oclass->srctempl, "src") ;
  gst_pad_use_fixed_caps (emuldec->srcpad);
  gst_pad_set_event_function (emuldec->srcpad,
    GST_DEBUG_FUNCPTR(gst_emuldec_src_event));

  gst_element_add_pad (GST_ELEMENT(emuldec), emuldec->sinkpad);
  gst_element_add_pad (GST_ELEMENT(emuldec), emuldec->srcpad);

  // init
  emuldec->context = g_malloc0 (sizeof(CodecContext));
  if (!emuldec->context) {
    printf("failed to allocate memory.\n");
  }
  emuldec->dev = g_malloc0 (sizeof(CodecDevice));
  if (!emuldec->dev) {
    printf("failed to allocate memory.\n");
  }

  emuldec->format.video.par_n = -1;
  emuldec->format.video.fps_n = -1;
  emuldec->format.video.old_fps_n = -1;

  emuldec->queued = NULL;
  gst_segment_init (&emuldec->segment, GST_FORMAT_TIME);
}

static void
gst_emuldec_finalize (GObject *object)
{
  // Deinit Decoder
  GstEmulDec *emuldec = (GstEmulDec *) object;

  printf ("gst_emuldec_finalize\n");
  if (emuldec->context != NULL) {
    g_free (emuldec->context);
    emuldec->context = NULL;
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
    /* Quality Of Service (QOS) event contains a report
      about the current real-time performance of the stream.*/
  case GST_EVENT_QOS:
  {
    gdouble proportion;
    GstClockTimeDiff diff;
    GstClockTime timestamp;

  	printf("GST_EVENT_QOS\n");
    gst_event_parse_qos (event, &proportion, &diff, &timestamp);
    GST_LOG_OBJECT (emuldec, "update QOS: %f, %" GST_TIME_FORMAT,
      proportion, GST_TIME_ARGS (timestamp));

    /* update our QoS values */
    gst_emuldec_update_qos (emuldec, proportion, timestamp + diff);

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

static gboolean
gst_emuldec_sink_event (GstPad *pad, GstEvent *event)
{
  GstEmulDec *emuldec;
  gboolean ret = FALSE;

  emuldec = (GstEmulDec *) gst_pad_get_parent (pad);

  GST_DEBUG_OBJECT (emuldec, "Handling %s event",
    GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_EOS:
  	printf("GST_EVENT_EOS\n");
    gst_emuldec_drain (emuldec);
    break;
  case GST_EVENT_FLUSH_STOP:
  {
  	printf("GST_EVENT_FLUSH_STOP\n");

    gst_emuldec_reset_ts (emuldec);
//    gst_emuldec_reset_qos (emuldec);
    gst_segment_init (&emuldec->segment, GST_FORMAT_TIME);

    /* clear queue */
    clear_queued (emuldec);
  }
    break;
  case GST_EVENT_NEWSEGMENT:
  {
    gboolean update;
    GstFormat format;
    gint64 start, stop, time;
    gdouble rate, arate;

    printf ("GST_EVENT_NEWSEGMENT\n");
    gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
        &start, &stop, &time);

    switch (format) {
    case GST_FORMAT_TIME:
      printf ("GST_FORMAT_TIME\n");
      break;
    case GST_FORMAT_BYTES:
    {
      gint bit_rate;
      bit_rate = emuldec->context->audio.bit_rate;

      printf ("GST_FORMAT_BYTES\n");
      if (!bit_rate) {
        // no_bitrate
        GST_WARNING_OBJECT (emuldec, "no bitrate to convert BYTES to TIME");
        gst_event_unref (event);
        gst_object_unref (emuldec);
        return ret;
      }

      GST_DEBUG_OBJECT (emuldec, "bitrate: %d", bit_rate);

      if (start != -1) {
        start = gst_util_uint64_scale_int (start, GST_SECOND, bit_rate);
      }
      if (stop != -1) {
        stop = gst_util_uint64_scale_int (stop, GST_SECOND, bit_rate);
      }
      if (time != -1) {
        time = gst_util_uint64_scale_int (time, GST_SECOND, bit_rate);
      }

      gst_event_unref (event);

      format = GST_FORMAT_TIME;

      stop = -1;
      event = gst_event_new_new_segment (update, rate, format,
          start, stop, time);
      break;
    }
    default:
      // invlalid format
      break;
    }

#if 1 
    if (emuldec->context->codec) {
      gst_emuldec_drain (emuldec);
    }
#endif

    gst_segment_set_newsegment_full (&emuldec->segment, update,
        rate, arate, format, start, stop, time);
    break;
  }
  default:
    break;
  }

  ret = gst_pad_push_event (emuldec->srcpad, event);

  gst_object_unref (emuldec);

  return ret;
}



static gboolean
gst_emuldec_setcaps (GstPad *pad, GstCaps *caps)
{
  GstEmulDec *emuldec;
  GstEmulDecClass *oclass;
  GstStructure *structure;
  const GValue *par;
  const GValue *fps;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (pad, "setcaps called.");

  emuldec = (GstEmulDec *) (gst_pad_get_parent (pad));
  oclass = (GstEmulDecClass *) (G_OBJECT_GET_CLASS (emuldec));

  GST_OBJECT_LOCK (emuldec);

#if 0
  if (emuldec->opened) {
    GST_OBJECT_UNLOCK (emuldec);
    gst_emuldec_drain (emuldec);
    GST_OBJECT_LOCK (emuldec);
    gst_emuldec_close (emuldec);
  }
#endif

  GST_LOG_OBJECT (emuldec, "size %dx%d", emuldec->context->video.width,
      emuldec->context->video.height);

  gst_emul_caps_with_codecname (oclass->codec->name, oclass->codec->media_type,
      caps, emuldec->context);

  GST_LOG_OBJECT (emuldec, "size after %dx%d", emuldec->context->video.width,
      emuldec->context->video.height);

  if (!emuldec->context->video.fps_d || !emuldec->context->video.fps_n) {
    GST_DEBUG_OBJECT (emuldec, "forcing 25/1 framerate");
    emuldec->context->video.fps_n = 1;
    emuldec->context->video.fps_d = 25;
  }

  structure = gst_caps_get_structure (caps, 0);

  par = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if (par) {
    GST_DEBUG_OBJECT (emuldec, "sink caps have pixel-aspect-ratio of %d:%d",
        gst_value_get_fraction_numerator (par),
        gst_value_get_fraction_denominator (par));
#if 0 // TODO
    if (emuldec->par) {
      g_free(emuldec->par);
    }
    emuldec->par = g_new0 (GValue, 1);
    gst_value_init_and_copy (emuldec->par, par);
#endif
  }

  fps = gst_structure_get_value (structure, "framerate");
  if (fps != NULL && GST_VALUE_HOLDS_FRACTION (fps)) {
    emuldec->format.video.fps_n = gst_value_get_fraction_numerator (fps);
    emuldec->format.video.fps_d = gst_value_get_fraction_denominator (fps);
    GST_DEBUG_OBJECT (emuldec, "Using framerate %d/%d from incoming",
        emuldec->format.video.fps_n, emuldec->format.video.fps_d);
  } else {
    emuldec->format.video.fps_n = -1;
    GST_DEBUG_OBJECT (emuldec, "Using framerate from codec");
  }

  if (strcmp (oclass->codec->name, "aac") == 0) {
    const gchar *format = gst_structure_get_string (structure, "stream-format");
#if 0
    if (format == NULL || strcmp ("format", "raw") == 0) {
      emuldec->turnoff_parser = TRUE;
    }
#endif
  }

  if (!gst_emuldec_open (emuldec)) {
    GST_DEBUG_OBJECT (emuldec, "Failed to open");
#if 0
    if (emuldec->par) {
      g_free(emuldec->par);
      emuldec->par = NULL;
    }
#endif
    GST_OBJECT_UNLOCK (emuldec);
    gst_object_unref (emuldec);

    return FALSE;
  }

  gst_structure_get_int (structure, "width",
    &emuldec->format.video.clip_width);
  gst_structure_get_int (structure, "height",
    &emuldec->format.video.clip_height);

  GST_DEBUG_OBJECT (pad, "clipping to %dx%d",
    emuldec->format.video.clip_width, emuldec->format.video.clip_height);

  GST_OBJECT_UNLOCK (emuldec);
  gst_object_unref (emuldec);

  return ret;
}

static GStaticMutex gst_avcodec_mutex = G_STATIC_MUTEX_INIT;

int
gst_emul_avcodec_open (CodecContext *ctx, CodecElement *codec, CodecDevice *dev)
{
  int ret;

  g_static_mutex_lock (&gst_avcodec_mutex);

  if (gst_emul_codec_device_open (dev) < 0) {
    printf("failed to open device.\n");
    return -1;
  }
  ret = emul_avcodec_init (ctx, codec, dev);
  g_static_mutex_unlock (&gst_avcodec_mutex);

  return ret;
}

int
gst_emul_avcodec_close (CodecContext *ctx, CodecDevice *dev)
{
  int ret;

  g_static_mutex_lock (&gst_avcodec_mutex);

  printf ("gst_emul_avcodec_close\n");
  ret = emul_avcodec_deinit (ctx, dev);

  gst_emul_codec_device_close (dev);
  g_static_mutex_unlock (&gst_avcodec_mutex);

  return ret;
}

static gboolean
gst_emuldec_open (GstEmulDec *emuldec)
{
  GstEmulDecClass *oclass;
  int width, height, buf_size;

  oclass = (GstEmulDecClass *) (G_OBJECT_GET_CLASS (emuldec));

  if (!emuldec->dev) {
    return FALSE;
  }

  switch (oclass->codec->media_type) {
  case AVMEDIA_TYPE_VIDEO:
    width = emuldec->context->video.width;
    height = emuldec->context->video.height;
    buf_size = gst_emul_avpicture_size (0, width, height);
    break;
  case AVMEDIA_TYPE_AUDIO:
//    buf_size = FF_MAX_AUDIO_FRAME_SIZE;
    buf_size = 1024 * 256; // 256K
    break;
  default:
    buf_size = -1;
    break;
  }

  if (buf_size < 0) {
    return FALSE;
  }

  emuldec->dev->buf_size = buf_size;

  if (gst_emul_avcodec_open (emuldec->context, oclass->codec, emuldec->dev) < 0) {
    gst_emuldec_close (emuldec);
    GST_DEBUG_OBJECT (emuldec, "tzdec_%s: Failed to open codec",
        oclass->codec->name);
  }

#if 0
  emuldec->opened = TRUE;
#endif

  GST_LOG_OBJECT (emuldec, "Opened codec %s", oclass->codec->name);

  switch (oclass->codec->media_type) {
  case AVMEDIA_TYPE_VIDEO:
    emuldec->format.video.width = 0;
    emuldec->format.video.height = 0;
    emuldec->format.video.clip_width = -1;
    emuldec->format.video.clip_height = -1;
    emuldec->format.video.pix_fmt = PIX_FMT_NB;
    emuldec->format.video.interlaced = FALSE;
    break;
  case AVMEDIA_TYPE_AUDIO:
    emuldec->format.audio.samplerate = 0;
    emuldec->format.audio.channels = 0;
    emuldec->format.audio.depth = 0;
    break;
  default:
    break;
  }

  gst_emuldec_reset_ts (emuldec);

  emuldec->proportion = 0.0;
  emuldec->earliest_time = -1;

  return TRUE;
}

static gboolean
gst_emuldec_close (GstEmulDec *emuldec)
{
  int ret;

  printf ("gst_emuldec_close\n");
  gst_emul_avcodec_close (emuldec->context, emuldec->dev);

  return TRUE;
}


static gboolean
gst_emuldec_negotiate (GstEmulDec *emuldec, gboolean force)
{
  GstEmulDecClass *oclass;
  GstCaps *caps;

  oclass = (GstEmulDecClass *) (G_OBJECT_GET_CLASS (emuldec));

  switch (oclass->codec->media_type) {
  case AVMEDIA_TYPE_VIDEO:
    if (!force && emuldec->format.video.width == emuldec->context->video.width
      && emuldec->format.video.height == emuldec->context->video.height
      && emuldec->format.video.fps_n == emuldec->format.video.old_fps_n
      && emuldec->format.video.fps_d == emuldec->format.video.old_fps_d
      && emuldec->format.video.pix_fmt == emuldec->context->video.pix_fmt
      && emuldec->format.video.par_n == emuldec->context->video.par_n
      && emuldec->format.video.par_d == emuldec->context->video.par_d) {
      return TRUE;
    }
    emuldec->format.video.width = emuldec->context->video.width;
    emuldec->format.video.height = emuldec->context->video.height;
    emuldec->format.video.old_fps_n = emuldec->format.video.fps_n;
    emuldec->format.video.old_fps_d = emuldec->format.video.fps_d;
    emuldec->format.video.pix_fmt = emuldec->context->video.pix_fmt;
    emuldec->format.video.par_n = emuldec->context->video.par_n;
    emuldec->format.video.par_d = emuldec->context->video.par_d;
    break;
  case AVMEDIA_TYPE_AUDIO:
  {
    gint depth = gst_emul_smpfmt_depth (emuldec->context->audio.sample_fmt);
    if (!force && emuldec->format.audio.samplerate ==
      emuldec->context->audio.sample_rate &&
      emuldec->format.audio.channels == emuldec->context->audio.channels &&
      emuldec->format.audio.depth == depth) {
      return TRUE;
    }
    emuldec->format.audio.samplerate = emuldec->context->audio.sample_rate;
    emuldec->format.audio.channels = emuldec->context->audio.channels;
    emuldec->format.audio.depth = depth;
  }
    break;
  default:
    break;
  }



  caps =
    gst_emul_codectype_to_caps (oclass->codec->media_type, emuldec->context,
      oclass->codec->name, FALSE);

  if (caps == NULL) {
    GST_ELEMENT_ERROR (emuldec, CORE, NEGOTIATION,
      ("Could not find GStreamer caps mapping for codec '%s'.",
      oclass->codec->name), (NULL));
    return FALSE;
  }

  switch (oclass->codec->media_type) {
  case AVMEDIA_TYPE_VIDEO:
  {
    gint width, height;
    gboolean interlaced;

    width = emuldec->format.video.clip_width;
    height = emuldec->format.video.clip_height;
    interlaced = emuldec->format.video.interlaced;

    if (width != -1 && height != -1) {
      if (width < emuldec->context->video.width) {
        gst_caps_set_simple (caps, "width", G_TYPE_INT, width, NULL);
      }
      if (height < emuldec->context->video.height) {
          gst_caps_set_simple (caps, "height", G_TYPE_INT, height, NULL);
      }
      gst_caps_set_simple (caps, "interlaced", G_TYPE_BOOLEAN, interlaced,
        NULL);

      if (emuldec->format.video.fps_n != -1) {
          gst_caps_set_simple (caps, "framerate",
            GST_TYPE_FRACTION, emuldec->format.video.fps_n,
            emuldec->format.video.fps_d, NULL);
      }
#if 0
      gst_emuldec_add_pixel_aspect_ratio (emuldec,
        gst_caps_get_structure (caps, 0));
#endif
    }
  }
    break;
  case AVMEDIA_TYPE_AUDIO:
  default:
    break;
  }

  if (!gst_pad_set_caps (emuldec->srcpad, caps)) {
    GST_ELEMENT_ERROR (emuldec, CORE, NEGOTIATION, (NULL),
      ("Could not set caps for decoder (%s), not fixed?",
      oclass->codec->name));
    gst_caps_unref (caps);
    return FALSE;
  }

  gst_caps_unref (caps);

  return TRUE;
}

GstBuffer *
new_aligned_buffer (gint size, GstCaps *caps)
{
  GstBuffer *buf;

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = g_malloc0 (size);
  GST_BUFFER_SIZE (buf) = size;
  GST_BUFFER_FREE_FUNC (buf) = g_free;
  if (caps) {
    gst_buffer_set_caps (buf, caps);
  }

  return buf;
}

static gboolean
clip_audio_buffer (GstEmulDec *dec, GstBuffer *buf,
    GstClockTime in_ts, GstClockTime in_dur)
{
  GstClockTime stop;
  gint64 diff, cstart, cstop;
  gboolean res = TRUE;

  if (G_UNLIKELY (dec->segment.format != GST_FORMAT_TIME)) {
    GST_LOG_OBJECT (dec, "%sdropping", (res ? "not " : ""));
    return res;
  }

  // in_ts: in_timestamp. check a start time.
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (in_ts))) {
    GST_LOG_OBJECT (dec, "%sdropping", (res ? "not " : ""));
    return res;
  }

  stop =
    GST_CLOCK_TIME_IS_VALID (in_dur) ? (in_ts + in_dur) : GST_CLOCK_TIME_NONE;

  res = gst_segment_clip (&dec->segment, GST_FORMAT_TIME, in_ts,
                          stop, &cstart, &cstop);
  if (G_UNLIKELY (!res)) {
    GST_LOG_OBJECT (dec, "out of segment");
    GST_LOG_OBJECT (dec, "%sdropping", (res ? "not " : ""));
    return res;
  }

  if (G_UNLIKELY ((diff = cstart - in_ts) > 0)) {
    diff =
      gst_util_uint64_scale_int (diff, dec->format.audio.samplerate,
      GST_SECOND * (dec->format.audio.depth * dec->format.audio.channels));

    GST_DEBUG_OBJECT (dec, "clipping start to %" GST_TIME_FORMAT " %"
        G_GINT64_FORMAT " bytes", GST_TIME_ARGS (cstart), diff);

    GST_BUFFER_SIZE (buf) -= diff;
    GST_BUFFER_DATA (buf) += diff;
  }

  if (G_UNLIKELY ((diff = stop - cstop) > 0)) {
    diff =
      gst_util_uint64_scale_int (diff, dec->format.audio.samplerate,
      GST_SECOND * (dec->format.audio.depth * dec->format.audio.channels));

    GST_DEBUG_OBJECT (dec, "clipping stop to %" GST_TIME_FORMAT " %"
        G_GINT64_FORMAT " bytes", GST_TIME_ARGS (cstop), diff);

    GST_BUFFER_SIZE (buf) -= diff;
  }

  GST_BUFFER_TIMESTAMP (buf) = cstart;
  GST_BUFFER_DURATION (buf) = cstop - cstart;

  GST_LOG_OBJECT (dec, "%sdropping", (res ? "not " : ""));
  return res;
}

static gint
gst_emuldec_video_frame (GstEmulDec *emuldec, guint8 *data, guint size,
		const GstTSInfo *dec_info, GstBuffer **outbuf,
		GstFlowReturn *ret)
{
  gint len = -1;
  GstClockTime out_timestamp, out_duration;
  gint64 out_offset;

#if 0
  len =
    emul_avcodec_decode_video (emuldec->context, data, size,
    		(int16_t *) GST_BUFFER_DATA (*outbuf), &have_data, emuldec->dev);
#endif

  return len;
}

static gint
gst_emuldec_audio_frame (GstEmulDec *emuldec, CodecElement *codec,
                          guint8 *data, guint size,
                          const GstTSInfo *dec_info, GstBuffer **outbuf,
                          GstFlowReturn *ret)
{
  gint len = -1;
  gint have_data = FF_MAX_AUDIO_FRAME_SIZE;
  GstClockTime out_timestamp, out_duration;
  gint64 out_offset;

  *outbuf =
      new_aligned_buffer (FF_MAX_AUDIO_FRAME_SIZE,
          GST_PAD_CAPS (emuldec->srcpad));

  len = emul_avcodec_decode_audio (emuldec->context,
      (int16_t *) GST_BUFFER_DATA (*outbuf), &have_data,
      data, size, emuldec->dev);

  GST_DEBUG_OBJECT (emuldec,
    "Decode audio: len=%d, have_data=%d", len, have_data);

  if (len >= 0 && have_data > 0) {
    GST_DEBUG_OBJECT (emuldec, "Creating output buffer");
    if (!gst_emuldec_negotiate (emuldec, FALSE)) {
      gst_buffer_unref (*outbuf);
      *outbuf = NULL;
      len = -1;
      GST_DEBUG_OBJECT (emuldec, "return flow %d, out %p, len %d",
        *ret, *outbuf, len);
      return len;
    }

    GST_BUFFER_SIZE (*outbuf) = have_data;

    if (GST_CLOCK_TIME_IS_VALID (dec_info->timestamp)) {
      out_timestamp = dec_info->timestamp;
    } else {
      out_timestamp = emuldec->next_out;
    }

    out_duration = gst_util_uint64_scale (have_data, GST_SECOND,
        emuldec->format.audio.depth * emuldec->format.audio.channels *
        emuldec->format.audio.samplerate);

    out_offset = dec_info->offset;

    GST_DEBUG_OBJECT (emuldec,
        "Buffer created. Size: %d, timestamp: %" GST_TIME_FORMAT
        ", duration: %" GST_TIME_FORMAT, have_data,
        GST_TIME_ARGS (out_timestamp), GST_TIME_ARGS (out_duration));

    GST_BUFFER_TIMESTAMP (*outbuf) = out_timestamp;
    GST_BUFFER_DURATION (*outbuf) = out_duration;
    GST_BUFFER_OFFSET (*outbuf) = out_offset;
    gst_buffer_set_caps (*outbuf, GST_PAD_CAPS (emuldec->srcpad));

    if (GST_CLOCK_TIME_IS_VALID (out_timestamp)) {
      emuldec->next_out = out_timestamp + out_duration;
    }

    if (G_UNLIKELY (!clip_audio_buffer (emuldec, *outbuf,
        out_timestamp, out_duration))) {
      GST_DEBUG_OBJECT (emuldec, "buffer_clipped");
      gst_buffer_unref (*outbuf);
      *outbuf = NULL;
      GST_DEBUG_OBJECT (emuldec, "return flow %d, out %p, len %d", *ret, *outbuf, len);
      return len;
    }
  } else {
    gst_buffer_unref (*outbuf);
    *outbuf = NULL;
  }

  if (len == -1 && !strcmp(codec->name, "aac")) {
    GST_ELEMENT_ERROR (emuldec, STREAM, DECODE, (NULL),
        ("Decoding of AAC stream by gst-emul-codec failed."));
    *ret = GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (emuldec, "return flow %d, out %p, len %d",
    *ret, *outbuf, len);
  return len;
}

static gint
gst_emuldec_frame (GstEmulDec *emuldec, guint8 *data, guint size,
		gint *got_data, const GstTSInfo *dec_info, GstFlowReturn *ret)
{
  GstEmulDecClass *oclass;
  GstBuffer *outbuf = NULL;
  gint have_data = 0, len = 0;

  if (G_UNLIKELY (emuldec->context->codec == NULL)) {
    // no_codec
    GST_ERROR_OBJECT (emuldec, "no codec context");
    return -1;
  }

  *ret = GST_FLOW_OK;

  oclass = (GstEmulDecClass *) (G_OBJECT_GET_CLASS (emuldec));

  switch (oclass->codec->media_type) {
  case AVMEDIA_TYPE_VIDEO:
    len = gst_emuldec_video_frame (emuldec, data, size,
    		dec_info, &outbuf, ret);
    break;
  case AVMEDIA_TYPE_AUDIO:
    len = gst_emuldec_audio_frame (emuldec, oclass->codec, data, size,
    		dec_info, &outbuf, ret);
    if (outbuf == NULL && emuldec->discont) {
      GST_DEBUG_OBJECT (emuldec, "no buffer but keeping timestamp");
//      emuldec->clear_ts = FALSE;
    }
    break;
  default:
    GST_ERROR_OBJECT (emuldec, "Asked to decode non-audio/video frame!");
    g_assert_not_reached ();
    break;
  }

  if (outbuf) {
    have_data = 1;
  }

  if (len < 0 || have_data < 0) {
    GST_WARNING_OBJECT (emuldec,
    		"tzdec_%s: decoding error (len: %d, have_data: %d)",
    		oclass->codec->name, len, have_data);
    *got_data = 0;
    return len;
  } else if (len == 0 && have_data == 0) {
    *got_data = 0;
    return len;
  } else {
    *got_data = 1;
  }

  if (outbuf) {
    GST_LOG_OBJECT (emuldec,
        "Decoded data, now pushing buffer %p with offset %" G_GINT64_FORMAT
        ", timestamp %" GST_TIME_FORMAT " and duration %" GST_TIME_FORMAT,
        outbuf, GST_BUFFER_OFFSET (outbuf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

    if (emuldec->discont) {
      /* GST_BUFFER_FLAG_DISCONT :
       * the buffer marks a data discontinuity in the stream. This typically
       * occurs after a seek or a dropped buffer from a live or network source.
       */
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
      emuldec->discont = FALSE;
    }

    if (emuldec->segment.rate > 0.0) {
      // push forward
      *ret = gst_pad_push (emuldec->srcpad, outbuf);
    } else {
      // push reverse
      printf("push reverse.\n");
      emuldec->queued = g_list_prepend (emuldec->queued, outbuf);
      *ret = GST_FLOW_OK;
    }
  } else {
    GST_DEBUG_OBJECT (emuldec, "Didn't get a decoded buffer");
  }

  return len;
}

static GstFlowReturn
gst_emuldec_chain (GstPad *pad, GstBuffer *buffer)
{
  GstEmulDec *emuldec;
  GstEmulDecClass *oclass;
  guint8 *in_buf;
  guint in_size, len, have_data;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime in_timestamp;
  GstClockTime in_duration;
  gboolean discont;
  gint64 in_offset;
  const GstTSInfo *in_info;
  const GstTSInfo *dec_info;

  emuldec = (GstEmulDec *) (GST_PAD_PARENT (pad));

#if 0
  if (G_UNLIKELY (!emuldec->opened)) {
    // not_negotiated
    oclass = (GstEmulDecClass *) (G_OBJECT_GET_CLASS (emuldec));
    GST_ELEMENT_ERROR (emuldec, CORE, NEGOTIATION, (NULL),
      ("tzdec_%s: input format was not set before data start",
    		oclass->codec->name));
    gst_buffer_unref (buffer);
    return GST_FLOW_NOT_NEGOTIATED;
  }
#endif

  discont = GST_BUFFER_IS_DISCONT (buffer);

// FIXME
  if (G_UNLIKELY (discont)) {
    GST_DEBUG_OBJECT (emuldec, "received DISCONT");
    printf("discont 1\n");
    gst_emuldec_drain (emuldec);
    printf("discont 2\n");
//    gst_emuldec_flush_pcache (emuldec);
    // flush buffers
    emuldec->discont = TRUE;
    gst_emuldec_reset_ts (emuldec);
  }
//  emuldec->clear_ts = TRUE;

  oclass = (GstEmulDecClass *) (G_OBJECT_GET_CLASS (emuldec));
#if 0
  if (G_UNLIKELY (emuldec->waiting_for_key)) {
    if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT) &&
      oclass->codec->media_type != AVMEDIA_TYPE_AUDIO) {
      // skip_keyframe
    }
    emuldec->waiting_for_key = FALSE;
  }

  if (emuldec->pcache) {
	  GST_LOG_OBJECT (emuldec, "join parse cache");
	  buffer = gst_buffer_join (emuldec->pcache, buffer);
	  emuldec->pcache = NULL;
  }
#endif

  in_timestamp = GST_BUFFER_TIMESTAMP (buffer);
  in_duration = GST_BUFFER_DURATION (buffer);
  in_offset = GST_BUFFER_OFFSET (buffer);

  in_info = gst_ts_info_store (emuldec, in_timestamp, in_duration, in_offset);

#if 0
  if (in_timestamp != -1) {
    if (!emuldec->reordered_in && emuldec->last_in != -1) {
      if (in_timestamp < emuldec->last_in) {
        GST_LOG_OBJECT (emuldec, "detected reordered input timestamps");
        emuldec->reordered_in = TRUE;
        emuldec->last_diff = GST_CLOCK_TIME_NONE;
      } else if (in_timestamp > emuldec->last_in) {
        GstClockTime diff;
        diff = in_timestamp - emuldec->last_in;
        if (emuldec->last_frames) {
          diff /= emuldec->last_frames;
        }

        GST_LOG_OBJECT (emuldec, "estimated duration %" GST_TIME_FORMAT " %u",
    		  GST_TIME_ARGS (diff), emuldec->last_frames);

        emuldec->last_diff = diff;
      }
    }
    emuldec->last_in = in_timestamp;
    emuldec->last_frames;
  }
#endif

  in_buf = GST_BUFFER_DATA (buffer);
  in_size = GST_BUFFER_SIZE (buffer);

  dec_info = in_info;

  len =
		gst_emuldec_frame (emuldec, in_buf, in_size, &have_data, dec_info, &ret);

  gst_buffer_unref (buffer);

  return ret;
}

static GstStateChangeReturn
gst_emuldec_change_state (GstElement *element, GstStateChange transition)
{
  GstEmulDec *emuldec = (GstEmulDec *) element;
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
  case GST_STATE_CHANGE_PAUSED_TO_READY:
    GST_OBJECT_LOCK (emuldec);
    gst_emuldec_close (emuldec);
    GST_OBJECT_UNLOCK (emuldec);

    /* clear queue */
    clear_queued (emuldec);
    break;
  default:
    break;
  }

  return ret;
}

gboolean
gst_emuldec_register (GstPlugin *plugin, GList *element)
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
  gboolean ret = TRUE;
  GList *elem = element;
  CodecElement *codec = NULL;

  /* register element */
  while ((elem = g_list_next (elem))) {
    codec = (CodecElement *)elem->data;
    if (!codec) {
      ret = FALSE;
      break;
    }

    if (codec->codec_type != CODEC_TYPE_DECODE) {
      continue;
    }

    type_name = g_strdup_printf ("tzdec_%s", codec->name);
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
