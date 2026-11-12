/* SPDX-License-Identifier: GPL-2.0-or-later
 * Lazy bundled-runtime start (runtime.h): run by run-runtime-lazy.py with
 * one of: ready, hang, slow, failed. */
#include "../runtime.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
static gboolean always(void *unused) { (void)unused; return TRUE; }
static long long since(gint64 start) { return (long long)((g_get_monotonic_time() - start) / 1000); }
static bool loading_at(const char *name)
{
 g_mutex_lock(&pv_gst_lock);
 const bool there = pv_gst_state == PIXELVIEW_GST_LOADING && !strcmp(pv_gst_current, name);
 g_mutex_unlock(&pv_gst_lock);
 return there;
}
int main(int argc, char **argv)
{
 assert(argc == 2);
 const char *mode = argv[1];
 assert(pixelview_gst_configure());
 assert(!gst_is_initialized()); /* configure never starts GStreamer */
 assert(!strcmp(g_getenv("GST_REGISTRY_FORK"), "no"));
 assert(!*g_getenv("GST_PLUGIN_PATH_1_0") && !*g_getenv("GST_PLUGIN_SYSTEM_PATH_1_0"));
 assert(!pixelview_gst_ready());
 const gint64 start = g_get_monotonic_time();
 if (!strcmp(mode, "ready")) {
  assert(pixelview_gst_wait(60000u, NULL, NULL) == PIXELVIEW_GST_READY);
  assert(pixelview_gst_ready());
  GstElement *rtc = gst_element_factory_make("webrtcbin", NULL); assert(rtc);
  assert(gst_element_set_state(rtc, GST_STATE_READY) != GST_STATE_CHANGE_FAILURE);
  gst_element_set_state(rtc, GST_STATE_NULL); gst_object_unref(rtc);
  assert(pixelview_gst_wait(10u, NULL, NULL) == PIXELVIEW_GST_READY);
  printf("PASS runtime ready in-process in %lld ms; webrtcbin READY\n", since(start));
  return 0;
 }
 if (!strcmp(mode, "failed")) {
  assert(pixelview_gst_wait(60000u, NULL, NULL) == PIXELVIEW_GST_FAILED);
  assert(pixelview_gst_wait(10u, NULL, NULL) == PIXELVIEW_GST_FAILED); /* latched */
  printf("PASS bad bundled plugin fails the runtime in %lld ms\n", since(start));
  return 0;
 }
 /* hang/slow: built with a short PIXELVIEW_GST_BUDGET_MS. Plugins load in
  * name order; wait until the loader is inside the test plugin. */
 enum pixelview_gst_state state = PIXELVIEW_GST_LOADING;
 for (int i = 0; i < 240 && !loading_at("libgstzzhang.dylib"); i++) {
  state = pixelview_gst_wait(500u, NULL, NULL);
  assert(state == PIXELVIEW_GST_LOADING || state == PIXELVIEW_GST_STALLED);
  if (state == PIXELVIEW_GST_STALLED) g_usleep(500000); /* past the budget a wait returns at once */
 }
 assert(loading_at("libgstzzhang.dylib"));
 /* Past the budget a wait reports STALLED (the typed failure) at once,
  * and never latches it: the loader is still LOADING. */
 const gint64 wait = g_get_monotonic_time();
 state = pixelview_gst_wait(60000u, NULL, NULL);
 assert(state == PIXELVIEW_GST_STALLED);
 assert(since(start) >= PIXELVIEW_GST_BUDGET_MS && since(wait) <= PIXELVIEW_GST_BUDGET_MS + 200);
 const gint64 again = g_get_monotonic_time();
 assert(pixelview_gst_wait(60000u, always, NULL) == PIXELVIEW_GST_LOADING); /* cancelled at once */
 assert(since(again) < 100);
 if (!strcmp(mode, "slow")) {
  /* The plugin finishes after the budget: the next receive works. */
  for (int i = 0; i < 120 && state != PIXELVIEW_GST_READY; i++) {
   state = pixelview_gst_wait(500u, NULL, NULL);
   if (state == PIXELVIEW_GST_STALLED) g_usleep(500000);
  }
  assert(state == PIXELVIEW_GST_READY && pixelview_gst_ready());
  printf("PASS slow load reported unavailable past the budget, then READY after %lld ms\n", since(start));
  return 0;
 }
 assert(!strcmp(mode, "hang"));
 printf("PASS hung plugin load reported unavailable past the %u ms budget; waits cancel\n", PIXELVIEW_GST_BUDGET_MS);
 fflush(stdout);
 _exit(0); /* the loader thread is still stuck, as in the app */
}
