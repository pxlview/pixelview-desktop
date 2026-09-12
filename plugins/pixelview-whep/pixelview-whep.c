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
#include "video-format.h"
#include "capability-probe.h"
#include "profile-offer.h"
#include "native-422-filter.h"
#include "native-422-diagnostic.h"
#include "source-feed-queue.h"
#include <math.h>
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
 struct pv422_diagnostic native422_failure; /* lock; first failure per generation */
 GCond wake;
 GThread *thread;
 /* One source-owned preview worker: latest owned RAW sample plus one in flight.
  * Stop proc invalidates pending work; pipeline teardown joins before restart;
  * a third-party blocked OBS call has no quiescence deadline. No detached thread
  * or receiver/module unload while it runs. */
 GThread *preview_thread;
 GstSample *preview_sample;
 uint64_t preview_timestamp, preview_generation;
 bool preview_clear, preview_enabled, preview_native, preview_stop;
 const char *state;
 char *endpoint;
 uint64_t frames, audio_frames, last_video;
 uint64_t native422_frames, native_audio_frames, last_native_video;
 struct pv_feed_queue feed;
 float source_gain; /* receiver lock protects the signal-owned control snapshot */
 bool source_muted;
 guint latency, active_latency;
 bool latency_override, active_latency_override;
 struct pixelview_receive_capabilities active_caps;
 struct receive_attempt *attempt;
 bool offer_failed;
 bool ordinary_audio; /* lock; selected parsed video CAPS disable the early observer */
 int jitter_latency;
 bool quit, changed, accept_samples;
 GstElement *pipe; /* worker-owned; callbacks finish before teardown */
};
/* libobs volume signals precede user_volume assignment; mute signals follow
 * user_muted assignment. Read calldata, NEVER those ordinary fields on media
 * threads. Source init sets unity/unmuted before create; scene load uses setters
 * afterwards, and DO_NOT_DUPLICATE forbids the silent field-copy duplicate path.
 * Subscribe during create before worker/publication. Control semantics are the
 * source signal values (not a later third-party mutation of volume calldata).
 * Signal dispatch holds its per-signal mutex through callbacks; disconnect waits
 * for in-flight dispatch. Never disconnect while holding receiver/delivery locks.
 */
static void source_volume(void *opaque, calldata_t *cd)
{
 struct receiver *r=opaque;
 g_mutex_lock(&r->lock); r->source_gain=(float)calldata_float(cd, "volume"); g_mutex_unlock(&r->lock);
}
static void source_mute(void *opaque, calldata_t *cd)
{
 struct receiver *r=opaque;
 g_mutex_lock(&r->lock); r->source_muted=calldata_bool(cd, "muted"); g_mutex_unlock(&r->lock);
}
static void source_controls_init(struct receiver *r)
{
 r->source_gain=1.f; r->source_muted=false;
 signal_handler_t *signals=obs_source_get_signal_handler(r->source);
 signal_handler_connect(signals, "volume", source_volume, r);
 signal_handler_connect(signals, "mute", source_mute, r);
}
static void source_controls_disconnect(struct receiver *r)
{
 signal_handler_t *signals=obs_source_get_signal_handler(r->source);
 signal_handler_disconnect(signals, "volume", source_volume, r);
 signal_handler_disconnect(signals, "mute", source_mute, r);
}
static void feed_proc(void *opaque, calldata_t *cd)
{
 struct receiver *r = opaque;
 calldata_set_int(cd, "version", PV_FEED_VERSION);
 struct pv_feed_request *request = calldata_ptr(cd, "request");
 g_mutex_lock(&r->lock);
 pv_feed_request(&r->feed, r->generation, request);
 g_mutex_unlock(&r->lock);
}
static void native_preview_proc(void *opaque, calldata_t *cd)
{
 struct receiver *r=opaque;
 g_mutex_lock(&r->lock);
 r->preview_enabled=calldata_bool(cd,"enabled");
 if (!r->preview_enabled && r->preview_native) {
  if (r->preview_sample) { gst_sample_unref(r->preview_sample); r->preview_sample=NULL; }
  if (r->preview_thread) r->preview_clear=true;
 }
 g_cond_broadcast(&r->wake); g_mutex_unlock(&r->lock);
}
/* Worker holds receiver lock; late old-pipeline messages cannot repopulate
 * cancelled/new generations. No arbitrary Gst error/debug text is consumed. */
