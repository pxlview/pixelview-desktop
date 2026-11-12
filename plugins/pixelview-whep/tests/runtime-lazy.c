/* SPDX-License-Identifier: GPL-2.0-or-later
 * Lazy bundled-runtime start (runtime.h): run by run-runtime-lazy.py. */
#include "../runtime.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
static gboolean always(void *unused) { (void)unused; return TRUE; }
int main(int argc, char **argv)
{
 assert(argc == 2);
 const bool hang = !strcmp(argv[1], "hang");
 assert(pixelview_gst_configure());
 assert(!gst_is_initialized()); /* configure never starts GStreamer */
 assert(!strcmp(g_getenv("GST_REGISTRY_FORK"), "no"));
 assert(!*g_getenv("GST_PLUGIN_PATH_1_0") && !*g_getenv("GST_PLUGIN_SYSTEM_PATH_1_0"));
 if (hang) {
  /* Plugins load in name order and a fresh copy can be slow at first; wait
   * until the loader is inside the plugin that never returns. */
  for (int i = 0; i < 120; i++) {
   assert(pixelview_gst_wait(500u, NULL, NULL) == PIXELVIEW_GST_LOADING);
   g_mutex_lock(&pv_gst_lock); const bool there = !strcmp(pv_gst_current, "libgstzzhang.dylib"); g_mutex_unlock(&pv_gst_lock);
   if (there) break;
  }
 }
 const gint64 start = g_get_monotonic_time();
 enum pixelview_gst_state state = pixelview_gst_wait(hang ? 1500u : 60000u, NULL, NULL);
 const gint64 ms = (g_get_monotonic_time() - start) / 1000;
 if (!hang) {
  assert(state == PIXELVIEW_GST_READY);
  GstElement *rtc = gst_element_factory_make("webrtcbin", NULL); assert(rtc);
  assert(gst_element_set_state(rtc, GST_STATE_READY) != GST_STATE_CHANGE_FAILURE);
  gst_element_set_state(rtc, GST_STATE_NULL); gst_object_unref(rtc);
  assert(pixelview_gst_wait(10u, NULL, NULL) == PIXELVIEW_GST_READY);
  printf("PASS runtime ready in-process in %lld ms; webrtcbin READY\n", (long long)ms);
  return 0;
 }
 assert(state == PIXELVIEW_GST_LOADING);
 assert(ms >= 1400 && ms < 3000);
 /* A later receive waits again (no second loader); a cancelled one returns at once. */
 assert(pixelview_gst_wait(200u, NULL, NULL) == PIXELVIEW_GST_LOADING);
 const gint64 again = g_get_monotonic_time();
 assert(pixelview_gst_wait(60000u, always, NULL) == PIXELVIEW_GST_LOADING);
 assert(g_get_monotonic_time() - again < 100 * G_TIME_SPAN_MILLISECOND);
 printf("PASS hung plugin load bounded to %lld ms; waits repeat and cancel\n", (long long)ms);
 fflush(stdout);
 _exit(0); /* the loader thread is still stuck, as in the app */
}
