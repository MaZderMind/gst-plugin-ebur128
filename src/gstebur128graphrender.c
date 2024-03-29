#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstebur128graphrender.h"
#include <math.h>

GST_DEBUG_CATEGORY_STATIC(gst_ebur128graphrenderer_debug);
#define GST_CAT_DEFAULT gst_ebur128graphrenderer_debug

static void gst_ebur128graph_render_color_areas(GstEbur128Graph *graph, cairo_t *ctx, GstEbur128Position *position);
static void gst_ebur128graph_render_color_areas_peak_gauge(GstEbur128Graph *graph, cairo_t *ctx,
                                                           GstEbur128Position *position);
static void gst_ebur128graph_render_border(GstEbur128Graph *graph, cairo_t *ctx, GstEbur128Position *position);
static void gst_ebur128graph_with_sign(char *buffer, size_t len, gint num);
static void gst_ebur128graph_scale_text(GstEbur128Graph *graph, char *buffer, size_t len, gint scale_index);
static void gst_ebur128graph_render_scale_texts(GstEbur128Graph *graph, cairo_t *ctx);
static void gst_ebur128graph_render_header(GstEbur128Graph *graph, cairo_t *ctx);
static void gst_ebur128graph_render_graph_add_datapoint(GstEbur128Graph *graph, cairo_t *ctx,
                                                        const gint datapoint_index, const gint data_point_zero_y,
                                                        gint data_point_x);
static void gst_ebur128graph_render_graph(GstEbur128Graph *graph, cairo_t *ctx);
static void gst_ebur128graph_render_loudness_gauge(GstEbur128Graph *graph, cairo_t *ctx, GstEbur128Position *position,
                                                   gdouble measurement);
static void gst_ebur128graph_render_db_gauge(GstEbur128Graph *graph, cairo_t *ctx, GstEbur128Position *position,
                                             guint num_channels, gdouble *measurements);
static void gst_ebur128graph_render_scale_lines(GstEbur128Graph *graph, cairo_t *ctx, GstEbur128Position *position);
static void gst_ebur128graph_render_gauge_label(GstEbur128Graph *graph, cairo_t *ctx, GstEbur128Position *position,
                                                const char *label);
static void gst_ebur128graph_render_peak_gauge_scale(GstEbur128Graph *graph, cairo_t *ctx,
                                                     GstEbur128Position *position);
static void cairo_set_source_rgba_from_argb_int(cairo_t *ctx, int argb_color);

static gint clamp(gint in, gint min, gint max) {
  if (G_UNLIKELY(in < min)) {
    return min;
  } else if (G_UNLIKELY(in > max)) {
    return max;
  } else {
    return in;
  }
}

static gdouble clamp_d(gdouble in, gdouble min, gdouble max) {
  if (G_UNLIKELY(in < min)) {
    return min;
  } else if (G_UNLIKELY(in > max)) {
    return max;
  } else {
    return in;
  }
}

static gint min(gint a, gint b) {
  if (a < b) {
    return a;
  } else {
    return b;
  }
}

void gst_ebur128graph_render_init() {
  GST_DEBUG_CATEGORY_INIT(gst_ebur128graphrenderer_debug, "ebur128graphrenderer", 0, "ebur128graph Renderer");
}

/**
 * Called when the Size of the Target-Surface changed. Draws all background
 * elements that do not change dynamicly
 */
