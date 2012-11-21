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
#include "gstemulcommon.h"

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

  guint extradata_size;
  guint8 *extradata;

  CodecDev codecbuf;
} GstEmulEnc;

typedef struct _GstEmulEncClass
{
  GstElementClass parent_class;

  GstPadTemplate *sinktempl;
  GstPadTemplate *srctempl;
} GstEmulEncClass;

static GstElementClass *parent_class = NULL;

static void gst_emulenc_base_init (GstEmulEncClass *klass);
static void gst_emulenc_class_init (GstEmulEncClass *klass);
static void gst_emulenc_init (GstEmulEnc *emulenc);
static void gst_emulenc_finalize (GObject *object);

static gboolean gst_emulenc_setcaps (GstPad *pad, GstCaps *caps);
static gboolean gst_emulenc_sink_event (GstPad *pad, GstEvent *event);
static GstFlowReturn gst_emulenc_chain (GstPad *pad, GstBuffer *buffer);

static gboolean gst_emulenc_src_event (GstPad *pad, GstEvent *event);
static GstStateChangeReturn gst_emulenc_change_state (GstElement *element, GstStateChange transition);

int gst_emul_codec_init (GstEmulEnc *emulenc);
void gst_emul_codec_deinit (GstEmulEnc *emulenc);
int gst_emul_codec_encode_video (GstEmulEnc *emulenc, guint8 *in_buf, guint in_size,
        GstBuffer **out_buf, GstClockTime in_timestamp);

int gst_emul_codec_dev_open (GstEmulEnc *emulenc);


#define GST_EMULENC_PARAMS_QDATA g_quark_from_static_string("emulenc-params"); 

/*
 * Implementation
 */
    static void
gst_emulenc_base_init (GstEmulEncClass *klass)
{
    GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
    GstCaps *sinkcaps, *srccaps;
    GstPadTemplate *sinktempl, *srctempl;
    const char *mimetype = "video/x-h264";
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

    sinkcaps = gst_caps_new_simple (mimetype,
            "width", GST_TYPE_INT_RANGE, 16, 4096,
            "height", GST_TYPE_INT_RANGE, 16, 4096,
            "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
    if (!sinkcaps) {
        sinkcaps = gst_caps_from_string ("unknown/unknown");
    }

    // if type is video
    srccaps = gst_caps_from_string ("video/x-raw-rgb; video/x-raw-yuv");
    // otherwise
    // srcaps = gst_emul_codectype_to_audio_caps ();

    if (!srccaps) {
        srccaps = gst_caps_from_string ("unknown/unknown");
    }

    sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
            GST_PAD_ALWAYS, sinkcaps);
    srctempl = gst_pad_template_new ("src", GST_PAD_SRC,
            GST_PAD_ALWAYS, srccaps);

    gst_element_class_add_pad_template (element_class, srctempl);
    gst_element_class_add_pad_template (element_class, sinktempl);

    klass->sinktempl = sinktempl;
    klass->srctempl = srctempl;
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
    gst_pad_set_event_function (emulenc->sinkpad,
            GST_DEBUG_FUNCPTR(gst_emulenc_sink_event));
    gst_pad_set_chain_function (emulenc->sinkpad,
            GST_DEBUG_FUNCPTR(gst_emulenc_chain));
    gst_element_add_pad (GST_ELEMENT(emulenc), emulenc->sinkpad);

    emulenc->srcpad = gst_pad_new_from_template (oclass->srctempl, "src") ;
    gst_pad_use_fixed_caps (emulenc->srcpad);
    gst_pad_set_event_function (emulenc->srcpad,
            GST_DEBUG_FUNCPTR(gst_emulenc_src_event));
    gst_element_add_pad (GST_ELEMENT(emulenc), emulenc->srcpad);
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

    GST_OBJECT_LOCK (emulenc);

    gst_emulenc_get_caps (emulenc, caps);

#if 0
    if (!emulenc->format.video.framerate_den ||
            !emulenc->format.video.framerate_num) {
        emulenc->format.video.framerate_num = 1; 
        emulenc->format.video.framerate_den = 25;
    }
#endif

    if (gst_emul_codec_dev_open (emulenc) < 0) {
        CODEC_LOG(1, "failed to access %s or mmap operation\n", CODEC_DEV);
        GST_OBJECT_UNLOCK (emulenc);
        gst_object_unref (emulenc);
        return FALSE;
    }

    if (gst_emul_codec_init (emulenc) < 0) {
        CODEC_LOG(1, "cannot initialize codec\n");
        GST_OBJECT_UNLOCK (emulenc);
        gst_object_unref (emulenc);
        return FALSE;
    }

#if 0 /* open a parser */
    gst_emul_codec_parser (emulenc);
#endif

    GST_OBJECT_UNLOCK (emulenc);

    gst_object_unref (emulenc);

    return ret;
}

    static gboolean
