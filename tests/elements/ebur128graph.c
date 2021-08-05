/* suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifndef GST_PLUGIN_LOADING_WHITELIST
#define GST_PLUGIN_LOADING_WHITELIST ""
#endif

#include <gst/audio/audio.h>
#include <gst/check/gstcheck.h>

#define SUPPORTED_AUDIO_CAPS_STRING                                                                                    \
  "audio/x-raw, "                                                                                                      \
  "           format = { (string)S16LE, (string)S32LE, (string)F32LE, (string)F64LE }, "                               \
  "             rate = [ 1, 2147483647 ], "                                                                            \
  "         channels = { (int)1, (int)2, (int)5 }, "                                                                   \
  "           layout = interleaved"

#define SUPPORTED_VIDEO_CAPS_STRING                                                                                    \
  "video/x-raw, "                                                                                                      \
  "           format = { (string)BGRx, (string)BGRA }, "                                                               \
  "            width = [ 1, 2147483647 ], "                                                                            \
  "           height = [ 1, 2147483647 ], "                                                                            \
  "        framerate = [ 0/1, 2147483647/1 ]"

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
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(SUPPORTED_VIDEO_CAPS_STRING));
static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(SUPPORTED_AUDIO_CAPS_STRING));

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;
static GstElement *element;
static GstBus *bus;

/* takes over reference for outcaps */
static void setup_element(const gchar *caps_str) {
  GST_INFO("setup_element");
  element = gst_check_setup_element("ebur128graph");
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

GST_START_TEST(test_setup_and_teardown) {
  setup_element(S16_CAPS_STRING);
  cleanup_element();
}
GST_END_TEST;

static Suite *element_suite(void) {
  Suite *s = suite_create("ebur128");

  TCase *tc_general = tcase_create("general");
  suite_add_tcase(s, tc_general);
  tcase_add_test(tc_general, test_setup_and_teardown);

  // tcase_add_test(tc_general, test_generates_video_frame_size_150x100);
  // tcase_add_test(tc_general, test_generates_video_frame_size_640x480);
  // tcase_add_test(tc_general, test_generates_video_frame_size_1920x1080);

  // tcase_add_test(tc_general, test_generates_video_frame_rate_5fps);
  // tcase_add_test(tc_general, test_generates_video_frame_rate_25fps);
  // tcase_add_test(tc_general, test_generates_video_frame_rate_30fps);
  // tcase_add_test(tc_general, test_generates_video_frame_rate_60fps);

  // TCase *tc_audio_formats = tcase_create("audio_formats");
  // suite_add_tcase(s, tc_audio_formats);
  // tcase_add_test(tc_audio_formats, test_accepts_s16);
  // tcase_add_test(tc_audio_formats, test_accepts_s32);
  // tcase_add_test(tc_audio_formats, test_accepts_f32);
  // tcase_add_test(tc_audio_formats, test_accepts_f64);

  // TCase *tc_properties = tcase_create("properties");
  // suite_add_tcase(s, tc_properties);
  // tcase_add_test(tc_properties, test_prop_color_background);
  // tcase_add_test(tc_properties, test_prop_color_border);
  // tcase_add_test(tc_properties, test_prop_color_scale);
  // tcase_add_test(tc_properties, test_prop_color_scale_lines);
  // tcase_add_test(tc_properties, test_prop_color_header);
  // tcase_add_test(tc_properties, test_prop_color_graph);
  // tcase_add_test(tc_properties, test_prop_color_too_loud);
  // tcase_add_test(tc_properties, test_prop_color_loudness_ok);
  // tcase_add_test(tc_properties, test_prop_color_not_loud_enough);
  // tcase_add_test(tc_properties, test_prop_gutter);
  // tcase_add_test(tc_properties, test_prop_scale_w);
  // tcase_add_test(tc_properties, test_prop_gauge_w);
  // tcase_add_test(tc_properties, test_prop_scale_from);
  // tcase_add_test(tc_properties, test_prop_scale_to);
  // tcase_add_test(tc_properties, test_prop_scale_mode);
  // tcase_add_test(tc_properties, test_prop_scale_target);
  // tcase_add_test(tc_properties, test_prop_font_size_header);
  // tcase_add_test(tc_properties, test_prop_font_size_scale);
  // tcase_add_test(tc_properties, test_prop_measurement);
  // tcase_add_test(tc_properties, test_prop_timebase);

  // TCase *tc_buffer_size = tcase_create("buffer_size");
  // suite_add_tcase(s, tc_buffer_size);
  // tcase_add_test(tc_buffer_size, test_small_buffers);
  // tcase_add_test(tc_buffer_size, test_medium_buffers);
  // tcase_add_test(tc_buffer_size, test_large_buffers);

  return s;
}

GST_CHECK_MAIN(element);