void gst_ebur128graph_render_background(GstEbur128Graph *graph, cairo_t *ctx, gint width, gint height) {

  // border stroke
  cairo_set_line_width(ctx, 1.0);

  // background
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_background);
  cairo_rectangle(ctx, 0, 0, width, height);
  cairo_fill(ctx);

  // scale
  gst_ebur128graph_render_scale_texts(graph, ctx);

  // graph: color areas
  gst_ebur128graph_render_color_areas(graph, ctx, &graph->positions.graph);

  // graph: border
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_border);
  cairo_rectangle(ctx, graph->positions.graph.x + .5, graph->positions.graph.y + .5, graph->positions.graph.w - 1,
                  graph->positions.graph.h - 1);
  cairo_stroke(ctx);

  // gauges
  if (graph->properties.short_term_gauge) {
    gst_ebur128graph_render_color_areas(graph, ctx, &graph->positions.short_term_gauge);
    gst_ebur128graph_render_border(graph, ctx, &graph->positions.short_term_gauge);
  }
  if (graph->properties.momentary_gauge) {
    gst_ebur128graph_render_color_areas(graph, ctx, &graph->positions.momentary_gauge);
    gst_ebur128graph_render_border(graph, ctx, &graph->positions.momentary_gauge);
  }
  if (graph->properties.peak_gauge) {
    gst_ebur128graph_render_color_areas_peak_gauge(graph, ctx, &graph->positions.peak_gauge);
    gst_ebur128graph_render_border(graph, ctx, &graph->positions.peak_gauge);
  }
}

static void gst_ebur128graph_render_border(GstEbur128Graph *graph, cairo_t *ctx, GstEbur128Position *position) {
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_border);
  cairo_rectangle(ctx, position->x + .5, position->y + .5, position->w - 1, position->h - 1);
  cairo_stroke(ctx);
}

static void gst_ebur128graph_render_color_areas(GstEbur128Graph *graph, cairo_t *ctx, GstEbur128Position *position) {
  gint total = abs(graph->properties.scale_from - graph->properties.scale_to);
  gint num_too_loud = clamp(graph->properties.scale_from, 0, G_MAXINT);
  gint num_loudness_ok =
      min(clamp(2 - graph->properties.scale_to, 0, 2), clamp(graph->properties.scale_from + 2, 0, 2));
  gint num_not_loud_enough = clamp(total - num_too_loud - num_loudness_ok + 2, 0, G_MAXINT);

  GST_INFO("from %d to %d (total %d steps): num_too_loud=%d, num_ok=%d, num_not_loud_enough=%d",
           graph->properties.scale_from, graph->properties.scale_to, total, num_too_loud, num_loudness_ok,
           num_not_loud_enough);

  // too_loud
  double height_too_loud = 0;
  if (num_too_loud > 0) {
    height_too_loud = graph->positions.scale_spacing * num_too_loud;
    cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_too_loud);
    cairo_rectangle(ctx, position->x + 1, position->y + 1, position->w - 2, height_too_loud);
    cairo_fill(ctx);
  }

  // loudness_ok
  double height_loudness_ok = 0;
  if (num_loudness_ok > 0) {
    height_loudness_ok = graph->positions.scale_spacing * num_loudness_ok;
    cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_loudness_ok);
    cairo_rectangle(ctx, position->x + 1, position->y + 1 + height_too_loud, position->w - 2, height_loudness_ok);
    cairo_fill(ctx);
  }

  double height_not_loud_enough = 0;
  if (num_not_loud_enough > 0) {
    height_not_loud_enough = graph->positions.scale_spacing * num_not_loud_enough;
    // not_loud_enough
    cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_not_loud_enough);
    cairo_rectangle(ctx, position->x + 1, position->y + 1 + height_too_loud + height_loudness_ok, position->w - 2,
                    height_not_loud_enough);
    cairo_fill(ctx);
  }
}

// -60db -> 0.00 (very quiet)
// -30db -> 0.25
// -15db -> 0.50
//  -5db -> 0.75
//  -0db -> 1.00 (very loud)
static double linearize_db(double db) { return 1. - log10(-0.15 * db + 1); }

