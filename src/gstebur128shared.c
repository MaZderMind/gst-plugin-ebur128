#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstebur128shared.h"
#include <ebur128.h>

gboolean gst_ebur128_validate_lib_return(GstElement *element,
                                         const char *invocation,
                                         const int return_value) {
  if (return_value != EBUR128_SUCCESS) {
    GST_ERROR_OBJECT(element, "Error-Code %d from libebur128 call to %s",
                     return_value, invocation);
    return FALSE;
  }

  return TRUE;
}