static void native422_failure_locked(struct receiver *r,GstMessage *msg)
{
 if(r->quit || r->changed || r->generation!=r->active_generation || r->native422_failure.reason) return;
 if(pv422_diagnostic_read(msg,&r->native422_failure)) {
  char *text=pv422_diagnostic_text(&r->native422_failure,r->generation);
  blog(LOG_WARNING,"[pixelview-whep] native422 %s",text);g_free(text);
 }
}
static void log_media_stop(GstMessage *msg,bool stale)
{
 if(msg) {
  if(GST_MESSAGE_TYPE(msg)==GST_MESSAGE_EOS) {
   blog(LOG_WARNING,"[pixelview-whep] media stopped: end of stream");
   return;
  }
  GError *error=NULL;gchar *debug=NULL;
  gst_message_parse_error(msg,&error,&debug);
  const char *domain=error?g_quark_to_string(error->domain):NULL;
  GstObject *source=GST_MESSAGE_SRC(msg);
  blog(LOG_ERROR,"[pixelview-whep] media pipeline error: source=%s domain=%s code=%d",
   source?GST_OBJECT_NAME(source):"unknown",domain?domain:"unknown",error?error->code:0);
  g_clear_error(&error);g_free(debug);
 } else if(stale) {
  blog(LOG_ERROR,"[pixelview-whep] media stopped: no video received for 15 seconds");
 }
}
static void status_proc(void *opaque, calldata_t *cd)
{
 struct receiver *r = opaque;
 g_mutex_lock(&r->lock);
 const uint64_t now = os_gettime_ns();
 const uint64_t last = r->native422_frames ? r->last_native_video : r->last_video;
 calldata_set_bool(cd, "ready", !r->quit && !r->changed && r->accept_samples &&
  r->generation == r->active_generation && r->state && !strcmp(r->state, "playing") &&
  (r->native422_frames || r->frames) && last && now >= last && now - last < 500000000ULL);
 calldata_set_string(cd, "state", r->state);
 calldata_set_int(cd, "frames", r->frames);
 calldata_set_int(cd, "audio_frames", r->audio_frames);
 calldata_set_int(cd, "native422_frames", r->native422_frames);
 calldata_set_int(cd, "native_audio_frames", r->native_audio_frames);
 calldata_set_int(cd, "latency", r->latency_override ? (int)r->latency : -1);
 calldata_set_int(cd, "jitter_latency", r->jitter_latency);
 char *diagnostic=pv422_diagnostic_text(&r->native422_failure,r->generation);
 calldata_set_string(cd,"native422_diagnostic",diagnostic);g_free(diagnostic);
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
 int64_t latency = 0;
 bool latency_override = calldata_get_int(cd, "latency", &latency);
 g_mutex_lock(&r->lock);
 r->generation++;
 r->native422_failure=(struct pv422_diagnostic){0};
 pv_feed_reset(&r->feed);
 wipe(&r->endpoint);
 bool valid = valid_endpoint(endpoint) && (!latency_override || (latency >= 0 && latency <= 2000));
 r->endpoint = valid ? g_strdup(endpoint) : NULL;
 r->latency_override = valid && latency_override;
 r->latency = r->latency_override ? (guint)latency : 0;
 r->state = valid ? "connecting" : "error";
 r->frames = r->audio_frames = 0; r->jitter_latency = -1;
 r->native422_frames = r->native_audio_frames = 0;
 r->changed = true; r->accept_samples = false;
 g_cond_signal(&r->wake); g_mutex_unlock(&r->lock);
}
static void disconnect_proc(void *opaque, calldata_t *cd)
{
 (void)cd; struct receiver *r = opaque;
 g_mutex_lock(&r->lock);
 r->generation++;
 r->native422_failure=(struct pv422_diagnostic){0};
 pv_feed_reset(&r->feed);
 wipe(&r->endpoint); r->state = "idle"; r->changed = true; r->accept_samples = false;
 g_cond_signal(&r->wake); g_mutex_unlock(&r->lock);
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
static gpointer preview_worker(gpointer opaque)
{
 struct receiver *r = opaque;
 for (;;) {
  g_mutex_lock(&r->lock);
  while (!r->quit && !r->preview_stop && !r->preview_sample && !r->preview_clear)
   g_cond_wait_until(&r->wake, &r->lock, g_get_monotonic_time()+100000);
  GstSample *sample = r->preview_sample; r->preview_sample = NULL;
  uint64_t pts = r->preview_timestamp, generation = r->preview_generation;
  bool native_preview = r->preview_native;
  bool clear = r->preview_clear; r->preview_clear = false;
  bool quit = r->quit || r->preview_stop;
  bool current = !quit && (!native_preview || r->preview_enabled) && !r->changed && r->accept_samples && r->generation == generation;
  g_mutex_unlock(&r->lock);
  if (clear && !quit) obs_source_output_video(r->source, NULL);
  if (sample && current) {
   GstVideoInfo info; GstVideoFrame mapped = {0}; struct obs_source_frame2 frame;
   if (pixelview_video_info(gst_sample_get_caps(sample), &info) &&
       gst_video_frame_map(&mapped, &info, gst_sample_get_buffer(sample), GST_MAP_READ)) {
    if (pixelview_video_frame(&mapped, &frame)) {
     frame.timestamp = pts;
     /* Dispatch handoff linearizes under lock AFTER all preparation (including
      * a preceding clear). Cancellation before this reservation suppresses OBS.
      * Once reserved, exactly this one call is irrevocable: it can ENTER as well
      * as finish after disconnect/disable returns if this thread is descheduled
      * after unlock. This is not already-entered OBS, nor a Stop completion fence.
      * Pipeline teardown joins through return/unmap/unref before clearing OBS
      * and allowing direct delivery in a new generation. Disconnect proc never waits.
      * No third-party OBS call executes with a receiver/attempt/delivery lock. */
     g_mutex_lock(&r->lock);
     bool dispatch = !r->quit && (!native_preview || r->preview_enabled) && !r->changed && r->accept_samples &&
      r->generation == generation && r->active_generation == generation;
     g_mutex_unlock(&r->lock);
     if (dispatch) {
      obs_source_output_video2(r->source, &frame);
      g_mutex_lock(&r->lock);
      if (!r->quit && (!native_preview || r->preview_enabled) && !r->changed && r->accept_samples &&
          r->generation == generation && r->active_generation == generation) {
       r->frames++;
       /* Native playout, not optional preview, owns readiness. */
      }
      g_mutex_unlock(&r->lock);
     }
    }
    gst_video_frame_unmap(&mapped);
   }
  }
  if (sample) gst_sample_unref(sample);
  if (quit) return NULL;
 }
}
static GstFlowReturn video_sample(GstAppSink *sink, gpointer opaque)
{
 struct receiver *r = opaque;
 GstSample *sample = gst_app_sink_pull_sample(sink);
 if (!sample) return GST_FLOW_EOS;
 GstVideoInfo info; GstVideoFrame mapped = {0};
 /* Missing colorimetry must not become GstVideoInfo's resolution-based defaults. */
 GstCaps *caps = gst_sample_get_caps(sample);
 gboolean native_preview = FALSE;
 if (caps && gst_caps_is_fixed(caps)) gst_structure_get_boolean(gst_caps_get_structure(caps, 0), "pixelview-native422", &native_preview);
 /* Only optional native422 preview uses latest-sample dispatch. Teardown joins
  * that worker and clears OBS before any new pipeline may deliver directly. */
 if (native_preview && gst_buffer_get_size(gst_sample_get_buffer(sample)) > 1920u*1080u*3u) {
  gst_sample_unref(sample); return GST_FLOW_ERROR;
 }
 struct obs_source_frame2 frame;
 if (!pixelview_video_info(caps, &info)) { gst_sample_unref(sample); return GST_FLOW_ERROR; }
 if (!native_preview) {
  /* The ordinary clocked appsink maps and delivers directly to OBS. */
  if (!gst_video_frame_map(&mapped, &info, gst_sample_get_buffer(sample), GST_MAP_READ)) {
   gst_sample_unref(sample); return GST_FLOW_ERROR;
  }
  if (!pixelview_video_frame(&mapped, &frame)) {
   gst_video_frame_unmap(&mapped); gst_sample_unref(sample); return GST_FLOW_NOT_NEGOTIATED;
  }
 }
 if (native_preview) {
  uint64_t pts = timestamp(sample, r->pipe);
  g_mutex_lock(&r->lock);
  bool current = !r->quit && (!native_preview || r->preview_enabled) && !r->changed && r->accept_samples && r->generation == r->active_generation;
  if (current) {
   if (r->preview_sample) gst_sample_unref(r->preview_sample);
   r->preview_sample = gst_sample_ref(sample); r->preview_timestamp = pts; r->preview_generation = r->generation;
   r->preview_native = native_preview;
   if (!r->preview_thread) r->preview_thread = g_thread_new("pixelview-preview", preview_worker, r);
   g_cond_broadcast(&r->wake);
  }
  g_mutex_unlock(&r->lock);
  gst_sample_unref(sample); return GST_FLOW_OK;
 }
 frame.timestamp = timestamp(sample, r->pipe);
 g_rec_mutex_lock(&r->delivery);
 g_mutex_lock(&r->lock);
 bool deliver = !r->quit && !r->changed && r->accept_samples && r->generation == r->active_generation;
 if (deliver) { r->frames++; r->state = "playing"; r->last_video = os_gettime_ns(); }
 g_mutex_unlock(&r->lock);
 if (deliver) obs_source_output_video2(r->source, &frame);
 g_rec_mutex_unlock(&r->delivery);
 gst_video_frame_unmap(&mapped); gst_sample_unref(sample);
 return GST_FLOW_OK;
}
static void feed_audio(struct receiver *r, GstSample *sample, const GstAudioInfo *info,
 const GstMapInfo *mapped, float gain)
{
 if (!r->feed.token) return;
 const GstSegment *segment = gst_sample_get_segment(sample);
 GstBuffer *buffer = gst_sample_get_buffer(sample);
 if (GST_AUDIO_INFO_FORMAT(info) != GST_AUDIO_FORMAT_F32LE || info->rate != 48000 ||
     info->channels != 2 || info->bpf != 8 || !mapped->size || mapped->size % 8 ||
     mapped->size > PV_FEED_MAX_AUDIO_FRAMES * 8u || !segment || segment->format != GST_FORMAT_TIME ||
     !GST_BUFFER_PTS_IS_VALID(buffer)) { pv_feed_reset(&r->feed); return; }
 GstClockTime running = gst_segment_to_running_time(segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer));
 GstClockTime base = r->pipe ? gst_element_get_base_time(r->pipe) : GST_CLOCK_TIME_NONE;
 if (!GST_CLOCK_TIME_IS_VALID(running) || !GST_CLOCK_TIME_IS_VALID(base) ||
     running >= GST_CLOCK_TIME_NONE - base) { pv_feed_reset(&r->feed); return; }
 uint8_t pcm[PV_FEED_MAX_AUDIO_FRAMES * 4u];
 for (size_t i=0; i<mapped->size/4; i++) {
  float input; memcpy(&input, mapped->data + i*4, 4);
  double value = (double)input * gain;
  long rounded = !isfinite(value) ? 0 : value <= -1 ? INT16_MIN : value >= 1 ? INT16_MAX : lrint(value * 32768.0);
  int16_t code = (int16_t)CLAMP(rounded, INT16_MIN, INT16_MAX);
  GST_WRITE_UINT16_LE(pcm + i*2, (uint16_t)code);
 }
 struct pv_feed_request media = {.data=pcm, .bytes=mapped->size/2, .audio_frames=mapped->size/8,
  .timestamp_ns=base+running, .duration_ns=gst_util_uint64_scale(mapped->size/8, GST_SECOND, 48000)};
 pv_feed_push(&r->feed, r->active_generation, &media, true);
}
/* Source PCM must reach the card before synchronized preview playback. The
 * downstream preview decoder may add latency to the whole Gst pipeline; waiting
 * at its clocked audio sink makes otherwise valid PCM expire at the card. */
