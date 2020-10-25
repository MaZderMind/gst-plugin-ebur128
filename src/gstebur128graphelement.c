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

#include "gstebur128graphelement.h"
#include "gstebur128shared.h"
#include <math.h>

GST_DEBUG_CATEGORY_STATIC(gst_ebur128graph_debug);
#define GST_CAT_DEFAULT gst_ebur128graph_debug

/* Filter signals and args */
enum { LAST_SIGNAL };

enum {
  PROP_0,

  PROP_COLOR_BACKGROUND,
  PROP_COLOR_BORDER,
  PROP_COLOR_SCALE,
  PROP_COLOR_SCALE_LINES,
  PROP_COLOR_HEADER,
  PROP_COLOR_GRAPH,

  PROP_COLOR_TOO_LOUD,
  PROP_COLOR_LOUDNESS_OK,
  PROP_COLOR_NOT_LOUD_ENOUGH,

  PROP_GUTTER,
  PROP_SCALE_W,
  PROP_GAUGE_W,

  PROP_SCALE_FROM,
  PROP_SCALE_TO,
  PROP_SCALE_MODE,
  PROP_SCALE_TARGET,

  PROP_FONT_SIZE_HEADER,
  PROP_FONT_SIZE_SCALE,

  PROP_MEASUREMENT
};

#define DEFAULT_COLOR_BACKGROUND 0xFF000000
#define DEFAULT_COLOR_BORDER 0xFF00CC00
#define DEFAULT_COLOR_SCALE 0xFF009999
#define DEFAULT_COLOR_SCALE_LINES 0x4CFFFFFF
#define DEFAULT_COLOR_HEADER 0xFFFFFF00
#define DEFAULT_COLOR_GRAPH 0x99000000

#define DEFAULT_COLOR_TOO_LOUD 0xFFDB6666
#define DEFAULT_COLOR_LOUDNESS_OK 0xFF66DB66
#define DEFAULT_COLOR_NOT_LOUD_ENOUGH 0xFF6666DB

#define DEFAULT_GUTTER 5
#define DEFAULT_SCALE_W 20
#define DEFAULT_GAUGE_W 20

#define DEFAULT_SCALE_FROM +18
#define DEFAULT_SCALE_TO -36
#define DEFAULT_SCALE_MODE GST_EBUR128_SCALE_MODE_RELATIVE
#define DEFAULT_SCALE_TARGET -23

#define DEFAULT_FONT_SIZE_HEADER 12.0
#define DEFAULT_FONT_SIZE_SCALE 8.0

#define DEFAULT_MEASUREMENT GST_EBUR128_MEASUREMENT_SHORT_TERM

#define GST_TYPE_EBUR128GRAPH_SCALE_MODE (gst_ebur128graph_scale_mode_get_type())
static GType gst_ebur128graph_scale_mode_get_type(void) {
  static GType ebur128graph_scale_mode = 0;
  static const GEnumValue scale_modes[] = {{GST_EBUR128_SCALE_MODE_RELATIVE, "Relative", "relative"},
                                           {GST_EBUR128_SCALE_MODE_ABSOLUTE, "Absolute", "absolute"},
                                           {0, NULL, NULL}};
  if (!ebur128graph_scale_mode) {
    ebur128graph_scale_mode = g_enum_register_static("GstEbur128GraphScaleMode", scale_modes);
  }
  return ebur128graph_scale_mode;
}

#define GST_TYPE_EBUR128GRAPH_MEASUREMENT (gst_ebur128graph_measurement_get_type())
static GType gst_ebur128graph_measurement_get_type(void) {
  static GType ebur128graph_measurement = 0;
  static const GEnumValue measurements[] = {{GST_EBUR128_MEASUREMENT_MOMENTARY, "Mentary", "momentary"},
                                            {GST_EBUR128_MEASUREMENT_SHORT_TERM, "Short-Term", "short-term"},
                                            {0, NULL, NULL}};
  if (!ebur128graph_measurement) {
    ebur128graph_measurement = g_enum_register_static("GstEbur128GraphMeasurement", measurements);
  }
  return ebur128graph_measurement;
}