static void gst_ebur128graph_render_color_areas_peak_gauge(GstEbur128Graph *graph, cairo_t *ctx,
                                                           GstEbur128Position *position) {
  double height_not_loud_enough = position->h * linearize_db(graph->properties.peak_gauge_lower_limit);
  double height_loudness_ok = position->h * linearize_db(graph->properties.peak_gauge_upper_limit);

  // too_loud
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_too_loud);
  cairo_rectangle(ctx, position->x + 1, position->y + 1, position->w - 2, position->h - height_loudness_ok);
  cairo_fill(ctx);

  // loudness_ok
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_loudness_ok);
  cairo_rectangle(ctx, position->x + 1, position->y + position->h - height_not_loud_enough, position->w - 2,
                  -height_loudness_ok + height_not_loud_enough);
  cairo_fill(ctx);

  // not_loud_enough
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_not_loud_enough);
  cairo_rectangle(ctx, position->x + 1, position->y + position->h, position->w - 2, -height_not_loud_enough);
  cairo_fill(ctx);
}

static void gst_ebur128graph_with_sign(char *buffer, size_t len, gint num) {
  if (num == 0) {
    g_snprintf(buffer, len, "%i", abs(num));
  } else {
    const char sign = num < 0 ? '-' : '+';
    g_snprintf(buffer, len, "%c%i", sign, abs(num));
  }
}

static void gst_ebur128graph_scale_text(GstEbur128Graph *graph, char *buffer, size_t len, gint scale_index) {
  gint scale_num;
  if (graph->properties.scale_mode == GST_EBUR128_SCALE_MODE_RELATIVE) {
    scale_num = graph->properties.scale_from - scale_index;
  } else {
    scale_num = graph->properties.scale_from - scale_index + graph->properties.scale_target;
  }

  gst_ebur128graph_with_sign(buffer, len, scale_num);
}

static void gst_ebur128graph_render_scale_texts(GstEbur128Graph *graph, cairo_t *ctx) {
  cairo_select_font_face(ctx, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(ctx, graph->properties.font_size_scale);
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_scale);

  gchar scale_text[10];
  for (gint scale_index = 0; scale_index < graph->positions.num_scales;
       scale_index += graph->positions.scale_show_every) {
    gst_ebur128graph_scale_text(graph, scale_text, sizeof(scale_text) / sizeof(*scale_text), scale_index);

    cairo_text_extents_t extents;
    cairo_text_extents(ctx, scale_text, &extents);

    gint text_x = graph->positions.scale.x + graph->positions.scale.w - extents.width;
    double text_y = graph->positions.scale.y +
                    ceil(graph->positions.scale_spacing * scale_index + graph->positions.scale_spacing) +
                    (extents.height / 2 - 1);

    cairo_move_to(ctx, text_x, text_y);
    cairo_show_text(ctx, scale_text);
  }

  cairo_fill(ctx);
}

/**
 * Called for every Frame. The Background has already be copied over. Draws all
 * foreground elements that do change dynamicly.
 */
void gst_ebur128graph_render_foreground(GstEbur128Graph *graph, cairo_t *ctx, gint width, gint height) {
  gst_ebur128graph_render_header(graph, ctx);
  gst_ebur128graph_render_graph(graph, ctx);
  gst_ebur128graph_render_scale_lines(graph, ctx, &graph->positions.graph);

  // gauges
  if (graph->properties.short_term_gauge) {
    gst_ebur128graph_render_loudness_gauge(graph, ctx, &graph->positions.short_term_gauge,
                                           graph->measurements.short_term);
    gst_ebur128graph_render_scale_lines(graph, ctx, &graph->positions.short_term_gauge);
    gst_ebur128graph_render_gauge_label(graph, ctx, &graph->positions.short_term_gauge, "S");
  }
  if (graph->properties.momentary_gauge) {
    gst_ebur128graph_render_loudness_gauge(graph, ctx, &graph->positions.momentary_gauge,
                                           graph->measurements.momentary);
    gst_ebur128graph_render_scale_lines(graph, ctx, &graph->positions.momentary_gauge);
    gst_ebur128graph_render_gauge_label(graph, ctx, &graph->positions.momentary_gauge, "M");
  }
  if (graph->properties.peak_gauge) {
    gst_ebur128graph_render_db_gauge(graph, ctx, &graph->positions.peak_gauge, graph->measurements.peak_num_channels,
                                     graph->measurements.peak_channel);
    gst_ebur128graph_render_gauge_label(graph, ctx, &graph->positions.peak_gauge, "TP");
    gst_ebur128graph_render_peak_gauge_scale(graph, ctx, &graph->positions.peak_gauge);
  }
}

