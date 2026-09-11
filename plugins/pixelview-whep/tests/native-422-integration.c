/* SPDX-License-Identifier: GPL-2.0-or-later */
#define PIXELVIEW_WHEP_TEST
#include "../native-422-filter.h"
/* Elementary-file fidelity fixture has no RTP and is not admission evidence. */
#define pv_native422_filter_require_rtp(filter, receiver) ((void)(filter), (void)(receiver))
#include "../pixelview-whep.c"
#undef pv_native422_filter_require_rtp
#include <gst/app/gstappsrc.h>
#include <assert.h>
#include <stdio.h>

struct fixture_output { FILE *file; guint count; guint64 base; };
static const char *fixture_name(void *unused) { (void)unused; return "Native output fixture"; }
static void *fixture_create(obs_data_t *settings, obs_source_t *source) { (void)settings; return source; }
static void fixture_destroy(void *data) { (void)data; }
int main(int argc, char **argv)
{
 assert(argc == 3); gst_init(NULL, NULL); assert(obs_startup("en-US", NULL, NULL));
 struct obs_audio_info audio_info = {.samples_per_sec=48000, .speakers=SPEAKERS_STEREO};
 assert(obs_reset_audio(&audio_info));
 struct obs_source_info info = {.id="native422_fixture_owner", .type=OBS_SOURCE_TYPE_INPUT,
  .output_flags=OBS_SOURCE_ASYNC_VIDEO, .get_name=fixture_name, .create=fixture_create, .destroy=fixture_destroy};
 obs_register_source(&info);
 obs_source_t *source = obs_source_create_private(info.id, "isolated-native422", NULL); assert(source);
 struct fixture_output out = {.file=fopen(argv[2], "wb")}; assert(out.file);
 struct receiver r = {.source=source, .generation=1, .active_generation=1, .accept_samples=true,
  .active_caps={.profiles=31, .hevc_level_id=123}, .active_latency=50};
 g_mutex_init(&r.lock); g_rec_mutex_init(&r.delivery);
 source_controls_init(&r);
 proc_handler_t *ph = obs_source_get_proc_handler(source);
 proc_handler_add(ph, "void native422_feed(ptr request, out int version)", feed_proc, &r);
 struct pv_feed_request feed = {.size=sizeof(feed), .version=PV_FEED_VERSION, .command=PV_FEED_ATTACH, .route=PV_FEED_NATIVE};
 calldata_t request; calldata_init(&request); calldata_set_ptr(&request, "request", &feed);
 assert(proc_handler_call(ph, "native422_feed", &request) && feed.status == PV_FEED_OK);
 uint8_t *bytes = malloc(PV_FEED_MAX_VIDEO_BYTES); assert(bytes);
 feed.command=PV_FEED_VIDEO; feed.data=bytes; feed.capacity=PV_FEED_MAX_VIDEO_BYTES;
 /* Query the production source API, without invoking subscriber code under
  * delivery locks. A separate real source owns/serves the versioned ABI. */
 assert(obs_module_load());
 obs_source_t *api_source = obs_source_create_private("pixelview_whep_source", "feed-api", NULL);
 assert(api_source);
 calldata_t query; calldata_init(&query);
 assert(proc_handler_call(obs_source_get_proc_handler(api_source), "native422_feed", &query));
 assert(calldata_int(&query, "version") == PV_FEED_VERSION);
 calldata_free(&query); obs_source_release(api_source);
 /* Real production graph and registered request-encoded-filter signal. Its
  * WHEP client remains NULL throughout: this fixture does not claim RTP/SDP. */
 GstElement *transport = make_pipeline(&r, "http://127.0.0.1:9/whep"); assert(transport);
 GstElement *rx = gst_bin_get_by_name(GST_BIN(transport), "rx"); assert(rx);
 GstCaps *caps = gst_caps_from_string("video/x-raw,format=P010_10LE");
 GstElement *filter = NULL;
 g_signal_emit_by_name(rx, "request-encoded-filter", "fixture", "video_0", caps, &filter); assert(filter);
 assert(!g_object_is_floating(filter)); /* Rust's returned GValue is an owned ref. */
 GstElement *pipeline = gst_pipeline_new(NULL), *src = gst_element_factory_make("appsrc", NULL);
 GstElement *preview = gst_element_factory_make("fakesink", NULL);
 g_object_set(src, "format", GST_FORMAT_TIME, NULL); g_object_set(preview, "sync", FALSE, NULL);
 gst_bin_add_many(GST_BIN(pipeline), src, filter, preview, NULL); assert(gst_element_link_many(src, filter, preview, NULL));
 gst_object_unref(filter); /* signal return is owned, separate from bin ownership */
 r.pipe = pipeline;
 gst_element_set_start_time(pipeline, GST_CLOCK_TIME_NONE);
 out.base = 1000000000; gst_element_set_base_time(pipeline, out.base);
 assert(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
 GstElement *reader = gst_parse_launch("filesrc name=file ! h265parse ! video/x-h265,stream-format=hvc1,alignment=au ! appsink name=read sync=false", NULL);
 GstElement *file = gst_bin_get_by_name(GST_BIN(reader), "file"); g_object_set(file, "location", argv[1], NULL); gst_object_unref(file);
 GstAppSink *read = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(reader), "read"));
 assert(gst_element_set_state(reader, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
 guint submitted = 0;
 for (;;) {
  GstSample *sample = gst_app_sink_try_pull_sample(read, 5 * GST_SECOND); if (!sample) break;
  GstBuffer *buffer = gst_buffer_copy(gst_sample_get_buffer(sample));
  GST_BUFFER_PTS(buffer) = gst_util_uint64_scale(submitted++, 1001 * GST_SECOND, 30000);
  GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(1, 1001 * GST_SECOND, 30000);
  gst_app_src_set_caps(GST_APP_SRC(src), gst_sample_get_caps(sample));
  assert(gst_app_src_push_buffer(GST_APP_SRC(src), buffer) == GST_FLOW_OK); gst_sample_unref(sample);
 }
 assert(submitted == 3); gst_app_src_end_of_stream(GST_APP_SRC(src));
 GstBus *bus = gst_element_get_bus(pipeline);
 GstMessage *message = gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
 assert(message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS);
 for (unsigned i=0; i<3; i++) {
  assert(proc_handler_call(ph, "native422_feed", &request) && feed.status == PV_FEED_OK);
  assert(feed.generation == 1 && feed.fps_num == 30000 && feed.fps_den == 1001);
  assert(feed.timestamp_ns == out.base + gst_util_uint64_scale(i, 1001 * GST_SECOND, 30000));
  assert(fwrite(bytes, feed.bytes, 1, out.file) == 1); out.count++;
 }
 assert(out.count == 3 && r.native422_frames == 3);
 calldata_t readiness; calldata_init(&readiness); status_proc(&r, &readiness);
 assert(!r.frames && calldata_bool(&readiness, "ready"));
 calldata_free(&readiness);
 gst_message_unref(message); gst_object_unref(bus); fclose(out.file);
 gst_element_set_state(reader, GST_STATE_NULL); gst_object_unref(read); gst_object_unref(reader);
 /* Exercise the real audio appsink adapter, not the global OBS mix. */
 GstElement *audio_pipe = gst_parse_launch("appsrc name=src format=time ! audio/x-raw,format=F32LE,layout=interleaved,channels=2,rate=48000 ! appsink name=sink sync=false", NULL);
 GstElement *audio_src = gst_bin_get_by_name(GST_BIN(audio_pipe), "src");
 GstAppSink *audio_sink = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(audio_pipe), "sink"));
 assert(gst_element_set_state(audio_pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
 for (unsigned muted=0; muted<2; muted++) {
  obs_source_set_muted(source, muted);
  float samples[] = {-1.f, -.5f, 0.f, .5f, 0.999999f, NAN};
  GstBuffer *audio = gst_buffer_new_allocate(NULL, sizeof(samples), NULL);
  gst_buffer_fill(audio, 0, samples, sizeof(samples));
  GST_BUFFER_PTS(audio) = muted * 20000000; GST_BUFFER_DURATION(audio) = 62500;
  assert(gst_app_src_push_buffer(GST_APP_SRC(audio_src), audio) == GST_FLOW_OK);
  assert(native_audio_sample(audio_sink, &r) == GST_FLOW_OK);
  feed.command=PV_FEED_AUDIO;
  assert(proc_handler_call(ph, "native422_feed", &request) && feed.status == PV_FEED_OK);
  assert(feed.audio_frames == 3 && feed.bytes == 12 && feed.timestamp_ns == out.base + muted * 20000000);
  int16_t expected[] = {-32768, -16384, 0, 16384, 32767, 0};
  if (muted) memset(expected, 0, sizeof(expected));
  assert(!memcmp(bytes, expected, sizeof(expected)));
 }
 gst_element_set_state(audio_pipe, GST_STATE_NULL);
 gst_object_unref(audio_src); gst_object_unref(audio_sink); gst_object_unref(audio_pipe);
 feed.command=PV_FEED_VIDEO;
 /* The actual source stop detaches the attempt before destroying its graph.
  * Retained rx signal closures must not resurrect a filter after detachment. */
 /* A missing native branch dependency must not silently restore the ordinary
  * lossy decoder route. Removing a registry feature affects this process only. */
 GstPluginFeature *queue = GST_PLUGIN_FEATURE(gst_element_factory_find("queue")); assert(queue);
 gst_registry_remove_feature(gst_registry_get(), queue); gst_object_unref(queue);
 GstElement *unavailable = NULL;
 g_signal_emit_by_name(rx, "request-encoded-filter", "fixture", "video_1", caps, &unavailable);
 assert(!unavailable && r.offer_failed && !r.accept_samples);
 stop_pipeline(&r);
 GstElement *late = NULL;
 g_signal_emit_by_name(rx, "request-encoded-filter", "fixture", "video_0", caps, &late); assert(!late);
 gst_caps_unref(caps); gst_object_unref(rx); gst_object_unref(transport);
 assert(proc_handler_call(ph, "native422_feed", &request) && feed.status == PV_FEED_RESET);
 feed.command=PV_FEED_DETACH;
 assert(proc_handler_call(ph, "native422_feed", &request) && feed.status == PV_FEED_OK);
 free(bytes); calldata_free(&request);
 source_controls_disconnect(&r);
 obs_source_release(source); g_mutex_clear(&r.lock); g_rec_mutex_clear(&r.delivery); obs_shutdown();
 puts("PASS production encoded-filter signal -> native VT -> source-bound exact v210; detach rejects late filter");
}
