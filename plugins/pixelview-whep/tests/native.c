/* SPDX-License-Identifier: GPL-2.0-or-later */
#define PIXELVIEW_WHEP_TEST 1
#include <obs-module.h>
#include <gst/gst.h>
#include <assert.h>
static GstElement *test_parse(const char *, GError **);
static GstStateChangeReturn test_set_state(GstElement *, GstState);
static void test_video(obs_source_t *, const struct obs_source_frame *);
static void test_video2(obs_source_t *, const struct obs_source_frame2 *);
static void test_audio(obs_source_t *, const struct obs_source_audio *);
#define gst_parse_launch test_parse
#define gst_element_set_state test_set_state
#define obs_source_output_video test_video
#define obs_source_output_video2 test_video2
#define obs_source_output_audio test_audio
#include "../pixelview-whep.c"
#undef gst_parse_launch
#undef gst_element_set_state
#undef obs_source_output_video
#undef obs_source_output_video2
#undef obs_source_output_audio
static struct receiver *cancel_during_build;
static bool replace_during_build;
static GstElement *cancelled_pipeline;
static gint obsolete_starts, delivered_video, delivered_audio;
static GstElement *test_parse(const char *spec, GError **error)
{
 GstElement *pipe = gst_parse_launch(spec, error);

 GstElement *rx=pipe?gst_bin_get_by_name(GST_BIN(pipe),"rx"):NULL;
 if(rx) {g_object_set(rx,"stun-server",NULL,NULL);gst_object_unref(rx);}
 if (cancel_during_build) {
  cancelled_pipeline = pipe;
  disconnect_proc(cancel_during_build, NULL);
  if (replace_during_build) {
   calldata_t cd; calldata_init(&cd);
   calldata_set_string(&cd, "endpoint", "test://synthetic");
   connect_proc(cancel_during_build, &cd);
   calldata_free(&cd);
  }
  gst_object_ref(pipe); /* Preserve identity until the worker has finished. */
  cancel_during_build = NULL;
 }
 return pipe;
}
static GstStateChangeReturn test_set_state(GstElement *pipe, GstState state)
{
 if (pipe == cancelled_pipeline && state == GST_STATE_PLAYING)
  g_atomic_int_inc(&obsolete_starts);
 return gst_element_set_state(pipe, state);
}
static void test_video(obs_source_t *source, const struct obs_source_frame *frame)
{
 if (source) obs_source_output_video(source, frame);
 else if (frame) g_atomic_int_inc(&delivered_video);
}
static void test_video2(obs_source_t *source, const struct obs_source_frame2 *frame)
{
 if (source) obs_source_output_video2(source,frame);
 else if (frame) g_atomic_int_inc(&delivered_video);
}
static void test_audio(obs_source_t *source, const struct obs_source_audio *audio)
{
 if (source) obs_source_output_audio(source, audio);
 else g_atomic_int_inc(&delivered_audio);
}
static void cancellation_during_build(bool replace)
{
 struct receiver r = {.state="connecting", .endpoint=g_strdup("test://synthetic"),
                      .latency=50, .changed=true};
 cancel_during_build = &r;
 replace_during_build = replace;
 r.thread = g_thread_new("cancel-test", worker, &r);
 g_usleep(300000);
 g_mutex_lock(&r.lock); r.quit=true; g_cond_signal(&r.wake); g_mutex_unlock(&r.lock);
 g_thread_join(r.thread);
 assert(g_atomic_int_get(&obsolete_starts) == 0);

 assert(!strcmp(r.state, replace ? "playing" : "idle"));
 if (replace) assert(r.frames > 0 && r.audio_frames > 0);
 g_cond_clear(&r.wake); g_mutex_clear(&r.lock); g_rec_mutex_clear(&r.delivery);
 gst_object_unref(cancelled_pipeline);
 cancelled_pipeline = NULL;
 puts("PASS cancellation during construction never starts obsolete pipeline");
}
static void cancelled_media_is_not_delivered(void)
{
 struct receiver r = {.state="connecting", .accept_samples=true};
 g_atomic_int_set(&delivered_video, 0); g_atomic_int_set(&delivered_audio, 0);
 r.pipe = make_pipeline(&r, "test://synthetic");
 assert(r.pipe);
 assert(gst_element_set_state(r.pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
 for (int i=0; i<100 && (!g_atomic_int_get(&delivered_video) || !g_atomic_int_get(&delivered_audio)); i++) g_usleep(10000);
 assert(g_atomic_int_get(&delivered_video) > 0 && g_atomic_int_get(&delivered_audio) > 0);
 disconnect_proc(&r, NULL);
 int video = g_atomic_int_get(&delivered_video), audio = g_atomic_int_get(&delivered_audio);
 /* Leave streaming alive: cancellation must gate the OBS calls themselves. */
 g_usleep(200000);
 assert(g_atomic_int_get(&delivered_video) == video);
 assert(g_atomic_int_get(&delivered_audio) == audio);
 stop_pipeline(&r);
 g_rec_mutex_clear(&r.delivery); g_mutex_clear(&r.lock); g_cond_clear(&r.wake);
 puts("PASS cancellation gates actual video and audio delivery");
}
int main(void)
{
 gst_init(NULL, NULL);
 struct pixelview_receive_capabilities caps;assert(pixelview_capability_probe_get(&caps,NULL,NULL));assert(caps.profiles);
 cancellation_during_build(false);
 cancellation_during_build(true);
 cancelled_media_is_not_delivered();
 assert(obs_startup("en-US", NULL, NULL));
 struct obs_audio_info ai = {.samples_per_sec=48000, .speakers=SPEAKERS_STEREO};
 assert(obs_reset_audio(&ai));
 assert(obs_module_load());
 obs_data_t *settings = obs_data_create();
 obs_data_set_string(settings, "endpoint", "https://example.invalid/secret");
 obs_source_t *source = obs_source_create_private("pixelview_whep_source", "test", settings);
 assert(source);
 obs_data_t *saved = obs_source_get_settings(source);
 assert(!strstr(obs_data_get_json(saved), "secret"));
 assert(obs_data_get_int(saved, "latency") == 100);
 calldata_t cd; calldata_init(&cd);
 assert(proc_handler_call(obs_source_get_proc_handler(source), "get_status", &cd));
 assert(!strcmp(calldata_string(&cd, "state"), "idle"));
 assert(calldata_int(&cd, "frames") == 0);
 assert(proc_handler_call(obs_source_get_proc_handler(source), "disconnect", &cd));
 calldata_set_string(&cd, "endpoint", "test://synthetic");
 calldata_set_int(&cd, "latency", 50);
 assert(proc_handler_call(obs_source_get_proc_handler(source), "connect", &cd));
 for (int i=0; i<100; i++) {
   g_usleep(50000);
   proc_handler_call(obs_source_get_proc_handler(source), "get_status", &cd);
   if (calldata_int(&cd,"frames") >= 5 && calldata_int(&cd,"audio_frames") > 0) break;
 }
 assert(calldata_int(&cd,"frames") >= 5);
 assert(calldata_int(&cd,"audio_frames") > 0);
 assert(!strcmp(calldata_string(&cd,"state"), "playing"));
 assert(proc_handler_call(obs_source_get_proc_handler(source), "disconnect", &cd));
 GstElement *rtc = gst_element_factory_make("webrtcbin", NULL);
 assert(rtc);
 struct receiver latency_test = {.latency = 50, .active_latency = 50};
 struct receive_attempt *latency_attempt=attempt_new(&latency_test);
 webrtc_ready(NULL, "test", rtc, latency_attempt);attempt_unref(latency_attempt,NULL);
 guint latency=0; g_object_get(rtc,"latency",&latency,NULL);
 assert(latency == 50); gst_object_unref(rtc);
 for (int n=0;n<20;n++) {
  calldata_set_string(&cd,"endpoint","test://synthetic"); calldata_set_int(&cd,"latency",50);
  proc_handler_call(obs_source_get_proc_handler(source),"connect",&cd);
  g_usleep(10000);
  proc_handler_call(obs_source_get_proc_handler(source),"disconnect",&cd);
  g_usleep(20000);
  proc_handler_call(obs_source_get_proc_handler(source),"get_status",&cd);
  assert(!strcmp(calldata_string(&cd,"state"),"idle"));
 }
 calldata_set_string(&cd,"endpoint","http://example.com/private");
 proc_handler_call(obs_source_get_proc_handler(source),"connect",&cd);
 proc_handler_call(obs_source_get_proc_handler(source),"get_status",&cd);
 assert(!strcmp(calldata_string(&cd,"state"),"error"));
 calldata_set_string(&cd,"endpoint","http://127.0.0.1:9/private-test");
 proc_handler_call(obs_source_get_proc_handler(source),"connect",&cd);
 for (int i=0;i<100;i++) {
  g_usleep(50000); proc_handler_call(obs_source_get_proc_handler(source),"get_status",&cd);
  if (calldata_int(&cd,"jitter_latency")==50) break;
 }
 assert(calldata_int(&cd,"jitter_latency")==50);
 proc_handler_call(obs_source_get_proc_handler(source),"disconnect",&cd);
 calldata_free(&cd); obs_data_release(saved); obs_data_release(settings);
 obs_source_release(source); obs_shutdown();
 puts("PASS private settings and native source lifecycle");
}
