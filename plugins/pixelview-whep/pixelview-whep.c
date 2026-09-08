/* SPDX-License-Identifier: GPL-2.0-or-later
 * Pixelview native WHEP input. Raw appsink integration informed by
 * obs-gstreamer (C) 2018-2021 Florian Zwoch, GPL-2.0-or-later.
 */
#include <obs-module.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <util/platform.h>
#include <string.h>
#ifndef PIXELVIEW_WHEP_TEST
#include "runtime.h"
#endif
OBS_DECLARE_MODULE()

struct receiver {
 obs_source_t *source;
 GMutex lock;
 /* Serializes cancellation with startup and actual OBS delivery. Never hold
  * lock across GStreamer state changes (webrtc-ready may run synchronously). */
 GRecMutex delivery;
 uint64_t generation, active_generation;
 GCond wake;
 GThread *thread;
 const char *state;
 char *endpoint;
 uint64_t frames, audio_frames, last_video;
 guint latency, active_latency;
 int jitter_latency;
 bool quit, changed, accept_samples;
 GstElement *pipe; /* worker-owned; callbacks finish before teardown */
};
static void status_proc(void *opaque, calldata_t *cd)
{
 struct receiver *r = opaque;
 g_mutex_lock(&r->lock);
 calldata_set_string(cd, "state", r->state);
 calldata_set_int(cd, "frames", r->frames);
 calldata_set_int(cd, "audio_frames", r->audio_frames);
 calldata_set_int(cd, "latency", r->latency);
 calldata_set_int(cd, "jitter_latency", r->jitter_latency);
 g_mutex_unlock(&r->lock);
}
static void wipe(char **text)
{
 if (!*text) return;
 volatile char *p = *text;
 size_t n = strlen(*text);
 while (n--) *p++ = 0;
 g_free(*text); *text = NULL;
}
static bool valid_endpoint(const char *endpoint)
{
 if (!endpoint || strlen(endpoint) > 16384) return false;
#ifdef PIXELVIEW_WHEP_TEST
 if (!strcmp(endpoint, "test://synthetic")) return true;
#endif
 GUri *uri = g_uri_parse(endpoint, G_URI_FLAGS_NONE, NULL);
 if (!uri) return false;
 const char *scheme = g_uri_get_scheme(uri), *host = g_uri_get_host(uri);
 bool ok = scheme && host && *host && !g_uri_get_userinfo(uri) && !g_uri_get_fragment(uri) &&
  (!strcmp(scheme, "https") || (!strcmp(scheme, "http") &&
   (!strcmp(host,"127.0.0.1") || !strcmp(host,"::1") || !strcmp(host,"localhost"))));
 g_uri_unref(uri); return ok;
}
static void connect_proc(void *opaque, calldata_t *cd)
{
 struct receiver *r = opaque;
 const char *endpoint = calldata_string(cd, "endpoint");
 int64_t latency = 50;
 calldata_get_int(cd, "latency", &latency);
 g_rec_mutex_lock(&r->delivery);
 g_mutex_lock(&r->lock);
 r->generation++;
 wipe(&r->endpoint);
 bool valid = valid_endpoint(endpoint) && latency >= 0 && latency <= 2000;
 r->endpoint = valid ? g_strdup(endpoint) : NULL;
 r->latency = valid ? (guint)latency : 50;
 r->state = valid ? "connecting" : "error";
 r->frames = r->audio_frames = 0; r->jitter_latency = -1;
 r->changed = true; r->accept_samples = false;
 g_cond_signal(&r->wake); g_mutex_unlock(&r->lock);
 g_rec_mutex_unlock(&r->delivery);
}
static void disconnect_proc(void *opaque, calldata_t *cd)
{
 (void)cd; struct receiver *r = opaque;
 g_rec_mutex_lock(&r->delivery);
 g_mutex_lock(&r->lock);
 r->generation++;
 wipe(&r->endpoint); r->state = "idle"; r->changed = true; r->accept_samples = false;
 g_cond_signal(&r->wake); g_mutex_unlock(&r->lock);
 g_rec_mutex_unlock(&r->delivery);
}
static uint64_t timestamp(GstSample *sample, GstElement *pipe)
{
 GstBuffer *b = gst_sample_get_buffer(sample);
 const GstSegment *segment = gst_sample_get_segment(sample);
 GstClockTime time = GST_CLOCK_TIME_NONE;
 if (segment && GST_BUFFER_PTS_IS_VALID(b))
  time = gst_segment_to_running_time(segment, GST_FORMAT_TIME, GST_BUFFER_PTS(b));
 /* GstSystemClock and OBS use monotonic nanoseconds on macOS. Both media
  * branches use the same pipeline base, preserving relative A/V PTS. */
 return GST_CLOCK_TIME_IS_VALID(time) ? gst_element_get_base_time(pipe) + time : os_gettime_ns();
}
static GstFlowReturn video_sample(GstAppSink *sink, gpointer opaque)
{
 struct receiver *r = opaque;
 GstSample *sample = gst_app_sink_pull_sample(sink);
 if (!sample) return GST_FLOW_EOS;
 GstVideoInfo info; GstVideoFrame mapped;
 if (!gst_video_info_from_caps(&info, gst_sample_get_caps(sample)) ||
     !gst_video_frame_map(&mapped, &info, gst_sample_get_buffer(sample), GST_MAP_READ)) {
  gst_sample_unref(sample); return GST_FLOW_ERROR;
 }
 struct obs_source_frame frame = {0};
 frame.format = VIDEO_FORMAT_BGRA; frame.width = info.width; frame.height = info.height;
 frame.data[0] = GST_VIDEO_FRAME_PLANE_DATA(&mapped,0);
 frame.linesize[0] = GST_VIDEO_FRAME_PLANE_STRIDE(&mapped,0);
 frame.timestamp = timestamp(sample, r->pipe); frame.full_range = true;
 g_rec_mutex_lock(&r->delivery);
 g_mutex_lock(&r->lock);
 bool deliver = !r->quit && !r->changed && r->accept_samples && r->generation == r->active_generation;
 if (deliver) { r->frames++; r->state = "playing"; r->last_video = os_gettime_ns(); }
 g_mutex_unlock(&r->lock);
 if (deliver) obs_source_output_video(r->source, &frame);
 g_rec_mutex_unlock(&r->delivery);
 gst_video_frame_unmap(&mapped); gst_sample_unref(sample);
 return GST_FLOW_OK;
}
static GstFlowReturn audio_sample(GstAppSink *sink, gpointer opaque)
{
 struct receiver *r = opaque;
 GstSample *sample = gst_app_sink_pull_sample(sink);
 if (!sample) return GST_FLOW_EOS;
 GstAudioInfo info; GstMapInfo mapped;
 if (!gst_audio_info_from_caps(&info, gst_sample_get_caps(sample)) ||
     !gst_buffer_map(gst_sample_get_buffer(sample), &mapped, GST_MAP_READ)) {
  gst_sample_unref(sample); return GST_FLOW_ERROR;
 }
 struct obs_source_audio audio = {0};
 audio.format = AUDIO_FORMAT_FLOAT; audio.speakers = SPEAKERS_STEREO;
 audio.samples_per_sec = info.rate; audio.frames = mapped.size / info.bpf;
 audio.data[0] = mapped.data; audio.timestamp = timestamp(sample, r->pipe);
 g_rec_mutex_lock(&r->delivery);
 g_mutex_lock(&r->lock);
 bool deliver = !r->quit && !r->changed && r->accept_samples && r->generation == r->active_generation;
 if (deliver) r->audio_frames += audio.frames;
 g_mutex_unlock(&r->lock);
 if (deliver) obs_source_output_audio(r->source, &audio);
 g_rec_mutex_unlock(&r->delivery);
 gst_buffer_unmap(gst_sample_get_buffer(sample), &mapped); gst_sample_unref(sample);
 return GST_FLOW_OK;
}
/* rswebrtc 0.15.2 omits packetization-mode from raw-output H264
 * receive offers (RFC6184 then defaults to mode 0). Pixelview/Pion sends
 * mode 1, supported by rtph264depay; advertise that before SDP creation. */
