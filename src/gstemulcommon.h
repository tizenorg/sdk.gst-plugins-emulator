#ifndef __GST_EMUL_H__
#define __GST_EMUL_H__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <gst/gst.h>

GST_DEBUG_CATEGORY_EXTERN (emul_debug);
#define GST_CAT_DEFAULT	emul_debug

G_BEGIN_DECLS

static int codec_debug_level = 2;
#define CODEC_LOG(level, fmt, ...) \
  do {    \
    if (codec_debug_level >= (level)) \
      printf("[gst-ffmpeg-emul][%d] " fmt, __LINE__, ##__VA_ARGS__); \
  } while (0)

#define CODEC_DEV   "/dev/codec"
#define CODEC_VER   10

typedef struct CodecDev
{
  int fd; 
  void *buffer;  
} CodecDev;

typedef struct _CodecInfo {
  uint16_t media_type;
  uint16_t codec_type;
  gchar codec_name[32];
  gchar codec_longname[64];
  int8_t sample_fmts[8];
} CodecInfo;

enum CODEC_FUNC_TYPE {
  CODEC_QUERY = 1,
  CODEC_INIT,
  CODEC_DEINIT,
  CODEC_DECODE_VIDEO,
  CODEC_ENCODE_VIDEO,
  CODEC_DECODE_AUDIO,
  CODEC_ENCODE_AUDIO,
};

enum CODEC_MEDIA_TYPE {
  AVMEDIA_TYPE_UNKNOWN = -1,
  AVMEDIA_TYPE_VIDEO,
  AVMEDIA_TYPE_AUDIO,
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