#define SUPPORTED_AUDIO_FORMATS                                                                                        \
  "{ " GST_AUDIO_NE(S16) ", " GST_AUDIO_NE(S32) "," GST_AUDIO_NE(F32) ", " GST_AUDIO_NE(F64) " }"

#define SUPPORTED_AUDIO_CHANNELS "(int) {1, 2, 5 }"

#define SUPPORTED_AUDIO_CAPS_STRING                                                                                    \
  "audio/x-raw, "                                                                                                      \
  "format = (string) " SUPPORTED_AUDIO_FORMATS ", "                                                                    \
  "rate = " GST_AUDIO_RATE_RANGE ", "                                                                                  \
  "channels = " SUPPORTED_AUDIO_CHANNELS ", "                                                                          \
  "layout = (string) interleaved "

#define SUPPORTED_VIDEO_CAPS_STRING GST_VIDEO_CAPS_MAKE("{ BGRx, BGRA }")

static GstStaticPadTemplate sink_template_factory =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(SUPPORTED_AUDIO_CAPS_STRING));

static GstStaticPadTemplate src_template_factory =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(SUPPORTED_VIDEO_CAPS_STRING));

#define gst_ebur128graph_parent_class parent_class
G_DEFINE_TYPE(GstEbur128Graph, gst_ebur128graph, GST_TYPE_AUDIO_VISUALIZER);

/* forward declarations */
static void gst_ebur128graph_init_libebur128(GstEbur128Graph *graph);
static void gst_ebur128graph_destroy_libebur128(GstEbur128Graph *graph);
static void gst_ebur128graph_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_ebur128graph_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_ebur128graph_finalize(GObject *object);

static gboolean gst_ebur128graph_setup(GstAudioVisualizer *visualizer);
static void gst_ebur128graph_destroy_cairo(GstEbur128Graph *graph);
static void gst_ebur128graph_init_cairo(GstEbur128Graph *graph);
static void gst_ebur128graph_calculate_positions(GstEbur128Graph *graph);
static void gst_ebur128graph_render_background(GstEbur128Graph *graph, cairo_t *ctx, gint width, gint height);
static void gst_ebur128graph_render_foreground(GstEbur128Graph *graph, cairo_t *ctx, gint width, gint height);
static void gst_ebur128graph_render_color_areas(GstEbur128Graph *graph, cairo_t *ctx, GstEbur128Position *position);
static void gst_ebur128graph_render_scale_texts(GstEbur128Graph *graph, cairo_t *ctx);
static gboolean gst_ebur128graph_render(GstAudioVisualizer *visualizer, GstBuffer *audio, GstVideoFrame *video);
static void gst_ebur128graph_render_header(GstEbur128Graph *graph, cairo_t *ctx);
static void gst_ebur128graph_render_graph(GstEbur128Graph *graph, cairo_t *ctx);
static void gst_ebur128graph_render_gauge(GstEbur128Graph *graph, cairo_t *ctx);
static void gst_ebur128graph_render_scale_lines(GstEbur128Graph *graph, cairo_t *ctx);

/* GObject vmethod implementations */

