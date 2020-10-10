#ifndef __GST_EBUR128_H__
#define __GST_EBUR128_H__

#include <ebur128.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_EBUR128 (gst_ebur128_get_type())
G_DECLARE_FINAL_TYPE(GstEbur128, gst_ebur128, GST, EBUR128, GstBaseTransform)

struct _GstEbur128 {
  GstBaseTransform base_transform;

  GstPad *sinkpad, *srcpad;

  gboolean post_messages;

  GstClockTime interval;
  guint interval_frames;
  guint frames_since_last_mesage;

  GstClockTime start_ts;
  guint64 frames_processed;

  gboolean momentary;
  gboolean shortterm;
  gboolean global;
  gulong window;
  gboolean range;
  gboolean sample_peak;
  gboolean true_peak;
  gulong max_history;

  ebur128_state *state;
  GstAudioInfo audio_info;
};

G_END_DECLS

#endif /* __GST_EBUR128_H__ */
