/* suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifndef GST_PLUGIN_LOADING_WHITELIST
#define GST_PLUGIN_LOADING_WHITELIST ""
#endif

#include <gst/audio/audio.h>
#include <gst/check/gstcheck.h>

GST_START_TEST(test_foo) {
  // nop
}
GST_END_TEST;

static Suite *level_suite(void) {
  Suite *s = suite_create("ebur128");
  TCase *tc_general = tcase_create("general");

  suite_add_tcase(s, tc_general);
  tcase_add_test(tc_general, test_foo);

  return s;
}

GST_CHECK_MAIN(level);
