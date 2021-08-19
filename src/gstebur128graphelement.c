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
#include "gstebur128graphrender.h"
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

  PROP_COLOR_SHORT_TERM_GAUGE,
  PROP_COLOR_MOMENTARY_GAUGE,
  PROP_COLOR_PEAK_GAUGE,

  PROP_GUTTER,
  PROP_SCALE_W,
  PROP_GAUGE_W,

  PROP_SCALE_FROM,
  PROP_SCALE_TO,
  PROP_SCALE_MODE,
  PROP_SCALE_TARGET,

  PROP_FONT_SIZE_HEADER,
  PROP_FONT_SIZE_SCALE,

  PROP_MEASUREMENT,
  PROP_TIMEBASE,

  PROP_SHORT_TERM_GAUGE,
  PROP_MOMENTARY_GAUGE,
  PROP_PEAK_GAUGE
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

#define DEFAULT_COLOR_SHORT_TERM_GAUGE 0x9900ff00
#define DEFAULT_COLOR_MOMENTARY_GAUGE 0x99000000
#define DEFAULT_COLOR_PEAK_GAUGE 0x9900ffff

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
#define DEFAULT_TIMEBASE (60 * GST_SECOND)

#define DEFAULT_SHORT_TERM_GAUGE FALSE
#define DEFAULT_MOMENTARY_GAUGE TRUE
#define DEFAULT_PEAK_GAUGE FALSE

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
#define PREFERRED_VIDEO_WIDTH 640
#define PREFERRED_VIDEO_HEIGHT 480
#define PREFERRED_VIDEO_FRAMERATE_NUMERATOR 30
#define PREFERRED_VIDEO_FRAMERATE_DENOMINATOR 1

static GstStaticPadTemplate sink_template_factory =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(SUPPORTED_AUDIO_CAPS_STRING));

static GstStaticPadTemplate src_template_factory =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(SUPPORTED_VIDEO_CAPS_STRING));

#define gst_ebur128graph_parent_class parent_class
G_DEFINE_TYPE(GstEbur128Graph, gst_ebur128graph, GST_TYPE_BASE_TRANSFORM);

/* forward declarations */
static void gst_ebur128graph_init_libebur128(GstEbur128Graph *graph);
static void gst_ebur128graph_destroy_libebur128(GstEbur128Graph *graph);
static void gst_ebur128graph_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_ebur128graph_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_ebur128graph_finalize(GObject *object);
static gboolean gst_ebur128graph_take_measurement(GstEbur128Graph *graph);

static gboolean gst_ebur128graph_setup(GstEbur128Graph *graph);
static void gst_ebur128graph_destroy_cairo(GstEbur128Graph *graph);
static void gst_ebur128graph_init_cairo(GstEbur128Graph *graph);
static void gst_ebur128graph_calculate_positions(GstEbur128Graph *graph);

static GstCaps *gst_ebur128graph_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                                GstCaps *filter);
static GstCaps *gst_ebur128graph_fixate_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                             GstCaps *othercaps);
static GstFlowReturn gst_ebur128graph_dummy_transform(GstBaseTransform *trans, GstBuffer *inbuf, GstBuffer *outbuf);
static gboolean gst_ebur128graph_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps);
static gboolean gst_ebur128graph_transform_size(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                                gsize size, GstCaps *othercaps, gsize *othersize);
static GstFlowReturn gst_ebur128graph_generate_output(GstBaseTransform *trans, GstBuffer **outbuf);
static GstFlowReturn gst_ebur128graph_generate_video_frame(GstEbur128Graph *graph, GstBuffer **outbuf);
static cairo_format_t gst_ebur128graph_get_cairo_format(GstEbur128Graph *graph);