/* A fragmented READ map may allocate/coalesce its entire input. Bound the
 * advertised storage first; validate the resulting mapping independently. */
static bool map_audio(struct receiver *r, GstSample *sample, GstAudioInfo *info, GstMapInfo *mapped)
{
 GstBuffer *buffer=gst_sample_get_buffer(sample);
 GstCaps *caps=gst_sample_get_caps(sample);
 if (!buffer || !caps || !gst_audio_info_from_caps(info, caps) ||
     GST_AUDIO_INFO_FORMAT(info)!=GST_AUDIO_FORMAT_F32LE || info->rate!=48000 ||
     info->channels!=2 || info->bpf!=8 ||
     !gst_buffer_get_size(buffer) || gst_buffer_get_size(buffer)>PV_FEED_MAX_AUDIO_FRAMES*8u ||
     gst_buffer_get_size(buffer)%8) goto reject;
 if (!gst_buffer_map(buffer, mapped, GST_MAP_READ)) goto reject;
 if (!mapped->size || mapped->size>PV_FEED_MAX_AUDIO_FRAMES*8u || mapped->size%8) {
  gst_buffer_unmap(buffer, mapped); goto reject;
 }
 return true;
reject:
 g_mutex_lock(&r->lock); pv_feed_reset(&r->feed); g_mutex_unlock(&r->lock);
 return false;
}
static GstFlowReturn native_audio_process(GstSample *sample, struct receiver *r)
{
 GstAudioInfo info; GstMapInfo mapped;
 if (!map_audio(r, sample, &info, &mapped)) {
  gst_sample_unref(sample); return GST_FLOW_ERROR;
 }
 g_mutex_lock(&r->lock);
 float gain = r->source_muted ? 0.f : r->source_gain;
 if (!r->ordinary_audio && !r->quit && !r->changed && r->accept_samples && r->generation == r->active_generation) {
  r->native_audio_frames += mapped.size / info.bpf;
  if (r->feed.route == PV_FEED_NATIVE) feed_audio(r, sample, &info, &mapped, gain);
 }
 g_mutex_unlock(&r->lock);
 gst_buffer_unmap(gst_sample_get_buffer(sample), &mapped); gst_sample_unref(sample);
 return GST_FLOW_OK;
}
/* Observe the shared PCM before its single clocked queue. No tee, duplicated
 * queue or unsynchronized sink: the bounded native copy cannot wait on OBS.
 * Audio can precede parsed video CAPS, so preserve native startup until selection.
 * Pipeline NULL drains this pad callback before receiver teardown. */