gst_emulenc_sink_event (GstPad *pad, GstEvent *event)
{
    GstEmulEnc *emulenc;
    gboolean ret = FALSE;

    emulenc = (GstEmulEnc *) gst_pad_get_parent (pad);
#if 0
    switch (GST_TYPE_EVENT (event)) {
        case GST_EVENT_EOS:
            CODEC_LOG(2, "received GST_EVENT_EOS\n");
            break;
        case GST_EVENT_NEWSEGMENT:
            CODEC_LOG(2, "received GST_EVENT_NEWSEGMENT\n");
            break;
    }
    ret = gst_pad_push_event (emulenc->srcpad, event);
#endif

    gst_object_unref (emulenc);

    return ret;
}

    static GstFlowReturn
gst_emulenc_chain (GstPad *pad, GstBuffer *buffer)
{
    GstEmulEnc *emulenc;
    guint8 *in_buf = NULL;
    GstBuffer *out_buf;
    gint in_size = 0;
    GstClockTime in_timestamp;
    GstFlowReturn ret = GST_FLOW_OK;

    emulenc = (GstEmulEnc *) (GST_PAD_PARENT (pad));

    in_size = GST_BUFFER_SIZE (buffer);
    in_buf = GST_BUFFER_DATA (buffer);
    in_timestamp = GST_BUFFER_TIMESTAMP (buffer);

    gst_emul_codec_encode_video (emulenc, in_buf, in_size, &out_buf, in_timestamp);

    CODEC_LOG(1, "out_buf:%p, ret:%d\n", out_buf, ret);

    ret = gst_pad_push (emulenc->srcpad, out_buf);

    //  g_free (out_buf);

    gst_buffer_unref (buffer);

    return ret;
}

    static GstStateChangeReturn
gst_emulenc_change_state (GstElement *element, GstStateChange transition)
{
    GstEmulEnc *emulenc = (GstEmulEnc*)element;
    GstStateChangeReturn ret;

    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

    switch (transition) {
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            gst_emul_codec_deinit (emulenc);
        default:
            break;
    }

    return ret;
}

    gboolean
gst_emulenc_register (GstPlugin *plugin)
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

    /* register element */
    {
        int codec_fd, codec_cnt = 0;
        int func_type = CODEC_QUERY;
        int index = 0, size = 0;
        void *buf;
        CodecInfo *codec_info;

        codec_fd = open(CODEC_DEV, O_RDWR);
        if (codec_fd < 0) {
            perror("failed to open codec device");
            return FALSE;
        }

        printf("[codec] fd:%d\n", codec_fd);
        buf = mmap (NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, codec_fd, 0);
        if (!buf) {
            perror("failed to mmap");
        }

        printf("[codec] mmap:%p\n", buf);

        write (codec_fd, &func_type, sizeof(int));
        memcpy (&codec_cnt, buf, sizeof(int));
        size = sizeof(uint32_t);

        codec_info = g_malloc0 (codec_cnt * sizeof(CodecInfo));
        for (; index < codec_cnt; index++) {
            memcpy(&codec_info[index].media_type, (uint8_t *)buf + size, sizeof(uint16_t));
            size += sizeof(uint16_t);
            memcpy(&codec_info[index].codec_type, (uint8_t *)buf + size, sizeof(uint16_t));
            size += sizeof(uint16_t);
            memcpy(&codec_info[index].codec_name, (uint8_t *)buf + size, 32);
            size += 32;
            memcpy(&codec_info[index].codec_longname, (uint8_t *)buf + size, 64);
            size += 64;
        }

        for (index = 0; index < codec_cnt; index++) {
            if (codec_info[index].codec_type != 0) {
                continue;
            }

            type_name = g_strdup_printf ("emulenc_%s", codec_info[index].codec_name);
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

        printf("[codec] close\n");
        munmap (buf, 4096);
        close (codec_fd);
    }

    return TRUE;
}

