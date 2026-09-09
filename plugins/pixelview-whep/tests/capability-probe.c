/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../capability-probe.h"
#include "../profile-offer.h"
#include <gst/gst.h>
#include <stdio.h>
#include <string.h>

/* Test-only injection is absent from production objects. */
void pixelview_capability_probe_test_configure(const char *, unsigned, unsigned);
unsigned pixelview_capability_probe_test_worker_count(void);
static gboolean cancelled(void *data) { return ++*(unsigned *)data >= 2; }
static gpointer concurrent_get(gpointer data)
{
 struct pixelview_receive_capabilities *out = data;
 g_assert_true(pixelview_capability_probe_get(out, NULL, NULL));
 return NULL;
}
int main(int argc, char **argv)
{
 g_assert_cmpint(argc, ==, 2);
 struct pixelview_receive_capabilities result = {999, 999};
 g_assert_false(pixelview_capability_probe_get(&result, NULL, NULL));
 g_assert_cmpuint(result.profiles, ==, 0);
 gst_init(NULL, NULL);
 gboolean real = !strcmp(argv[1], "real");
 gboolean cancel = !strcmp(argv[1], "cancel");
 gboolean timeout = !strcmp(argv[1], "timeout");
 gboolean partial = !strcmp(argv[1], "malformed-main10");
 pixelview_capability_probe_test_configure(!strcmp(argv[1], "absent") ? "pixelview_missing_decoder" : "vtdec_hw",
                                          partial ? PV_PROFILE_HEVC_MAIN10 : !strcmp(argv[1], "malformed") ? 31 :
                                          !strcmp(argv[1], "wrong-rate") ? 256 : !strcmp(argv[1], "wrong-timing") ? 512 : 0,
                                          timeout ? PIXELVIEW_CAPABILITY_PROBE_BUDGET_MS + 200 : cancel ? 200 : 0);
 gint64 start = g_get_monotonic_time();
 if (cancel) {
  unsigned calls = 0;
  g_assert_false(pixelview_capability_probe_get(&result, cancelled, &calls));
  g_assert_cmpuint(result.profiles, ==, 0);
  g_assert_cmpint(g_get_monotonic_time() - start, <, 150000);
 }
 struct pixelview_receive_capabilities parallel[8] = {{0}};
 GThread *threads[8];
 for (unsigned i = 0; i < 8; i++) threads[i] = g_thread_new("test-get", concurrent_get, &parallel[i]);
 for (unsigned i = 0; i < 8; i++) g_thread_join(threads[i]);
 g_assert_true(pixelview_capability_probe_get(&result, NULL, NULL));
 gint64 elapsed = g_get_monotonic_time() - start;
 g_assert_cmpint(elapsed, <, (PIXELVIEW_CAPABILITY_PROBE_BUDGET_MS + 500) * 1000);
 for (unsigned i = 0; i < 8; i++) {
  g_assert_cmpuint(parallel[i].profiles, ==, result.profiles);
  g_assert_cmpuint(parallel[i].hevc_level_id, ==, result.hevc_level_id);
 }
 if (real || cancel || partial) {
  unsigned expected = PV_PROFILE_H264 | PV_PROFILE_HEVC_MAIN |
                      PV_PROFILE_HEVC_MAIN10 | PV_PROFILE_VP9_0 | PV_PROFILE_VP9_2;
  if (partial) expected &= ~PV_PROFILE_HEVC_MAIN10;
  g_assert_cmpuint(result.profiles, ==, expected);
  g_assert_cmpuint(result.hevc_level_id, ==, 123);
 } else {
  g_assert_cmpuint(result.profiles, ==, 0);
  g_assert_cmpuint(result.hevc_level_id, ==, 0);
 }
 if (timeout) g_usleep(400000);
 struct pixelview_receive_capabilities cached;
 start = g_get_monotonic_time();
 g_assert_true(pixelview_capability_probe_get(&cached, NULL, NULL));
 g_assert_cmpint(g_get_monotonic_time() - start, <, 50000);
 g_assert_cmpuint(cached.profiles, ==, result.profiles);
 g_assert_cmpuint(cached.hevc_level_id, ==, result.hevc_level_id);
 g_assert_cmpuint(pixelview_capability_probe_test_worker_count(), ==, 1);
 printf("mask=%u hevc_level=%u elapsed_ms=%" G_GINT64_FORMAT " immutable_cache=1 worker_count=1\n",
        result.profiles, result.hevc_level_id, elapsed / 1000);
 return 0;
}