static void gst_ebur128graph_render_header(GstEbur128Graph *graph, cairo_t *ctx) {
  const gchar *unit = graph->properties.scale_mode == GST_EBUR128_SCALE_MODE_ABSOLUTE ? "LUFS" : "LU";
  const gdouble correction =
      graph->properties.scale_mode == GST_EBUR128_SCALE_MODE_ABSOLUTE ? 0.0 : graph->properties.scale_target;

  gchar header_str[200];
  g_snprintf(header_str, 200,
             "TARGET: %+d LUFS | "
             "M: %+6.1f %s | "
             "S: %+6.1f %s | "
             "I: %+6.1f %s | "
             "LRA: %+7.2f LU | "
             "TPmax: %+5.1f db",
             graph->properties.scale_target, graph->measurements.momentary - correction, unit,
             graph->measurements.short_term - correction, unit, graph->measurements.global - correction, unit,
             graph->measurements.range, graph->measurements.max_true_peak);

  cairo_select_font_face(ctx, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(ctx, graph->properties.font_size_header);
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_header);
  cairo_move_to(ctx, graph->positions.header.x, graph->positions.header.y + graph->positions.header.h + .5);
  cairo_show_text(ctx, header_str);
  cairo_fill(ctx);
}

static void gst_ebur128graph_render_graph_add_datapoint(GstEbur128Graph *graph, cairo_t *ctx,
                                                        const gint datapoint_index, const gint data_point_zero_y,
                                                        gint data_point_x) {
  gdouble measurement = graph->measurements.history[datapoint_index];
  gdouble value_relative_to_target = measurement - graph->properties.scale_target;

  gint data_point_delta_y = (value_relative_to_target - graph->properties.scale_to) * graph->positions.scale_spacing +
                            graph->positions.scale_spacing - 2;

  data_point_delta_y = clamp(data_point_delta_y, 0, graph->positions.graph.h - 2);

  cairo_line_to(ctx, data_point_x, data_point_zero_y - data_point_delta_y);
}

static void gst_ebur128graph_render_graph(GstEbur128Graph *graph, cairo_t *ctx) {
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_graph);
  gint data_point_x = graph->positions.graph.x + 1;
  gint data_point_zero_y = graph->positions.graph.y + graph->positions.graph.h - 1;
  cairo_move_to(ctx, data_point_x, data_point_zero_y);

  for (gint i = graph->measurements.history_head; i < graph->measurements.history_size; i++) {
    gst_ebur128graph_render_graph_add_datapoint(graph, ctx, i, data_point_zero_y, data_point_x);
    data_point_x++;
  }
  for (gint i = 0; i < graph->measurements.history_head; i++) {
    gst_ebur128graph_render_graph_add_datapoint(graph, ctx, i, data_point_zero_y, data_point_x);
    data_point_x++;
  }

  cairo_line_to(ctx, data_point_x - 1, data_point_zero_y);
  cairo_fill(ctx);
}

static void gst_ebur128graph_render_loudness_gauge(GstEbur128Graph *graph, cairo_t *ctx, GstEbur128Position *position,
                                                   gdouble measurement) {
  gdouble value_relative_to_target = fmax(measurement - graph->properties.scale_target, graph->properties.scale_to);

  gint data_point_delta_y = (value_relative_to_target - graph->properties.scale_to) * graph->positions.scale_spacing +
                            graph->positions.scale_spacing - 2;

  data_point_delta_y = clamp(data_point_delta_y, G_MININT, position->h - 2);

  cairo_rectangle(ctx, position->x + 1, position->y + position->h - 1, position->w - 2, -data_point_delta_y);
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_graph);
  cairo_fill(ctx);
}

