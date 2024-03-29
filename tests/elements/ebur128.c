/* suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifndef GST_PLUGIN_LOADING_WHITELIST
#define GST_PLUGIN_LOADING_WHITELIST ""
#endif

#include <gst/audio/audio.h>
#include <gst/check/gstcheck.h>

#define SUPPORTED_AUDIO_FORMATS                                                                                        \
  "{ " GST_AUDIO_NE(S16) ", " GST_AUDIO_NE(S32) "," GST_AUDIO_NE(F32) ", " GST_AUDIO_NE(F64) " }"

#define SUPPORTED_AUDIO_CHANNELS "(int) {1, 2, 5 }"

#define SUPPORTED_CAPS_STRING                                                                                          \
  "audio/x-raw, "                                                                                                      \
  "format = (string) " SUPPORTED_AUDIO_FORMATS ", "                                                                    \
  "rate = " GST_AUDIO_RATE_RANGE ", "                                                                                  \
  "channels = " SUPPORTED_AUDIO_CHANNELS ", "                                                                          \
  "layout = (string) interleaved "

#define S16_CAPS_STRING                                                                                                \
  "audio/x-raw, "                                                                                                      \
  "format = (string) " GST_AUDIO_NE(S16) ", "                                                                          \
                                         "layout = (string) interleaved, "                                             \
                                         "rate = (int) 48000, "                                                        \
                                         "channels = (int) 2"

#define S32_CAPS_STRING                                                                                                \
  "audio/x-raw, "                                                                                                      \
  "format = (string) " GST_AUDIO_NE(S32) ", "                                                                          \
                                         "layout = (string) interleaved, "                                             \
                                         "rate = (int) 48000, "                                                        \
                                         "channels = (int) 2"
#define F32_CAPS_STRING                                                                                                \
  "audio/x-raw, "                                                                                                      \
  "format = (string) " GST_AUDIO_NE(F32) ", "                                                                          \
                                         "layout = (string) interleaved, "                                             \
                                         "rate = (int) 48000, "                                                        \
                                         "channels = (int) 2"
#define F64_CAPS_STRING                                                                                                \
  "audio/x-raw, "                                                                                                      \
  "format = (string) " GST_AUDIO_NE(F64) ", "                                                                          \
                                         "layout = (string) interleaved, "                                             \
                                         "rate = (int) 48000, "                                                        \
                                         "channels = (int) 2"

static GstStaticPadTemplate sinktemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(SUPPORTED_CAPS_STRING));
static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(SUPPORTED_CAPS_STRING));

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;
static GstElement *element;
static GstBus *bus;

/* takes over reference for outcaps */
static void setup_element(const gchar *caps_str) {
  GST_INFO("setup_element");
  element = gst_check_setup_element("ebur128");
  mysrcpad = gst_check_setup_src_pad(element, &srctemplate);
  mysinkpad = gst_check_setup_sink_pad(element, &sinktemplate);
  gst_pad_set_active(mysrcpad, TRUE);
  gst_pad_set_active(mysinkpad, TRUE);

  /* setup event capturing */
  GstCaps *caps = gst_caps_from_string(caps_str);
  gst_check_setup_events(mysrcpad, element, caps, GST_FORMAT_TIME);
  gst_caps_unref(caps);

  /* set to playing */
  fail_unless(gst_element_set_state(element, GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
              "could not set to playing");

  /* create a bus to get the element message on */
  bus = gst_bus_new();
  ASSERT_OBJECT_REFCOUNT(bus, "bus", 1);
  gst_element_set_bus(element, bus);
  ASSERT_OBJECT_REFCOUNT(bus, "bus", 2);
}

static void cleanup_element() {
  GST_INFO("cleanup_element");

  /* flush bus */
  gst_bus_set_flushing(bus, TRUE);

  /* cleanup bus */
  gst_element_set_bus(element, NULL);
  ASSERT_OBJECT_REFCOUNT(bus, "bus", 1);
  gst_object_unref(bus);
  fail_unless(gst_element_set_state(element, GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
  ASSERT_OBJECT_REFCOUNT(element, "element", 1);

  gst_check_drop_buffers();
  gst_pad_set_active(mysrcpad, FALSE);
  gst_pad_set_active(mysinkpad, FALSE);
  gst_check_teardown_src_pad(element);
  gst_check_teardown_sink_pad(element);
  gst_check_teardown_element(element);
}

static void caps_to_audio_info(const char *caps_string, GstAudioInfo *audio_info) {
  GstCaps *caps = gst_caps_from_string(caps_string);
  gst_audio_info_from_caps(audio_info, caps);
  gst_caps_unref(caps);
}

static GstBuffer *create_buffer(const char *caps_string, const guint num_msecs) {
  GstAudioInfo audio_info;
  caps_to_audio_info(caps_string, &audio_info);

  guint num_frames = audio_info.rate * num_msecs / 1000;
  gsize num_bytes = audio_info.bpf * num_frames;
  GST_INFO("create buffer of %ld bytes for %d frames of %d bpf (%d bps * %d "
           "channels) in %d msecs",
           num_bytes, num_frames, audio_info.bpf, audio_info.bpf / audio_info.channels, audio_info.channels, num_msecs);
  GstBuffer *buf = gst_buffer_new_and_alloc(num_bytes);
  GST_BUFFER_TIMESTAMP(buf) = G_GUINT64_CONSTANT(0);

  return buf;
}

#define DEFINE_TRIANGLE_BUFFER(NAME, T, MIN, MAX)                                                                      \
  static void fill_triangle_buffer_##NAME(guint8 *buffer_data, guint num_samples_per_wave, guint num_frames,           \
                                          const guint channels) {                                                      \
    T *ptr = (T *)buffer_data;                                                                                         \
                                                                                                                       \
    for (guint frame_idx = 0; frame_idx < num_frames; frame_idx++) {                                                   \
                                                                                                                       \
      T sample = (frame_idx % num_samples_per_wave) * (MAX / num_samples_per_wave * 2) - MIN;                          \
                                                                                                                       \
      for (gint channel_idx = 0; channel_idx < channels; channel_idx++) {                                              \
        ptr[frame_idx * channels + channel_idx] = sample / 8;                                                          \
      }                                                                                                                \
    }                                                                                                                  \
  };

DEFINE_TRIANGLE_BUFFER(s16, gshort, G_MINSHORT, G_MAXSHORT)
DEFINE_TRIANGLE_BUFFER(s32, gint, G_MININT, G_MAXINT)
DEFINE_TRIANGLE_BUFFER(f32, gfloat, -1.0, 1.0)
DEFINE_TRIANGLE_BUFFER(f64, gdouble, -1.0, 1.0)

static GstBuffer *create_triangle_buffer(const char *caps_string, const guint num_msecs) {
  GstBuffer *buf = create_buffer(caps_string, num_msecs);

  GstAudioInfo audio_info;
  caps_to_audio_info(caps_string, &audio_info);

  GstMapInfo map;
  gst_buffer_map(buf, &map, GST_MAP_WRITE);

  // 500 Hz Triangle, 1/8 (0.2) FS
  guint num_samples_per_wave = audio_info.rate / 500 /* Hz */;
  guint num_frames = audio_info.rate * num_msecs / 1000;

  GST_INFO("num_samples_per_wave=%d", num_samples_per_wave);
  if (audio_info.finfo->format == GST_AUDIO_FORMAT_S16LE) {
    fill_triangle_buffer_s16(map.data, num_samples_per_wave, num_frames, audio_info.channels);
  } else if (audio_info.finfo->format == GST_AUDIO_FORMAT_S32LE) {
    fill_triangle_buffer_s32(map.data, num_samples_per_wave, num_frames, audio_info.channels);
  } else if (audio_info.finfo->format == GST_AUDIO_FORMAT_F32LE) {
    fill_triangle_buffer_f32(map.data, num_samples_per_wave, num_frames, audio_info.channels);
  } else if (audio_info.finfo->format == GST_AUDIO_FORMAT_F64LE) {
    fill_triangle_buffer_f64(map.data, num_samples_per_wave, num_frames, audio_info.channels);
  } else {
    fail("Unhandled Format");
  }

  gst_buffer_unmap(buf, &map);

  return buf;
}

GST_START_TEST(test_setup_and_teardown) {
  setup_element(S16_CAPS_STRING);
  cleanup_element();
}
GST_END_TEST;

GST_START_TEST(test_emits_message) {
  GstMessage *message;
  GstBuffer *inbuffer;

  setup_element(S16_CAPS_STRING);
  g_object_set(element, "interval", 100 * GST_MSECOND, NULL);

  /* create a fake 100 msec buffer with all zeros */
  inbuffer = create_buffer(S16_CAPS_STRING, 100);

  fail_unless(gst_pad_push(mysrcpad, inbuffer) == GST_FLOW_OK);

  message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, -1);
  ASSERT_OBJECT_REFCOUNT(message, "message", 1);

  fail_unless(message != NULL);
  fail_unless(GST_MESSAGE_SRC(message) == GST_OBJECT(element));
  fail_unless(GST_MESSAGE_TYPE(message) == GST_MESSAGE_ELEMENT);

  /* message has a ref to the element */
  ASSERT_OBJECT_REFCOUNT(element, "element", 2);
  gst_message_unref(message);
  ASSERT_OBJECT_REFCOUNT(element, "element", 1);

  cleanup_element();
}
GST_END_TEST;

GST_START_TEST(test_passess_buffer_unchaged) {
  GstBuffer *inbuffer, *outbuffer;

  setup_element(S16_CAPS_STRING);

  /* create a fake 100 msec buffer with all zeros */
  inbuffer = create_buffer(S16_CAPS_STRING, 100);
  ASSERT_BUFFER_REFCOUNT(inbuffer, "inbuffer", 1);

  /* pushing gives away the reference ... */
  fail_unless(gst_pad_push(mysrcpad, inbuffer) == GST_FLOW_OK);

  /* ... but it ends up being collected on the global buffer list */
  ASSERT_BUFFER_REFCOUNT(inbuffer, "inbuffer", 1);
  fail_unless_equals_int(g_list_length(buffers), 1);
  fail_if((outbuffer = (GstBuffer *)buffers->data) == NULL);
  fail_unless(inbuffer == outbuffer);

  cleanup_element();
}
GST_END_TEST;

GST_START_TEST(test_timestamps) {
  GstMessage *message;
  GstBuffer *inbuffer;

  setup_element(S16_CAPS_STRING);
  g_object_set(element, "interval", 100 * GST_MSECOND, NULL);

  for (size_t iteration = 0; iteration < 3; iteration++) {
    inbuffer = create_buffer(S16_CAPS_STRING, 100);
    gst_pad_push(mysrcpad, inbuffer);

    message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, -1);
    fail_unless(message != NULL);
    fail_unless(GST_MESSAGE_SRC(message) == GST_OBJECT(element));
    fail_unless(GST_MESSAGE_TYPE(message) == GST_MESSAGE_ELEMENT);

    const GstStructure *structure = gst_message_get_structure(message);
    ck_assert_str_eq(gst_structure_get_name(structure), "loudness");

    fail_unless(gst_structure_has_field(structure, "timestamp"));
    fail_unless(gst_structure_has_field(structure, "stream-time"));
    fail_unless(gst_structure_has_field(structure, "running-time"));

    GstClockTime expectation = GST_MSECOND * (iteration + 1) * 100;

    GstClockTime timestamp;
    gst_structure_get_clock_time(structure, "timestamp", &timestamp);
    GST_INFO("Gost timestamp=%" GST_TIME_FORMAT ", expected %" GST_TIME_FORMAT, GST_TIME_ARGS(timestamp),
             GST_TIME_ARGS(expectation));
    fail_unless(timestamp == expectation);

    GstClockTime stream_time;
    gst_structure_get_clock_time(structure, "stream-time", &stream_time);
    fail_unless(stream_time == expectation);

    GstClockTime running_time;
    gst_structure_get_clock_time(structure, "running-time", &running_time);
    fail_unless(running_time == expectation);

    gst_message_unref(message);
    message = NULL;
  }

  cleanup_element();
}
GST_END_TEST;