void *gst_emul_codec_query (GstEmulEnc *emulenc)
{
    int fd, codec_cnt;
    int size = 0, i;
    void *mmapbuf;
    int func_type = CODEC_QUERY;
    CodecInfo *codec_info;

    CODEC_LOG(1, "enter: %s\n", __func__);

    fd = emulenc->codecbuf.fd;
    mmapbuf = emulenc->codecbuf.mmapbuf;
    if (fd < 0) {
        CODEC_LOG(1, "failed to get %s fd\n", CODEC_DEV);
        return NULL;
    }

    if (!mmapbuf) {
        CODEC_LOG(1, "failed to get mmaped memory address\n");
        return NULL;
    }

    write (fd, &func_type, sizeof(func_type));
    memcpy (&codec_cnt, mmapbuf, sizeof(uint32_t));
    size += sizeof(uint32_t);

    codec_info = g_malloc0 (codec_cnt * sizeof(CodecInfo));

    for (i = 0; i < codec_cnt; i++) {
        memcpy (&codec_info[i].mediatype, (uint8_t *)mmapbuf + size, sizeof(uint16_t));
        size += sizeof(uint16_t);
        memcpy (&codec_info[i].codectype, (uint8_t *)mmapbuf + size, sizeof(uint16_t));
        size += sizeof(uint16_t);
        memcpy (codec_info[i].name, (uint8_t *)mmapbuf + size, 32);
        size += 32;
        memcpy (codec_info[i].long_name, (uint8_t *)mmapbuf + size, 64);
        size += 64;
    }

    CODEC_LOG(1, "leave: %s\n", __func__);

    return codec_info;
}

int gst_emul_codec_init (GstEmulEnc *emulenc)
{
    int fd;
    int size = 0, ret;
    guint extradata_size = 0;
    guint8 *extradata = NULL;
    void *mmapbuf;
    int func_type = CODEC_INIT;

    CODEC_LOG(1, "enter: %s\n", __func__);

    fd = emulenc->codecbuf.fd;
    mmapbuf = emulenc->codecbuf.mmapbuf;
    if (fd < 0) {
        CODEC_LOG(1, "failed to get %s fd\n", CODEC_DEV);
        return -1;
    }

    if (!mmapbuf) {
        CODEC_LOG(1, "failed to get mmaped memory address\n");
        return -1;
    }

    extradata_size = emulenc->extradata_size;
    extradata = emulenc->extradata;

    /* copy basic info to initialize codec on the host side.
     * e.g. width, height, FPS ant etc. */
    memcpy ((uint8_t *)mmapbuf, &emulenc->format.video, sizeof(emulenc->format.video));
    size += sizeof(emulenc->format.video);
    if (extradata) {
        memcpy ((uint8_t *)mmapbuf + size, &extradata_size, sizeof(extradata_size));
        size += sizeof(extradata_size);
        memcpy ((uint8_t *)mmapbuf + size, extradata, extradata_size);
    }

    ret = write (fd, &func_type, sizeof(func_type));

    CODEC_LOG(1, "leave: %s\n", __func__);

    return ret; 
}

void gst_emul_codec_dev_close (GstEmulEnc *emulenc)
{
    int fd, ret = 0;
    int size = 0;
    void *mmapbuf;

    CODEC_LOG(1, "enter: %s\n", __func__);

    fd = emulenc->codecbuf.fd;
    mmapbuf = emulenc->codecbuf.mmapbuf;

    if (fd < 0) {
        CODEC_LOG(1, "failed to get %s fd\n", CODEC_DEV);
        return;
    }

    if (!mmapbuf) {
        CODEC_LOG(1, "failed to get mmaped memory address\n");
        return;
    }

    CODEC_LOG(2, "release mmaped memory region of %s\n", CODEC_DEV);
    ret = munmap(mmapbuf, 12 * 4096);
    if (ret != 0) {
        CODEC_LOG(1, "failed to release mmaped memory region of %s\n", CODEC_DEV);
    }

    CODEC_LOG(2, "close %s fd\n", CODEC_DEV);
    ret = close (fd);
    if (ret != 0) {
        CODEC_LOG(1, "failed to close %s\n", CODEC_DEV);
    }

    CODEC_LOG(1, "leave: %s\n", __func__);
}

void gst_emul_codec_deinit (GstEmulEnc *emulenc)
{
    int fd, ret;
    int func_type = CODEC_DEINIT;
    void *mmapbuf;

    CODEC_LOG(1, "enter: %s\n", __func__);

    fd = emulenc->codecbuf.fd;
    mmapbuf = emulenc->codecbuf.mmapbuf;

    if (fd < 0) {
        CODEC_LOG(1, "failed to get %s fd\n", CODEC_DEV);
        return;
    }

    if (!mmapbuf) {
        CODEC_LOG(1, "failed to get mmaped memory address\n");
        return;
    }

    ret = write (fd, &func_type, sizeof(func_type));

    /* close device fd and release mapped memory region */
    gst_emul_codec_dev_close (emulenc);

    CODEC_LOG(1, "leave: %s\n", __func__);
}

