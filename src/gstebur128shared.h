#ifndef __GST_EBUR128SHARED_H__
#define __GST_EBUR128SHARED_H__

#include <gst/gst.h>

gboolean gst_ebur128_validate_lib_return(GstElement *element,
                                         const char *invocation,
                                         const int return_value);

#endif // __GST_EBUR128SHARED_H__