GST_START_TEST(test_per_channel_array) {
  setup_element(S16_CAPS_STRING);
  g_object_set(element,
               // interval
               "interval", 100 * GST_MSECOND,

               // enable sample-peak
               "sample-peak", TRUE,

               // enable true-peak
               "true-peak", TRUE,

               // sentinel
               NULL);

  GstBuffer *inbuffer = create_buffer(S16_CAPS_STRING, 100);
  gst_pad_push(mysrcpad, inbuffer);

  GstMessage *message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, -1);
  const GstStructure *structure = gst_message_get_structure(message);

  const GValue *sample_peak;
  sample_peak = gst_structure_get_value(structure, "sample-peak");
  fail_unless(G_VALUE_TYPE(sample_peak) == G_TYPE_VALUE_ARRAY);
  const GValueArray *sample_peak_channels = g_value_get_boxed(sample_peak);
  fail_unless(sample_peak_channels->n_values == 2);

  const GValue *true_peak;
  true_peak = gst_structure_get_value(structure, "sample-peak");
  fail_unless(G_VALUE_TYPE(true_peak) == G_TYPE_VALUE_ARRAY);
  const GValueArray *true_peak_channels = g_value_get_boxed(sample_peak);
  fail_unless(true_peak_channels->n_values == 2);

  gst_message_unref(message);
  cleanup_element();
}
GST_END_TEST;

