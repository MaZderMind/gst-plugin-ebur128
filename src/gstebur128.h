#ifndef __GST_EBUR128_H__
#define __GST_EBUR128_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_EBUR128 (gst_ebur128_get_type())
G_DECLARE_FINAL_TYPE (Gstebur128, gst_ebur128,
    GST, EBUR128, GstElement)

struct _Gstebur128
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gboolean silent;
};

G_END_DECLS

#endif /* __GST_EBUR128_H__ */
