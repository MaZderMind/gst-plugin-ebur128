#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstebur128shared.h"
#include <ebur128.h>

gboolean gst_ebur128_validate_lib_return(const char *invocation,
                                         const int return_value) {
  if (return_value != EBUR128_SUCCESS) {
    GST_ERROR("Error-Code %d from libebur128 call to %s", return_value,
              invocation);
    return FALSE;
  }

  return TRUE;
}

gboolean gst_ebur128_add_frames(ebur128_state *state, GstAudioFormat format,
                                guint8 *data, gint num_frames) {
  gboolean success = TRUE;
  int ret;

  GST_DEBUG("Adding %u frames to libebur128 at %p", num_frames, data);

  switch (format) {
  case GST_AUDIO_FORMAT_S16LE:
  case GST_AUDIO_FORMAT_S16BE:
    ret = ebur128_add_frames_short(state, (const short *)data, num_frames);
    success &= gst_ebur128_validate_lib_return("ebur128_add_frames_short", ret);
    break;
  case GST_AUDIO_FORMAT_S32LE:
  case GST_AUDIO_FORMAT_S32BE:
    ret = ebur128_add_frames_int(state, (const int *)data, num_frames);
    success &= gst_ebur128_validate_lib_return("ebur128_add_frames_int", ret);
    break;
  case GST_AUDIO_FORMAT_F32LE:
  case GST_AUDIO_FORMAT_F32BE:
    ret = ebur128_add_frames_float(state, (const float *)data, num_frames);

    success &= gst_ebur128_validate_lib_return("ebur128_add_frames_float", ret);
    break;
  case GST_AUDIO_FORMAT_F64LE:
  case GST_AUDIO_FORMAT_F64BE:
    ret = ebur128_add_frames_double(state, (const double *)data, num_frames);
    success &=
        gst_ebur128_validate_lib_return("ebur128_add_frames_double", ret);
    break;
  default:
    GST_ERROR("Unhandled Audio-Format: %s", gst_audio_format_to_string(format));
    success = FALSE;
  }

  return success;
}
