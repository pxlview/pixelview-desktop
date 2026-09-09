/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "capability-probe.h"
#include "profile-offer.h"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <string.h>

struct probe_packet { const guint8 *data; gsize size; };
#include "capability-fixtures.h"
struct probe_fixture {
 unsigned bit;
 const char *media, *parser, *profile, *format;
 const struct probe_packet *packets;
 unsigned count;
};
#define FIXTURE(bit, media, parser, profile, format, name) \
 {bit, media, parser, profile, format, fixture_##name, G_N_ELEMENTS(fixture_##name)}
static const struct probe_fixture fixtures[] = {
 FIXTURE(PV_PROFILE_H264, "video/x-h264", "h264parse", "constrained-baseline", "NV12", h264),
 FIXTURE(PV_PROFILE_HEVC_MAIN, "video/x-h265", "h265parse", "main", "NV12", main),
 FIXTURE(PV_PROFILE_HEVC_MAIN10, "video/x-h265", "h265parse", "main-10", "P010_10LE", main10),
 FIXTURE(PV_PROFILE_VP9_0, "video/x-vp9", "vp9parse", "0", "NV12", vp9_0),
 FIXTURE(PV_PROFILE_VP9_2, "video/x-vp9", "vp9parse", "2", "P010_10LE", vp9_2),
};
#undef FIXTURE
static GMutex cache_mutex;
static GCond cache_cond;
static gboolean started, sealed;
static gint64 deadline;
static struct pixelview_receive_capabilities pending, cached;
#ifdef PIXELVIEW_CAPABILITY_TESTING
static const char *test_decoder = "vtdec_hw";
static unsigned test_malformed;
static unsigned test_delay_ms;
static gint test_workers;
void pixelview_capability_probe_test_configure(const char *decoder, unsigned malformed, unsigned delay_ms)
{
 g_assert(!started);
 test_decoder = decoder;
 test_malformed = malformed;
 test_delay_ms = delay_ms;
}
unsigned pixelview_capability_probe_test_worker_count(void) { return (unsigned)g_atomic_int_get(&test_workers); }
#endif

static gboolean validate_sample(GstSample *sample, GstElement *decoder, const struct probe_fixture *fixture, unsigned index)
{
 GstVideoInfo info;
 GstCaps *caps = gst_sample_get_caps(sample);
 if (!caps || !gst_video_info_from_caps(&info, caps) ||
     GST_VIDEO_INFO_FPS_N(&info) != 60 || GST_VIDEO_INFO_FPS_D(&info) != 1 ||
     GST_VIDEO_INFO_WIDTH(&info) != 1920 || GST_VIDEO_INFO_HEIGHT(&info) != 1080 ||
     GST_VIDEO_INFO_FORMAT(&info) != gst_video_format_from_string(fixture->format)) return FALSE;
 GstPad *pad = gst_element_get_static_pad(decoder, "sink");
 GstCaps *input = gst_pad_get_current_caps(pad);
 gst_object_unref(pad);
 if (!input || gst_caps_is_empty(input)) { if (input) gst_caps_unref(input); return FALSE; }
 const GstStructure *s = gst_caps_get_structure(input, 0);
 int width = 0, height = 0, fps_n = 0, fps_d = 0;
 gboolean valid = !g_strcmp0(gst_structure_get_string(s, "profile"), fixture->profile) &&
  gst_structure_get_int(s, "width", &width) && width == 1920 &&
  gst_structure_get_int(s, "height", &height) && height == 1080 &&
  gst_structure_get_fraction(s, "framerate", &fps_n, &fps_d) && fps_n == 60 && fps_d == 1;
 if (fixture->bit & (PV_PROFILE_HEVC_MAIN | PV_PROFILE_HEVC_MAIN10))
  valid = valid && !g_strcmp0(gst_structure_get_string(s, "level"), "4.1") &&
          !g_strcmp0(gst_structure_get_string(s, "tier"), "main");
 if (fixture->bit == PV_PROFILE_H264)
  valid = valid && !g_strcmp0(gst_structure_get_string(s, "level"), "4.2");
 gst_caps_unref(input);
 GstBuffer *buffer = gst_sample_get_buffer(sample);
 GstClockTime expected = gst_util_uint64_scale(index, GST_SECOND, 60);
 /* Allow one RTP clock tick of timestamp quantization, not a lower cadence. */
 if (!buffer || !GST_BUFFER_PTS_IS_VALID(buffer) || !GST_BUFFER_DURATION_IS_VALID(buffer) ||
     GST_BUFFER_PTS(buffer) + GST_SECOND / 90000 < expected ||
     GST_BUFFER_PTS(buffer) > expected + GST_SECOND / 90000 ||
     GST_BUFFER_DURATION(buffer) < GST_SECOND / 60 - 1 ||
     GST_BUFFER_DURATION(buffer) > GST_SECOND / 60 + 1) valid = FALSE;
 GstVideoFrame frame;
 if (!valid || !gst_video_frame_map(&frame, &info, gst_sample_get_buffer(sample), GST_MAP_READ)) return FALSE;
 /* Require accessible, non-flat actual luma, not merely negotiated caps. This
  * deliberately is not a reference-decoder/fidelity comparison. */
 const guint8 *base = GST_VIDEO_FRAME_PLANE_DATA(&frame, 0);
 int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
 unsigned bytes = !strcmp(fixture->format, "NV12") ? 1920 : 3840;
 gboolean varied = FALSE;
 for (unsigned y = 0; y < 1080 && !varied; y++)
  for (unsigned x = 0; x < bytes; x++)
   if (base[(gssize)y * stride + x] != base[0]) { varied = TRUE; break; }
 gst_video_frame_unmap(&frame);
 return varied;
}

static gboolean decode_fixture(const struct probe_fixture *fixture, gint64 until)
{
 const char *decoder_name = "vtdec_hw";
 unsigned output_rate = 60, timestamp_rate = 60;
#ifdef PIXELVIEW_CAPABILITY_TESTING
 decoder_name = test_decoder;
 if (test_malformed & 256) output_rate = 30;
 if (test_malformed & 512) timestamp_rate = 30;
#endif
 /* Explicit decoder, no decodebin, videoconvert, fallback or external source.
  * H264 traverses actual RFC6184 non-interleaved packetization/depacketization. */
 gchar *description = g_strdup_printf(
  "appsrc name=in format=time is-live=false block=false ! %s ! %s%s name=decoder ! "
  "video/x-raw,format=%s,width=1920,height=1080,framerate=%u/1 ! "
  "appsink name=out sync=false max-buffers=3 drop=false wait-on-eos=true",
  fixture->parser,
  fixture->bit == PV_PROFILE_H264 ?
   "rtph264pay mtu=1200 aggregate-mode=zero-latency ! "
   "application/x-rtp,media=video,encoding-name=H264,clock-rate=90000,packetization-mode=(string)1 ! "
   "rtph264depay ! h264parse ! " : "", decoder_name, fixture->format, output_rate);
 GError *error = NULL;
 GstElement *pipe = gst_parse_launch(description, &error);
 g_free(description);
 if (error || !pipe) {
  g_clear_error(&error);
  if (pipe) gst_object_unref(pipe);
  return FALSE;
 }
 GstElement *src = gst_bin_get_by_name(GST_BIN(pipe), "in");
 GstElement *sink = gst_bin_get_by_name(GST_BIN(pipe), "out");
 GstElement *decoder = gst_bin_get_by_name(GST_BIN(pipe), "decoder");
 GstBus *bus = gst_element_get_bus(pipe);
 GstCaps *input = gst_caps_new_simple(fixture->media, "width", G_TYPE_INT, 1920,
                                      "height", G_TYPE_INT, 1080, "framerate", GST_TYPE_FRACTION, 60, 1, NULL);
 if (strcmp(fixture->media, "video/x-vp9"))
  gst_caps_set_simple(input, "stream-format", G_TYPE_STRING, "byte-stream", NULL);
 gst_app_src_set_caps(GST_APP_SRC(src), input);
 gst_caps_unref(input);
 gboolean okay = gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE;
 for (unsigned i = 0; okay && i < fixture->count; i++) {
  const guint8 *data = fixture->packets[i].data;
  gsize size = fixture->packets[i].size;
#ifdef PIXELVIEW_CAPABILITY_TESTING
  static const guint8 malformed[] = "not a coded picture";
  if (test_malformed & fixture->bit) { data = malformed; size = sizeof(malformed); }
#endif
  GstBuffer *buffer = gst_buffer_new_allocate(NULL, size, NULL);
  gst_buffer_fill(buffer, 0, data, size);
  GST_BUFFER_PTS(buffer) = gst_util_uint64_scale(i, GST_SECOND, timestamp_rate);
  GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(i + 1, GST_SECOND, timestamp_rate) - GST_BUFFER_PTS(buffer);
  okay = gst_app_src_push_buffer(GST_APP_SRC(src), buffer) == GST_FLOW_OK;
 }
 okay = gst_app_src_end_of_stream(GST_APP_SRC(src)) == GST_FLOW_OK && okay;
 unsigned frames = 0;
 while (okay && g_get_monotonic_time() < until) {
  GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 10 * GST_MSECOND);
  if (sample) {
   okay = frames < fixture->count && validate_sample(sample, decoder, fixture, frames);
   if (okay) frames++;
   gst_sample_unref(sample);
  }
  GstMessage *message = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
  if (message) { okay = FALSE; gst_message_unref(message); }
  /* Some vtdec versions can leave decoded output in their private reorder queue
   * when pausing for EOS. Appsink polling cannot recover that upstream loss;
   * retain the full acceptance check and fail closed at the profile deadline. */
  if (gst_app_sink_is_eos(GST_APP_SINK(sink))) {
   if (frames == fixture->count) break;
   if (!sample) g_usleep(1000); /* Missing output must not busy-spin until deadline. */
  }
 }
 /* On a deadline, incomplete/unresponsive profiles fail closed even if a frame
  * was previously delivered. Each completed profile is independent. */
 gboolean supported = okay && frames == fixture->count && gst_app_sink_is_eos(GST_APP_SINK(sink)) &&
                      g_get_monotonic_time() < until;
 gst_element_set_state(pipe, GST_STATE_NULL);
 gst_object_unref(bus);
 gst_object_unref(src);
 gst_object_unref(sink);
 gst_object_unref(decoder);
 gst_object_unref(pipe);
 return supported;
}

