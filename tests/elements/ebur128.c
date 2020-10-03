/* suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifndef GST_PLUGIN_LOADING_WHITELIST
#define GST_PLUGIN_LOADING_WHITELIST ""
#endif

#include <gst/audio/audio.h>
#include <gst/check/gstcheck.h>

#define SUPPORTED_AUDIO_FORMATS                                                \
  "{ " GST_AUDIO_NE(S16) ", " GST_AUDIO_NE(S32) "," GST_AUDIO_NE(              \
      F32) ", " GST_AUDIO_NE(F64) " }"

#define SUPPORTED_AUDIO_CHANNELS "(int) {1, 2, 5 }"

#define SUPPORTED_CAPS_STRING                                                  \
  "audio/x-raw, "                                                              \
  "format = (string) " SUPPORTED_AUDIO_FORMATS ", "                            \
  "rate = " GST_AUDIO_RATE_RANGE ", "                                          \
  "channels = " SUPPORTED_AUDIO_CHANNELS ", "                                  \
  "layout = (string) interleaved "

#define S16_CAPS_STRING                                                        \
  "audio/x-raw, "                                                              \
  "format = (string) " GST_AUDIO_NE(S16) ", "                                  \
                                         "layout = (string) interleaved, "     \
                                         "rate = (int) 48000, "                \
                                         "channels = (int) 2"

#define S32_CAPS_STRING                                                        \
  "audio/x-raw, "                                                              \
  "format = (string) " GST_AUDIO_NE(S32) ", "                                  \
                                         "layout = (string) interleaved, "     \
                                         "rate = (int) 48000, "                \
                                         "channels = (int) 2"

static GstStaticPadTemplate sinktemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS(SUPPORTED_CAPS_STRING));
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(SUPPORTED_CAPS_STRING));

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;
static GstElement *element;
static GstBus *bus;

/* takes over reference for outcaps */
static void setup_element(const gchar *caps_str) {
  GST_DEBUG("setup_element");
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
  fail_unless(gst_element_set_state(element, GST_STATE_PLAYING) ==
                  GST_STATE_CHANGE_SUCCESS,
              "could not set to playing");

  /* create a bus to get the element message on */
  bus = gst_bus_new();
  ASSERT_OBJECT_REFCOUNT(bus, "bus", 1);
  gst_element_set_bus(element, bus);
  ASSERT_OBJECT_REFCOUNT(bus, "bus", 2);
}

static void cleanup_element() {
  GST_DEBUG("cleanup_element");

  /* flush bus */
  gst_bus_set_flushing(bus, TRUE);

  /* cleanup bus */
  gst_element_set_bus(element, NULL);
  ASSERT_OBJECT_REFCOUNT(bus, "bus", 1);
  gst_object_unref(bus);
  fail_unless(gst_element_set_state(element, GST_STATE_NULL) ==
                  GST_STATE_CHANGE_SUCCESS,
              "could not set to null");
  ASSERT_OBJECT_REFCOUNT(element, "element", 1);

  gst_check_drop_buffers();
  gst_pad_set_active(mysrcpad, FALSE);
  gst_pad_set_active(mysinkpad, FALSE);
  gst_check_teardown_src_pad(element);
  gst_check_teardown_sink_pad(element);
  gst_check_teardown_element(element);
}

static GstBuffer *create_buffer(const char *caps_string,
                                const guint num_msecs) {
  GstCaps *caps = gst_caps_from_string(caps_string);
  GstAudioInfo audio_info;
  gst_audio_info_from_caps(&audio_info, caps);
  gst_caps_unref(caps);

  guint num_frames = audio_info.rate * num_msecs / 1000;
  gsize num_bytes = audio_info.bpf * num_frames;
  GST_DEBUG("create bufffer of %ld bytes for %d frames in %d msecs", num_bytes,
            num_frames, num_msecs);
  GstBuffer *buf = gst_buffer_new_and_alloc(num_bytes);
  GST_BUFFER_TIMESTAMP(buf) = G_GUINT64_CONSTANT(0);

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

  /* create a fake 100 msec buffer with all zeros */
  inbuffer = create_buffer(S16_CAPS_STRING, 100);
  ASSERT_BUFFER_REFCOUNT(inbuffer, "inbuffer", 1);

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

static Suite *element_suite(void) {
  Suite *s = suite_create("ebur128");
  TCase *tc_general = tcase_create("general");

  suite_add_tcase(s, tc_general);
  tcase_add_test(tc_general, test_setup_and_teardown);
  tcase_add_test(tc_general, test_emits_message);
  tcase_add_test(tc_general, test_passess_buffer_unchaged);

  return s;
}

GST_CHECK_MAIN(element);
