/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test-only producer: execute the production encoded-filter hook and source
 * adapters. WHEP stays NULL; no RTP/profile-admission claim and no credentials. */
#define PIXELVIEW_WHEP_TEST
#include <obs-module.h>
#include <gst/app/gstappsink.h>
#include <assert.h>
static GstSample *injected_audio, *bounded_audio;
static GstMemory *bounded_first, *bounded_second;
static GstSample *test_pull_audio(GstAppSink *sink)
{
 if (!injected_audio) return gst_app_sink_pull_sample(sink);
 GstSample *s=injected_audio; injected_audio=NULL; return s;
}
static void test_unref_audio(GstSample *s)
{
 if (s==bounded_audio) {
  GstBuffer *b=gst_sample_get_buffer(s);
  assert(gst_buffer_n_memory(b)==2);
  assert(gst_buffer_peek_memory(b, 0)==bounded_first && gst_buffer_peek_memory(b, 1)==bounded_second);
  bounded_audio=NULL;
 }
 gst_sample_unref(s);
}
#define gst_app_sink_pull_sample test_pull_audio
#define gst_sample_unref test_unref_audio
static unsigned streaming_getters;
static bool audit_streaming_getters;
static G_GNUC_UNUSED bool audited_muted(obs_source_t *s)
{ if (audit_streaming_getters) ++streaming_getters; return obs_source_muted(s); }
static G_GNUC_UNUSED float audited_volume(obs_source_t *s)
{ if (audit_streaming_getters) ++streaming_getters; return obs_source_get_volume(s); }
#define obs_source_muted audited_muted
#define obs_source_get_volume audited_volume
#include "../../pixelview-whep/native-422-filter.h"
/* Elementary-file fidelity fixture has no RTP and is not admission evidence. */
#define pv_native422_filter_require_rtp(filter, receiver) ((void)(filter), (void)(receiver))
#include "../../pixelview-whep/pixelview-whep.c"
#undef pv_native422_filter_require_rtp
#undef obs_source_muted
#undef obs_source_get_volume
#include <gst/app/gstappsrc.h>
#include <assert.h>
struct fixture {
	struct receiver r;
	GstElement *transport, *rx, *src;
};
static const char *fixture_name(void *p)
{
	(void)p;
	return "production native feed fixture";
}
static void *fixture_create(obs_data_t *s, obs_source_t *source)
{
	(void)s;
	struct fixture *f = calloc(1, sizeof(*f));
	assert(f);
	f->r.source = source;
	f->r.generation = f->r.active_generation = 1;
	f->r.accept_samples = true;
	f->r.active_caps.profiles = 31;
	f->r.active_caps.hevc_level_id = 123;
	f->r.active_latency = 50;
	g_mutex_init(&f->r.lock);
	g_rec_mutex_init(&f->r.delivery);
	source_controls_init(&f->r);
	proc_handler_add(obs_source_get_proc_handler(source), "void native422_feed(ptr request, out int version)",
			 feed_proc, &f->r);
	f->transport = make_pipeline(&f->r, "http://127.0.0.1:9/whep");
	assert(f->transport);
	f->rx = gst_bin_get_by_name(GST_BIN(f->transport), "rx");
	assert(f->rx);
	GstCaps *caps = gst_caps_from_string("video/x-raw,format=P010_10LE");
	GstElement *filter = NULL;
	g_signal_emit_by_name(f->rx, "request-encoded-filter", "fixture", "video_0", caps, &filter);
	gst_caps_unref(caps);
	assert(filter);
	f->r.pipe = gst_pipeline_new(NULL);
	f->src = gst_element_factory_make("appsrc", NULL);
	GstElement *sink = gst_element_factory_make("fakesink", NULL);
	g_object_set(f->src, "format", GST_FORMAT_TIME, NULL);
	g_object_set(sink, "sync", FALSE, NULL);
	gst_bin_add_many(GST_BIN(f->r.pipe), f->src, filter, sink, NULL);
	assert(gst_element_link_many(f->src, filter, sink, NULL));
	gst_object_unref(filter); /* release the signal-returned owned reference */
	gst_element_set_start_time(f->r.pipe, GST_CLOCK_TIME_NONE);
	gst_element_set_base_time(f->r.pipe, 1000000000);
	assert(gst_element_set_state(f->r.pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
	return f;
}
static void fixture_destroy(void *p)
{
	struct fixture *f = p;
	source_controls_disconnect(&f->r);
	stop_pipeline(&f->r);
	gst_object_unref(f->rx);
	gst_object_unref(f->transport);
	g_mutex_clear(&f->r.lock);
	g_rec_mutex_clear(&f->r.delivery);
	free(f);
}
static void readiness_regression(void)
{
	struct receiver r = {0};
	g_mutex_init(&r.lock);
	r.generation = r.active_generation = 1;
	r.accept_samples = true;
	r.frames = 42; r.last_video = os_gettime_ns(); r.state = "playing";
	calldata_t cd; calldata_init(&cd);
	status_proc(&r, &cd);
	assert(calldata_bool(&cd, "ready"));
	const char *states[] = {"error", "ended", "idle", "connecting"};
	for (unsigned i = 0; i < 4; ++i) {
		r.state = states[i]; status_proc(&r, &cd);
		assert(!calldata_bool(&cd, "ready"));
	}
	r.state = "playing"; r.last_video = os_gettime_ns() - 500000001;
	status_proc(&r, &cd); assert(!calldata_bool(&cd, "ready"));
	r.last_video = os_gettime_ns(); r.generation++;
	status_proc(&r, &cd); assert(!calldata_bool(&cd, "ready"));
	r.active_generation = r.generation; r.frames = 0;
	status_proc(&r, &cd); assert(!calldata_bool(&cd, "ready"));
	r.frames = 1; r.accept_samples = false;
	status_proc(&r, &cd); assert(!calldata_bool(&cd, "ready"));
	r.accept_samples = true; r.changed = true;
	status_proc(&r, &cd); assert(!calldata_bool(&cd, "ready"));
	r.changed = false;
	status_proc(&r, &cd); assert(calldata_bool(&cd, "ready"));
	calldata_free(&cd); g_mutex_clear(&r.lock);
}
obs_source_t *pv_owner_fixture_create(void)
{
	readiness_regression();
	gst_init(NULL, NULL);
	struct obs_source_info info = {.id = "decklink_production_feed",
				       .type = OBS_SOURCE_TYPE_INPUT,
				       .output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO,
				       .get_name = fixture_name,
				       .create = fixture_create,
				       .destroy = fixture_destroy};
	static bool registered;
	if (!registered) { obs_register_source(&info); registered=true; }
	return obs_source_create_private(info.id, "production-native-source", NULL);
}
/* Feed one actual appsink callback at the selected delivery boundary. */
void pv_owner_fixture_audio(obs_source_t *source, bool early, uint64_t timestamp_ns)
{
 struct fixture *f = obs_obj_get_data(source);
 GstElement *p = gst_parse_launch("appsrc name=src format=time ! audio/x-raw,format=F32LE,layout=interleaved,channels=2,rate=48000 ! appsink name=sink sync=false", NULL);
 GstElement *src = gst_bin_get_by_name(GST_BIN(p), "src");
 GstAppSink *sink = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(p), "sink"));
 gst_element_set_state(p, GST_STATE_PLAYING);
 float pcm[1920]; for (unsigned i=0; i<1920; ++i) pcm[i]=.5f;
 GstBuffer *b = gst_buffer_new_allocate(NULL, sizeof(pcm), NULL);
 gst_buffer_fill(b, 0, pcm, sizeof(pcm));
 GST_BUFFER_PTS(b)=timestamp_ns-gst_element_get_base_time(f->r.pipe);
 GST_BUFFER_DURATION(b)=20000000;
 assert(gst_app_src_push_buffer(GST_APP_SRC(src), b)==GST_FLOW_OK);
 assert((early ? native_audio_sample(sink, &f->r) : audio_sample(sink, &f->r))==GST_FLOW_OK);
 gst_element_set_state(p, GST_STATE_NULL);
 gst_object_unref(src); gst_object_unref(sink); gst_object_unref(p);
}
void pv_owner_fixture_bounds(obs_source_t *source)
{
 struct fixture *f=obs_obj_get_data(source);
 for (unsigned early=0; early<2; ++early) {
  GstBuffer *b=gst_buffer_new();
  const size_t n=(PV_FEED_MAX_AUDIO_FRAMES+1)*8u;
  gst_buffer_append_memory(b, gst_allocator_alloc(NULL, n/2, NULL));
  gst_buffer_append_memory(b, gst_allocator_alloc(NULL, n-n/2, NULL));
  bounded_first=gst_buffer_peek_memory(b, 0); bounded_second=gst_buffer_peek_memory(b, 1);
  GstCaps *caps=gst_caps_from_string("audio/x-raw,format=F32LE,layout=interleaved,channels=2,rate=48000");
  GstSegment segment; gst_segment_init(&segment, GST_FORMAT_TIME);
  GST_BUFFER_PTS(b)=0;
  injected_audio=bounded_audio=gst_sample_new(b, caps, &segment, NULL);
  gst_buffer_unref(b); gst_caps_unref(caps); /* Sole sample owner: READ map can coalesce. */
  assert((early ? native_audio_sample(NULL, &f->r) : audio_sample(NULL, &f->r))==GST_FLOW_ERROR);
  assert(!bounded_audio && !injected_audio);
 }
 puts("both PCM callbacks: fragmented oversized refusal before coalescing/map PASS");
}
struct control_race { obs_source_t *source; gint stop, started; };
static gpointer change_controls(gpointer p)
{
 struct control_race *race=p; g_atomic_int_set(&race->started, 1);
 for (unsigned i=0; i<10000 && !g_atomic_int_get(&race->stop); ++i) {
  obs_source_set_volume(race->source, .25f); obs_source_set_muted(race->source, true);
  obs_source_set_muted(race->source, false); obs_source_set_volume(race->source, 1.f);
  g_usleep(100);
 }
 return NULL;
}
struct control_lifetime { struct receiver *r; GMutex lock; GCond wake; bool entered, release, mute; gint disconnecting, done; };
static void block_control(void *p, calldata_t *cd)
{
 (void)cd; struct control_lifetime *l=p;
 g_mutex_lock(&l->lock); l->entered=true; g_cond_broadcast(&l->wake);
 while (!l->release) g_cond_wait(&l->wake, &l->lock);
 g_mutex_unlock(&l->lock);
}
static gpointer lifetime_set(gpointer p)
{ struct control_lifetime *l=p; if (l->mute) obs_source_set_muted(l->r->source, true); else obs_source_set_volume(l->r->source, .25f); return NULL; }
static gpointer lifetime_disconnect(gpointer p)
{
 struct control_lifetime *l=p; g_atomic_int_set(&l->disconnecting, 1);
 source_controls_disconnect(l->r); g_atomic_int_set(&l->done, 1); return NULL;
}
static void controls_lifetime(obs_source_t *source, bool mute)
{
 // Retain the source/handler while independently retiring the callback target.
 struct receiver *r=g_new0(struct receiver, 1); r->source=source; g_mutex_init(&r->lock);
 source_controls_init(r);
 struct control_lifetime l={.r=r, .mute=mute}; g_mutex_init(&l.lock); g_cond_init(&l.wake);
 signal_handler_t *signals=obs_source_get_signal_handler(source);
 signal_handler_connect(signals, mute ? "mute" : "volume", block_control, &l);
 GThread *setter=g_thread_new("control-set", lifetime_set, &l);
 g_mutex_lock(&l.lock); while (!l.entered) g_cond_wait(&l.wake, &l.lock); g_mutex_unlock(&l.lock);
 GThread *drain=g_thread_new("control-drain", lifetime_disconnect, &l);
 while (!g_atomic_int_get(&l.disconnecting)) g_thread_yield();
 g_usleep(20000); assert(!g_atomic_int_get(&l.done));
 g_mutex_lock(&l.lock); l.release=true; g_cond_broadcast(&l.wake); g_mutex_unlock(&l.lock);
 g_thread_join(setter); g_thread_join(drain); assert(g_atomic_int_get(&l.done));
 signal_handler_disconnect(signals, mute ? "mute" : "volume", block_control, &l);
 g_mutex_clear(&r->lock); g_free(r);
 // Real libobs dispatch after callback storage is freed must not touch it.
 obs_source_set_volume(source, 1.f); obs_source_set_muted(source, true); obs_source_set_muted(source, false);
 g_cond_clear(&l.wake); g_mutex_clear(&l.lock);
 puts("source controls: concurrent signal disconnect drains, post-free dispatch safe PASS");
}
void pv_owner_fixture_controls(obs_source_t *source)
{
 struct fixture *f=obs_obj_get_data(source);
 pv_owner_fixture_bounds(source);
 struct pv_feed_request r={.size=sizeof(r), .version=PV_FEED_VERSION, .command=PV_FEED_ATTACH, .route=PV_FEED_NATIVE};
 pv_feed_request(&f->r.feed, 1, &r); assert(r.status==PV_FEED_OK);
 int16_t pcm[1920]; r.data=pcm; r.capacity=sizeof(pcm);
 for (unsigned i=0; i<4; ++i) {
  if (i) { obs_source_set_volume(source, .25f); obs_source_set_muted(source, i==2); }
  audit_streaming_getters=true;
  pv_owner_fixture_audio(source, true, 2000000000);
  audit_streaming_getters=false;
  assert(streaming_getters==0);
  r.command=PV_FEED_AUDIO; pv_feed_request(&f->r.feed, 1, &r);
  assert(r.status==PV_FEED_OK && r.audio_frames==960);
  for (unsigned j=0; j<1920; ++j) assert(pcm[j]==(i==2 ? 0 : i==0 ? 16384 : 4096));
 }
 struct control_race race={.source=source};
 GThread *writer=g_thread_new("source-controls", change_controls, &race);
 while (!g_atomic_int_get(&race.started)) g_thread_yield();
 for (unsigned i=0; i<100; ++i) {
  audit_streaming_getters=true; pv_owner_fixture_audio(source, true, 2000000000); audit_streaming_getters=false;
  r.command=PV_FEED_AUDIO; g_mutex_lock(&f->r.lock); pv_feed_request(&f->r.feed, 1, &r); g_mutex_unlock(&f->r.lock);
  assert(r.status==PV_FEED_OK && streaming_getters==0);
  assert(pcm[0]==0 || pcm[0]==4096 || pcm[0]==16384);
  for (unsigned j=1; j<1920; ++j) assert(pcm[j]==pcm[0]);
 }
 g_atomic_int_set(&race.stop, 1); g_thread_join(writer);
 controls_lifetime(source, false); controls_lifetime(source, true);
 r.command=PV_FEED_DETACH; pv_feed_request(&f->r.feed, 1, &r);
 const uint64_t native_token=r.token;
 r.command=PV_FEED_ATTACH; r.route=PV_FEED_RENDERED;
 pv_feed_request(&f->r.feed, 1, &r); assert(r.status==PV_FEED_OK && r.token!=native_token);
 for (unsigned i=0; i<2; ++i) {
  obs_source_set_volume(source, .25f); obs_source_set_muted(source, i==1);
  audit_streaming_getters=true; pv_owner_fixture_audio(source, true, 2000000000);
  r.command=PV_FEED_AUDIO; pv_feed_request(&f->r.feed, 1, &r); assert(r.status==PV_FEED_EMPTY);
  pv_owner_fixture_audio(source, false, 2000000000); audit_streaming_getters=false;
  pv_feed_request(&f->r.feed, 1, &r); assert(r.status==PV_FEED_OK && streaming_getters==0);
  for (unsigned j=0; j<1920; ++j) assert(pcm[j]==(i ? 0 : 4096));
 }
 r.command=PV_FEED_DETACH; pv_feed_request(&f->r.feed, 1, &r);
 obs_source_set_volume(source, 1.f); obs_source_set_muted(source, false);
 puts("source controls: initialized gain/mute and signal changes without streaming getters PASS");
}
void pv_owner_fixture_produce(obs_source_t *source, const char *path)
{
	struct fixture *f = obs_obj_get_data(source);
	bool attached = false;
	for (unsigned i = 0; i < 1000; i++) {
		g_mutex_lock(&f->r.lock);
		attached = f->r.feed.token != 0;
		g_mutex_unlock(&f->r.lock);
		if (attached)
			break;
		g_usleep(1000);
	}
	assert(attached);
	GstElement *reader = gst_parse_launch(
		"filesrc name=file ! h265parse ! video/x-h265,stream-format=hvc1,alignment=au ! appsink name=read sync=false",
		NULL);
	assert(reader);
	GstElement *file = gst_bin_get_by_name(GST_BIN(reader), "file");
	g_object_set(file, "location", path, NULL);
	gst_object_unref(file);
	GstAppSink *read = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(reader), "read"));
	gst_element_set_state(reader, GST_STATE_PLAYING);
	unsigned count = 0;
	for (;;) {
		GstSample *s = gst_app_sink_try_pull_sample(read, 5 * GST_SECOND);
		if (!s)
			break;
		GstBuffer *b = gst_buffer_copy(gst_sample_get_buffer(s));
		GST_BUFFER_PTS(b) = gst_util_uint64_scale(count++, 1001 * GST_SECOND, 30000);
		GST_BUFFER_DURATION(b) = gst_util_uint64_scale(1, 1001 * GST_SECOND, 30000);
		gst_app_src_set_caps(GST_APP_SRC(f->src), gst_sample_get_caps(s));
		assert(gst_app_src_push_buffer(GST_APP_SRC(f->src), b) == GST_FLOW_OK);
		gst_sample_unref(s);
	}
	assert(count == 3);
	gst_app_src_end_of_stream(GST_APP_SRC(f->src));
	GstBus *bus = gst_element_get_bus(f->r.pipe);
	GstMessage *m = gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
	assert(m && GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS);
	gst_message_unref(m);
	gst_object_unref(bus);
	gst_element_set_state(reader, GST_STATE_NULL);
	gst_object_unref(read);
	gst_object_unref(reader);
	GstElement *ap = gst_parse_launch(
		"appsrc name=src format=time ! audio/x-raw,format=F32LE,layout=interleaved,channels=2,rate=48000 ! appsink name=sink sync=false",
		NULL);
	GstElement *as = gst_bin_get_by_name(GST_BIN(ap), "src");
	GstAppSink *sink = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(ap), "sink"));
	gst_element_set_state(ap, GST_STATE_PLAYING);
	float pcm[960];
	for (unsigned i = 0; i < 960; i++)
		pcm[i] = (i & 1) ? -.5f : .5f;
	GstBuffer *b = gst_buffer_new_allocate(NULL, sizeof(pcm), NULL);
	gst_buffer_fill(b, 0, pcm, sizeof(pcm));
	GST_BUFFER_PTS(b) = 0;
	GST_BUFFER_DURATION(b) = 10000000;
	assert(gst_app_src_push_buffer(GST_APP_SRC(as), b) == GST_FLOW_OK);
	assert(native_audio_sample(sink, &f->r) == GST_FLOW_OK);
	gst_element_set_state(ap, GST_STATE_NULL);
	gst_object_unref(as);
	gst_object_unref(sink);
	gst_object_unref(ap);
}
