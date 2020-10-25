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

typedef enum {
  GST_EBUR128_SCALE_MODE_RELATIVE,
  GST_EBUR128_SCALE_MODE_ABSOLUTE
} GstEbur128ScaleMode;

typedef enum {
  GST_EBUR128_MEASUREMENT_MOMENTARY,
  GST_EBUR128_MEASUREMENT_SHORT_TERM
} GstEbur128Measurement;

typedef struct _GstEbur128Positions GstEbur128Positions;
struct _GstEbur128Positions {
  GstEbur128Position header;
  GstEbur128Position scale;
  GstEbur128Position gauge;
  GstEbur128Position graph;

  gint num_scales;
  double scale_spacing;
  gint scale_show_every;
};

typedef struct _GstEbur128Properties GstEbur128Properties;
struct _GstEbur128Properties {
  // colors
  gint color_background;
  gint color_border;
  gint color_scale;
  gint color_scale_lines;
  gint color_header;
  gint color_graph;

  gint color_too_loud;
  gint color_loudness_ok;
  gint color_not_loud_enough;

  // sizes
  gint gutter;
  gint scale_w;
  gint gauge_w;

  // scale
  gint scale_from;
  gint scale_to;
  GstEbur128ScaleMode scale_mode;
  gint scale_target;

  // measurement
  GstEbur128Measurement measurement;

  // font
  gdouble font_size_header;
  gdouble font_size_scale;
};

typedef struct _GstEbur128Measurements GstEbur128Measurements;
struct _GstEbur128Measurements {
  gdouble momentary;
  gdouble short_term;
  gdouble global;
  gdouble range;

  // single-headed ring-buffer
  gint history_size;
  gint history_head;
  gdouble *history;
};

typedef struct _GstEbur128Graph GstEbur128Graph;
struct _GstEbur128Graph {
  GstAudioVisualizer audio_visualizer;

  GstEbur128Positions positions;
  GstEbur128Properties properties;
  GstEbur128Measurements measurements;

  cairo_surface_t *background_image;
  cairo_t *background_context;

  GstPad *sinkpad, *srcpad;

  ebur128_state *state;
};

typedef struct _GstEbur128GraphClass GstEbur128GraphClass;
struct _GstEbur128GraphClass {
  GstAudioVisualizerClass parent_class;
};

GType gst_ebur128graph_get_type(void);
gboolean gst_ebur128graph_plugin_init(GstPlugin *plugin);

G_END_DECLS

#endif /* __GST_EBUR128GRAPH_H__ */