/* initialize the ebur128's class */
static void gst_ebur128graph_class_init(GstEbur128GraphClass *klass) {
  GST_DEBUG_CATEGORY_INIT(gst_ebur128graph_debug, "ebur128graph", 0, "ebur128graph Element");

  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
  GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_CLASS(klass);

  // configure vmethods
  gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_ebur128graph_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_ebur128graph_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_ebur128graph_finalize);

  transform_class->transform_caps = GST_DEBUG_FUNCPTR(gst_ebur128graph_transform_caps);
  transform_class->fixate_caps = GST_DEBUG_FUNCPTR(gst_ebur128graph_fixate_caps);
  transform_class->transform = GST_DEBUG_FUNCPTR(
      gst_ebur128graph_dummy_transform); // required to force base_transform out of passthrough mode, though it is never
                                         // actually called because we implement out orn generate_output vmethod
  transform_class->set_caps = GST_DEBUG_FUNCPTR(gst_ebur128graph_set_caps);
  transform_class->transform_size = GST_DEBUG_FUNCPTR(gst_ebur128graph_transform_size);
  transform_class->generate_output = GST_DEBUG_FUNCPTR(gst_ebur128graph_generate_output);

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
                        "Color of the Loudness-Okay Area (big-endian ARGB)",
                        /* MIN */ 0, /* MAX */ G_MAXUINT32, DEFAULT_COLOR_LOUDNESS_OK,
                        G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_COLOR_NOT_LOUD_ENOUGH,
      g_param_spec_uint("color-not-loud-enough", "Not-loud-enough Area Color",
                        "Color of the Not-loud-enough Area (big-endian ARGB)",
                        /* MIN */ 0, /* MAX */ G_MAXUINT32, DEFAULT_COLOR_NOT_LOUD_ENOUGH,
                        G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_COLOR_SHORT_TERM_GAUGE,
      g_param_spec_uint("color-gauge-short-term", "Short-Term-Gauge Color",
                        "Color of the Short-Term-Gauge (big-endian ARGB)",
                        /* MIN */ 0, /* MAX */ G_MAXUINT32, DEFAULT_COLOR_SHORT_TERM_GAUGE,
                        G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_COLOR_MOMENTARY_GAUGE,
      g_param_spec_uint("color-gauge-momentary", "Long-Term-Gauge Color",
                        "Color of the Long-Term-Gauge (big-endian ARGB)",
                        /* MIN */ 0, /* MAX */ G_MAXUINT32, DEFAULT_COLOR_MOMENTARY_GAUGE,
                        G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_COLOR_PEAK_GAUGE,
      g_param_spec_uint("color-gauge-peak", "Peak-Gauge Color", "Color of the Peak-Gauge (big-endian ARGB)",
                        /* MIN */ 0, /* MAX */ G_MAXUINT32, DEFAULT_COLOR_PEAK_GAUGE,
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

  g_object_class_install_property(
      gobject_class, PROP_TIMEBASE,
      g_param_spec_uint64("timebase", "Timebase", "Time as displayed on the X-Axis (in nanoseconds)",
                          /* MIN */ 0,
                          /* MAX */ G_MAXUINT64, DEFAULT_TIMEBASE,
                          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_SHORT_TERM_GAUGE,
      g_param_spec_boolean("gauge-short-term", "Short-Term Loudness Gauge", "Enable Short-Term Loudness Gauge",
                           DEFAULT_SHORT_TERM_GAUGE,
                           G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_MOMENTARY_GAUGE,
      g_param_spec_boolean("gauge-momentary", "Momentary Loudness Gauge", "Enable Momentary Loudness Gauge",
                           DEFAULT_MOMENTARY_GAUGE,
                           G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_PEAK_GAUGE,
      g_param_spec_boolean("gauge-peak", "True-Peak Gauge", "Enable True-Peak Gauge", DEFAULT_PEAK_GAUGE,
                           G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template(element_class, &sink_template_factory);
  gst_element_class_add_static_pad_template(element_class, &src_template_factory);

  gst_element_class_set_static_metadata(element_class, "ebur128graph", "Audio/Analyzer/Visualization",
                                        "Calculates the EBU-R 128 Loudness of an Audio-Stream and "
                                        "visualizes it as Video-Feed",
                                        "Peter Körner <peter@mazdermind.de>");
}

static GstCaps *gst_ebur128graph_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                                GstCaps *filter) {
  GstCaps *res;
  switch (direction) {
  case GST_PAD_SRC:
    GST_DEBUG_OBJECT(
        trans, "gst_ebur128graph_transform_caps called for direction=GST_PAD_SRC with caps=%" GST_PTR_FORMAT, caps);
    res = gst_caps_from_string(SUPPORTED_AUDIO_CAPS_STRING);
    GST_DEBUG_OBJECT(trans, "gst_ebur128graph_transform_caps returning caps=%" GST_PTR_FORMAT, res);
    break;
  case GST_PAD_SINK:
    GST_DEBUG_OBJECT(
        trans, "gst_ebur128graph_transform_caps called for direction=GST_PAD_SINK with caps=%" GST_PTR_FORMAT, caps);
    res = gst_caps_from_string(SUPPORTED_VIDEO_CAPS_STRING);
    GST_DEBUG_OBJECT(trans, "gst_ebur128graph_transform_caps returning caps=%" GST_PTR_FORMAT, res);
    break;
  case GST_PAD_UNKNOWN:
    return NULL;
  }

  if (filter) {
    GstCaps *intersection;

    GST_DEBUG_OBJECT(trans, "Using filter caps %" GST_PTR_FORMAT, filter);
    intersection = gst_caps_intersect_full(filter, res, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref(res);
    res = intersection;
    GST_DEBUG_OBJECT(trans, "Intersection %" GST_PTR_FORMAT, res);
  }

  return res;
}

static GstCaps *gst_ebur128graph_fixate_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                             GstCaps *othercaps) {
  switch (direction) {
  case GST_PAD_SRC:
    GST_DEBUG_OBJECT(trans,
                     "gst_ebur128graph_fixate_caps called for direction=GST_PAD_SINK with othercaps=%" GST_PTR_FORMAT,
                     othercaps);
    othercaps = gst_caps_fixate(othercaps);
    break;
  case GST_PAD_SINK:
    GST_DEBUG_OBJECT(trans,
                     "gst_ebur128graph_fixate_caps called for direction=GST_PAD_SRC with othercaps=%" GST_PTR_FORMAT,
                     othercaps);
    othercaps = gst_caps_make_writable(othercaps);
    GstStructure *structure = gst_caps_get_structure(othercaps, 0);

    gst_structure_fixate_field_nearest_int(structure, "width", PREFERRED_VIDEO_WIDTH);
    gst_structure_fixate_field_nearest_int(structure, "height", PREFERRED_VIDEO_HEIGHT);

    if (gst_structure_has_field(structure, "framerate")) {
      gst_structure_fixate_field_nearest_fraction(structure, "framerate", PREFERRED_VIDEO_FRAMERATE_NUMERATOR,
                                                  PREFERRED_VIDEO_FRAMERATE_DENOMINATOR);
    } else {
      gst_structure_set(structure, "framerate", GST_TYPE_FRACTION, PREFERRED_VIDEO_FRAMERATE_NUMERATOR,
                        PREFERRED_VIDEO_FRAMERATE_DENOMINATOR, NULL);
    }

    othercaps = gst_caps_fixate(othercaps);
    break;
  case GST_PAD_UNKNOWN:
    return NULL;
  }

  GST_DEBUG_OBJECT(trans, "fixated to %" GST_PTR_FORMAT, othercaps);
  return othercaps;
}

static gboolean gst_ebur128graph_transform_size(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                                gsize size, GstCaps *othercaps, gsize *othersize) {
  GstEbur128Graph *graph = GST_EBUR128GRAPH(trans);

  switch (direction) {
  case GST_PAD_SRC:
  case GST_PAD_SINK:
    *othersize = graph->video_info.size;
    GST_DEBUG_OBJECT(graph, "gst_ebur128graph_transform_size called for direction=GST_PAD_SRC, returning %ld",
                     *othersize);
    return TRUE;

  default:
    return FALSE;
  }
}

static GstFlowReturn gst_ebur128graph_dummy_transform(GstBaseTransform *trans, GstBuffer *inbuf, GstBuffer *outbuf) {
  GST_ERROR_OBJECT(trans, "transform should have never been called, logic is completely handled by generate_output");
  return GST_FLOW_NOT_SUPPORTED;
}

static gboolean gst_ebur128graph_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
  GstEbur128Graph *graph = GST_EBUR128GRAPH(trans);
  GST_INFO_OBJECT(graph, "gst_ebur128graph_set_caps, incaps=%" GST_PTR_FORMAT " outcaps=%" GST_PTR_FORMAT, incaps,
                  outcaps);

  /* store audio-info */
  if (!gst_audio_info_from_caps(&graph->audio_info, incaps)) {
    GST_ERROR_OBJECT(graph, "Unhandled Input-Caps: %" GST_PTR_FORMAT, incaps);
    return FALSE;
  }

  /* store video-info */
  if (!gst_video_info_from_caps(&graph->video_info, outcaps)) {
    GST_ERROR_OBJECT(graph, "Unhandled Output-Caps: %" GST_PTR_FORMAT, outcaps);
    return FALSE;
  }

  gst_ebur128graph_setup(graph);

  return TRUE;
}

/**
 * This Method is called over and over with an input-buffer in trans->queued_buf until it does not produce any more
 * output frames. The Buffer in trans->queued_buf can be of any size. It is ok to not return any output-buffers at all,
 * 1 or more.
 */
static GstFlowReturn gst_ebur128graph_generate_output(GstBaseTransform *trans, GstBuffer **outbuf) {
  GstEbur128Graph *graph = GST_EBUR128GRAPH(trans);
  GstBuffer *inbuf = trans->queued_buf;
  GST_DEBUG_OBJECT(graph, "generate_output called with inbuf=%p", inbuf);

  GstAudioFormat format = GST_AUDIO_INFO_FORMAT(&graph->audio_info);
  const gint bytes_per_frame = GST_AUDIO_INFO_BPF(&graph->audio_info);

  // if buffer is not mapped yet, map it and calculate total_frames & remaining_frames
  if (!graph->input_buffer_state.is_mapped) {
    GST_DEBUG_OBJECT(graph, "inbuf is not mapped yet, mapping");
    gst_buffer_map(inbuf, &graph->input_buffer_state.map_info, GST_MAP_READ);
    graph->input_buffer_state.is_mapped = TRUE;

    graph->input_buffer_state.read_ptr = graph->input_buffer_state.map_info.data;
    graph->input_buffer_state.total_frames = graph->input_buffer_state.remaining_frames =
        graph->input_buffer_state.map_info.size / bytes_per_frame;

    GST_DEBUG_OBJECT(graph, "mapped inbuf (%ld bytes) to read_ptr=%p", graph->input_buffer_state.map_info.size,
                     graph->input_buffer_state.read_ptr);
  } else {
    GST_DEBUG_OBJECT(graph, "continuing to work on inbuf %p (read_ptr is at %p)", inbuf,
                     graph->input_buffer_state.read_ptr);
  }

  while (graph->input_buffer_state.remaining_frames > 0) {
    guint frames_to_process_until_next_measurement =
        graph->measurement_interval_frames - graph->frames_since_last_measurement;
    guint frames_to_process_until_next_video_frame =
        graph->video_interval_frames - graph->frames_since_last_video_frame;

    guint frames_to_next_action =
        MIN(frames_to_process_until_next_measurement, frames_to_process_until_next_video_frame);
    guint frames_to_process = MIN(frames_to_next_action, graph->input_buffer_state.remaining_frames);

    GST_DEBUG_OBJECT(graph,
                     "need to process %d frames until next measurement and "
                     "%d frames until next video-frame. Buffer has %d frames left. "
                     "going to process %d of %d frames",
                     frames_to_process_until_next_measurement, frames_to_process_until_next_video_frame,
                     graph->input_buffer_state.remaining_frames, frames_to_process,
                     graph->input_buffer_state.total_frames);

    gst_ebur128_add_frames(graph->state, format, graph->input_buffer_state.read_ptr, frames_to_process);

    graph->input_buffer_state.remaining_frames -= frames_to_process;
    graph->input_buffer_state.read_ptr += frames_to_process * bytes_per_frame;
    graph->frames_since_last_video_frame += frames_to_process;
    graph->frames_since_last_measurement += frames_to_process;
    graph->frames_processed += frames_to_process;

    if (graph->frames_since_last_measurement >= graph->measurement_interval_frames) {
      GST_DEBUG_OBJECT(graph, "taking measurement after %d audio-frames", graph->frames_since_last_measurement);
      graph->frames_since_last_measurement = 0;
      gst_ebur128graph_take_measurement(graph);
    }

    if (graph->frames_since_last_video_frame >= graph->video_interval_frames) {
      GST_DEBUG_OBJECT(graph, "emitting video-frame after %d audio-frames", graph->frames_since_last_video_frame);

      GstFlowReturn ret = gst_ebur128graph_generate_video_frame(graph, outbuf);

      graph->frames_since_last_video_frame = 0;
      GST_DEBUG_OBJECT(graph, "returning %s with outbuf=%p", gst_flow_get_name(ret), &outbuf);
      return ret;
    }
  }

  if (graph->input_buffer_state.remaining_frames == 0) {
    GST_DEBUG_OBJECT(graph, "inbuf consumed completely, unmapping");
    gst_buffer_unmap(inbuf, &graph->input_buffer_state.map_info);
    graph->input_buffer_state.is_mapped = FALSE;

    graph->input_buffer_state.read_ptr = NULL;
    graph->input_buffer_state.total_frames = graph->input_buffer_state.remaining_frames = 0;
  }

  return GST_FLOW_OK;
}

static void gst_ebur128graph_fill_video_frame(GstEbur128Graph *graph, GstBuffer *outbuf) {
  GstMapInfo map_info;
  gst_buffer_map(outbuf, &map_info, GST_MAP_WRITE);
  GST_DEBUG_OBJECT(graph, "mapped outbuf (%ld bytes)", map_info.size);

  GstVideoInfo *video_info = &graph->video_info;
  gint width = video_info->width;
  gint height = video_info->height;

  GST_LOG_OBJECT(graph, "Render w=%d h=%d, fmt=%s", width, height, graph->video_info.finfo->name);

  // copy background over
  // this can also be done with cairo (cairo_set_source_surface, cairo_rect,
  // cairo_fill) but because we *know* that both image surfaces use the same
  // format we can use memcpy which is probably quite a bit faster and we're on
  // the hot path here.
  memcpy(map_info.data, cairo_image_surface_get_data(graph->background_image), map_info.size);

  // create cairo image-surcface directly on the allocated buffer
  cairo_format_t cairo_format = gst_ebur128graph_get_cairo_format(graph);
  cairo_surface_t *image =
      cairo_image_surface_create_for_data(map_info.data, cairo_format, width, height, video_info->stride[0]);
  cairo_t *ctx = cairo_create(image);

  gst_ebur128graph_render_foreground(graph, ctx, width, height);

  cairo_destroy(ctx);
  cairo_surface_destroy(image);

  gst_buffer_unmap(outbuf, &map_info);
}

static GstFlowReturn gst_ebur128graph_generate_video_frame(GstEbur128Graph *graph, GstBuffer **outbuf) {
  GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_GET_CLASS(graph);
  GstBaseTransform *trans = GST_BASE_TRANSFORM(graph);

  GST_DEBUG_OBJECT(graph, "calling prepare buffer");
  GstFlowReturn ret = transform_class->prepare_output_buffer(GST_BASE_TRANSFORM(graph), trans->queued_buf, outbuf);

  GST_DEBUG_OBJECT(graph, "filled outbuf");
  gst_ebur128graph_fill_video_frame(graph, *outbuf);

  GstClockTime buffer_end = graph->frames_processed * GST_SECOND / graph->audio_info.rate;
  GST_BUFFER_TIMESTAMP(*outbuf) = graph->last_video_timestamp;
  GST_BUFFER_DURATION(*outbuf) = buffer_end - graph->last_video_timestamp;
  graph->last_video_timestamp = buffer_end;

  GST_BUFFER_OFFSET(*outbuf) = graph->num_video_frames_processed;
  GST_BUFFER_OFFSET_END(*outbuf) = ++graph->num_video_frames_processed;

  GST_DEBUG_OBJECT(
      graph, "set outbuf meta: timestamp=%" GST_TIME_FORMAT " duration=%" GST_TIME_FORMAT " offset=%ld offset_end=%ld",
      GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(*outbuf)), GST_TIME_ARGS(GST_BUFFER_DURATION(*outbuf)),
      GST_BUFFER_OFFSET(*outbuf), GST_BUFFER_OFFSET_END(*outbuf));

  return ret;
}

static void gst_ebur128graph_init(GstEbur128Graph *graph) {
  gst_audio_info_init(&graph->audio_info);
  gst_video_info_init(&graph->video_info);

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

  graph->properties.color_gauge_short_term = DEFAULT_COLOR_SHORT_TERM_GAUGE;
  graph->properties.color_gauge_momentary = DEFAULT_COLOR_MOMENTARY_GAUGE;
  graph->properties.color_gauge_peak = DEFAULT_COLOR_PEAK_GAUGE;

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
  graph->properties.timebase = DEFAULT_TIMEBASE;

  // gauges
  graph->properties.short_term_gauge = DEFAULT_SHORT_TERM_GAUGE;
  graph->properties.momentary_gauge = DEFAULT_MOMENTARY_GAUGE;
  graph->properties.peak_gauge = DEFAULT_PEAK_GAUGE;

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
  gint rate = GST_AUDIO_INFO_RATE(&graph->audio_info);
  gint channels = GST_AUDIO_INFO_CHANNELS(&graph->audio_info);
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
  case PROP_COLOR_SHORT_TERM_GAUGE:
    graph->properties.color_gauge_short_term = g_value_get_uint(value);
    break;
  case PROP_COLOR_MOMENTARY_GAUGE:
    graph->properties.color_gauge_momentary = g_value_get_uint(value);
    break;
  case PROP_COLOR_PEAK_GAUGE:
    graph->properties.color_gauge_peak = g_value_get_uint(value);
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
  case PROP_TIMEBASE:
    graph->properties.timebase = g_value_get_uint64(value);
    break;
  case PROP_SHORT_TERM_GAUGE:
    graph->properties.short_term_gauge = g_value_get_boolean(value);
    break;
  case PROP_MOMENTARY_GAUGE:
    graph->properties.momentary_gauge = g_value_get_boolean(value);
    break;
  case PROP_PEAK_GAUGE:
    graph->properties.peak_gauge = g_value_get_boolean(value);
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
  case PROP_COLOR_SHORT_TERM_GAUGE:
    g_value_set_uint(value, graph->properties.color_gauge_short_term);
    break;
  case PROP_COLOR_MOMENTARY_GAUGE:
    g_value_set_uint(value, graph->properties.color_gauge_momentary);
    break;
  case PROP_COLOR_PEAK_GAUGE:
    g_value_set_uint(value, graph->properties.color_gauge_peak);
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
  case PROP_TIMEBASE:
    g_value_set_uint64(value, graph->properties.timebase);
    break;
  case PROP_SHORT_TERM_GAUGE:
    g_value_set_boolean(value, graph->properties.short_term_gauge);
    break;
  case PROP_MOMENTARY_GAUGE:
    g_value_set_boolean(value, graph->properties.momentary_gauge);
    break;
  case PROP_PEAK_GAUGE:
    g_value_set_boolean(value, graph->properties.peak_gauge);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static cairo_format_t gst_ebur128graph_get_cairo_format(GstEbur128Graph *graph) {
  GstVideoInfo *video_info = &graph->video_info;
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

static void gst_ebur128graph_recalc_measurement_interval_frames(GstEbur128Graph *graph) {
  GstClockTime measurement_interval = graph->properties.timebase / graph->positions.graph.w;
  guint sample_rate = graph->audio_info.rate;

  guint measurement_interval_frames = GST_CLOCK_TIME_TO_FRAMES(measurement_interval, sample_rate);

  GST_INFO_OBJECT(graph,
                  "timebase=%" GST_TIME_FORMAT " for a graph of w=%d at sample_rate=%d "
                  "results in measurement_interval=%" GST_TIME_FORMAT ". "
                  "this results in measurement_interval_frames=%d "
                  "(number of audio-frames per measurement)",
                  GST_TIME_ARGS(graph->properties.timebase), graph->positions.graph.w, sample_rate,
                  GST_TIME_ARGS(measurement_interval), measurement_interval_frames);

  if (measurement_interval_frames == 0) {
    GST_WARNING_OBJECT(graph,
                       "measurement_interval=%" GST_TIME_FORMAT " is too small, "
                       "should be at least min=%" GST_TIME_FORMAT " for sample_rate=%u",
                       GST_TIME_ARGS(measurement_interval), GST_TIME_ARGS(GST_FRAMES_TO_CLOCK_TIME(1, sample_rate)),
                       sample_rate);
    measurement_interval_frames = 1;
  }

  graph->measurement_interval_frames = measurement_interval_frames;
}

static void gst_ebur128graph_recalc_video_interval_frames(GstEbur128Graph *graph) {
  guint sample_rate = graph->audio_info.rate;
  guint video_interval_frames = sample_rate * graph->video_info.fps_d / graph->video_info.fps_n;

  GST_INFO_OBJECT(graph,
                  "framerate=%d/%d at sample_rate=%d "
                  "results in video_interval_frames=%d "
                  "(number of audio-frames per video-frame)",
                  graph->video_info.fps_d, graph->video_info.fps_n, sample_rate, video_interval_frames);

  if (video_interval_frames == 0) {
    GST_WARNING_OBJECT(graph, "framerate=%d/%d is too high for sample_rate=%d", graph->video_info.fps_d,
                       graph->video_info.fps_n, sample_rate);

    video_interval_frames = 1;
  }

  graph->video_interval_frames = video_interval_frames;
}

static gboolean gst_ebur128graph_setup(GstEbur128Graph *graph) {
  // cleanup existing state
  gst_ebur128graph_destroy_libebur128(graph);
  gst_ebur128graph_destroy_cairo(graph);

  // initialize libraries
  gst_ebur128graph_init_libebur128(graph);
  gst_ebur128graph_init_cairo(graph);

  // re-calculate all positions
  gst_ebur128graph_calculate_positions(graph);

  // re-calculate audio-frame intervals to take measurements and emit video-frames
  gst_ebur128graph_recalc_measurement_interval_frames(graph);
  gst_ebur128graph_recalc_video_interval_frames(graph);

  // render background
  GstVideoInfo *video_info = &graph->video_info;
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
  GstVideoInfo *video_info = &graph->video_info;
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
  GstVideoInfo *video_info = &graph->video_info;
  gint width = video_info->width;
  gint height = video_info->height;

  gint gutter = graph->properties.gutter;

  guint num_gauges =
      graph->properties.short_term_gauge + graph->properties.momentary_gauge + graph->properties.peak_gauge;
  guint num_gauges_left = graph->properties.peak_gauge;
  guint gauges_reserved_space = (graph->properties.gauge_w - gutter) * num_gauges;
  guint gauges_reserved_space_left = (graph->properties.gauge_w - gutter) * num_gauges_left;

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
  graph->positions.scale.x = gutter + gauges_reserved_space_left;
  graph->positions.scale.y = gutter + graph->positions.header.h + gutter;

  // gauges
  graph->positions.gauge.w = graph->properties.gauge_w;
  graph->positions.gauge.h = graph->positions.scale.h;
  graph->positions.gauge.x = width - graph->positions.gauge.w - gutter;
  graph->positions.gauge.y = graph->positions.scale.y;

  // graph
  graph->positions.graph.w = width - gutter - graph->positions.scale.w - gutter - gutter - gauges_reserved_space;
  graph->positions.graph.h = graph->positions.scale.h;
  graph->positions.graph.x = gutter + graph->positions.scale.w + gutter + gauges_reserved_space_left;
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