GST_START_TEST(test_mode_change) {
  GstBuffer *inbuffer;
  GstMessage *message;
  const GstStructure *structure;

  setup_element(S16_CAPS_STRING);

  // initialize with only momentary enabled
  g_object_set(element,
               // interval
               "interval", 100 * GST_MSECOND,

               // enable momentary
               "momentary", TRUE,

               // sentinel
               NULL);

  inbuffer = create_buffer(S16_CAPS_STRING, 100);
  gst_pad_push(mysrcpad, inbuffer);

  message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, -1);
  structure = gst_message_get_structure(message);

  fail_unless(gst_structure_n_fields(structure) == 4);
  fail_unless(gst_structure_has_field(structure, "timestamp"));
  fail_unless(gst_structure_has_field(structure, "stream-time"));
  fail_unless(gst_structure_has_field(structure, "running-time"));
  fail_unless(gst_structure_has_field(structure, "momentary"));
  gst_message_unref(message);

  // also enable sample-peak and window
  g_object_set(element,
               // interval
               "interval", 100 * GST_MSECOND,

               // enable momentary
               "momentary", TRUE,

               // enable window
               "window", 200,

               // enable sample-peak
               "sample-peak", TRUE,

               // sentinel
               NULL);

  inbuffer = create_buffer(S16_CAPS_STRING, 100);
  gst_pad_push(mysrcpad, inbuffer);

  message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, -1);
  structure = gst_message_get_structure(message);

  fail_unless(gst_structure_n_fields(structure) == 6);
  fail_unless(gst_structure_has_field(structure, "timestamp"));
  fail_unless(gst_structure_has_field(structure, "stream-time"));
  fail_unless(gst_structure_has_field(structure, "running-time"));
  fail_unless(gst_structure_has_field(structure, "momentary"));
  fail_unless(gst_structure_has_field(structure, "window"));
  fail_unless(gst_structure_has_field(structure, "sample-peak"));
  gst_message_unref(message);

  cleanup_element();
}
GST_END_TEST;

