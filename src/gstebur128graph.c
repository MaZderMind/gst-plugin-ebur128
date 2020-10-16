/**
 * SECTION:element-ebur128graph
 *
 * Calculates the EBU-R 128 Loudness of an Audio-Stream and
 * visualizes it as Video-Feed
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -m audiotestsrc ! \
      audio/x-raw,format=S16LE,channels=2,rate=48000 ! \
      ebur128 ! autoaudiosink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) as logn as GArray is not supported
 * everywhere */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "gstebur128graph.h"
#include <math.h>

GST_DEBUG_CATEGORY_STATIC(gst_ebur128graph_debug);
#define GST_CAT_DEFAULT gst_ebur128graph_debug

/* Filter signals and args */
enum { LAST_SIGNAL };

enum { PROP_0 };

#define PROP_INTERVAL_DEFAULT (GST_SECOND / 10)

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */

#define SUPPORTED_AUDIO_FORMATS                                                \
  "{ " GST_AUDIO_NE(S16) ", " GST_AUDIO_NE(S32) "," GST_AUDIO_NE(              \
      F32) ", " GST_AUDIO_NE(F64) " }"

#define SUPPORTED_AUDIO_CHANNELS "(int) {1, 2, 5 }"

#define SUPPORTED_AUDIO_CAPS_STRING                                            \
  "audio/x-raw, "                                                              \
  "format = (string) " SUPPORTED_AUDIO_FORMATS ", "                            \
  "rate = " GST_AUDIO_RATE_RANGE ", "                                          \
  "channels = " SUPPORTED_AUDIO_CHANNELS ", "                                  \
  "layout = (string) interleaved "

#define SUPPORTED_VIDEO_CAPS_STRING GST_VIDEO_CAPS_MAKE("{ BGRx, BGRA }")

static GstStaticPadTemplate sink_template_factory =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS(SUPPORTED_AUDIO_CAPS_STRING));

static GstStaticPadTemplate src_template_factory =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS(SUPPORTED_VIDEO_CAPS_STRING));

#define gst_ebur128graph_parent_class parent_class
G_DEFINE_TYPE(GstEbur128Graph, gst_ebur128graph, GST_TYPE_AUDIO_VISUALIZER);

/* forward declarations */
static void gst_ebur128graph_set_property(GObject *object, guint prop_id,
                                          const GValue *value,
                                          GParamSpec *pspec);
static void gst_ebur128graph_get_property(GObject *object, guint prop_id,
                                          GValue *value, GParamSpec *pspec);
static void gst_ebur128graph_finalize(GObject *object);

static gboolean gst_ebur128graph_setup(GstAudioVisualizer *visualizer);
static void gst_ebur128graph_destroy_cairo_state(GstEbur128Graph *graph);
static void gst_ebur128graph_setup_cairo_state(GstEbur128Graph *graph);
static void gst_ebur128graph_calculate_positions(GstEbur128Graph *graph);
static void gst_ebur128graph_render_background(GstEbur128Graph *graph,
                                               cairo_t *ctx, gint width,
                                               gint height);
static void gst_ebur128graph_render_foreground(GstEbur128Graph *graph,
                                               cairo_t *ctx, gint width,
                                               gint height);
static void gst_ebur128graph_render_color_areas(GstEbur128Graph *graph,
                                                cairo_t *ctx,
                                                GstEbur128Position *position);
static void gst_ebur128graph_render_scale_texts(GstEbur128Graph *graph,
                                                cairo_t *ctx);
static gboolean gst_ebur128graph_render(GstAudioVisualizer *visualizer,
                                        GstBuffer *audio, GstVideoFrame *video);

/* GObject vmethod implementations */

/* initialize the ebur128's class */
static void gst_ebur128graph_class_init(GstEbur128GraphClass *klass) {
  GST_DEBUG_CATEGORY_INIT(gst_ebur128graph_debug, "ebur128graph", 0,
                          "ebur128graph Element");

  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
  GstAudioVisualizerClass *audio_visualizer = (GstAudioVisualizerClass *)klass;

  gobject_class->set_property = gst_ebur128graph_set_property;
  gobject_class->get_property = gst_ebur128graph_get_property;
  gobject_class->finalize = gst_ebur128graph_finalize;

  audio_visualizer->setup = GST_DEBUG_FUNCPTR(gst_ebur128graph_setup);
  audio_visualizer->render = GST_DEBUG_FUNCPTR(gst_ebur128graph_render);

  gst_element_class_add_static_pad_template(element_class,
                                            &sink_template_factory);
  gst_element_class_add_static_pad_template(element_class,
                                            &src_template_factory);

  gst_element_class_set_static_metadata(
      element_class, "ebur128graph", "Audio/Analyzer/Visualization",
      "Calculates the EBU-R 128 Loudness of an Audio-Stream and "
      "visualizes it as Video-Feed",
      "Peter Körner <peter@mazdermind.de>");
}

