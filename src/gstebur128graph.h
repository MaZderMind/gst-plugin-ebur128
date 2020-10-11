#ifndef __GST_EBUR128GRAPH_H__
#define __GST_EBUR128GRAPH_H__

#include <cairo.h>
#include <ebur128.h>
#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <gst/pbutils/gstaudiovisualizer.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_EBUR128GRAPH (gst_ebur128graph_get_type())
#define GST_EBUR128GRAPH(obj)                                                  \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_EBUR128GRAPH, GstEbur128Graph))
#define GST_EBUR128GRAPH_CLASS(klass)                                          \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_EBUR128GRAPH,                     \
                           GstEbur128GraphClass))
#define GST_IS_EBUR128GRAPH(obj)                                               \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_EBUR128GRAPH))
#define GST_IS_EBUR128GRAPH_CLASS(klass)                                       \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_EBUR128GRAPH))

typedef struct _GstEbur128Position GstEbur128Position;
struct _GstEbur128Position {
  gint x, y, w, h;
};

typedef struct _GstEbur128Positions GstEbur128Positions;
struct _GstEbur128Positions {
  GstEbur128Position header;
  GstEbur128Position scale;
  GstEbur128Position gauge;
  GstEbur128Position graph;

  gint num_scales;
  double scale_spacing;
};

typedef struct _GstEbur128Properties GstEbur128Properties;
struct _GstEbur128Properties {
  // colors
  gint background_color;
  gint border_color;

  // sizes
  gint gutter;
  gint scale_w;
  gint gauge_w;

  // scale
  gint scale_from;
  gint scale_to;
  gint scale_show_every;

  // font
  double font_size;
};

typedef struct _GstEbur128Graph GstEbur128Graph;
struct _GstEbur128Graph {
  GstAudioVisualizer audio_visualizer;

  GstEbur128Positions positions;
  GstEbur128Properties properties;

  cairo_surface_t *background_image;
  cairo_t *background_context;

  GstPad *sinkpad, *srcpad;
};

typedef struct _GstEbur128GraphClass GstEbur128GraphClass;
struct _GstEbur128GraphClass {
  GstAudioVisualizerClass parent_class;
};

GType gst_ebur128graph_get_type(void);
gboolean gst_ebur128graph_plugin_init(GstPlugin *plugin);

G_END_DECLS

#endif /* __GST_EBUR128GRAPH_H__ */
