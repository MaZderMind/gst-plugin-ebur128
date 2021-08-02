#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstebur128graphrender.h"
#include <math.h>

static void gst_ebur128graph_render_color_areas(GstEbur128Graph *graph, cairo_t *ctx, GstEbur128Position *position);
static void gst_ebur128graph_with_sign(char *buffer, size_t len, gint num);
static void gst_ebur128graph_scale_text(GstEbur128Graph *graph, char *buffer, size_t len, gint scale_index);
static void gst_ebur128graph_render_scale_texts(GstEbur128Graph *graph, cairo_t *ctx);
static void gst_ebur128graph_render_header(GstEbur128Graph *graph, cairo_t *ctx);
static void gst_ebur128graph_render_graph_add_datapoint(GstEbur128Graph *graph, cairo_t *ctx,
                                                        const gint datapoint_index, const gint data_point_zero_y,
                                                        gint *data_point_x);
static void gst_ebur128graph_render_graph(GstEbur128Graph *graph, cairo_t *ctx);
static void gst_ebur128graph_render_gauge(GstEbur128Graph *graph, cairo_t *ctx);
static void gst_ebur128graph_render_scale_lines(GstEbur128Graph *graph, cairo_t *ctx);
static void cairo_set_source_rgba_from_argb_int(cairo_t *ctx, int argb_color);

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

  // gauge: color areas
  gst_ebur128graph_render_color_areas(graph, ctx, &graph->positions.gauge);

  // gauge: border
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_border);
  cairo_rectangle(ctx, graph->positions.gauge.x + .5, graph->positions.gauge.y + .5, graph->positions.gauge.w - 1,
                  graph->positions.gauge.h - 1);
  cairo_stroke(ctx);
}

static void gst_ebur128graph_render_color_areas(GstEbur128Graph *graph, cairo_t *ctx, GstEbur128Position *position) {
  gint num_too_loud = abs(graph->properties.scale_from);
  gint num_loudness_ok = 2;
  gint num_not_loud_enough = abs(graph->properties.scale_to);

  double height_too_loud = ceil(graph->positions.scale_spacing * num_too_loud) - 1;
  double height_loudness_ok = ceil(graph->positions.scale_spacing * num_loudness_ok) - 1;
  double height_not_loud_enough = ceil(graph->positions.scale_spacing * num_not_loud_enough) - 1;

  // too_loud
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_too_loud);
  cairo_rectangle(ctx, position->x + 1, position->y + 1, position->w - 2, height_too_loud);
  cairo_fill(ctx);

  // loudness_ok
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_loudness_ok);
  cairo_rectangle(ctx, position->x + 1, position->y + 1 + height_too_loud, position->w - 2, height_loudness_ok);
  cairo_fill(ctx);

  // not_loud_enough
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_not_loud_enough);
  cairo_rectangle(ctx, position->x + 1, position->y + 1 + height_too_loud + height_loudness_ok, position->w - 2,
                  height_not_loud_enough);
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
    gst_ebur128graph_scale_text(graph, scale_text, 10, scale_index);

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
  gst_ebur128graph_render_gauge(graph, ctx);
  gst_ebur128graph_render_scale_lines(graph, ctx);
}

static void gst_ebur128graph_render_header(GstEbur128Graph *graph, cairo_t *ctx) {
  const gchar *unit = graph->properties.scale_mode == GST_EBUR128_SCALE_MODE_ABSOLUTE ? "LUFS" : "LU";
  const gdouble correction =
      graph->properties.scale_mode == GST_EBUR128_SCALE_MODE_ABSOLUTE ? 0.0 : graph->properties.scale_target;

  gchar header_str[200];
  g_snprintf(header_str, 200,
             "TARGET: %+d LUFS | "
             "M: %+7.2f %s | "
             "S: %+7.2f %s | "
             "I: %+7.2f %s | "
             "LRA: %+7.2f LU",
             graph->properties.scale_target, graph->measurements.momentary - correction, unit,
             graph->measurements.short_term - correction, unit, graph->measurements.global - correction, unit,
             graph->measurements.range);

  cairo_select_font_face(ctx, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(ctx, graph->properties.font_size_header);
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_header);
  cairo_move_to(ctx, graph->positions.header.x, graph->positions.header.y + graph->positions.header.h + .5);
  cairo_show_text(ctx, header_str);
  cairo_fill(ctx);
}

static void gst_ebur128graph_render_graph_add_datapoint(GstEbur128Graph *graph, cairo_t *ctx,
                                                        const gint datapoint_index, const gint data_point_zero_y,
                                                        gint *data_point_x) {
  gdouble measurement = graph->measurements.history[datapoint_index];
  gdouble value_relative_to_target = fmax(measurement - graph->properties.scale_target, graph->properties.scale_to);

  gint data_point_delta_y = (value_relative_to_target - graph->properties.scale_to) * graph->positions.scale_spacing +
                            graph->positions.scale_spacing - 2;
  cairo_line_to(ctx, *data_point_x, data_point_zero_y - data_point_delta_y);

  (*data_point_x)++;
}

static void gst_ebur128graph_render_graph(GstEbur128Graph *graph, cairo_t *ctx) {
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_graph);
  gint data_point_x = graph->positions.graph.x + 1;
  gint data_point_zero_y = graph->positions.graph.y + graph->positions.graph.h - 1;
  cairo_move_to(ctx, data_point_x, data_point_zero_y);

  for (gint i = graph->measurements.history_head; i < graph->measurements.history_size; i++) {
    gst_ebur128graph_render_graph_add_datapoint(graph, ctx, i, data_point_zero_y, &data_point_x);
  }
  for (gint i = 0; i < graph->measurements.history_head; i++) {
    gst_ebur128graph_render_graph_add_datapoint(graph, ctx, i, data_point_zero_y, &data_point_x);
  }

  data_point_x += 1;
  cairo_line_to(ctx, data_point_x, data_point_zero_y);
  cairo_line_to(ctx, data_point_x + graph->positions.graph.w - 1, data_point_zero_y);
  cairo_fill(ctx);
}

static void gst_ebur128graph_render_gauge(GstEbur128Graph *graph, cairo_t *ctx) {
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_graph);
  gdouble measurement = graph->measurements.history[graph->measurements.history_head - 1];
  gdouble value_relative_to_target = fmax(measurement - graph->properties.scale_target, graph->properties.scale_to);

  gint data_point_delta_y = (value_relative_to_target - graph->properties.scale_to) * graph->positions.scale_spacing +
                            graph->positions.scale_spacing - 2;

  cairo_rectangle(ctx, graph->positions.gauge.x + 1, graph->positions.gauge.y + graph->positions.gauge.h - 1,
                  graph->positions.gauge.w - 2, -data_point_delta_y);
  cairo_fill(ctx);
}

static void gst_ebur128graph_render_scale_lines(GstEbur128Graph *graph, cairo_t *ctx) {
  cairo_set_line_width(ctx, 1.0);
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_scale_lines);

  for (gint scale_index = 0; scale_index < graph->positions.num_scales; scale_index++) {
    double y = graph->positions.gauge.y +
               ceil(scale_index * graph->positions.scale_spacing + graph->positions.scale_spacing) + .5;

    cairo_move_to(ctx, graph->positions.gauge.x + 1, y);
    cairo_line_to(ctx, graph->positions.gauge.x + graph->positions.gauge.w - 1, y);

    cairo_move_to(ctx, graph->positions.graph.x + 1, y);
    cairo_line_to(ctx, graph->positions.graph.x + graph->positions.graph.w - 1, y);
  }

  cairo_stroke(ctx);
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
