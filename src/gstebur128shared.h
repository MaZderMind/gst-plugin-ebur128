#ifndef __GST_EBUR128SHARED_H__
#define __GST_EBUR128SHARED_H__

#include <ebur128.h>
#include <gst/audio/audio.h>
#include <gst/gst.h>

gboolean gst_ebur128_validate_lib_return(const char *invocation,
                                         const int return_value);

gboolean gst_ebur128_add_frames(ebur128_state *state, GstAudioFormat format,
                                guint8 *data, gint num_frames);

#endif // __GST_EBUR128SHARED_H__