int gst_emul_codec_encode_video (GstEmulEnc *emulenc, guint8 *in_buf, guint in_size,
        GstBuffer **out_buf, GstClockTime in_timestamp)
{
    int fd, size = 0, ret;
    guint out_size;
    int func_type = CODEC_DECODE_VIDEO;
    void *mmapbuf;
    *out_buf = NULL;

    CODEC_LOG(1, "enter: %s\n", __func__);

    fd = emulenc->codecbuf.fd;
    mmapbuf = emulenc->codecbuf.mmapbuf;

    if (fd < 0) {
        CODEC_LOG(1, "failed to get %s fd\n", CODEC_DEV);
        return -1;
    }

    if (!mmapbuf) {
        CODEC_LOG(1, "failed to get mmaped memory address\n");
        return -1;
    }

    memcpy ((uint8_t *)mmapbuf + size, &in_size, sizeof(guint));
    size += sizeof(guint);
    memcpy ((uint8_t *)mmapbuf + size, &in_timestamp, sizeof(GstClockTime));
    size += sizeof(GstClockTime);
    memcpy ((uint8_t *)mmapbuf + size, in_buf, in_size);

    /* provide raw image for decoding to qemu */
    ret = write (fd, &func_type, sizeof(func_type));

    size = 0;
    memcpy (&out_size, (uint8_t *)mmapbuf + size, sizeof(uint));
    size += sizeof(guint);

    ret = gst_pad_alloc_buffer_and_set_caps (emulenc->srcpad,
            GST_BUFFER_OFFSET_NONE, out_size,
            GST_PAD_CAPS (emulenc->srcpad), out_buf);

    gst_buffer_set_caps (*out_buf, GST_PAD_CAPS (emulenc->srcpad));

    if (GST_BUFFER_DATA(*out_buf)) {
        memcpy (GST_BUFFER_DATA(*out_buf), (uint8_t *)mmapbuf + size, out_size);
    } else {
        CODEC_LOG(1, "failed to allocate output buffer\n");
    }

    CODEC_LOG(1, "leave: %s\n", __func__);

    return ret;
}

#if 0
void emulenc_encode_audio ()
{
    int fd, size = 0, ret;
    guint out_size;
    int func_type = CODEC_DECODE_AUDIO;
    void *mmapbuf;
    *out_buf = NULL;

    CODEC_LOG(1, "enter: %s\n", __func__);

    fd = emulenc->codecbuf.fd;
    mmapbuf = emulenc->codecbuf.mmapbuf;

    if (fd < 0) {
        CODEC_LOG(1, "failed to get %s fd\n", CODEC_DEV);
        return -1;
    }

    if (!mmapbuf) {
        CODEC_LOG(1, "failed to get mmaped memory address\n");
        return -1;
    }

    memcpy ((uint8_t *)mmapbuf + size, &in_size, sizeof(guint));
    size += sizeof(guint);
    memcpy ((uint8_t *)mmapbuf + size, &in_timestamp, sizeof(GstClockTime));
    size += sizeof(GstClockTime);
    memcpy ((uint8_t *)mmapbuf + size, in_buf, in_size);

    /* provide raw image for decoding to qemu */
    ret = write (fd, &func_type, sizeof(func_type));

    size = 0;
    memcpy (&out_size, (uint8_t *)mmapbuf + size, sizeof(uint));
    size += sizeof(guint);

    *out_buf = gst_buffer_new();
    GST_BUFFER_DATA (out_buf) = GST_BUFFER_MALLOCDATA (out_buf) = av_malloc (out_size);
    GST_BUFFER_SIZE (out_buf) = out_size;
    //  GST_BUFFER_FREE_FUNC (out_buf) = av_free;
    if (GST_PAD_CAPS(emulenc->srcpad)) {
        gst_buffer_set_caps (*out_buf, GST_PAD_CAPS (emulenc->srcpad));
    }

    if (GST_BUFFER_DATA(*out_buf)) {
        memcpy (GST_BUFFER_DATA(*out_buf), (uint8_t *)mmapbuf + size, out_size);
    } else {
        CODEC_LOG(1, "failed to allocate output buffer\n");
    }
    CODEC_LOG(1, "leave: %s\n", __func__);

    return ret;
}
#endif