static void configure_transceiver(GstElement *rtc, GObject *transceiver, gpointer opaque)
{
 (void)rtc; (void)opaque;
 GstCaps *caps = NULL;
 g_object_get(transceiver, "codec-preferences", &caps, NULL);
 if (!caps) return;
 caps = gst_caps_make_writable(caps);
 for (guint i = 0; i < gst_caps_get_size(caps); i++) {
  GstStructure *s = gst_caps_get_structure(caps, i);
  const char *encoding = gst_structure_get_string(s, "encoding-name");
  if (encoding && !g_ascii_strcasecmp(encoding, "H264"))
   gst_structure_set(s, "packetization-mode", G_TYPE_STRING, "1", NULL);
 }
 g_object_set(transceiver, "codec-preferences", caps, NULL);
 gst_caps_unref(caps);
}
static void webrtc_ready(GObject *signaller, const char *peer, GstElement *rtc, gpointer opaque)
{
 (void)signaller; (void)peer;
 struct receiver *r = opaque;
 g_mutex_lock(&r->lock);
 guint latency = r->active_latency;
 g_mutex_unlock(&r->lock);
 g_signal_connect(rtc, "on-new-transceiver", G_CALLBACK(configure_transceiver), NULL);
 g_object_set(rtc, "latency", latency, NULL);
 g_object_get(rtc, "latency", &latency, NULL);
 g_mutex_lock(&r->lock); r->jitter_latency = (int)latency; g_mutex_unlock(&r->lock);
}
static GstElement *make_pipeline(struct receiver *r, const char *endpoint)
{
 /* Only fixed code is parsed; never interpolate credentials into a pipeline. */
 const char *spec =
  "whepclientsrc name=rx video-codecs=\"<H264,H265>\" audio-codecs=\"<OPUS>\" "
  "rx. ! video/x-raw ! queue max-size-buffers=3 max-size-bytes=0 max-size-time=0 leaky=downstream ! videoconvert ! video/x-raw,format=BGRA ! appsink name=video "
  "rx. ! audio/x-raw ! queue max-size-time=200000000 max-size-bytes=0 max-size-buffers=0 ! audioconvert ! audioresample ! audio/x-raw,format=F32LE,layout=interleaved,channels=2,rate=48000 ! appsink name=audio";
#ifdef PIXELVIEW_WHEP_TEST
 if (!strcmp(endpoint,"test://synthetic")) spec =
  "videotestsrc is-live=true ! video/x-raw,format=BGRA,width=64,height=64,framerate=30/1 ! appsink name=video "
  "audiotestsrc is-live=true ! audio/x-raw,format=F32LE,channels=2,rate=48000 ! appsink name=audio";
#endif
 GError *error = NULL;
 GstElement *pipe = gst_parse_launch(spec, &error);
 if (error || !pipe) { g_clear_error(&error); if (pipe) gst_object_unref(pipe); return NULL; }
 GstElement *rx = gst_bin_get_by_name(GST_BIN(pipe), "rx");
 if (rx) {
  GObject *signaller = NULL;
  g_object_get(rx, "signaller", &signaller, NULL);
  if (!signaller || !g_signal_lookup("webrtcbin-ready", G_OBJECT_TYPE(signaller))) {
   if (signaller) g_object_unref(signaller);
   gst_object_unref(rx); gst_object_unref(pipe); return NULL;
  }
  g_signal_connect(signaller, "webrtcbin-ready", G_CALLBACK(webrtc_ready), r);
  g_object_set(signaller, "whep-endpoint", endpoint, NULL);
  g_object_unref(signaller); gst_object_unref(rx);
 }
 const char *names[] = {"video", "audio"};
 for (int i=0; i<2; i++) {
  GstElement *sink = gst_bin_get_by_name(GST_BIN(pipe), names[i]);
  g_object_set(sink, "sync", TRUE, "max-buffers", 3u, "drop", TRUE, "enable-last-sample", FALSE, NULL);
  GstAppSinkCallbacks cb = {0}; cb.new_sample = i ? audio_sample : video_sample;
  gst_app_sink_set_callbacks(GST_APP_SINK(sink), &cb, r, NULL);
  gst_object_unref(sink);
 }
 return pipe;
}
static void stop_pipeline(struct receiver *r)
{
 if (!r->pipe) return;
 gst_element_set_state(r->pipe, GST_STATE_NULL);
 gst_object_unref(r->pipe); r->pipe = NULL;
 obs_source_output_video(r->source, NULL);
}
static gpointer worker(gpointer opaque)
{
 struct receiver *r = opaque;
 for (;;) {
  g_mutex_lock(&r->lock);
  if (r->quit) { g_mutex_unlock(&r->lock); break; }
  if (r->changed) {
   uint64_t generation = r->generation;
   char *endpoint = g_strdup(r->endpoint);
   wipe(&r->endpoint);
   r->active_latency = r->latency;
   r->active_generation = generation;
   r->changed = false;
   g_mutex_unlock(&r->lock);
   stop_pipeline(r);
   if (endpoint) {
    r->pipe = make_pipeline(r, endpoint);
    wipe(&endpoint);
    g_rec_mutex_lock(&r->delivery);
    g_mutex_lock(&r->lock);
    bool current = !r->quit && generation == r->generation;
    r->accept_samples = current && r->pipe;
    g_mutex_unlock(&r->lock);
    bool failed = !current || !r->pipe || gst_element_set_state(r->pipe, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE;
    g_rec_mutex_unlock(&r->delivery);
    g_mutex_lock(&r->lock);
    r->last_video = os_gettime_ns();
    if (failed && !r->quit && generation == r->generation) r->state = "error";
    g_mutex_unlock(&r->lock);
    if (failed) stop_pipeline(r);
   }
  } else { g_mutex_unlock(&r->lock); }
  if (r->pipe) {
   GstBus *bus = gst_element_get_bus(r->pipe);
   GstMessage *msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
   gst_object_unref(bus);
   g_mutex_lock(&r->lock);
   bool stale = os_gettime_ns() - r->last_video > 15ULL * 1000000000;
   if ((msg || stale) && !r->changed) {
    r->accept_samples = false;
    r->state = msg && GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS ? "ended" : "error";
   }
   g_mutex_unlock(&r->lock);
   if (msg || stale) stop_pipeline(r); /* no reconnect spin, no raw errors */
   if (msg) gst_message_unref(msg);
  } else {
   g_mutex_lock(&r->lock);
   if (!r->changed && !r->quit) g_cond_wait_until(&r->wake, &r->lock, g_get_monotonic_time()+100000);
   g_mutex_unlock(&r->lock);
  }
 }
 stop_pipeline(r); return NULL;
}
static void defaults(obs_data_t *settings) { obs_data_set_default_int(settings, "latency", 50); }
static void sanitize(void *opaque, obs_data_t *settings)
{
 (void)opaque;
 obs_data_erase(settings, "endpoint"); obs_data_erase(settings, "pipeline");
}
static void *create(obs_data_t *settings, obs_source_t *source)
{
 sanitize(NULL, settings);
 struct receiver *r = g_new0(struct receiver, 1);
 r->source = source; r->state = "idle"; r->jitter_latency = -1; r->latency = r->active_latency = 50;
 g_mutex_init(&r->lock); g_rec_mutex_init(&r->delivery); g_cond_init(&r->wake);
 proc_handler_t *ph = obs_source_get_proc_handler(source);
 proc_handler_add(ph, "void connect(string endpoint, int latency)", connect_proc, r);
 proc_handler_add(ph, "void disconnect()", disconnect_proc, r);
 proc_handler_add(ph, "void get_status(out string state, out int frames, out int audio_frames, out int latency, out int jitter_latency)", status_proc, r);
 r->thread = g_thread_new("pixelview-whep", worker, r);
 return r;
}
static void destroy(void *opaque)
{
 struct receiver *r = opaque;
 g_rec_mutex_lock(&r->delivery);
 g_mutex_lock(&r->lock); r->quit = true; r->generation++; r->accept_samples = false; g_cond_signal(&r->wake); g_mutex_unlock(&r->lock);
 g_rec_mutex_unlock(&r->delivery);
 g_thread_join(r->thread); wipe(&r->endpoint);
 g_cond_clear(&r->wake); g_rec_mutex_clear(&r->delivery); g_mutex_clear(&r->lock); g_free(r);
}
static const char *source_name(void *unused) { (void)unused; return "Pixelview WHEP Receiver"; }
bool obs_module_load(void)
{
#ifndef PIXELVIEW_WHEP_TEST
 if (!pixelview_gst_init()) { blog(LOG_ERROR, "[pixelview-whep] bundled runtime unavailable"); return false; }
#endif
 struct obs_source_info info = {
 .id = "pixelview_whep_source", .type = OBS_SOURCE_TYPE_INPUT,
 .output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE,
 .get_name = source_name, .create = create, .destroy = destroy,
 .get_defaults = defaults, .update = sanitize, .save = sanitize,
 };
 obs_register_source(&info); return true;
}
