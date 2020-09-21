/**
 * SECTION:element-ebur128
 *
 * Calculates the EBU-R 128 Loudness of an Audio-Stream and emits them as
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

/* suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) as logn as GArray is not supported
 * everywhere */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

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
  "{ " GST_AUDIO_NE(S16) ", " GST_AUDIO_NE(S24) "," GST_AUDIO_NE(              \
      F32) ", " GST_AUDIO_NE(F64) " }"

#define SUPPORTED_AUDIO_CHANNELS "(int) {1, 2, 5 }"

#define SUPPORTED_CAPS_STRING                                                  \
  "audio/x-raw, "                                                              \
  "format = (string) " SUPPORTED_AUDIO_FORMATS ", "                            \
  "rate = " GST_AUDIO_RATE_RANGE ", "                                          \
  "channels = " SUPPORTED_AUDIO_CHANNELS ", "                                  \
  "layout = (string)interleaved "

static GstStaticPadTemplate sink_template_factory =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS(SUPPORTED_CAPS_STRING));

static GstStaticPadTemplate src_template_factory = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(SUPPORTED_CAPS_STRING));

#define gst_ebur128_parent_class parent_class
G_DEFINE_TYPE(Gstebur128, gst_ebur128, GST_TYPE_BASE_TRANSFORM);

/* forward declarations */
static void gst_ebur128_set_property(GObject *object, guint prop_id,
                                     const GValue *value, GParamSpec *pspec);
static void gst_ebur128_get_property(GObject *object, guint prop_id,
                                     GValue *value, GParamSpec *pspec);
static void gst_ebur128_finalize(GObject *object);

static gboolean gst_ebur128_set_caps(GstBaseTransform *trans, GstCaps *in,
                                     GstCaps *out);
static gboolean gst_ebur128_start(GstBaseTransform *trans);
static gboolean gst_ebur128_sink_event(GstBaseTransform *trans,
                                       GstEvent *event);
static GstFlowReturn gst_ebur128_transform_ip(GstBaseTransform *trans,
                                              GstBuffer *in);

static gint gst_ebur128_calculate_libebur128_mode(Gstebur128 *filter);
static void gst_ebur128_init_libebur128(Gstebur128 *filter);
static void gst_ebur128_reinit_libebur128_if_mode_changed(Gstebur128 *filter);
static void gst_ebur128_destroy_libebur128(Gstebur128 *filter);
static void gst_ebur128_recalc_interval_frames(Gstebur128 *filter);
static gboolean gst_ebur128_post_message(Gstebur128 *filter);
typedef int (*per_channel_func_t)(ebur128_state *st,
                                  unsigned int channel_number, double *out);

static gboolean gst_ebur128_fill_channel_array(Gstebur128 *filter,
                                               GValue *array_gvalue,
                                               const char *func_name,
                                               per_channel_func_t func);

static gboolean gst_ebur128_validate_lib_return(Gstebur128 *filter,
                                                const char *invocation,
                                                const int return_value);

static gboolean gst_ebur128_add_frames(Gstebur128 *filter,
                                       GstAudioFormat format, guint8 *data,
                                       gint num_frames);

/* GObject vmethod implementations */