static void test_accepts(const char *caps_str) {
  setup_element(caps_str);
  g_object_set(element, "interval", 1000 * GST_MSECOND, NULL);
  GstBuffer *inbuffer = create_triangle_buffer(caps_str, 1000);
  gst_pad_push(mysrcpad, inbuffer);

  GstMessage *message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, -1);
  const GstStructure *structure = gst_message_get_structure(message);

  gdouble momentary;
  gst_structure_get_double(structure, "momentary", &momentary);
  GST_INFO("got momentary=%f", momentary);
  fail_unless(-20.0 < momentary && momentary < -19.0);

  gst_message_unref(message);
  cleanup_element();
}

GST_START_TEST(test_accepts_s16) { test_accepts(S16_CAPS_STRING); }
GST_END_TEST;

GST_START_TEST(test_accepts_s32) { test_accepts(S32_CAPS_STRING); }
GST_END_TEST;

GST_START_TEST(test_accepts_f32) { test_accepts(F32_CAPS_STRING); }
GST_END_TEST;

GST_START_TEST(test_accepts_f64) { test_accepts(F64_CAPS_STRING); }
GST_END_TEST;

static void has_only_property(const char *prop_name) {
  GstMessage *message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, -1);
  const GstStructure *structure = gst_message_get_structure(message);

  fail_unless(gst_structure_n_fields(structure) == 4);
  fail_unless(gst_structure_has_field(structure, "timestamp"));
  fail_unless(gst_structure_has_field(structure, "stream-time"));
  fail_unless(gst_structure_has_field(structure, "running-time"));
  fail_unless(gst_structure_has_field(structure, prop_name));

  gst_message_unref(message);
}