static void gst_ebur128graph_render_db_gauge(GstEbur128Graph *graph, cairo_t *ctx, GstEbur128Position *position,
                                             guint num_channels, gdouble *measurements) {
  double bar_width = (position->w - 2) / num_channels;
  double x = position->x;
  for (guint channel_index = 0; channel_index < num_channels; channel_index++) {
    // in dbTP from -Inf to -0.0 dbTP and a little bit further
    gdouble measurement = measurements[channel_index];
    gdouble linearized = linearize_db(measurement);
    double height = linearized * position->h;

    // 2px for the Border
    height = clamp_d(height, 0, position->h - 2);

    cairo_rectangle(ctx, x + 1, position->y + position->h - 1, bar_width, -height);
    x += bar_width;
  }

  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_graph);
  cairo_fill(ctx);
}

static void gst_ebur128graph_render_scale_lines(GstEbur128Graph *graph, cairo_t *ctx, GstEbur128Position *position) {
  cairo_set_line_width(ctx, 1.0);
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_scale_lines);

  for (gint scale_index = 0; scale_index < graph->positions.num_scales; scale_index++) {
    double y = position->y + ceil(scale_index * graph->positions.scale_spacing + graph->positions.scale_spacing) + .5;

    cairo_move_to(ctx, position->x + 1, y);
    cairo_line_to(ctx, position->x + position->w - 1, y);
  }

  cairo_stroke(ctx);
}

static void gst_ebur128graph_render_gauge_label(GstEbur128Graph *graph, cairo_t *ctx, GstEbur128Position *position,
                                                const char *label) {
  cairo_select_font_face(ctx, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(ctx, graph->properties.font_size_header);
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_header);

  cairo_text_extents_t extents;
  cairo_text_extents(ctx, label, &extents);

  gint text_x = position->x + (position->w - extents.width) / 2;

  cairo_move_to(ctx, text_x, position->y + position->h - 5);
  cairo_show_text(ctx, label);
  cairo_fill(ctx);
}

static void gst_ebur128graph_render_peak_gauge_scale(GstEbur128Graph *graph, cairo_t *ctx,
                                                     GstEbur128Position *position) {
  cairo_select_font_face(ctx, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(ctx, graph->properties.font_size_scale);
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_scale);

  gchar buffer[10];
  int dbs[] = {-40, -20, -10, -5, -4, -3, -2, -1};
  for (unsigned int i = 0; i < sizeof(dbs) / sizeof(*dbs); i++) {
    int db = dbs[i];
    g_snprintf(buffer, sizeof(buffer) / sizeof(*buffer), "%i", db);

    cairo_text_extents_t extents;
    cairo_text_extents(ctx, buffer, &extents);

    gint text_x = position->x - extents.width - graph->properties.gutter;
    double text_y = position->y + position->h - (linearize_db(db) * position->h) + (extents.height / 2 - 1);

    cairo_move_to(ctx, text_x, text_y);
    cairo_show_text(ctx, buffer);
  }

  cairo_fill(ctx);
}

/**
 * ARGB is kind of a standard when it comes to specifying colors in GStreamer
 * It is also used in the videotestsrc element and some others
 */
static void cairo_set_source_rgba_from_argb_int(cairo_t *ctx, int argb_color) {
  double a = (double)(argb_color >> 24 & 0xFF) / 255.;
  double r = (double)(argb_color >> 16 & 0xFF) / 255.;
  double g = (double)(argb_color >> 8 & 0xFF) / 255.;
  double b = (double)(argb_color & 0xFF) / 255.;
  cairo_set_source_rgba(ctx, r, g, b, a);
}
