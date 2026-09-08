#include "../runtime.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 assert(pixelview_gst_init());
 assert(g_getenv("GST_PLUGIN_SYSTEM_PATH_1_0") && !*g_getenv("GST_PLUGIN_SYSTEM_PATH_1_0"));
 GstElement *rtc=gst_element_factory_make("webrtcbin",NULL); assert(rtc);
 assert(gst_element_set_state(rtc,GST_STATE_READY)!=GST_STATE_CHANGE_FAILURE);
 gst_element_set_state(rtc,GST_STATE_NULL); gst_object_unref(rtc);
 puts("PASS bundled-only runtime and webrtcbin READY");
 return 0;
}