static void test_bool_property(const char *prop_name) {
  setup_element(S16_CAPS_STRING);
  g_object_set(element,
               // configure interval
               "interval", 100 * GST_MSECOND,

               // null default-true of momentary
               "momentary", FALSE,

               // sentinel
               NULL);

  // expect to read back false
  gboolean read_back_value = TRUE;
  g_object_get(element, prop_name, &read_back_value, NULL);
  fail_if(read_back_value);

  g_object_set(element,
               // configure interval
               "interval", 100 * GST_MSECOND,

               // null default-true of momentary
               "momentary", FALSE,

               // set property
               prop_name, TRUE,

               // sentinel
               NULL);

  // expect to read back true
  read_back_value = FALSE;
  g_object_get(element, prop_name, &read_back_value, NULL);
  fail_unless(read_back_value);

  GstBuffer *inbuffer = create_buffer(S16_CAPS_STRING, 100);
  gst_pad_push(mysrcpad, inbuffer);

  has_only_property(prop_name);

  cleanup_element();
}

static void test_ulong_property(const char *prop_name, gulong value) {
  setup_element(S16_CAPS_STRING);

  g_object_set(element,
               // configure interval
               "interval", 100 * GST_MSECOND,

               // null default-true of momentary
               "momentary", FALSE,

               // set property
               prop_name, value,

               // sentinel
               NULL);

  // expect to read back the correct value
  gulong read_back_value = -1;
  g_object_get(element, prop_name, &read_back_value, NULL);
  fail_unless(read_back_value == value);

  GstBuffer *inbuffer = create_buffer(S16_CAPS_STRING, 1000);
  gst_pad_push(mysrcpad, inbuffer);

  has_only_property(prop_name);

  cleanup_element();
}

GST_START_TEST(test_prop_momentary) { test_bool_property("momentary"); }
GST_END_TEST;

GST_START_TEST(test_prop_shortterm) { test_bool_property("shortterm"); }
GST_END_TEST;

GST_START_TEST(test_prop_global) { test_bool_property("global"); }
GST_END_TEST;

GST_START_TEST(test_prop_window) { test_ulong_property("window", 42); }
GST_END_TEST;

GST_START_TEST(test_prop_range) { test_bool_property("range"); }
GST_END_TEST;

GST_START_TEST(test_prop_sample_peak) { test_bool_property("sample-peak"); }
GST_END_TEST;