/* initialize the ebur128's class */
static void gst_ebur128_class_init(Gstebur128Class *klass) {
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS(klass);

  gobject_class->set_property = gst_ebur128_set_property;
  gobject_class->get_property = gst_ebur128_get_property;
  gobject_class->finalize = gst_ebur128_finalize;

  // configure gobject properties
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

  gst_element_class_add_static_pad_template(element_class,
                                            &sink_template_factory);
  gst_element_class_add_static_pad_template(element_class,
                                            &src_template_factory);

  gst_element_class_set_static_metadata(
      element_class, "ebur128", "Filter/Analyzer/Audio",
      "Calculates the EBU-R 128 Loudness of an Audio-Stream and "
      "emits them as Message",
      "Peter Körner <peter@mazdermind.de>");

  // configure vmethods
  trans_class->set_caps = GST_DEBUG_FUNCPTR(gst_ebur128_set_caps);
  trans_class->start = GST_DEBUG_FUNCPTR(gst_ebur128_start);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR(gst_ebur128_transform_ip);
  trans_class->sink_event = GST_DEBUG_FUNCPTR(gst_ebur128_sink_event);

  // enable passthrough
  trans_class->passthrough_on_same_caps = TRUE;
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void gst_ebur128_init(Gstebur128 *filter) {
  // configure base-transform class
  gst_base_transform_set_gap_aware(GST_BASE_TRANSFORM(filter), TRUE);

  // init property values
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

static gboolean gst_ebur128_post_message(Gstebur128 *filter) {
  if (!filter->post_messages) {
    return TRUE;
  }

  GstBaseTransform *trans = GST_BASE_TRANSFORM_CAST(filter);

  // Increment Message-Timestamp
  guint sample_rate = GST_AUDIO_INFO_RATE(&filter->audio_info);
  GstClockTime duration_processed =
      GST_FRAMES_TO_CLOCK_TIME(filter->frames_processed, sample_rate);

  GstClockTime timestamp = filter->start_ts + duration_processed;
  GstClockTime running_time =
      gst_segment_to_running_time(&trans->segment, GST_FORMAT_TIME, timestamp);
  GstClockTime stream_time =
      gst_segment_to_stream_time(&trans->segment, GST_FORMAT_TIME, timestamp);

  GstStructure *structure =
      gst_structure_new("loudness", "timestamp", G_TYPE_UINT64, timestamp,
                        "stream-time", G_TYPE_UINT64, stream_time,
                        "running-time", G_TYPE_UINT64, running_time, NULL);

  gboolean success = TRUE;
  // momentary loudness (last 400ms) in LUFS.
  if (filter->momentary) {
    double momentary;
    int ret = ebur128_loudness_momentary(filter->state, &momentary);
    success &= gst_ebur128_validate_lib_return(
        filter, "ebur128_loudness_momentary", ret);
    gst_structure_set(structure, "momentary", G_TYPE_DOUBLE, momentary, NULL);
  }

  // short-term loudness (last 3s) in LUFS.
  if (filter->shortterm) {
    double shortterm;
    int ret = ebur128_loudness_shortterm(filter->state, &shortterm);
    success &= gst_ebur128_validate_lib_return(
        filter, "ebur128_loudness_shortterm", ret);
    gst_structure_set(structure, "shortterm", G_TYPE_DOUBLE, shortterm, NULL);
  }

  // global integrated loudness in LUFS.
  if (filter->global) {
    double global;
    int ret = ebur128_loudness_global(filter->state, &global);
    success &=
        gst_ebur128_validate_lib_return(filter, "ebur128_loudness_global", ret);
    gst_structure_set(structure, "global", G_TYPE_DOUBLE, global, NULL);
  }

  // loudness of the specified window in LUFS.
  if (filter->window > 0) {
    double window;
    int ret = ebur128_loudness_window(filter->state, filter->window, &window);
    success &=
        gst_ebur128_validate_lib_return(filter, "ebur128_loudness_window", ret);
    gst_structure_set(structure, "window", G_TYPE_DOUBLE, window, NULL);
  }

  // loudness range (LRA) of programme in LU.
  if (filter->range) {
    double range;
    int ret = ebur128_loudness_range(filter->state, &range);
    success &=
        gst_ebur128_validate_lib_return(filter, "ebur128_loudness_range", ret);
    gst_structure_set(structure, "range", G_TYPE_DOUBLE, range, NULL);
  }

  // Maximum sample peak in float format (1.0 is 0 dBFS) from the last Frames,
  // by channel. The equation to convert to dBFS is: 20 * log10(out).
  if (filter->sample_peak) {
    GValue sample_peak = {
        0,
    };
    success &= gst_ebur128_fill_channel_array(
        filter, &sample_peak, "ebur128_sample_peak", &ebur128_sample_peak);
    gst_structure_take_value(structure, "sample-peak", &sample_peak);
  }

  // Maximum true peak in float format (1.0 is 0 dBFS) from all frames that have
  // been processed, by channel. The eqation to convert to dBTP is: 20 *
  // log10(out).
  if (filter->true_peak) {
    GValue true_peak = {
        0,
    };
    success &= gst_ebur128_fill_channel_array(
        filter, &true_peak, "ebur128_true_peak", &ebur128_true_peak);
    gst_structure_take_value(structure, "true-peak", &true_peak);
  }

  if (success) {
    GstMessage *message =
        gst_message_new_element(GST_OBJECT(filter), structure);
    gst_element_post_message(GST_ELEMENT(filter), message);

    GST_INFO_OBJECT(filter, "emitting loudness-message at %" GST_TIME_FORMAT,
                    GST_TIME_ARGS(timestamp));

  } else {
    GST_ERROR_OBJECT(
        filter,
        "error getting the requested calculation results from libebur128");
  }
  return success;
}

static gboolean gst_ebur128_fill_channel_array(Gstebur128 *filter,
                                               GValue *array_gvalue,
                                               const char *func_name,
                                               per_channel_func_t func) {
  g_value_init(array_gvalue, G_TYPE_VALUE_ARRAY);
  GValueArray *array = g_value_array_new(0);
  g_value_take_boxed(array_gvalue, array);

  double double_value = 0.0;
  GValue double_gvalue = {
      0,
  };
  g_value_init(&double_gvalue, G_TYPE_DOUBLE);

  gint channels = GST_AUDIO_INFO_CHANNELS(&filter->audio_info);

  gboolean success = TRUE;

  for (gint channel = 0; channel < channels; channel++) {
    int ret = func(filter->state, channel, &double_value);
    success &= gst_ebur128_validate_lib_return(filter, func_name, ret);
    g_value_set_double(&double_gvalue, double_value);
    g_value_array_append(array, &double_gvalue);
  }

  return success;
}

static gboolean gst_ebur128_validate_lib_return(Gstebur128 *filter,
                                                const char *invocation,
                                                const int return_value) {
  if (return_value != EBUR128_SUCCESS) {
    GST_ERROR_OBJECT(filter, "Error-Code %d from libebur128 call to %s",
                     return_value, invocation);
    return FALSE;
  }

  return TRUE;
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
    filter->true_peak = g_value_get_boolean(value);
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

static gboolean gst_ebur128_set_caps(GstBaseTransform *trans, GstCaps *in,
                                     GstCaps *out) {
  Gstebur128 *filter = GST_EBUR128(trans);

  GST_LOG_OBJECT(filter, "Received Caps in:  %" GST_PTR_FORMAT, in);
  GST_LOG_OBJECT(filter, "Received Caps out: %" GST_PTR_FORMAT, out);

  /* store audio-info */
  if (!gst_audio_info_from_caps(&filter->audio_info, in)) {
    GST_ERROR_OBJECT(filter, "Unhandled Caps: %" GST_PTR_FORMAT, in);
    return FALSE;
  }

  /* init libebur128 */
  gst_ebur128_init_libebur128(filter);

  /* calculate interval */
  gst_ebur128_recalc_interval_frames(filter);

  return TRUE;
}

static gboolean gst_ebur128_sink_event(GstBaseTransform *trans,
                                       GstEvent *event) {
  if (GST_EVENT_TYPE(event) == GST_EVENT_EOS) {
    Gstebur128 *filter = GST_EBUR128(trans);

    if (filter->state) {
      GST_DEBUG_OBJECT(filter, "received EOS, emitting last Message");
      gst_ebur128_post_message(filter);
    }
  }

  return GST_BASE_TRANSFORM_CLASS(parent_class)->sink_event(trans, event);
}

static gboolean gst_ebur128_start(GstBaseTransform *trans) {
  Gstebur128 *filter = GST_EBUR128(trans);

  filter->start_ts = GST_CLOCK_TIME_NONE;
  filter->frames_since_last_mesage = 0;

  return TRUE;
}

static GstFlowReturn gst_ebur128_transform_ip(GstBaseTransform *trans,
                                              GstBuffer *buf) {
  Gstebur128 *filter = GST_EBUR128(trans);

  // Map and Analyze buffer
  GstMapInfo map_info;
  gst_buffer_map(buf, &map_info, GST_MAP_READ);

  GstAudioFormat format = GST_AUDIO_INFO_FORMAT(&filter->audio_info);
  const gint bytes_per_frame = GST_AUDIO_INFO_BPF(&filter->audio_info);
  const gint num_frames = map_info.size / bytes_per_frame;
  const gint channels = GST_AUDIO_INFO_CHANNELS(&filter->audio_info);

  // Manage Message-Timestamp
  if (GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DISCONT)) {
    filter->start_ts = GST_BUFFER_TIMESTAMP(buf);
  }
  if (G_UNLIKELY(!GST_CLOCK_TIME_IS_VALID(filter->start_ts))) {
    filter->start_ts = GST_BUFFER_TIMESTAMP(buf);
  }

  GST_DEBUG_OBJECT(filter,
                   "Got %s Buffer of %lu bytes representing %u frames of %u "
                   "bytes in %u channels.",
                   GST_AUDIO_INFO_NAME(&filter->audio_info), map_info.size,
                   num_frames, bytes_per_frame, channels);

  gboolean success = TRUE;
  if (filter->frames_since_last_mesage + num_frames <=
      filter->interval_frames) {
    // frames to fit, at once

    success &=
        gst_ebur128_add_frames(filter, format, map_info.data, num_frames);

    filter->frames_since_last_mesage += num_frames;
  } else {
    // first half
    const guint first_half =
        filter->interval_frames - filter->frames_since_last_mesage;
    const guint second_half = num_frames - first_half;

    GST_DEBUG_OBJECT(filter,
                     "Adding %u frames to already processed %u frames would "
                     "exceed interval_frames=%u, processing in two "
                     "halfs of %u and %u frames each",
                     num_frames, filter->frames_since_last_mesage,
                     filter->interval_frames, first_half, second_half);

    success &=
        gst_ebur128_add_frames(filter, format, map_info.data, first_half);

    gst_ebur128_post_message(filter);

    // second half
    guint8 *second_half_ptr = map_info.data + (first_half * bytes_per_frame);
    success &=
        gst_ebur128_add_frames(filter, format, second_half_ptr, second_half);

    filter->frames_since_last_mesage = second_half;
  }

  if (filter->frames_since_last_mesage >= filter->interval_frames) {
    gst_ebur128_post_message(filter);
    filter->frames_since_last_mesage = 0;
  }

  gst_buffer_unmap(buf, &map_info);

  return success ? GST_FLOW_OK : GST_FLOW_ERROR;
}

static gboolean gst_ebur128_add_frames(Gstebur128 *filter,
                                       GstAudioFormat format, guint8 *data,
                                       gint num_frames) {
  gboolean success = TRUE;
  int ret;

  GST_DEBUG_OBJECT(filter, "Adding %u frames to libebur128 at %p", num_frames,
                   data);

  switch (format) {
  case GST_AUDIO_FORMAT_S16LE:
  case GST_AUDIO_FORMAT_S16BE:
    ret = ebur128_add_frames_short(filter->state, (const short *)data,
                                   num_frames);
    success &= gst_ebur128_validate_lib_return(filter,
                                               "ebur128_add_frames_short", ret);
    break;
  case GST_AUDIO_FORMAT_S24LE:
  case GST_AUDIO_FORMAT_S24BE:
    ret = ebur128_add_frames_int(filter->state, (const int *)data, num_frames);
    success &=
        gst_ebur128_validate_lib_return(filter, "ebur128_add_frames_int", ret);
    break;
  case GST_AUDIO_FORMAT_F32LE:
  case GST_AUDIO_FORMAT_F32BE:
    ret = ebur128_add_frames_float(filter->state, (const float *)data,
                                   num_frames);

    success &= gst_ebur128_validate_lib_return(filter,
                                               "ebur128_add_frames_float", ret);
    break;
  case GST_AUDIO_FORMAT_F64LE:
  case GST_AUDIO_FORMAT_F64BE:
    ret = ebur128_add_frames_double(filter->state, (const double *)data,
                                    num_frames);
    success &= gst_ebur128_validate_lib_return(
        filter, "ebur128_add_frames_double", ret);
    break;
  default:
    GST_ERROR_OBJECT(filter, "Unhandled Audio-Format: %s",
                     GST_AUDIO_INFO_NAME(&filter->audio_info));
    success = FALSE;
  }

  if (success) {
    filter->frames_processed += num_frames;
  }

  return success;
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
