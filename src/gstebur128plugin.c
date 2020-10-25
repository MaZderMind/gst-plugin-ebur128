#include "gstebur128element.h"
#include "gstebur128graphelement.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean ebur128_plugin_init(GstPlugin *ebur128) {
  gboolean success = TRUE;
  success &= gst_element_register(ebur128, "ebur128", GST_RANK_NONE, GST_TYPE_EBUR128);
  success &= gst_element_register(ebur128, "ebur128graph", GST_RANK_NONE, GST_TYPE_EBUR128GRAPH);
  return success;
}

/* PACKAGE: this is usually set by meson depending on some _INIT macro
 * in meson.build and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use meson to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "gst-ebur128-plugin"
#endif

/* gstreamer looks for this structure to register ebur128s
 */
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, ebur128,
                  "The EBU-R 128 Plugin ('ebur128') provides Elements for calculating the "
                  "EBU-R 128 Loudness of an Audio-Stream and "
                  "to generate a Video-Stream visualizing the Loudness over Time",
                  ebur128_plugin_init, PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