GST_START_TEST(test_prop_true_peak) { test_bool_property("true-peak"); }
GST_END_TEST;

GST_START_TEST(test_prop_post_messages) {
  setup_element(S16_CAPS_STRING);
  g_object_set(element,
               // interval
               "interval", 100 * GST_MSECOND,

               // post_messages
               "post-messages", FALSE,

               // sentinel
               NULL);

  GstBuffer *inbuffer = create_buffer(S16_CAPS_STRING, 100);
  gst_pad_push(mysrcpad, inbuffer);

  GstMessage *message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, 50 * GST_MSECOND);
  fail_unless(message == NULL);

  cleanup_element();
}
GST_END_TEST;

// buffers smaller then interval
GST_START_TEST(test_small_buffers) {
  GstMessage *message;
  const GstStructure *structure;
  GstBuffer *inbuffer;
  GstClockTime timestamp;

  setup_element(S16_CAPS_STRING);
  g_object_set(element, "interval", 100 * GST_MSECOND, NULL);

  // 40 ms
  inbuffer = create_buffer(S16_CAPS_STRING, 40);
  gst_pad_push(mysrcpad, inbuffer);
  message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, 50 * GST_MSECOND);
  fail_unless(message == NULL);

  // 80 ms
  inbuffer = create_buffer(S16_CAPS_STRING, 40);
  gst_pad_push(mysrcpad, inbuffer);
  message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, 50 * GST_MSECOND);
  fail_unless(message == NULL);

  // 120 ms
  inbuffer = create_buffer(S16_CAPS_STRING, 40);
  gst_pad_push(mysrcpad, inbuffer);
  message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, 50 * GST_MSECOND);
  fail_if(message == NULL); // message at 250ms, 50ms remaining

  // validate timestamp
  structure = gst_message_get_structure(message);
  gst_structure_get_clock_time(structure, "timestamp", &timestamp);
  fail_unless(timestamp == 100 * GST_MSECOND);
  gst_message_unref(message);

  // 160 ms
  inbuffer = create_buffer(S16_CAPS_STRING, 40);
  gst_pad_push(mysrcpad, inbuffer);
  message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, 50 * GST_MSECOND);
  fail_unless(message == NULL);

  // 200 ms
  inbuffer = create_buffer(S16_CAPS_STRING, 40);
  gst_pad_push(mysrcpad, inbuffer);
  message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, 50 * GST_MSECOND);
  fail_if(message == NULL); // message at 500ms

  // validate timestamp
  structure = gst_message_get_structure(message);
  gst_structure_get_clock_time(structure, "timestamp", &timestamp);
  fail_unless(timestamp == 200 * GST_MSECOND);
  gst_message_unref(message);

  cleanup_element();
}
GST_END_TEST;

// buffers equal to interval
GST_START_TEST(test_medium_buffers) {
  GstMessage *message;
  const GstStructure *structure;
  GstBuffer *inbuffer;
  GstClockTime timestamp;

  setup_element(S16_CAPS_STRING);
  g_object_set(element, "interval", 100 * GST_MSECOND, NULL);

  // 100 ms
  inbuffer = create_buffer(S16_CAPS_STRING, 100);
  gst_pad_push(mysrcpad, inbuffer);
  message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, -1);
  fail_if(message == NULL);

  // validate timestamp
  structure = gst_message_get_structure(message);
  gst_structure_get_clock_time(structure, "timestamp", &timestamp);
  fail_unless(timestamp == 100 * GST_MSECOND);
  gst_message_unref(message);

  // 200 ms
  inbuffer = create_buffer(S16_CAPS_STRING, 100);
  gst_pad_push(mysrcpad, inbuffer);
  message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, -1);
  fail_if(message == NULL);

  // validate timestamp
  structure = gst_message_get_structure(message);
  gst_structure_get_clock_time(structure, "timestamp", &timestamp);
  fail_unless(timestamp == 200 * GST_MSECOND);
  gst_message_unref(message);

  cleanup_element();
}
GST_END_TEST;