static void seal_locked(void)
{
 if (!sealed) { cached = pending; sealed = TRUE; g_cond_broadcast(&cache_cond); }
}
static gpointer probe_worker(gpointer unused)
{
 (void)unused;
#ifdef PIXELVIEW_CAPABILITY_TESTING
 g_atomic_int_inc(&test_workers);
#endif
 for (unsigned i = 0; i < G_N_ELEMENTS(fixtures); i++) {
  g_mutex_lock(&cache_mutex);
  gint64 now = g_get_monotonic_time();
  if (sealed || now >= deadline) { seal_locked(); g_mutex_unlock(&cache_mutex); return NULL; }
  gint64 until = MIN(deadline, now + 450 * G_TIME_SPAN_MILLISECOND);
  g_mutex_unlock(&cache_mutex);
  gboolean supported = decode_fixture(&fixtures[i], until);
#ifdef PIXELVIEW_CAPABILITY_TESTING
  /* Simulate a driver returning a successful decode after a waiter deadline. */
  if (i == 0) g_usleep((gulong)test_delay_ms * 1000);
#endif
  g_mutex_lock(&cache_mutex);
  if (!sealed && g_get_monotonic_time() < deadline && supported) {
   pending.profiles |= fixtures[i].bit;
   if (fixtures[i].bit & (PV_PROFILE_HEVC_MAIN | PV_PROFILE_HEVC_MAIN10)) pending.hevc_level_id = 123;
  }
  g_mutex_unlock(&cache_mutex);
 }
 g_mutex_lock(&cache_mutex);
 seal_locked();
 g_mutex_unlock(&cache_mutex);
 return NULL;
}

gboolean pixelview_capability_probe_get(struct pixelview_receive_capabilities *out,
                                       pixelview_capability_cancel_fn cancelled, void *data)
{
 if (!out) return FALSE;
 *out = (struct pixelview_receive_capabilities){0};
 if (!gst_is_initialized()) return FALSE;
 for (;;) {
  if (cancelled && cancelled(data)) return FALSE;
  g_mutex_lock(&cache_mutex);
  if (!started) {
   started = TRUE;
   deadline = g_get_monotonic_time() + PIXELVIEW_CAPABILITY_PROBE_BUDGET_MS * G_TIME_SPAN_MILLISECOND;
   GError *error = NULL;
   GThread *thread = g_thread_try_new("pv-rx-probe", probe_worker, NULL, &error);
   if (thread) g_thread_unref(thread); else seal_locked();
   g_clear_error(&error);
  }
  gint64 now = g_get_monotonic_time();
  if (now >= deadline) seal_locked();
  if (sealed) { *out = cached; g_mutex_unlock(&cache_mutex); return TRUE; }
  g_cond_wait_until(&cache_cond, &cache_mutex, MIN(deadline, now + 20 * G_TIME_SPAN_MILLISECOND));
  g_mutex_unlock(&cache_mutex);
 }
}
