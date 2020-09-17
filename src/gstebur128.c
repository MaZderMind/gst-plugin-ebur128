/**
 * SECTION:element-ebur128
 *
 * FIXME:Calculates the EBU-R 128 Loudness of an Audio-Stream and emits them as
 * Message
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

#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <gst/gst.h>

#include "gstebur128.h"

GST_DEBUG_CATEGORY_STATIC(gst_ebur128_debug);
#define GST_CAT_DEFAULT gst_ebur128_debug

/* Filter signals and args */
enum { LAST_SIGNAL };

enum {
  PROP_0,
  PROP_MOMENTARY,
  PROP_SHORTTERM,
  PROP_GLOBAL,
  PROP_WINDOW,
  PROP_RANGE,
  PROP_SAMPLE_PEAK,
  PROP_TRUE_PEAK,
  PROP_MAX_HISTORY,
  PROP_POST_MESSAGES,
  PROP_INTERVAL
};

#define PROP_INTERVAL_DEFAULT (GST_SECOND / 10)

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */

#define SUPPORTED_AUDIO_FORMATS                                                \
  "{ " GST_AUDIO_NE(S16) ", " GST_AUDIO_NE(F32) ", " GST_AUDIO_NE(F64) " }"

#define SUPPORTED_AUDIO_CHANNELS "(int) {1, 2, 5 }"

#define SUPPORTED_CAPS_STRING                                                  \
  "audio/x-raw, "                                                              \
  "format = (string) " SUPPORTED_AUDIO_FORMATS ", "                            \
  "rate = " GST_AUDIO_RATE_RANGE ", "                                          \
  "channels = " SUPPORTED_AUDIO_CHANNELS ", "                                  \
  "layout = (string)interleaved "

static GstStaticPadTemplate sink_factory =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS(SUPPORTED_CAPS_STRING));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(SUPPORTED_CAPS_STRING));

#define gst_ebur128_parent_class parent_class
G_DEFINE_TYPE(Gstebur128, gst_ebur128, GST_TYPE_ELEMENT);

/* forward declarations */
static void gst_ebur128_set_property(GObject *object, guint prop_id,
                                     const GValue *value, GParamSpec *pspec);
static void gst_ebur128_get_property(GObject *object, guint prop_id,
                                     GValue *value, GParamSpec *pspec);
static void gst_ebur128_finalize(GObject *object);
static gboolean gst_ebur128_sink_event(GstPad *pad, GstObject *parent,
                                       GstEvent *event);
static GstFlowReturn gst_ebur128_chain(GstPad *pad, GstObject *parent,
                                       GstBuffer *buf);

static gint gst_ebur128_calculate_libebur128_mode(Gstebur128 *filter);
static void gst_ebur128_init_libebur128(Gstebur128 *filter);
static void gst_ebur128_reinit_libebur128_if_mode_changed(Gstebur128 *filter);
static void gst_ebur128_destroy_libebur128(Gstebur128 *filter);
static void gst_ebur128_recalc_interval_frames(Gstebur128 *filter);
static void gst_ebur128_count_frames_and_emit_message(Gstebur128 *filter,
                                                      gint frames_processed,
                                                      GstBuffer *buf);
static void gst_ebur128_post_message(Gstebur128 *filter);

/* GObject vmethod implementations */