/* initialize the ebur128's class */
static void gst_ebur128graph_class_init(GstEbur128GraphClass *klass) {
  GST_DEBUG_CATEGORY_INIT(gst_ebur128graph_debug, "ebur128graph", 0, "ebur128graph Element");

  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
  GstAudioVisualizerClass *audio_visualizer = (GstAudioVisualizerClass *)klass;

  gobject_class->set_property = gst_ebur128graph_set_property;
  gobject_class->get_property = gst_ebur128graph_get_property;
  gobject_class->finalize = gst_ebur128graph_finalize;

  audio_visualizer->setup = GST_DEBUG_FUNCPTR(gst_ebur128graph_setup);
  audio_visualizer->render = GST_DEBUG_FUNCPTR(gst_ebur128graph_render);

  g_object_class_install_property(
      gobject_class, PROP_COLOR_BACKGROUND,
      g_param_spec_uint("color-background", "Background-Color", "Color od the Background (big-endian ARGB)",
                        /* MIN */ 0, /* MAX */ G_MAXUINT32, DEFAULT_COLOR_BACKGROUND,
                        G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_COLOR_BORDER,
      g_param_spec_uint("color-border", "Border-Color", "Color of the Borders (big-endian ARGB)",
                        /* MIN */ 0, /* MAX */ G_MAXUINT32, DEFAULT_COLOR_BORDER,
                        G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_COLOR_SCALE,
      g_param_spec_uint("color-scale", "Scale-Color", "Color of the Scale-Text (big-endian ARGB)",
                        /* MIN */ 0, /* MAX */ G_MAXUINT32, DEFAULT_COLOR_SCALE,
                        G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_COLOR_SCALE_LINES,
      g_param_spec_uint("color-scale-lines", "Scale-Line-Color", "Color of the Scale-Lines (big-endian ARGB)",
                        /* MIN */ 0, /* MAX */ G_MAXUINT32, DEFAULT_COLOR_SCALE_LINES,
                        G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_COLOR_HEADER,
      g_param_spec_uint("color-header", "Header-Color", "Color of the Header-Text (big-endian ARGB)",
                        /* MIN */ 0, /* MAX */ G_MAXUINT32, DEFAULT_COLOR_HEADER,
                        G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_COLOR_GRAPH,
      g_param_spec_uint("color-graph", "Graph-Color", "Color of the Graph-Area (big-endian ARGB)",
                        /* MIN */ 0, /* MAX */ G_MAXUINT32, DEFAULT_COLOR_GRAPH,
                        G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_COLOR_TOO_LOUD,
      g_param_spec_uint("color-too-loud", "Too-Loud Area Color", "Color of the Too-Loud Area (big-endian ARGB)",
                        /* MIN */ 0, /* MAX */ G_MAXUINT32, DEFAULT_COLOR_TOO_LOUD,
                        G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_COLOR_LOUDNESS_OK,
      g_param_spec_uint("color-loudness-ok", "Loudness-Okay Area Color",
                        "Color of the Loudness-Olay Area (big-endian ARGB)",
                        /* MIN */ 0, /* MAX */ G_MAXUINT32, DEFAULT_COLOR_LOUDNESS_OK,
                        G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_COLOR_NOT_LOUD_ENOUGH,
      g_param_spec_uint("color-not-loud-enough", "Not-loud-enough Area Color",
                        "Color of the Not-loud-enough Area (big-endian ARGB)",
                        /* MIN */ 0, /* MAX */ G_MAXUINT32, DEFAULT_COLOR_NOT_LOUD_ENOUGH,
                        G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_GUTTER,
      g_param_spec_uint("gutter", "Gutter-Width", "Width of the Gutter between Elements (Pixels)", /* MIN */ 0,
                        /* MAX */ G_MAXUINT32, DEFAULT_GUTTER,
                        G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_SCALE_W,
      g_param_spec_uint("scale-w", "Scale-Width", "Width of the Scale-Area (Pixels)", /* MIN */ 0,
                        /* MAX */ G_MAXUINT32, DEFAULT_SCALE_W,
                        G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_GAUGE_W,
      g_param_spec_uint("gauge-w", "Gauge-Width", "Width of the Gauge-Area (Pixels)", /* MIN */ 0,
                        /* MAX */ G_MAXUINT32, DEFAULT_GAUGE_W,
                        G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_SCALE_FROM,
      g_param_spec_int("scale-from", "Scale Upper Bound", "Upper Bound of the Scale (LUFS)", /* MIN */ G_MININT32,
                       /* MAX */ G_MAXINT32, DEFAULT_SCALE_FROM,
                       G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_SCALE_TO,
      g_param_spec_int("scale-to", "Scale Lower Bound", "Lower Bound of the Scale (LUFS)", /* MIN */ G_MININT32,
                       /* MAX */ G_MAXINT32, DEFAULT_SCALE_TO,
                       G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_SCALE_MODE,
                                  g_param_spec_enum("scale-mode", "Scale-Mode",
                                                    "Mode of Display of the Scale and the Header",
                                                    GST_TYPE_EBUR128GRAPH_SCALE_MODE, DEFAULT_SCALE_MODE,
                                                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_FONT_SIZE_HEADER,
      g_param_spec_double("font-size-header", "Header Font-Size", "Font-Size of the Header (User-Space Units)",
                          /* MIN */ 1.0, /* MAX */ DBL_MAX, DEFAULT_FONT_SIZE_HEADER,
                          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_FONT_SIZE_SCALE,
      g_param_spec_double("font-size-scale", "Scale Font-Size", "Font-Size of the Scale (User-Space Units)",
                          /* MIN */ 1.0,
                          /* MAX */ DBL_MAX, DEFAULT_FONT_SIZE_SCALE,
                          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_SCALE_TARGET,
      g_param_spec_int("scale-target", "Target LUFS", "Target of the Scale (LUFS)",
                       /* MIN */ G_MININT32,
                       /* MAX */ G_MAXINT32, DEFAULT_SCALE_TARGET,
                       G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(gobject_class, PROP_MEASUREMENT,
                                  g_param_spec_enum("measurement", "Measurement to Graph", "Measurement to Graph",
                                                    GST_TYPE_EBUR128GRAPH_MEASUREMENT, DEFAULT_MEASUREMENT,
                                                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template(element_class, &sink_template_factory);
  gst_element_class_add_static_pad_template(element_class, &src_template_factory);

  gst_element_class_set_static_metadata(element_class, "ebur128graph", "Audio/Analyzer/Visualization",
                                        "Calculates the EBU-R 128 Loudness of an Audio-Stream and "
                                        "visualizes it as Video-Feed",
                                        "Peter Körner <peter@mazdermind.de>");
}

static void gst_ebur128graph_init(GstEbur128Graph *graph) {
  // colors
  graph->properties.color_background = DEFAULT_COLOR_BACKGROUND;
  graph->properties.color_border = DEFAULT_COLOR_BORDER;
  graph->properties.color_scale = DEFAULT_COLOR_SCALE;
  graph->properties.color_scale_lines = DEFAULT_COLOR_SCALE_LINES;
  graph->properties.color_header = DEFAULT_COLOR_HEADER;
  graph->properties.color_graph = DEFAULT_COLOR_GRAPH;

  graph->properties.color_too_loud = DEFAULT_COLOR_TOO_LOUD;
  graph->properties.color_loudness_ok = DEFAULT_COLOR_LOUDNESS_OK;
  graph->properties.color_not_loud_enough = DEFAULT_COLOR_NOT_LOUD_ENOUGH;

  // sizes
  graph->properties.gutter = DEFAULT_GUTTER;
  graph->properties.scale_w = DEFAULT_SCALE_W;
  graph->properties.gauge_w = DEFAULT_GAUGE_W;

  // scale
  graph->properties.scale_from = DEFAULT_SCALE_FROM;
  graph->properties.scale_to = DEFAULT_SCALE_TO;
  graph->properties.scale_mode = DEFAULT_SCALE_MODE;
  graph->properties.scale_target = DEFAULT_SCALE_TARGET;

  // font
  graph->properties.font_size_header = DEFAULT_FONT_SIZE_HEADER;
  graph->properties.font_size_scale = DEFAULT_FONT_SIZE_SCALE;

  // measurement
  graph->properties.measurement = DEFAULT_MEASUREMENT;

  // measurements
  graph->measurements.momentary = 0;
  graph->measurements.short_term = 0;
  graph->measurements.global = 0;
  graph->measurements.range = 0;
  graph->measurements.history = NULL;
}

static void gst_ebur128graph_finalize(GObject *object) {
  GstEbur128Graph *graph = GST_EBUR128GRAPH(object);
  gst_ebur128graph_destroy_libebur128(graph);
  gst_ebur128graph_destroy_cairo(graph);

  g_clear_pointer(&graph->measurements.history, g_free);
}

static void gst_ebur128graph_init_libebur128(GstEbur128Graph *graph) {
  gint rate = GST_AUDIO_INFO_RATE(&graph->audio_visualizer.ainfo);
  gint channels = GST_AUDIO_INFO_CHANNELS(&graph->audio_visualizer.ainfo);
  gint mode = EBUR128_MODE_M | EBUR128_MODE_S | EBUR128_MODE_I | EBUR128_MODE_LRA;

  graph->state = ebur128_init(channels, rate, mode);

  GST_INFO_OBJECT(graph,
                  "Initializing libebur128: "
                  "rate=%d channels=%d mode=0x%x",
                  rate, channels, mode);
}

static void gst_ebur128graph_destroy_libebur128(GstEbur128Graph *graph) {
  if (graph->state != NULL) {
    GST_INFO_OBJECT(graph, "Destroying libebur128 State");
    ebur128_destroy(&graph->state);
  }
}

static void gst_ebur128graph_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
  GstEbur128Graph *graph = GST_EBUR128GRAPH(object);

  switch (prop_id) {
  case PROP_COLOR_BACKGROUND:
    graph->properties.color_background = g_value_get_uint(value);
    break;
  case PROP_COLOR_BORDER:
    graph->properties.color_border = g_value_get_uint(value);
    break;
  case PROP_COLOR_SCALE:
    graph->properties.color_scale = g_value_get_uint(value);
    break;
  case PROP_COLOR_SCALE_LINES:
    graph->properties.color_scale_lines = g_value_get_uint(value);
    break;
  case PROP_COLOR_HEADER:
    graph->properties.color_header = g_value_get_uint(value);
    break;
  case PROP_COLOR_GRAPH:
    graph->properties.color_graph = g_value_get_uint(value);
    break;
  case PROP_COLOR_TOO_LOUD:
    graph->properties.color_too_loud = g_value_get_uint(value);
    break;
  case PROP_COLOR_LOUDNESS_OK:
    graph->properties.color_loudness_ok = g_value_get_uint(value);
    break;
  case PROP_COLOR_NOT_LOUD_ENOUGH:
    graph->properties.color_not_loud_enough = g_value_get_uint(value);
    break;
  case PROP_GUTTER:
    graph->properties.gutter = g_value_get_uint(value);
    break;
  case PROP_SCALE_W:
    graph->properties.scale_w = g_value_get_uint(value);
    break;
  case PROP_GAUGE_W:
    graph->properties.gauge_w = g_value_get_uint(value);
    break;
  case PROP_SCALE_FROM:
    graph->properties.scale_from = g_value_get_int(value);
    break;
  case PROP_SCALE_TO:
    graph->properties.scale_to = g_value_get_int(value);
    break;
  case PROP_SCALE_MODE:
    graph->properties.scale_mode = g_value_get_enum(value);
    break;
  case PROP_SCALE_TARGET:
    graph->properties.scale_target = g_value_get_int(value);
    break;
  case PROP_FONT_SIZE_HEADER:
    graph->properties.font_size_header = g_value_get_double(value);
    break;
  case PROP_FONT_SIZE_SCALE:
    graph->properties.font_size_scale = g_value_get_double(value);
    break;
  case PROP_MEASUREMENT:
    graph->properties.measurement = g_value_get_enum(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void gst_ebur128graph_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
  GstEbur128Graph *graph = GST_EBUR128GRAPH(object);

  switch (prop_id) {
  case PROP_COLOR_BACKGROUND:
    g_value_set_uint(value, graph->properties.color_background);
    break;
  case PROP_COLOR_BORDER:
    g_value_set_uint(value, graph->properties.color_border);
    break;
  case PROP_COLOR_SCALE:
    g_value_set_uint(value, graph->properties.color_scale);
    break;
  case PROP_COLOR_SCALE_LINES:
    g_value_set_uint(value, graph->properties.color_scale_lines);
    break;
  case PROP_COLOR_HEADER:
    g_value_set_uint(value, graph->properties.color_header);
    break;
  case PROP_COLOR_GRAPH:
    g_value_set_uint(value, graph->properties.color_graph);
    break;
  case PROP_COLOR_TOO_LOUD:
    g_value_set_uint(value, graph->properties.color_too_loud);
    break;
  case PROP_COLOR_LOUDNESS_OK:
    g_value_set_uint(value, graph->properties.color_loudness_ok);
    break;
  case PROP_COLOR_NOT_LOUD_ENOUGH:
    g_value_set_uint(value, graph->properties.color_not_loud_enough);
    break;
  case PROP_GUTTER:
    g_value_set_uint(value, graph->properties.gutter);
    break;
  case PROP_SCALE_W:
    g_value_set_uint(value, graph->properties.scale_w);
    break;
  case PROP_GAUGE_W:
    g_value_set_uint(value, graph->properties.gauge_w);
    break;
  case PROP_SCALE_FROM:
    g_value_set_int(value, graph->properties.scale_from);
    break;
  case PROP_SCALE_TO:
    g_value_set_int(value, graph->properties.scale_to);
    break;
  case PROP_SCALE_MODE:
    g_value_set_enum(value, graph->properties.scale_mode);
    break;
  case PROP_SCALE_TARGET:
    g_value_set_int(value, graph->properties.scale_target);
    break;
  case PROP_FONT_SIZE_HEADER:
    g_value_set_double(value, graph->properties.font_size_header);
    break;
  case PROP_FONT_SIZE_SCALE:
    g_value_set_double(value, graph->properties.font_size_scale);
    break;
  case PROP_MEASUREMENT:
    g_value_set_enum(value, graph->properties.measurement);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static cairo_format_t gst_ebur128graph_get_cairo_format(GstEbur128Graph *graph) {
  GstVideoInfo *video_info = &graph->audio_visualizer.vinfo;
  switch (video_info->finfo->format) {
  case GST_VIDEO_FORMAT_BGRx:
    return CAIRO_FORMAT_RGB24;

  case GST_VIDEO_FORMAT_BGRA:
    return CAIRO_FORMAT_ARGB32;

  default:
    GST_ERROR_OBJECT(graph, "Unhandled Video-Format: %s", video_info->finfo->name);
    return CAIRO_FORMAT_INVALID;
  }
}

static gboolean gst_ebur128graph_setup(GstAudioVisualizer *visualizer) {
  GstEbur128Graph *graph = GST_EBUR128GRAPH(visualizer);

  // cleanup existing state
  gst_ebur128graph_destroy_libebur128(graph);
  gst_ebur128graph_destroy_cairo(graph);

  // initialize libraries
  gst_ebur128graph_init_libebur128(graph);
  gst_ebur128graph_init_cairo(graph);

  // re-calculate all positions
  gst_ebur128graph_calculate_positions(graph);

  // render background
  GstVideoInfo *video_info = &graph->audio_visualizer.vinfo;
  gint width = video_info->width;
  gint height = video_info->height;
  gst_ebur128graph_render_background(graph, graph->background_context, width, height);

  // allocate space for measurements, one measurement per pixel
  graph->measurements.history_size = graph->positions.graph.w - 2;
  graph->measurements.history_head = 0;
  g_clear_pointer(&graph->measurements.history, g_free);
  graph->measurements.history = g_malloc_n(graph->measurements.history_size, sizeof(gdouble));
  for (gint i = 0; i < graph->measurements.history_size; i++) {
    graph->measurements.history[i] = -HUGE_VAL;
  }

  return TRUE;
}

static void gst_ebur128graph_destroy_cairo(GstEbur128Graph *graph) {
  if (graph->background_context != NULL) {
    GST_INFO_OBJECT(graph, "Destroying existing 'background' Cairo-Context");
    cairo_destroy(graph->background_context);
  }

  if (graph->background_image != NULL) {
    GST_INFO_OBJECT(graph, "Destroying existing 'background' Cairo-Surface");
    cairo_surface_destroy(graph->background_image);
  }
}

static void gst_ebur128graph_init_cairo(GstEbur128Graph *graph) {
  GstVideoInfo *video_info = &graph->audio_visualizer.vinfo;
  gint width = video_info->width;
  gint height = video_info->height;

  GST_INFO_OBJECT(graph,
                  "Creating 'background' Cairo-Surface & Context width=%d "
                  "height=%d format=%s",
                  width, height, video_info->finfo->name);

  cairo_format_t cairo_format = gst_ebur128graph_get_cairo_format(graph);
  graph->background_image = cairo_image_surface_create(cairo_format, width, height);
  graph->background_context = cairo_create(graph->background_image);
}

static void gst_ebur128graph_calculate_positions(GstEbur128Graph *graph) {
  cairo_t *ctx = graph->background_context;
  GstVideoInfo *video_info = &graph->audio_visualizer.vinfo;
  gint width = video_info->width;
  gint height = video_info->height;

  gint gutter = graph->properties.gutter;

  cairo_text_extents_t extents;
  cairo_select_font_face(ctx, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(ctx, graph->properties.font_size_header);
  cairo_text_extents(ctx, "W", &extents);
  gint font_height_header = extents.height;

  // header
  graph->positions.header.w = width - gutter - graph->properties.scale_w - gutter - gutter;
  graph->positions.header.h = font_height_header;
  graph->positions.header.x = gutter + graph->properties.scale_w + gutter;
  graph->positions.header.y = gutter;

  // scale
  graph->positions.scale.w = graph->properties.scale_w;
  graph->positions.scale.h = height - graph->positions.header.h - gutter - gutter - gutter;
  graph->positions.scale.x = gutter;
  graph->positions.scale.y = gutter + graph->positions.header.h + gutter;

  // gauge
  graph->positions.gauge.w = graph->properties.gauge_w;
  graph->positions.gauge.h = graph->positions.scale.h;
  graph->positions.gauge.x = width - graph->positions.gauge.w - gutter;
  graph->positions.gauge.y = graph->positions.scale.y;

  // graph
  graph->positions.graph.w =
      width - gutter - graph->positions.scale.w - gutter - gutter - graph->positions.gauge.w - gutter;
  graph->positions.graph.h = graph->positions.scale.h;
  graph->positions.graph.x = gutter + graph->positions.scale.w + gutter;
  graph->positions.graph.y = graph->positions.scale.y;

  // scales
  graph->positions.num_scales = -(graph->properties.scale_to - graph->properties.scale_from) + 1;
  graph->positions.scale_spacing = (double)graph->positions.scale.h / (graph->positions.num_scales + 1);

  cairo_select_font_face(ctx, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(ctx, graph->properties.font_size_scale);
  cairo_text_extents(ctx, "W", &extents);
  gint font_height_scale = extents.height;

  double num_scales_per_space = graph->positions.scale_spacing / font_height_scale;
  double show_every_nth = 1 / num_scales_per_space;
  graph->positions.scale_show_every = fmax(ceil(show_every_nth), 1);
}

static gboolean gst_ebur128graph_add_audio_frames(GstEbur128Graph *graph, GstBuffer *audio) {
  // Map and Analyze buffer
  GstMapInfo map_info;
  gst_buffer_map(audio, &map_info, GST_MAP_READ);

  GstAudioFormat format = GST_AUDIO_INFO_FORMAT(&graph->audio_visualizer.ainfo);
  const gint bytes_per_frame = GST_AUDIO_INFO_BPF(&graph->audio_visualizer.ainfo);
  gint num_frames = map_info.size / bytes_per_frame;

  return gst_ebur128_add_frames(graph->state, format, map_info.data, num_frames);
}

static gboolean gst_ebur128graph_take_measurement(GstEbur128Graph *graph) {
  double momentary;  // momentary loudness (last 400ms) in LUFS
  double short_term; // short-term loudness (last 3s) in LUFS
  double range;      // loudness range (LRA) in LU
  double global;     // integrated loudness in LUFS

  if (!gst_ebur128_validate_lib_return("ebur128_loudness_momentary",
                                       ebur128_loudness_momentary(graph->state, &momentary))) {
    return FALSE;
  }
  if (!gst_ebur128_validate_lib_return("ebur128_loudness_shortterm",
                                       ebur128_loudness_shortterm(graph->state, &short_term))) {
    return FALSE;
  }
  if (!gst_ebur128_validate_lib_return("ebur128_loudness_range", ebur128_loudness_range(graph->state, &range))) {
    return FALSE;
  }
  if (!gst_ebur128_validate_lib_return("ebur128_loudness_global", ebur128_loudness_global(graph->state, &global))) {
    return FALSE;
  }

  graph->measurements.momentary = momentary;
  graph->measurements.short_term = short_term;
  graph->measurements.range = range;
  graph->measurements.global = global;

  double measurement = 0;
  switch (graph->properties.measurement) {
  case GST_EBUR128_MEASUREMENT_SHORT_TERM:
    measurement = short_term;
    break;
  case GST_EBUR128_MEASUREMENT_MOMENTARY:
    measurement = momentary;
    break;
  }

  // add to measurements ring-buffer
  GST_LOG_OBJECT(graph, "writing measurement into ring-buffer of size %d at head-position %d",
                 graph->measurements.history_size, graph->measurements.history_head);

  graph->measurements.history[graph->measurements.history_head] = measurement;
  if (++graph->measurements.history_head >= graph->measurements.history_size) {
    GST_LOG_OBJECT(graph, "ring-buffer head-position overflowed at %d, resetting to 0",
                   graph->measurements.history_head);
    graph->measurements.history_head = 0;
  }

  return TRUE;
}

static gboolean gst_ebur128graph_render(GstAudioVisualizer *visualizer, GstBuffer *audio, GstVideoFrame *video) {
  GstEbur128Graph *graph = GST_EBUR128GRAPH(visualizer);
  if (!gst_ebur128graph_add_audio_frames(graph, audio)) {
    return FALSE;
  }

  if (!gst_ebur128graph_take_measurement(graph)) {
    return FALSE;
  }

  GstVideoInfo *video_info = &visualizer->vinfo;
  gint width = video_info->width;
  gint height = video_info->height;

  GST_LOG_OBJECT(graph, "Render w=%d h=%d, fmt=%s", width, height, visualizer->vinfo.finfo->name);

  // copy background over
  // this can also be done with cairo (cairo_set_source_surface, cairo_rect,
  // cairo_fill) but because we *know* that both image surfaces use the same
  // format we can use memcpy which is probably quite a bit faster and we're on
  // the hot path here.
  memcpy(video->map->data, cairo_image_surface_get_data(graph->background_image), video->map->size);

  // create cairo image-surcface directly on the allocated buffer
  cairo_format_t cairo_format = gst_ebur128graph_get_cairo_format(graph);
  cairo_surface_t *image =
      cairo_image_surface_create_for_data(video->map->data, cairo_format, width, height, video_info->stride[0]);
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
static void gst_ebur128graph_render_background(GstEbur128Graph *graph, cairo_t *ctx, gint width, gint height) {
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
static void gst_ebur128graph_render_foreground(GstEbur128Graph *graph, cairo_t *ctx, gint width, gint height) {
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
