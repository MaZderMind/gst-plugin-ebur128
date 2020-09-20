#ifndef __GST_EBUR128_H__
#define __GST_EBUR128_H__

#include <ebur128.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_EBUR128 (gst_ebur128_get_type())
G_DECLARE_FINAL_TYPE(Gstebur128, gst_ebur128, GST, EBUR128, GstBaseTransform)

struct _Gstebur128 {
  GstBaseTransform base_transform;

  GstPad *sinkpad, *srcpad;

  gboolean post_messages;
  GstClockTime message_ts;

  GstClockTime interval;
  guint interval_frames;
  guint frames_processed_in_interval;

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
