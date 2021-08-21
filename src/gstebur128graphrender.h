#ifndef __GST_EBUR128GRAPHPAINT_H__
#define __GST_EBUR128GRAPHPAINT_H__

#include "gstebur128graphelement.h"

void gst_ebur128graph_render_init();
void gst_ebur128graph_render_background(GstEbur128Graph *graph, cairo_t *ctx, gint width, gint height);
void gst_ebur128graph_render_foreground(GstEbur128Graph *graph, cairo_t *ctx, gint width, gint height);

#endif // __GST_EBUR128GRAPHPAINT_H__