static GstPadProbeReturn early_audio_probe(GstPad *pad, GstPadProbeInfo *info, gpointer opaque)
{
 struct receiver *r=opaque;
 g_mutex_lock(&r->lock); bool ordinary=r->ordinary_audio; g_mutex_unlock(&r->lock);
 if (ordinary) return GST_PAD_PROBE_REMOVE;
 GstCaps *caps=gst_pad_get_current_caps(pad);
 GstEvent *event=gst_pad_get_sticky_event(pad,GST_EVENT_SEGMENT,0);
 const GstSegment *segment=NULL;
 if (event) gst_event_parse_segment(event,&segment);
 GstBufferList *list=(GST_PAD_PROBE_INFO_TYPE(info)&GST_PAD_PROBE_TYPE_BUFFER_LIST) ? GST_PAD_PROBE_INFO_BUFFER_LIST(info) : NULL;
 guint count=list ? gst_buffer_list_length(list) : 1;
 GstFlowReturn flow=GST_FLOW_OK;
 for (guint i=0; i<count && flow==GST_FLOW_OK; i++) {
  GstBuffer *buffer=list ? gst_buffer_list_get(list,i) : GST_PAD_PROBE_INFO_BUFFER(info);
  GstSample *sample=gst_sample_new(buffer,caps,segment,NULL);
  flow=native_audio_process(sample,r); /* consumes sample, never changes buffer/PTS */
 }
 if(event) gst_event_unref(event);
 if(caps) gst_caps_unref(caps);
 if(flow!=GST_FLOW_OK) {
  if(list) gst_buffer_list_unref(list); else gst_buffer_unref(GST_PAD_PROBE_INFO_BUFFER(info));
  GST_PAD_PROBE_INFO_FLOW_RETURN(info)=flow; return GST_PAD_PROBE_HANDLED;
 }
 return GST_PAD_PROBE_OK;
}
static GstFlowReturn audio_sample(GstAppSink *sink, gpointer opaque)
{
 struct receiver *r = opaque;
 GstSample *sample = gst_app_sink_pull_sample(sink);
 if (!sample) return GST_FLOW_EOS;
 GstAudioInfo info; GstMapInfo mapped;
 if (!map_audio(r, sample, &info, &mapped)) {
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
/* Signal closures can outlive pipeline teardown (rswebrtc owns async tasks).
 * They retain this attempt, never an unguarded receiver pointer. Detach under
 * gate before teardown, waiting for any callback already using the receiver.
 * Lock order: attempt gate -> receiver lock; never reverse it. */
struct receive_attempt {
 gatomicrefcount refs;
 GMutex gate;
 struct receiver *receiver;
 const uint64_t generation;
 const struct pixelview_receive_capabilities caps;
 const guint latency;
 const bool latency_override;
 bool failed;
};
static struct receive_attempt *attempt_new(struct receiver *r)
{
 struct receive_attempt initial = {.receiver=r, .generation=r->active_generation,
  .caps=r->active_caps, .latency=r->active_latency, .latency_override=r->active_latency_override};
 struct receive_attempt *a = g_malloc0(sizeof(*a));
 memcpy(a, &initial, sizeof(*a));
 g_atomic_ref_count_init(&a->refs); g_mutex_init(&a->gate);
 return a;
}
static struct receive_attempt *attempt_ref(struct receive_attempt *a)
{ g_atomic_ref_count_inc(&a->refs); return a; }
static void attempt_unref(gpointer opaque, GClosure *closure)
{
 (void)closure; struct receive_attempt *a = opaque;
 if (g_atomic_ref_count_dec(&a->refs)) { g_mutex_clear(&a->gate); g_free(a); }
}
/* gate held by caller */
static bool attempt_current(struct receive_attempt *a)
{
 struct receiver *r = a->receiver;
 if (!r) return false;
 g_mutex_lock(&r->lock);
 bool current = !r->quit && !r->changed && r->generation == a->generation;
 g_mutex_unlock(&r->lock);
 return current;
}
/* Every admitted profile was probed at HD60. Only an offered HEVC profile
 * can impose the older level-120 HD30 envelope; absent HEVC has level zero. */
static unsigned receive_max_fps(const struct pixelview_receive_capabilities *caps)
{
 bool hevc = caps->profiles & (PV_PROFILE_HEVC_MAIN | PV_PROFILE_HEVC_MAIN10);
 return hevc && caps->hevc_level_id < 123 ? 30u : 60u;
}
static void configure_transceiver(GstElement *rtc, GObject *transceiver, gpointer opaque)
{
 struct receive_attempt *a = opaque;
 g_mutex_lock(&a->gate);
 bool current = attempt_current(a);
 GstCaps *caps = NULL;
 g_object_get(transceiver, "codec-preferences", &caps, NULL);
 struct pixelview_receive_limits limits = {a->caps.profiles, a->caps.hevc_level_id,
  1920, 1080, receive_max_fps(&a->caps)};
 GstCaps *offer = current && !a->failed ? pixelview_profile_offer_caps_limited(caps, &limits) : NULL;
 if (caps) gst_caps_unref(caps);
 if (!offer || gst_caps_is_empty(offer)) {
  if (!offer) offer = gst_caps_new_empty();
  a->failed = true;
  if (current) {
   struct receiver *r = a->receiver;
   g_mutex_lock(&r->lock);
   if (!r->quit && r->generation == a->generation) { r->offer_failed = true; r->accept_samples = false; }
   g_mutex_unlock(&r->lock);
  }
  /* Empty (not NULL/ANY) codec preferences make create-offer fail rather than
   * restoring upstream codec defaults. Worker also consumes this safe error. */
  GST_ELEMENT_ERROR(rtc, CORE, NEGOTIATION, ("No verified receive profile"), (NULL));
 }
 g_object_set(transceiver, "codec-preferences", offer, NULL);
 gst_caps_unref(offer);
 g_mutex_unlock(&a->gate);
}
/* Empty preferences alone produce a valid rejected-media SDP in webrtcbin.
 * Stop the RUN_LAST create-offer action before its default implementation, so
 * rswebrtc cannot POST an audio-only/fallback offer after video policy failure. */
static void guard_offer(GstElement *rtc, GstStructure *options, GstPromise *promise, gpointer opaque)
{
 (void)options;
 struct receive_attempt *a = opaque;
 g_mutex_lock(&a->gate);
 bool allowed = !a->failed && a->caps.profiles && attempt_current(a);
 g_mutex_unlock(&a->gate);
 if (allowed) return;
 g_signal_stop_emission_by_name(rtc, "create-offer");
 GError *error = g_error_new_literal(GST_CORE_ERROR, GST_CORE_ERROR_NEGOTIATION, "No verified receive profile");
 gst_promise_reply(promise, gst_structure_new("application/x-gst-promise", "error", G_TYPE_ERROR, error, NULL));
 g_error_free(error);
}
static void webrtc_ready(GObject *signaller, const char *peer, GstElement *rtc, gpointer opaque)
{
 (void)signaller; (void)peer;
 struct receive_attempt *a = opaque;
 g_signal_connect_data(rtc, "on-new-transceiver", G_CALLBACK(configure_transceiver),
  attempt_ref(a), attempt_unref, 0);
 g_signal_connect_data(rtc, "create-offer", G_CALLBACK(guard_offer),
  attempt_ref(a), attempt_unref, 0);
 g_mutex_lock(&a->gate);
 if (attempt_current(a)) {
  guint latency = a->latency;
  /* Omission leaves the native property untouched; explicit zero is valid. */
  if (a->latency_override) g_object_set(rtc, "latency", latency, NULL);
  g_object_get(rtc, "latency", &latency, NULL);
  struct receiver *r = a->receiver;
  g_mutex_lock(&r->lock);
  if (!r->quit && r->generation == a->generation) r->jitter_latency = (int)latency;
  g_mutex_unlock(&r->lock);
 }
 g_mutex_unlock(&a->gate);
}
/* Bounded source-owned copy only: no consumer code or DeckLink calls under
 * attempt/receiver locks. Consumers pull through the versioned feed proc. */
static gboolean native422_delivery(void *opaque, GstSample *sample, const struct pv_native422_frame *frame)
{
 struct receive_attempt *a = opaque;
 const GstSegment *segment = gst_sample_get_segment(sample);
 if (!segment || segment->format != GST_FORMAT_TIME) return FALSE;
 GstClockTime running = gst_segment_to_running_time(segment, GST_FORMAT_TIME, frame->pts);
 if (!GST_CLOCK_TIME_IS_VALID(running)) return FALSE;
 g_mutex_lock(&a->gate);
 struct receiver *r = a->receiver;
 if (!r) { g_mutex_unlock(&a->gate); return TRUE; }
 g_mutex_lock(&r->lock);
 bool deliver = !r->quit && !r->changed && r->accept_samples && r->generation == a->generation;
 GstClockTime base = r->pipe ? gst_element_get_base_time(r->pipe) : GST_CLOCK_TIME_NONE;
 deliver = deliver && GST_CLOCK_TIME_IS_VALID(base) && running < GST_CLOCK_TIME_NONE - base;
 if (deliver) {
  r->native422_frames++; r->last_native_video = os_gettime_ns(); r->state = "playing";
  int fps_n=0, fps_d=0;
  gst_structure_get_fraction(gst_caps_get_structure(gst_sample_get_caps(sample), 0), "framerate", &fps_n, &fps_d);
  struct pv_feed_request media = {.data=(void *)frame->v210,
   .bytes=(size_t)frame->stride * frame->height, .width=frame->width, .height=frame->height,
   .stride=frame->stride, .fps_num=fps_n, .fps_den=fps_d,
   .timestamp_ns=base + running, .duration_ns=frame->duration};
  pv_feed_push(&r->feed, a->generation, &media, false);
 }
 g_mutex_unlock(&r->lock); g_mutex_unlock(&a->gate);
 return TRUE;
}
/* The selector publishes its validated pinned codec on this stock capsfilter.
 * notify::caps runs synchronously before its first AU, with attempt lifetime and
 * generation protection. Do not infer the codec from downstream raw caps. */
static void selected_audio_route(GObject *policy, GParamSpec *pspec, gpointer opaque)
{
 (void)pspec; struct receive_attempt *a=opaque;
 GstCaps *caps=NULL; g_object_get(policy,"caps",&caps,NULL);
 bool ordinary=false;
 if(caps && gst_caps_is_fixed(caps)) {
  const GstStructure *s=gst_caps_get_structure(caps,0);
  const char *profile=gst_structure_get_string(s,"profile");
  ordinary=gst_structure_has_name(s,"video/x-h264") || gst_structure_has_name(s,"video/x-vp9") ||
   (gst_structure_has_name(s,"video/x-h265") && (!g_strcmp0(profile,"main") || !g_strcmp0(profile,"main-10")));
 }
 if(caps) gst_caps_unref(caps);
 g_mutex_lock(&a->gate);
 if(ordinary && attempt_current(a)) {
  struct receiver *r=a->receiver; g_mutex_lock(&r->lock);
  if(!r->quit && !r->changed && r->generation==a->generation) {
   r->ordinary_audio=true;
   /* A native token attached before selection must never retain early PCM. */
   if(r->feed.route==PV_FEED_NATIVE) pv_feed_reset(&r->feed);
  }
  g_mutex_unlock(&r->lock);
 }
 g_mutex_unlock(&a->gate);
}
static void native422_attempt_release(gpointer opaque) { attempt_unref(opaque, NULL); }
static GstElement *request_encoded_filter(GstElement *rx, const char *peer, const char *pad,
 GstCaps *caps, gpointer opaque)
{
 (void)rx; (void)peer; (void)caps;
 struct receive_attempt *a = opaque;
 if (!pad || !g_str_has_prefix(pad, "video_")) return NULL;
 g_mutex_lock(&a->gate);
 gboolean current = attempt_current(a);
 GstElement *filter = current && !a->failed ? pv_native422_filter_new(native422_delivery,
  attempt_ref(a), native422_attempt_release) : NULL;
 if (filter) {
  GstElement *policy=gst_bin_get_by_name(GST_BIN(filter),"codec-route");
  g_signal_connect_data(policy,"notify::caps",G_CALLBACK(selected_audio_route),
   attempt_ref(a),attempt_unref,0);
  gst_object_unref(policy);
  pv_native422_filter_require_rtp(filter,rx);
  const char *format = caps && gst_caps_get_size(caps) ? gst_structure_get_string(gst_caps_get_structure(caps, 0), "format") : NULL;
  pv_native422_filter_preview_format(filter, !g_strcmp0(format, "NV12"));
  /* The signal's object GValue transfers an owned reference to Rust. A floating
   * bin would have its only reference stolen by bin.add, then unrefed again by
   * Rust's returned Element wrapper, leaving bus messages with a dangling src. */
  gst_object_ref_sink(filter);
 }
 if (current && !filter) {
  a->failed = true;
  struct receiver *r = a->receiver;
  g_mutex_lock(&r->lock);
  if (!r->quit && r->generation == a->generation) { r->offer_failed = true; r->accept_samples = false; }
  g_mutex_unlock(&r->lock);
  GST_ELEMENT_ERROR(rx, CORE, MISSING_PLUGIN, ("Native receive branch unavailable"), (NULL));
 }
 g_mutex_unlock(&a->gate);
 return filter;
}
static GstElement *make_pipeline(struct receiver *r, const char *endpoint)
{
 g_mutex_lock(&r->lock); r->ordinary_audio=false; g_mutex_unlock(&r->lock);
 /* Only fixed code is parsed; never interpolate credentials into a pipeline. */
 const char *spec =
  "whepclientsrc name=rx video-codecs=\"<H265,H264,VP9>\" audio-codecs=\"<OPUS>\" "
  "rx. ! capsfilter name=video-policy caps=\"" PIXELVIEW_RECEIVE_RAW_CAPS ",width=(int)[1,1920],height=(int)[1,1080],framerate=(fraction)[0/1,%u/1]\" ! appsink name=video "
  "rx. ! audio/x-raw ! queue name=audio-input max-size-time=200000000 max-size-bytes=0 max-size-buffers=0 ! audioconvert ! audioresample ! audio/x-raw,format=F32LE,layout=interleaved,channels=2,rate=48000 ! queue name=audio-clock max-size-time=2000000000 max-size-bytes=1048576 max-size-buffers=100 leaky=downstream ! appsink name=audio";
#ifdef PIXELVIEW_WHEP_TEST
 if (!strcmp(endpoint,"test://synthetic")) spec =
  "videotestsrc is-live=true ! video/x-raw,format=BGRA,colorimetry=sRGB,width=64,height=64,framerate=30/1 ! appsink name=video "
  "audiotestsrc is-live=true ! audio/x-raw,format=F32LE,channels=2,rate=48000 ! queue name=audio-clock leaky=downstream ! appsink name=audio";
#endif
 GError *error = NULL;
 /* Only the verified numeric frame-rate ceiling enters the fixed graph. */
 char *description = g_strdup_printf(spec, receive_max_fps(&r->active_caps));
 GstElement *pipe = gst_parse_launch(description, &error);
 g_free(description);
 if (error || !pipe) { g_clear_error(&error); if (pipe) gst_object_unref(pipe); return NULL; }
 GstElement *rx = gst_bin_get_by_name(GST_BIN(pipe), "rx");
 if (rx) {
  GObject *signaller = NULL;
  g_object_get(rx, "signaller", &signaller, NULL);
  if (!signaller || !g_signal_lookup("webrtcbin-ready", G_OBJECT_TYPE(signaller))) {
   if (signaller) g_object_unref(signaller);
   gst_object_unref(rx); gst_object_unref(pipe); return NULL;
  }
  r->attempt = attempt_new(r);
  g_signal_connect_data(rx, "request-encoded-filter", G_CALLBACK(request_encoded_filter),
   attempt_ref(r->attempt), attempt_unref, 0);
  g_signal_connect_data(signaller, "webrtcbin-ready", G_CALLBACK(webrtc_ready),
   attempt_ref(r->attempt), attempt_unref, 0);
  g_object_set(signaller, "whep-endpoint", endpoint, NULL);
  g_object_unref(signaller); gst_object_unref(rx);
 }
 GstElement *clock_queue=gst_bin_get_by_name(GST_BIN(pipe),"audio-clock");
 GstPad *early=gst_element_get_static_pad(clock_queue,"sink");
 gst_pad_add_probe(early,GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST,early_audio_probe,r,NULL);
 gst_object_unref(early);gst_object_unref(clock_queue);
 const char *names[] = {"video", "audio"};
 for (int i=0; i<2; i++) {
  GstElement *sink = gst_bin_get_by_name(GST_BIN(pipe), names[i]);
  g_object_set(sink, "sync", TRUE, "async", TRUE, "max-buffers", 3u, "drop", TRUE, "enable-last-sample", FALSE, NULL);
  GstAppSinkCallbacks cb = {0}; cb.new_sample = i ? audio_sample : video_sample;
  gst_app_sink_set_callbacks(GST_APP_SINK(sink), &cb, r, NULL);
  gst_object_unref(sink);
 }
 return pipe;
}
static void stop_pipeline(struct receiver *r)
{
 g_mutex_lock(&r->lock); r->accept_samples=false; pv_feed_reset(&r->feed);
 g_mutex_unlock(&r->lock);
 if (r->attempt) {
  g_mutex_lock(&r->attempt->gate); r->attempt->receiver = NULL; g_mutex_unlock(&r->attempt->gate);
  attempt_unref(r->attempt, NULL); r->attempt = NULL;
 }
 bool had_pipeline=r->pipe != NULL;
 if (r->pipe) {
  gst_element_set_state(r->pipe, GST_STATE_NULL);
  gst_object_unref(r->pipe); r->pipe = NULL;
 }
 /* NULL has drained all appsink callbacks; none can create another preview
  * worker. Join the optional native worker WITHOUT receiver/delivery locks.
  * Its last reserved OBS call (and any clear) must finish before the final
  * clear and a new ordinary pipeline. A wedged external OBS call can delay
  * restart/destruction; never detach it or allow stale frames to overtake. */
 g_mutex_lock(&r->lock);
 if (r->preview_sample) { gst_sample_unref(r->preview_sample); r->preview_sample=NULL; }
 r->preview_stop=true; r->preview_clear=false;
 GThread *preview=r->preview_thread;
 g_cond_broadcast(&r->wake);g_mutex_unlock(&r->lock);
 if(preview) g_thread_join(preview);
 g_mutex_lock(&r->lock);
 r->preview_thread=NULL;r->preview_stop=false;r->preview_native=false;
 g_mutex_unlock(&r->lock);
 if(had_pipeline || preview) obs_source_output_video(r->source,NULL);
}
struct probe_waiter { struct receiver *receiver; uint64_t generation; };
static gboolean probe_cancelled(void *opaque)
{
 struct probe_waiter *waiter = opaque;
 struct receiver *r = waiter->receiver;
 g_mutex_lock(&r->lock);
 bool cancelled = r->quit || r->generation != waiter->generation;
 g_mutex_unlock(&r->lock);
 return cancelled;
}
static bool probe_attempt(struct receiver *r, uint64_t generation)
{
 /* This stack context is only borrowed by the bounded waiter, never the cached
  * background decoder. No receiver lock is held across driver/plugin calls. */
 struct probe_waiter waiter = {r, generation};
 struct pixelview_receive_capabilities caps = {0};
 bool success = pixelview_capability_probe_get(&caps, probe_cancelled, &waiter);
 g_mutex_lock(&r->lock);
 bool current = !r->quit && r->generation == generation;
 if (current) { r->active_caps = caps; r->offer_failed = false; }
 g_mutex_unlock(&r->lock);
 return success && current && caps.profiles;
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
   r->active_latency_override = r->latency_override;
   r->active_generation = generation;
   r->changed = false;
   g_mutex_unlock(&r->lock);
   stop_pipeline(r);
   if (endpoint) {
    if (probe_attempt(r, generation)) r->pipe = make_pipeline(r, endpoint);
    wipe(&endpoint);
    g_rec_mutex_lock(&r->delivery);
    g_mutex_lock(&r->lock);
    bool current = !r->quit && generation == r->generation;
    r->accept_samples = current && r->pipe && !r->offer_failed;
    g_mutex_unlock(&r->lock);
    bool failed = !current || !r->pipe || gst_element_set_state(r->pipe, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE;
    g_rec_mutex_unlock(&r->delivery);
    g_mutex_lock(&r->lock);
    r->last_video = os_gettime_ns();
    if (failed && !r->quit && generation == r->generation) {
     r->state = "error";
     blog(LOG_ERROR,"[pixelview-whep] media startup failed: %s",
      !r->pipe?(r->offer_failed?"no verified receive offer":"pipeline creation failed"):
      "pipeline did not enter playing state");
    }
    g_mutex_unlock(&r->lock);
    if (failed) stop_pipeline(r);
   }
  } else { g_mutex_unlock(&r->lock); }
  if (r->pipe) {
   GstBus *bus = gst_element_get_bus(r->pipe);
   GstMessage *msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
   gst_object_unref(bus);
   g_mutex_lock(&r->lock);
   uint64_t last = r->native422_frames ? r->last_native_video : r->last_video;
   native422_failure_locked(r,msg);
   bool stale = os_gettime_ns() - last > 15ULL * 1000000000;
   if ((msg || stale) && !r->changed) {
    r->accept_samples = false;
    r->state = msg && GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS ? "ended" : "error";
    log_media_stop(msg,stale);
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
static void sanitize(void *opaque, obs_data_t *settings)
{
 (void)opaque;
 obs_data_erase(settings, "endpoint"); obs_data_erase(settings, "pipeline");
}
static void *create(obs_data_t *settings, obs_source_t *source)
{
 sanitize(NULL, settings);
 struct receiver *r = g_new0(struct receiver, 1);
 r->source = source; r->state = "idle"; r->jitter_latency = -1;
 obs_source_set_async_unbuffered(source, true);
 r->preview_enabled = true;
 g_mutex_init(&r->lock); g_rec_mutex_init(&r->delivery); g_cond_init(&r->wake);
 source_controls_init(r);
 proc_handler_t *ph = obs_source_get_proc_handler(source);
 proc_handler_add(ph, "void native422_feed(ptr request, out int version)", feed_proc, r);
 proc_handler_add(ph, "void set_native_preview(bool enabled)", native_preview_proc, r);
 proc_handler_add(ph, "void connect(string endpoint, int latency)", connect_proc, r);
 proc_handler_add(ph, "void disconnect()", disconnect_proc, r);
 proc_handler_add(ph, "void get_status(out bool ready, out string state, out int frames, out int audio_frames, out int latency, out int jitter_latency, out int native422_frames, out int native_audio_frames, out string native422_diagnostic)", status_proc, r);
 r->thread = g_thread_new("pixelview-whep", worker, r);
 return r;
}
static void destroy(void *opaque)
{
 struct receiver *r = opaque;
 source_controls_disconnect(r);
 g_rec_mutex_lock(&r->delivery);
 g_mutex_lock(&r->lock); r->quit = true; r->generation++;
 r->native422_failure=(struct pv422_diagnostic){0}; r->accept_samples = false; g_cond_signal(&r->wake); g_mutex_unlock(&r->lock);
 g_rec_mutex_unlock(&r->delivery);
 g_thread_join(r->thread); wipe(&r->endpoint);
 if (r->preview_thread) g_thread_join(r->preview_thread);
 g_cond_clear(&r->wake); g_rec_mutex_clear(&r->delivery); g_mutex_clear(&r->lock); g_free(r);
}
static const char *source_name(void *unused) { (void)unused; return "Pixelview WHEP Receiver"; }
bool obs_module_load(void)
{
#ifndef PIXELVIEW_WHEP_TEST
 /* The bounded waiter may leave one driver call running. Retain this image and
  * its linked GStreamer dependencies for process lifetime BEFORE any probe or
  * runtime registration. Intentionally never dlclose this handle/gst_deinit. */
 static void *resident_module;
 if (!resident_module) {
  const char *binary = obs_get_module_binary_path(obs_current_module());
  if (!binary || !(resident_module = dlopen(binary, RTLD_NOW | RTLD_NODELETE))) {
   blog(LOG_ERROR, "[pixelview-whep] runtime lifetime unavailable"); return false;
  }
 }
 if (!pixelview_gst_init()) { blog(LOG_ERROR, "[pixelview-whep] bundled runtime unavailable"); return false; }
#endif
 struct obs_source_info info = {
 .id = "pixelview_whep_source", .type = OBS_SOURCE_TYPE_INPUT,
 .output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE,
 .get_name = source_name, .create = create, .destroy = destroy,
 .update = sanitize, .save = sanitize,
 };
 obs_register_source(&info); return true;
}
