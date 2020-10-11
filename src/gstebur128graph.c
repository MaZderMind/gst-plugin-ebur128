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
static void gst_ebur128graph_render_background(GstEbur128Graph *graph,
                                               cairo_t *ctx, gint width,
                                               gint height);
static void gst_ebur128graph_render_foreground(GstEbur128Graph *graph,
                                               cairo_t *ctx, gint width,
                                               gint height);

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

static void gst_ebur128graph_init(GstEbur128Graph *filter) {}

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

  GstVideoInfo *video_info = &visualizer->vinfo;
  gint width = video_info->width;
  gint height = video_info->height;
  cairo_format_t cairo_format = gst_ebur128graph_get_cairo_format(graph);

  gst_ebur128graph_destroy_cairo_state(graph);

  GST_INFO_OBJECT(graph,
                  "Creating 'background' Cairo-Surface & Context width=%d "
                  "height=%d format=%s",
                  width, height, video_info->finfo->name);
  graph->background_image =
      cairo_image_surface_create(cairo_format, width, height);
  graph->background_context = cairo_create(graph->background_image);

  gst_ebur128graph_render_background(graph, graph->background_context, width,
                                     height);

  return TRUE;
}

static void gst_ebur128graph_finalize(GObject *object) {
  GstEbur128Graph *graph = GST_EBUR128GRAPH(object);
  gst_ebur128graph_destroy_cairo_state(graph);
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

static gboolean gst_ebur128graph_render(GstAudioVisualizer *visualizer,
                                        GstBuffer *audio,
                                        GstVideoFrame *video) {
  GstEbur128Graph *graph = GST_EBUR128GRAPH(visualizer);
  GstVideoInfo *video_info = &visualizer->vinfo;
  gint width = video_info->width;
  gint height = video_info->height;

  GST_INFO_OBJECT(graph, "Render w=%d h=%d, fmt=%s", width, height,
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
 * Called when the Size of the Target-Surface changed. Draws all background
 * elements that do not change dynamicly
 */
static void gst_ebur128graph_render_background(GstEbur128Graph *graph,
                                               cairo_t *ctx, gint width,
                                               gint height) {
  cairo_set_source_rgba(ctx, 1., 0., 0., .75);
  cairo_rectangle(ctx, 0., 0, 100., 100.);
  cairo_fill(ctx);
}

/**
 * Called for every Frame. The Background has already be copied over. Draws all
 * foreground elements that do change dynamicly.
 */
static void gst_ebur128graph_render_foreground(GstEbur128Graph *graph,
                                               cairo_t *ctx, gint width,
                                               gint height) {
  cairo_set_source_rgb(ctx, 0, 1, 0);
  cairo_move_to(ctx, 0, 0);
  cairo_line_to(ctx, width, height);
  cairo_stroke(ctx);
}
