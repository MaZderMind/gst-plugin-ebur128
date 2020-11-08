#ifndef __GST_EBUR128GRAPH_H__
#define __GST_EBUR128GRAPH_H__

#include <cairo.h>
#include <ebur128.h>
#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_EBUR128GRAPH (gst_ebur128graph_get_type())
G_DECLARE_FINAL_TYPE(GstEbur128Graph, gst_ebur128graph, GST, EBUR128GRAPH, GstBaseTransform)

typedef struct _GstEbur128Position GstEbur128Position;
struct _GstEbur128Position {
  gint x, y, w, h;
};

typedef enum {
  /**
   * tbd.
   */
  GST_EBUR128_SCALE_MODE_RELATIVE,

  /**
   * tbd.
   */
  GST_EBUR128_SCALE_MODE_ABSOLUTE
} GstEbur128ScaleMode;

typedef enum {
  /**
   * tbd.
   */
  GST_EBUR128_MEASUREMENT_MOMENTARY,

  /**
   * tbd.
   */
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
  guint color_background;
  guint color_border;
  guint color_scale;
  guint color_scale_lines;
  guint color_header;
  guint color_graph;

  guint color_too_loud;
  guint color_loudness_ok;
  guint color_not_loud_enough;

  // sizes
  guint gutter;
  guint scale_w;
  guint gauge_w;

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

struct _GstEbur128Graph {
  GstBaseTransform transform;

  GstEbur128Positions positions;
  GstEbur128Properties properties;
  GstEbur128Measurements measurements;

  cairo_surface_t *background_image;
  cairo_t *background_context;

  GstPad *sinkpad, *srcpad;

  ebur128_state *state;
  GstAudioInfo audio_info;
  GstVideoInfo video_info;
};

G_END_DECLS

#endif /* __GST_EBUR128GRAPH_H__ */
