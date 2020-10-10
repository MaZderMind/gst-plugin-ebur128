#ifndef __GST_EBUR128GRAPH_H__
#define __GST_EBUR128GRAPH_H__

#include <ebur128.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_EBUR128GRAPH (gst_ebur128graph_get_type())
G_DECLARE_FINAL_TYPE(GstEbur128Graph, gst_ebur128graph, GST, EBUR128GRAPH,
                     GstElement)

struct _GstEbur128Graph {
  GstElementClass element;

  GstPad *sinkpad, *srcpad;
};

G_END_DECLS

#endif /* __GST_EBUR128GRAPH_H__ */