/* initialize the ebur128's class */
static void gst_ebur128_class_init(Gstebur128Class *klass) {
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *)klass;
  gstelement_class = (GstElementClass *)klass;

  gobject_class->set_property = gst_ebur128_set_property;
  gobject_class->get_property = gst_ebur128_get_property;
  gobject_class->finalize = gst_ebur128_finalize;

  g_object_class_install_property(
      gobject_class, PROP_MOMENTARY,
      g_param_spec_boolean("momentary", "Momentary Loudness Metering",
                           "Enable Momentary Loudness Metering",
                           /* default */ TRUE,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_SHORTTERM,
      g_param_spec_boolean("shortterm", "Shortterm Loudness Metering",
                           "Enable Shortterm Loudness Metering",
                           /* default */ FALSE,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_GLOBAL,
      g_param_spec_boolean(
          "global", "Integrated (Global) Loudness Metering",
          "Enable Integrated (Global) Loudness Loudness Metering",
          /* default */ FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_WINDOW,
      g_param_spec_ulong("window", "Window Loudness Metering",
                         "Enable Window Loudness Metering by setting a "
                         "non-zero Window-Size in ms",
                         /* min */ 0,
                         /* max */ ULONG_MAX,
                         /* default */ 0,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_RANGE,
      g_param_spec_boolean(
          "range", "Loudness Range Metering", "Enable Loudness Range Metering",
          /* default */ FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_SAMPLE_PEAK,
      g_param_spec_boolean(
          "sample-peak", "Sample-Peak Metering", "Enable Sample-Peak Metering",
          /* default */ FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_TRUE_PEAK,
      g_param_spec_boolean(
          "true-peak", "True-Peak Metering", "Enable True-Peak Metering",
          /* default */ FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_MAX_HISTORY,
      g_param_spec_ulong(
          "max-history", "Maximum History Size",
          "Set the maximum history that will be stored for loudness "
          "integration. More history provides more accurate results, "
          "but requires more resources. "
          "Applies to Range Metering and Global Loudness Metering. "
          "Default is ULONG_MAX (at least ~50 days).",
          /* min */ 0,
          /* max */ ULONG_MAX,
          /* default */ ULONG_MAX, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_POST_MESSAGES,
      g_param_spec_boolean(
          "post-messages", "Post Messages",
          "Whether to post a 'loudness' element message on the bus for each "
          "passed interval",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property(
      gobject_class, PROP_INTERVAL,
      g_param_spec_uint64(
          "interval", "Interval",
          "Interval of time between message posts (in nanoseconds)", 1,
          G_MAXUINT64, PROP_INTERVAL_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_details_simple(
      gstelement_class, "ebur128", "Filter/Analyzer/Audio",
      "Calculates the EBU-R 128 Loudness of an Audio-Stream and "
      "emits them as Message",
      "Peter Körner <peter@mazdermind.de>");

  gst_element_class_add_pad_template(gstelement_class,
                                     gst_static_pad_template_get(&src_factory));
  gst_element_class_add_pad_template(
      gstelement_class, gst_static_pad_template_get(&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void gst_ebur128_init(Gstebur128 *filter) {
  // init pads
  filter->sinkpad = gst_pad_new_from_static_template(&sink_factory, "sink");
  gst_pad_set_event_function(filter->sinkpad,
                             GST_DEBUG_FUNCPTR(gst_ebur128_sink_event));
  gst_pad_set_chain_function(filter->sinkpad,
                             GST_DEBUG_FUNCPTR(gst_ebur128_chain));
  GST_PAD_SET_PROXY_CAPS(filter->sinkpad);
  gst_element_add_pad(GST_ELEMENT(filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template(&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS(filter->srcpad);
  gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);

  // init properties
  filter->momentary = TRUE;
  filter->shortterm = FALSE;
  filter->global = FALSE;
  filter->window = 0;
  filter->range = FALSE;
  filter->sample_peak = FALSE;
  filter->true_peak = FALSE;
  filter->max_history = ULONG_MAX;
  filter->post_messages = TRUE;
  filter->interval = PROP_INTERVAL_DEFAULT;

  filter->message_ts = GST_CLOCK_TIME_NONE;

  gst_audio_info_init(&filter->audio_info);
}

static void gst_ebur128_finalize(GObject *object) {
  Gstebur128 *filter = GST_EBUR128(object);
  gst_ebur128_destroy_libebur128(filter);
}

static gint gst_ebur128_calculate_libebur128_mode(Gstebur128 *filter) {
  gint mode = 0;

  if (filter->momentary)
    mode |= EBUR128_MODE_M;
  if (filter->shortterm)
    mode |= EBUR128_MODE_S;
  if (filter->global)
    mode |= EBUR128_MODE_I;
  if (filter->range)
    mode |= EBUR128_MODE_LRA;

  if (filter->sample_peak)
    mode |= EBUR128_MODE_SAMPLE_PEAK;
  if (filter->true_peak)
    mode |= EBUR128_MODE_TRUE_PEAK;

  return mode;
}

static void gst_ebur128_init_libebur128(Gstebur128 *filter) {
  gint rate = GST_AUDIO_INFO_RATE(&filter->audio_info);
  gint channels = GST_AUDIO_INFO_CHANNELS(&filter->audio_info);
  gint mode = gst_ebur128_calculate_libebur128_mode(filter);

  filter->state = ebur128_init(channels, rate, mode);
  if (filter->window > 0) {
    ebur128_set_max_window(filter->state, filter->window);
  }
  ebur128_set_max_history(filter->state, filter->max_history);

  GST_LOG_OBJECT(
      filter,
      "Initializing libebur128: "
      "rate=%d channels=%d mode=0x%x max_window=%lu, max_history=%lu",
      rate, channels, mode, filter->window, filter->max_history);
}

static void gst_ebur128_destroy_libebur128(Gstebur128 *filter) {
  if (filter->state != NULL) {
    GST_LOG_OBJECT(filter, "Destroying libebur128 State");
    ebur128_destroy(&filter->state);
  }
}

static void gst_ebur128_reinit_libebur128_if_mode_changed(Gstebur128 *filter) {
  if (!filter->state) {
    // libebur128 not initialized yet
    return;
  }

  gint new_mode = gst_ebur128_calculate_libebur128_mode(filter);
  gint current_mode = filter->state->mode;
  if (current_mode != new_mode) {
    GST_LOG_OBJECT(filter,
                   "libebur128 Mode has changed from %u to %u, Destroying and "
                   "Re-Initializing libebur128 state",
                   current_mode, new_mode);
    gst_ebur128_destroy_libebur128(filter);
    gst_ebur128_init_libebur128(filter);
  }
}

// Borrowed from gstlevel:
// https://github.com/GStreamer/gst-plugins-good/blob/46989dc/gst/level/gstlevel.c#L385
static void gst_ebur128_recalc_interval_frames(Gstebur128 *filter) {
  GstClockTime interval = filter->interval;
  guint sample_rate = GST_AUDIO_INFO_RATE(&filter->audio_info);
  guint interval_frames;

  interval_frames = GST_CLOCK_TIME_TO_FRAMES(interval, sample_rate);

  if (interval_frames == 0) {
    GST_WARNING_OBJECT(
        filter,
        "interval %" GST_TIME_FORMAT " is too small, "
        "should be at least %" GST_TIME_FORMAT " for sample rate %u",
        GST_TIME_ARGS(interval),
        GST_TIME_ARGS(GST_FRAMES_TO_CLOCK_TIME(1, sample_rate)), sample_rate);
    interval_frames = 1;
  }

  filter->interval_frames = interval_frames;

  GST_INFO_OBJECT(filter,
                  "interval_frames now %u for interval "
                  "%" GST_TIME_FORMAT " and sample rate %u",
                  interval_frames, GST_TIME_ARGS(interval), sample_rate);
}

static void gst_ebur128_count_frames_and_emit_message(Gstebur128 *filter,
                                                      gint frames_processed,
                                                      GstBuffer *buf) {
  /* Handle Timestamping and Messaging */
  if (GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DISCONT)) {
    filter->message_ts = GST_BUFFER_TIMESTAMP(buf);
  }
  if (G_UNLIKELY(!GST_CLOCK_TIME_IS_VALID(filter->message_ts))) {
    filter->message_ts = GST_BUFFER_TIMESTAMP(buf);
  }

  filter->frames_processed_in_interval += frames_processed;

  guint sample_rate = GST_AUDIO_INFO_RATE(&filter->audio_info);
  GstClockTime duration =
      GST_FRAMES_TO_CLOCK_TIME(frames_processed, sample_rate);
  filter->message_ts += duration;

  if (filter->frames_processed_in_interval > filter->interval_frames) {
    GST_DEBUG_OBJECT(filter,
                     "processed %u frames which is over the configured "
                     "interval_frames of %u (for interval of %" GST_TIME_FORMAT
                     ")",
                     filter->frames_processed_in_interval,
                     filter->interval_frames, GST_TIME_ARGS(filter->interval));

    if (filter->post_messages) {
      gst_ebur128_post_message(filter);
    }

    filter->frames_processed_in_interval = 0;
  }
}

static void gst_ebur128_post_message(Gstebur128 *filter) {
  GST_INFO_OBJECT(filter,
                  "emitting message "
                  "message_ts=%" GST_TIME_FORMAT " ",
                  GST_TIME_ARGS(filter->message_ts));
}

static void gst_ebur128_set_property(GObject *object, guint prop_id,
                                     const GValue *value, GParamSpec *pspec) {
  Gstebur128 *filter = GST_EBUR128(object);

  switch (prop_id) {
  case PROP_MOMENTARY:
    filter->momentary = g_value_get_boolean(value);
    break;
  case PROP_SHORTTERM:
    filter->shortterm = g_value_get_boolean(value);
    break;
  case PROP_GLOBAL:
    filter->global = g_value_get_boolean(value);
    break;
  case PROP_WINDOW:
    filter->window = g_value_get_ulong(value);
    break;
  case PROP_RANGE:
    filter->range = g_value_get_boolean(value);
    break;
  case PROP_SAMPLE_PEAK:
    filter->sample_peak = g_value_get_boolean(value);
    break;
  case PROP_TRUE_PEAK:
    filter->sample_peak = g_value_get_boolean(value);
    break;
  case PROP_MAX_HISTORY:
    filter->max_history = g_value_get_ulong(value);
    break;
  case PROP_POST_MESSAGES:
    filter->post_messages = g_value_get_boolean(value);
    break;
  case PROP_INTERVAL:
    filter->interval = g_value_get_uint64(value);
    /* Not exactly thread-safe, but property does not advertise that it
     * can be changed at runtime anyway */
    if (GST_AUDIO_INFO_RATE(&filter->audio_info)) {
      gst_ebur128_recalc_interval_frames(filter);
    }
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }

  gst_ebur128_reinit_libebur128_if_mode_changed(filter);
}

static void gst_ebur128_get_property(GObject *object, guint prop_id,
                                     GValue *value, GParamSpec *pspec) {
  Gstebur128 *filter = GST_EBUR128(object);

  switch (prop_id) {
  case PROP_MOMENTARY:
    g_value_set_boolean(value, filter->momentary);
    break;
  case PROP_SHORTTERM:
    g_value_set_boolean(value, filter->shortterm);
    break;
  case PROP_GLOBAL:
    g_value_set_boolean(value, filter->global);
    break;
  case PROP_WINDOW:
    g_value_set_ulong(value, filter->window);
    break;
  case PROP_RANGE:
    g_value_set_boolean(value, filter->range);
    break;
  case PROP_SAMPLE_PEAK:
    g_value_set_boolean(value, filter->sample_peak);
    break;
  case PROP_TRUE_PEAK:
    g_value_set_boolean(value, filter->true_peak);
    break;
  case PROP_MAX_HISTORY:
    g_value_set_ulong(value, filter->max_history);
    break;
  case PROP_POST_MESSAGES:
    g_value_set_boolean(value, filter->post_messages);
    break;
  case PROP_INTERVAL:
    g_value_set_uint64(value, filter->interval);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean gst_ebur128_sink_event(GstPad *pad, GstObject *parent,
                                       GstEvent *event) {
  Gstebur128 *filter;
  gboolean ret;

  filter = GST_EBUR128(parent);

  GST_LOG_OBJECT(filter, "Received %s event: %" GST_PTR_FORMAT,
                 GST_EVENT_TYPE_NAME(event), event);

  switch (GST_EVENT_TYPE(event)) {
  case GST_EVENT_CAPS: {
    /* parse caps */
    GstCaps *caps;
    gst_event_parse_caps(event, &caps);

    /* store audio-info */
    if (!gst_audio_info_from_caps(&filter->audio_info, caps)) {
      GST_ERROR_OBJECT(filter, "Unhandled Caps: %" GST_PTR_FORMAT, caps);
    }

    /* init libebur128 */
    gst_ebur128_init_libebur128(filter);

    /* calculate interval */
    gst_ebur128_recalc_interval_frames(filter);

    /* forward event */
    ret = gst_pad_event_default(pad, parent, event);
    break;
  }
  default:
    ret = gst_pad_event_default(pad, parent, event);
    break;
  }
  return ret;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn gst_ebur128_chain(GstPad *pad, GstObject *parent,
                                       GstBuffer *buf) {
  Gstebur128 *filter;

  filter = GST_EBUR128(parent);

  GstMapInfo map_info;
  gst_buffer_map(buf, &map_info, GST_MAP_READ);

  GstAudioFormat format = GST_AUDIO_INFO_FORMAT(&filter->audio_info);
  const gint bytes_per_frame = GST_AUDIO_INFO_BPF(&filter->audio_info);
  const gint num_frames = map_info.size / bytes_per_frame;

  GST_DEBUG_OBJECT(filter,
                   "Got %s Buffer of %lu bytes. Adding %u frames of %u bytes "
                   "in %u channels to libebur128",
                   GST_AUDIO_INFO_NAME(&filter->audio_info), map_info.size,
                   num_frames, bytes_per_frame,
                   GST_AUDIO_INFO_CHANNELS(&filter->audio_info));

  switch (format) {
  case GST_AUDIO_FORMAT_S16LE:
  case GST_AUDIO_FORMAT_S16BE:
    ebur128_add_frames_int(filter->state, (const int *)map_info.data,
                           num_frames);
    break;
  case GST_AUDIO_FORMAT_F32LE:
  case GST_AUDIO_FORMAT_F32BE:
    ebur128_add_frames_float(filter->state, (const float *)map_info.data,
                             num_frames);
    break;
  case GST_AUDIO_FORMAT_F64LE:
  case GST_AUDIO_FORMAT_F64BE:
    ebur128_add_frames_double(filter->state, (const double *)map_info.data,
                              num_frames);
    break;
  default:
    GST_ERROR_OBJECT(filter, "Unhandled Audio-Format: %s",
                     GST_AUDIO_INFO_NAME(&filter->audio_info));
  }

  gst_buffer_unmap(buf, &map_info);

  gst_ebur128_count_frames_and_emit_message(filter, num_frames, buf);

  /* push out the incoming buffer without touching it */
  return gst_pad_push(filter->srcpad, buf);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean ebur128_plugin_init(GstPlugin *ebur128) {
  /* debug category for fltering log messages
   */
  GST_DEBUG_CATEGORY_INIT(gst_ebur128_debug, "ebur128", 0, "EBU-R 128 Plugin");

  return gst_element_register(ebur128, "ebur128", GST_RANK_NONE,
                              GST_TYPE_EBUR128);
}

/* PACKAGE: this is usually set by meson depending on some _INIT macro
 * in meson.build and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use meson to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "ebur128"
#endif

/* gstreamer looks for this structure to register ebur128s
 */
GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR, GST_VERSION_MINOR, ebur128,
    "The EBU-R 128 Plugin ('ebur128') provides Elements for calculating the "
    "EBU-R 128 Loudness of an Audio-Stream and "
    "to generate a Video-Stream visualizing the Loudness over Time",
    ebur128_plugin_init, PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