static void gst_ebur128graph_init(GstEbur128Graph *graph) {
  // colors
  graph->properties.color_background = 0xFF000000;
  graph->properties.color_border = 0xFF00CC00;
  graph->properties.color_scale = 0xFF009999;
  graph->properties.color_scale_lines = 0x4CFFFFFF;

  graph->properties.color_too_loud = 0xFFDB6666;
  graph->properties.color_loudness_ok = 0xFF66DB66;
  graph->properties.color_not_loud_enough = 0xFF6666DB;

  // sizes
  graph->properties.gutter = 5;
  graph->properties.scale_w = 20;
  graph->properties.gauge_w = 20;

  // scale
  graph->properties.scale_from = +18;
  graph->properties.scale_to = -36;
  graph->properties.scale_mode = GST_EBUR128_SCALE_MODE_RELATIVE;
  graph->properties.scale_target = -23;

  // font
  graph->properties.font_size_header = 12.;
  graph->properties.font_size_scale = 8.;
}

static void gst_ebur128graph_set_property(GObject *object, guint prop_id,
                                          const GValue *value,
                                          GParamSpec *pspec) {
  // GstEbur128Graph *graph = GST_EBUR128GRAPH(object);

  switch (prop_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void gst_ebur128graph_get_property(GObject *object, guint prop_id,
                                          GValue *value, GParamSpec *pspec) {
  // GstEbur128Graph *graph = GST_EBUR128GRAPH(object);

  switch (prop_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static cairo_format_t
gst_ebur128graph_get_cairo_format(GstEbur128Graph *graph) {
  GstVideoInfo *video_info = &graph->audio_visualizer.vinfo;
  switch (video_info->finfo->format) {
  case GST_VIDEO_FORMAT_BGRx:
    return CAIRO_FORMAT_RGB24;

  case GST_VIDEO_FORMAT_BGRA:
    return CAIRO_FORMAT_ARGB32;

  default:
    GST_ERROR_OBJECT(graph, "Unhandled Video-Format: %s",
                     video_info->finfo->name);
    return CAIRO_FORMAT_INVALID;
  }
}

static gboolean gst_ebur128graph_setup(GstAudioVisualizer *visualizer) {
  GstEbur128Graph *graph = GST_EBUR128GRAPH(visualizer);

  // cleanup existing state
  gst_ebur128graph_destroy_cairo_state(graph);

  // initialize cairo
  gst_ebur128graph_setup_cairo_state(graph);

  // re-calculate all positions
  gst_ebur128graph_calculate_positions(graph);

  // render background
  GstVideoInfo *video_info = &graph->audio_visualizer.vinfo;
  gint width = video_info->width;
  gint height = video_info->height;
  gst_ebur128graph_render_background(graph, graph->background_context, width,
                                     height);

  return TRUE;
}

static void gst_ebur128graph_destroy_cairo_state(GstEbur128Graph *graph) {
  if (graph->background_context != NULL) {
    GST_INFO_OBJECT(graph, "Destroying existing 'background' Cairo-Context");
    cairo_destroy(graph->background_context);
  }

  if (graph->background_image != NULL) {
    GST_INFO_OBJECT(graph, "Destroying existing 'background' Cairo-Surface");
    cairo_surface_destroy(graph->background_image);
  }
}

static void gst_ebur128graph_setup_cairo_state(GstEbur128Graph *graph) {
  GstVideoInfo *video_info = &graph->audio_visualizer.vinfo;
  gint width = video_info->width;
  gint height = video_info->height;

  GST_INFO_OBJECT(graph,
                  "Creating 'background' Cairo-Surface & Context width=%d "
                  "height=%d format=%s",
                  width, height, video_info->finfo->name);

  cairo_format_t cairo_format = gst_ebur128graph_get_cairo_format(graph);
  graph->background_image =
      cairo_image_surface_create(cairo_format, width, height);
  graph->background_context = cairo_create(graph->background_image);
}

static void gst_ebur128graph_calculate_positions(GstEbur128Graph *graph) {
  cairo_t *ctx = graph->background_context;
  GstVideoInfo *video_info = &graph->audio_visualizer.vinfo;
  gint width = video_info->width;
  gint height = video_info->height;

  gint gutter = graph->properties.gutter;

  cairo_text_extents_t extents;
  cairo_select_font_face(ctx, "monospace", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(ctx, graph->properties.font_size_header);
  cairo_text_extents(ctx, "W", &extents);
  gint font_height_header = extents.height;

  // header
  graph->positions.header.w =
      width - gutter - graph->properties.scale_w - gutter - gutter;
  graph->positions.header.h = font_height_header;
  graph->positions.header.x = gutter + graph->properties.scale_w + gutter;
  graph->positions.header.y = gutter;

  // scale
  graph->positions.scale.w = graph->properties.scale_w;
  graph->positions.scale.h =
      height - graph->positions.header.h - gutter - gutter - gutter;
  graph->positions.scale.x = gutter;
  graph->positions.scale.y = gutter + graph->positions.header.h + gutter;

  // gauge
  graph->positions.gauge.w = graph->properties.gauge_w;
  graph->positions.gauge.h = graph->positions.scale.h;
  graph->positions.gauge.x = width - graph->positions.gauge.w - gutter;
  graph->positions.gauge.y = graph->positions.scale.y;

  // graph
  graph->positions.graph.w = width - gutter - graph->positions.scale.w -
                             gutter - gutter - graph->positions.gauge.w -
                             gutter;
  graph->positions.graph.h = graph->positions.scale.h;
  graph->positions.graph.x = gutter + graph->positions.scale.w + gutter;
  graph->positions.graph.y = graph->positions.scale.y;

  // scales
  graph->positions.num_scales =
      -(graph->properties.scale_to - graph->properties.scale_from) + 1;
  graph->positions.scale_spacing =
      (double)graph->positions.scale.h / (graph->positions.num_scales + 1);

  cairo_select_font_face(ctx, "monospace", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(ctx, graph->properties.font_size_scale);
  cairo_text_extents(ctx, "W", &extents);
  gint font_height_scale = extents.height;

  double num_scales_per_space =
      graph->positions.scale_spacing / font_height_scale;
  double show_every_nth = 1 / num_scales_per_space;
  graph->positions.scale_show_every = fmax(ceil(show_every_nth), 1);
}

static void gst_ebur128graph_finalize(GObject *object) {
  GstEbur128Graph *graph = GST_EBUR128GRAPH(object);
  gst_ebur128graph_destroy_cairo_state(graph);
}

static void gst_ebur128graph_add_audio_frames(GstEbur128Graph *graph,
                                              GstBuffer *audio) {
  // Map and Analyze buffer
  GstMapInfo map_info;
  gst_buffer_map(audio, &map_info, GST_MAP_READ);

  GstAudioFormat format = GST_AUDIO_INFO_FORMAT(&graph->audio_visualizer.ainfo);
  const gint bytes_per_frame =
      GST_AUDIO_INFO_BPF(&graph->audio_visualizer.ainfo);
  gint num_frames = map_info.size / bytes_per_frame;

  GST_LOG_OBJECT(graph, "Adding %d frames ", num_frames);


}

static gboolean gst_ebur128graph_render(GstAudioVisualizer *visualizer,
                                        GstBuffer *audio,
                                        GstVideoFrame *video) {
  GstEbur128Graph *graph = GST_EBUR128GRAPH(visualizer);
  gst_ebur128graph_add_audio_frames(graph, audio);

  GstVideoInfo *video_info = &visualizer->vinfo;
  gint width = video_info->width;
  gint height = video_info->height;

  GST_LOG_OBJECT(graph, "Render w=%d h=%d, fmt=%s", width, height,
                 visualizer->vinfo.finfo->name);

  // copy background over
  // this can also be done with cairo (cairo_set_source_surface, cairo_rect,
  // cairo_fill) but because we *know* that both image surfaces use the same
  // format we can use memcpy which is probably quite a bit faster and we're on
  // the hot path here.
  memcpy(video->map->data,
         cairo_image_surface_get_data(graph->background_image),
         video->map->size);

  // create cairo image-surcface directly on the allocated buffer
  cairo_format_t cairo_format = gst_ebur128graph_get_cairo_format(graph);
  cairo_surface_t *image = cairo_image_surface_create_for_data(
      video->map->data, cairo_format, width, height, video_info->stride[0]);
  cairo_t *ctx = cairo_create(image);

  gst_ebur128graph_render_foreground(graph, ctx, width, height);

  cairo_destroy(ctx);
  cairo_surface_destroy(image);

  return TRUE;
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

/**
 * Called when the Size of the Target-Surface changed. Draws all background
 * elements that do not change dynamicly
 */
static void gst_ebur128graph_render_background(GstEbur128Graph *graph,
                                               cairo_t *ctx, gint width,
                                               gint height) {

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
  cairo_rectangle(ctx, graph->positions.graph.x + .5,
                  graph->positions.graph.y + .5, graph->positions.graph.w - 1,
                  graph->positions.graph.h - 1);
  cairo_stroke(ctx);

  // gauge: color areas
  gst_ebur128graph_render_color_areas(graph, ctx, &graph->positions.gauge);

  // gauge: border
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_border);
  cairo_rectangle(ctx, graph->positions.gauge.x + .5,
                  graph->positions.gauge.y + .5, graph->positions.gauge.w - 1,
                  graph->positions.gauge.h - 1);
  cairo_stroke(ctx);
}

static void gst_ebur128graph_render_color_areas(GstEbur128Graph *graph,
                                                cairo_t *ctx,
                                                GstEbur128Position *position) {
  gint num_too_loud = abs(graph->properties.scale_from);
  gint num_loudness_ok = 2;
  gint num_not_loud_enough = abs(graph->properties.scale_to);

  double height_too_loud =
      ceil(graph->positions.scale_spacing * num_too_loud) - 1;
  double height_loudness_ok =
      ceil(graph->positions.scale_spacing * num_loudness_ok) - 1;
  double height_not_loud_enough =
      ceil(graph->positions.scale_spacing * num_not_loud_enough) - 1;

  // too_loud
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_too_loud);
  cairo_rectangle(ctx, position->x + 1, position->y + 1, position->w - 2,
                  height_too_loud);
  cairo_fill(ctx);

  // loudness_ok
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_loudness_ok);
  cairo_rectangle(ctx, position->x + 1, position->y + 1 + height_too_loud,
                  position->w - 2, height_loudness_ok);
  cairo_fill(ctx);

  // not_loud_enough
  cairo_set_source_rgba_from_argb_int(ctx,
                                      graph->properties.color_not_loud_enough);
  cairo_rectangle(ctx, position->x + 1,
                  position->y + 1 + height_too_loud + height_loudness_ok,
                  position->w - 2, height_not_loud_enough);
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

static void gst_ebur128graph_scale_text(GstEbur128Graph *graph, char *buffer,
                                        size_t len, gint scale_index) {
  gint scale_num;
  if (graph->properties.scale_mode == GST_EBUR128_SCALE_MODE_RELATIVE) {
    scale_num = graph->properties.scale_from - scale_index;
  } else {
    scale_num = graph->properties.scale_from - scale_index +
                graph->properties.scale_target;
  }

  gst_ebur128graph_with_sign(buffer, len, scale_num);
}

static void gst_ebur128graph_render_scale_texts(GstEbur128Graph *graph,
                                                cairo_t *ctx) {

  cairo_select_font_face(ctx, "monospace", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(ctx, graph->properties.font_size_scale);
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_scale);

  gchar scale_text[10];
  for (gint scale_index = 0; scale_index < graph->positions.num_scales;
       scale_index += graph->positions.scale_show_every) {
    gst_ebur128graph_scale_text(graph, scale_text, 10, scale_index);

    cairo_text_extents_t extents;
    cairo_text_extents(ctx, scale_text, &extents);

    gint text_x =
        graph->positions.scale.x + graph->positions.scale.w - extents.width;
    double text_y = graph->positions.scale.y +
                    ceil(graph->positions.scale_spacing * scale_index +
                         graph->positions.scale_spacing) +
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
static void gst_ebur128graph_render_foreground(GstEbur128Graph *graph,
                                               cairo_t *ctx, gint width,
                                               gint height) {
  // header

  // data

  // scale lines
  cairo_set_line_width(ctx, 1.0);
  cairo_set_source_rgba_from_argb_int(ctx, graph->properties.color_scale_lines);

  for (gint scale_index = 0; scale_index < graph->positions.num_scales;
       scale_index++) {
    double y = graph->positions.gauge.y +
               ceil(scale_index * graph->positions.scale_spacing +
                    graph->positions.scale_spacing) +
               .5;

    cairo_move_to(ctx, graph->positions.gauge.x + 1, y);
    cairo_line_to(ctx, graph->positions.gauge.x + graph->positions.gauge.w - 1,
                  y);

    cairo_move_to(ctx, graph->positions.graph.x + 1, y);
    cairo_line_to(ctx, graph->positions.graph.x + graph->positions.graph.w - 1,
                  y);
  }

  cairo_stroke(ctx);
}
