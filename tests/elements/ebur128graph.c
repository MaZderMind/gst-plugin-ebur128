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

static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(SUPPORTED_AUDIO_CAPS_STRING));

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;
static GstElement *element;
static GstBus *bus;

/* takes over reference for outcaps */
static void setup_element_with_caps(GstCaps *audio_caps, GstCaps *video_caps) {
  GST_INFO("setup_element");
  element = gst_check_setup_element("ebur128graph");
  mysrcpad = gst_check_setup_src_pad(element, &srctemplate);

  GstPadTemplate *sinktemplate = gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, video_caps);
  mysinkpad = gst_check_setup_sink_pad_from_template(element, sinktemplate);
  gst_object_unref(sinktemplate);

  gst_pad_set_active(mysrcpad, TRUE);
  gst_pad_set_active(mysinkpad, TRUE);

  /* setup event capturing */
  gst_check_setup_events(mysrcpad, element, audio_caps, GST_FORMAT_TIME);

  /* set to playing */
  fail_unless(gst_element_set_state(element, GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
              "could not set to playing");

  /* create a bus to get the element message on */
  bus = gst_bus_new();
  ASSERT_OBJECT_REFCOUNT(bus, "bus", 1);
  gst_element_set_bus(element, bus);
  ASSERT_OBJECT_REFCOUNT(bus, "bus", 2);
}

static void setup_element(const gchar *audio_caps_str) {
  GstCaps *audio_caps = gst_caps_from_string(audio_caps_str);
  GstCaps *video_caps = gst_caps_from_string(SUPPORTED_VIDEO_CAPS_STRING);

  setup_element_with_caps(audio_caps, video_caps);

  gst_caps_unref(audio_caps);
  gst_caps_unref(video_caps);
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

static void test_generates_video_frame_size(int width, int height) {
  GstBuffer *inbuffer, *outbuffer;

  GstCaps *audio_caps = gst_caps_from_string(S16_CAPS_STRING);

  // Setup ebur128 Element with 10 fps in BGRx-Mode with the given frame size
  GstCaps *video_caps = gst_caps_new_simple( //
      "video/x-raw",                         //
      "format", G_TYPE_STRING, "BGRx",       //
      "framerate", GST_TYPE_FRACTION, 10, 1, //
      "width", G_TYPE_INT, width,            //
      "height", G_TYPE_INT, height,          //
      NULL);

  setup_element_with_caps(audio_caps, video_caps);
  gst_caps_unref(video_caps);
  gst_caps_unref(audio_caps);

  /* create a fake 100 msec buffer with all zeros */
  inbuffer = create_buffer(S16_CAPS_STRING, 100);
  fail_unless(gst_pad_push(mysrcpad, inbuffer) == GST_FLOW_OK);

  /* at 10 fps, a frame is exactly 100 msec long, so we expect exactly one frame to be generated */
  fail_unless_equals_int(g_list_length(buffers), 1);
  fail_if((outbuffer = (GstBuffer *)buffers->data) == NULL);

  fail_unless_equals_int64(gst_buffer_get_size(outbuffer), width * height * 4);

  cleanup_element();
}

GST_START_TEST(test_generates_video_frame_size_150x100) { test_generates_video_frame_size(150, 100); }
GST_END_TEST;

GST_START_TEST(test_generates_video_frame_size_640x480) { test_generates_video_frame_size(640, 480); }
GST_END_TEST;

GST_START_TEST(test_generates_video_frame_size_1920x1080) { test_generates_video_frame_size(1920, 1080); }
GST_END_TEST;

static void test_generates_video_frame_rate(int framerate) {
  GstBuffer *inbuffer;

  GstCaps *audio_caps = gst_caps_from_string(S16_CAPS_STRING);

  // Setup ebur128 Element with 10 fps in BGRx-Mode with the given frame size
  GstCaps *video_caps = gst_caps_new_simple(        //
      "video/x-raw",                                //
      "format", G_TYPE_STRING, "BGRx",              //
      "framerate", GST_TYPE_FRACTION, framerate, 1, //
      "width", G_TYPE_INT, 640,                     //
      "height", G_TYPE_INT, 480,                    //
      NULL);

  setup_element_with_caps(audio_caps, video_caps);
  gst_caps_unref(video_caps);
  gst_caps_unref(audio_caps);

  /* create a fake 1000 msec buffer with all zeros */
  inbuffer = create_buffer(S16_CAPS_STRING, 1000);
  fail_unless(gst_pad_push(mysrcpad, inbuffer) == GST_FLOW_OK);

  /* at x fps, a frame is exactly 1000/x msec long, so we expect exactly x frames to be generated for our 1000ms buffer
   */
  fail_unless_equals_int(g_list_length(buffers), framerate);

  cleanup_element();
}

GST_START_TEST(test_generates_video_frame_rate_5fps) { test_generates_video_frame_rate(5); }
GST_END_TEST;

GST_START_TEST(test_generates_video_frame_rate_25fps) { test_generates_video_frame_rate(25); }
GST_END_TEST;

GST_START_TEST(test_generates_video_frame_rate_30fps) { test_generates_video_frame_rate(30); }
GST_END_TEST;

GST_START_TEST(test_generates_video_frame_rate_60fps) { test_generates_video_frame_rate(60); }
GST_END_TEST;

static void test_accepts(const char *caps_str) {
  GstBuffer *inbuffer;

  setup_element(caps_str);

  /* create a fake 100 msec buffer with all zeros */
  inbuffer = create_buffer(S16_CAPS_STRING, 100);
  fail_unless(gst_pad_push(mysrcpad, inbuffer) == GST_FLOW_OK);

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

static Suite *element_suite(void) {
  Suite *s = suite_create("ebur128graph");

  TCase *tc_general = tcase_create("general");
  suite_add_tcase(s, tc_general);
  tcase_add_test(tc_general, test_setup_and_teardown);

  tcase_add_test(tc_general, test_generates_video_frame_size_150x100);
  tcase_add_test(tc_general, test_generates_video_frame_size_640x480);
  tcase_add_test(tc_general, test_generates_video_frame_size_1920x1080);

  tcase_add_test(tc_general, test_generates_video_frame_rate_5fps);
  tcase_add_test(tc_general, test_generates_video_frame_rate_25fps);
  tcase_add_test(tc_general, test_generates_video_frame_rate_30fps);
  tcase_add_test(tc_general, test_generates_video_frame_rate_60fps);

  TCase *tc_audio_formats = tcase_create("audio_formats");
  suite_add_tcase(s, tc_audio_formats);
  tcase_add_test(tc_audio_formats, test_accepts_s16);
  tcase_add_test(tc_audio_formats, test_accepts_s32);
  tcase_add_test(tc_audio_formats, test_accepts_f32);
  tcase_add_test(tc_audio_formats, test_accepts_f64);

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