// buffers larger then to interval
GST_START_TEST(test_large_buffers) {
  GstMessage *message;
  const GstStructure *structure;
  GstBuffer *inbuffer;
  GstClockTime timestamp;

  setup_element(S16_CAPS_STRING);
  g_object_set(element, "interval", 100 * GST_MSECOND, NULL);

  // 1000 ms
  inbuffer = create_buffer(S16_CAPS_STRING, 1000);
  gst_pad_push(mysrcpad, inbuffer);

  // expect 10 messages
  for (gint iteration = 0; iteration < 10; iteration++) {
    message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, 50 * GST_MSECOND);
    fail_if(message == NULL);

    // validate timestamp
    structure = gst_message_get_structure(message);
    gst_structure_get_clock_time(structure, "timestamp", &timestamp);
    GST_INFO("iteration=%d, timestamp=%" GST_TIME_FORMAT, iteration, GST_TIME_ARGS(timestamp));
    fail_unless(timestamp == 100 * GST_MSECOND * (iteration + 1));
    gst_message_unref(message);
  }

  // 2000 ms
  inbuffer = create_buffer(S16_CAPS_STRING, 1000);
  gst_pad_push(mysrcpad, inbuffer);

  // expect 10 messages
  for (gint iteration = 0; iteration < 10; iteration++) {
    message = gst_bus_poll(bus, GST_MESSAGE_ELEMENT, 50 * GST_MSECOND);
    fail_if(message == NULL);

    // validate timestamp
    structure = gst_message_get_structure(message);
    gst_structure_get_clock_time(structure, "timestamp", &timestamp);
    GST_INFO("iteration=%d, timestamp=%" GST_TIME_FORMAT, iteration, GST_TIME_ARGS(timestamp));
    fail_unless(timestamp == (100 * GST_MSECOND * (iteration + 11)));
    gst_message_unref(message);
  }

  cleanup_element();
}
GST_END_TEST;

static Suite *element_suite(void) {
  Suite *s = suite_create("ebur128");

  TCase *tc_general = tcase_create("general");
  suite_add_tcase(s, tc_general);
  tcase_add_test(tc_general, test_setup_and_teardown);
  tcase_add_test(tc_general, test_emits_message);
  tcase_add_test(tc_general, test_passess_buffer_unchaged);
  tcase_add_test(tc_general, test_timestamps);
  tcase_add_test(tc_general, test_per_channel_array);
  tcase_add_test(tc_general, test_mode_change);

  TCase *tc_audio_formats = tcase_create("audio_formats");
  suite_add_tcase(s, tc_audio_formats);
  tcase_add_test(tc_audio_formats, test_accepts_s16);
  tcase_add_test(tc_audio_formats, test_accepts_s32);
  tcase_add_test(tc_audio_formats, test_accepts_f32);
  tcase_add_test(tc_audio_formats, test_accepts_f64);

  TCase *tc_properties = tcase_create("properties");
  suite_add_tcase(s, tc_properties);
  tcase_add_test(tc_properties, test_prop_momentary);
  tcase_add_test(tc_properties, test_prop_shortterm);
  tcase_add_test(tc_properties, test_prop_global);
  tcase_add_test(tc_properties, test_prop_window);
  tcase_add_test(tc_properties, test_prop_range);
  tcase_add_test(tc_properties, test_prop_sample_peak);
  tcase_add_test(tc_properties, test_prop_true_peak);
  tcase_add_test(tc_properties, test_prop_post_messages);
  // prop "interval" is thoroughly tested in the buffer-size testcase

  TCase *tc_buffer_size = tcase_create("buffer_size");
  suite_add_tcase(s, tc_buffer_size);
  tcase_add_test(tc_buffer_size, test_small_buffers);
  tcase_add_test(tc_buffer_size, test_medium_buffers);
  tcase_add_test(tc_buffer_size, test_large_buffers);

  return s;
}

GST_CHECK_MAIN(element);
