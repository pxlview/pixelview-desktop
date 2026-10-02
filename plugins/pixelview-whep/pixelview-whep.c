/* SPDX-License-Identifier: GPL-2.0-or-later
 * Pixelview native WHEP input. Raw appsink integration informed by
 * obs-gstreamer (C) 2018-2021 Florian Zwoch, GPL-2.0-or-later.
 */
#include <obs-module.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/base/gstbasesink.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <util/platform.h>
#include <string.h>
#include "video-format.h"
#include "capability-probe.h"
#include "profile-offer.h"
#include "codec-route.h"
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
 uint8_t *widened; size_t widened_capacity; /* delivery; eight-bit frames widened to P010 */
 uint64_t generation, active_generation;
 const char *failure; /* lock; canonical allowlisted reason for the current generation, or NULL */
 GCond wake;
 GThread *thread;
 const char *state;
 char *endpoint;
 uint64_t frames, audio_frames, last_video;
 guint latency, active_latency;
 bool latency_override, active_latency_override;
 enum pixelview_color color, active_color; /* lock; active_color is fixed for one pipeline */
 struct pixelview_receive_capabilities active_caps;
 struct receive_attempt *attempt;
 bool offer_failed;
 int jitter_latency;
 bool quit, changed, accept_samples;
 GstElement *pipe; /* worker-owned; callbacks finish before teardown */
 GstClockTime logged_latency; /* worker-owned; last sink latency written to the log */
 int64_t video_late, audio_late; /* lock; worst arrival minus stamped render time this window */
 uint64_t audio_dropped; /* lock; stale audio frames withheld from OBS this window */
 uint64_t late_window; /* worker-owned; start of the current lateness window */
};
/* Typed refusal from the route selector (codec-route.h). Only the
 * canonical reasons are retained; arbitrary error text is never consumed. */
static void unsupported_profile_locked(struct receiver *r,GstMessage *msg)
{
 if(!msg || GST_MESSAGE_TYPE(msg)!=GST_MESSAGE_ERROR) return;
 if(r->quit || r->changed || r->generation!=r->active_generation || r->failure) return;
 const GstStructure *s=NULL;gst_message_parse_error_details(msg,&s);
 if(!s || !gst_structure_has_name(s,PV_UNSUPPORTED_PROFILE_DETAILS)) return;
 const char *reason=gst_structure_get_string(s,"reason");
 if(!g_strcmp0(reason,PV_UNSUPPORTED_HEVC_MAIN_422_10)) r->failure=PV_UNSUPPORTED_HEVC_MAIN_422_10;
 else if(!g_strcmp0(reason,PV_UNSUPPORTED_HEVC_MAIN_444_10)) r->failure=PV_UNSUPPORTED_HEVC_MAIN_444_10;
 else if(!g_strcmp0(reason,PV_UNSUPPORTED_HEVC_PROFILE)) r->failure=PV_UNSUPPORTED_HEVC_PROFILE;
 if(r->failure) blog(LOG_ERROR,"[pixelview-whep] %s: this Mac did not verify that profile; the sender must use HEVC Main or Main10",r->failure);
}
/* First colour-mode refusal per generation; the reason names the fix, not the stream. */
static void color_failure(struct receiver *r, GstCaps *caps, enum pixelview_color color)
{
 const char *reason = pixelview_video_color_mismatch(caps, color);
 if (!reason) return;
 g_mutex_lock(&r->lock);
 const bool first = !r->quit && !r->changed && r->generation == r->active_generation && !r->failure;
 if (first) r->failure = reason;
 g_mutex_unlock(&r->lock);
 if (first) {
  const char *colorimetry = caps && gst_caps_is_fixed(caps) ? gst_structure_get_string(gst_caps_get_structure(caps, 0), "colorimetry") : NULL;
  blog(LOG_ERROR, "[pixelview-whep] %s: receive mode %s, stream colorimetry %s", reason,
   color == PIXELVIEW_COLOR_PQ ? "hdr-pq" : color == PIXELVIEW_COLOR_HLG ? "hdr-hlg" : "sdr",
   colorimetry ? colorimetry : "unsignalled");
 }
}
static void log_media_stop(GstMessage *msg,unsigned stale_seconds)
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
 } else if(stale_seconds) {
  blog(LOG_ERROR,"[pixelview-whep] media stopped: no video received for %u seconds",stale_seconds);
 }
}
static void status_proc(void *opaque, calldata_t *cd)
{
 struct receiver *r = opaque;
 g_mutex_lock(&r->lock);
 const uint64_t now = os_gettime_ns();
 const uint64_t last = r->last_video;
 calldata_set_bool(cd, "ready", !r->quit && !r->changed && r->accept_samples &&
  r->generation == r->active_generation && r->state && !strcmp(r->state, "playing") &&
  r->frames && last && now >= last && now - last < 500000000ULL);
 calldata_set_string(cd, "state", r->state);
 calldata_set_int(cd, "frames", r->frames);
 calldata_set_int(cd, "audio_frames", r->audio_frames);
 calldata_set_int(cd, "latency", r->latency_override ? (int)r->latency : -1);
 calldata_set_int(cd, "jitter_latency", r->jitter_latency);
 calldata_set_string(cd,"failure",r->failure?r->failure:"");
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
/* The session token from the endpoint's ?token= query, decoded like Go's url.Query() (the
 * engine's binding), or NULL; caller wipes. */
static char *endpoint_token(const char *endpoint)
{
 GUri *uri = g_uri_parse(endpoint, G_URI_FLAGS_ENCODED_QUERY, NULL);
 const char *query = uri ? g_uri_get_query(uri) : NULL;
 GHashTable *params = query ? g_uri_parse_params(query, -1, "&", G_URI_PARAMS_WWW_FORM, NULL) : NULL;
 const char *token = params ? g_hash_table_lookup(params, "token") : NULL;
 char *copy = token && *token ? g_strdup(token) : NULL;
 if (params) {
  /* Parsed values are secrets too: overwrite before the table frees them. */
  GHashTableIter it; gpointer key, value; g_hash_table_iter_init(&it, params);
  while (g_hash_table_iter_next(&it, &key, &value)) if (value) memset(value, 0, strlen(value));
  g_hash_table_unref(params);
 }
 if (uri) g_uri_unref(uri);
 return copy;
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
 /* Operator colour mode; absent means the historical SDR receive. */
 const char *color_name = calldata_string(cd, "color");
 enum pixelview_color color = PIXELVIEW_COLOR_SDR;
 bool color_valid = !color_name || !*color_name || !strcmp(color_name, "sdr");
 if (color_name && !strcmp(color_name, "pq")) { color = PIXELVIEW_COLOR_PQ; color_valid = true; }
 if (color_name && !strcmp(color_name, "hlg")) { color = PIXELVIEW_COLOR_HLG; color_valid = true; }
 g_mutex_lock(&r->lock);
 r->generation++;
 r->failure=NULL;
 wipe(&r->endpoint);
 bool valid = color_valid && valid_endpoint(endpoint) && (!latency_override || (latency >= 0 && latency <= 2000));
 r->endpoint = valid ? g_strdup(endpoint) : NULL;
 r->color = color;
 r->latency_override = valid && latency_override;
 r->latency = r->latency_override ? (guint)latency : 0;
 r->state = valid ? "connecting" : "error";
 r->frames = r->audio_frames = 0; r->jitter_latency = -1;
 r->changed = true; r->accept_samples = false;
 g_cond_signal(&r->wake); g_mutex_unlock(&r->lock);
}
static void disconnect_proc(void *opaque, calldata_t *cd)
{
 (void)cd; struct receiver *r = opaque;
 g_mutex_lock(&r->lock);
 r->generation++;
 r->failure=NULL;
 wipe(&r->endpoint); r->state = "idle"; r->changed = true; r->accept_samples = false;
 g_cond_signal(&r->wake); g_mutex_unlock(&r->lock);
}
static uint64_t timestamp(GstSample *sample, GstElement *pipe, GstAppSink *sink)
{
 GstBuffer *b = gst_sample_get_buffer(sample);
 const GstSegment *segment = gst_sample_get_segment(sample);
 GstClockTime time = GST_CLOCK_TIME_NONE;
 if (segment && GST_BUFFER_PTS_IS_VALID(b))
  time = gst_segment_to_running_time(segment, GST_FORMAT_TIME, GST_BUFFER_PTS(b));
 /* GstSystemClock and OBS use monotonic nanoseconds on macOS. A clocked live
  * sink releases a buffer at base + running + pipeline latency, so stamp that
  * render time: without the latency every audio buffer reaches OBS already
  * stale and libobs permanently raises global audio buffering, while
  * unbuffered video shows on arrival (audio lags by the pipeline latency).
  * Both branches share base and latency, preserving relative A/V PTS. */
 return GST_CLOCK_TIME_IS_VALID(time) ?
  gst_element_get_base_time(pipe) + time + (sink ? gst_base_sink_get_latency(GST_BASE_SINK(sink)) : 0) :
  os_gettime_ns();
}
static GstFlowReturn video_sample(GstAppSink *sink, gpointer opaque)
{
 struct receiver *r = opaque;
 GstSample *sample = gst_app_sink_pull_sample(sink);
 if (!sample) return GST_FLOW_EOS;
 GstVideoInfo info; GstVideoFrame mapped = {0};
 /* Missing colorimetry must not become GstVideoInfo's resolution-based defaults. */
 GstCaps *caps = gst_sample_get_caps(sample);
 struct obs_source_frame2 frame;
 const enum pixelview_color color = r->active_color;
 if (!pixelview_video_info(caps, color, &info)) {
  color_failure(r, caps, color); gst_sample_unref(sample); return GST_FLOW_ERROR;
 }
 /* The clocked appsink maps and delivers directly to OBS (P010, v210 for an
  * HEVC 4:2:2 stream, P416 for a 4:4:4 one). */
 if (!gst_video_frame_map(&mapped, &info, gst_sample_get_buffer(sample), GST_MAP_READ)) {
  gst_sample_unref(sample); return GST_FLOW_ERROR;
 }
 if (!pixelview_video_frame(&mapped, color, &frame)) {
  color_failure(r, caps, color);
  gst_video_frame_unmap(&mapped); gst_sample_unref(sample); return GST_FLOW_NOT_NEGOTIATED;
 }
 frame.timestamp = timestamp(sample, r->pipe, sink);
 g_rec_mutex_lock(&r->delivery);
 /* An eight-bit stream is decoded as NV12 and reaches OBS as exact ten-bit P010. */
 /* A 4:4:4 stream is decoded as AYUV64 and reaches OBS as exact ten-bit P416. */
 if (!pixelview_video_widen_nv12(&frame, &r->widened, &r->widened_capacity) ||
     !pixelview_video_unpack_ayuv64(&frame, &r->widened, &r->widened_capacity)) {
  g_rec_mutex_unlock(&r->delivery);
  gst_video_frame_unmap(&mapped); gst_sample_unref(sample); return GST_FLOW_ERROR;
 }
 g_mutex_lock(&r->lock);
 bool deliver = !r->quit && !r->changed && r->accept_samples && r->generation == r->active_generation;
 if (deliver) {
  r->frames++; r->state = "playing"; r->last_video = os_gettime_ns();
  r->video_late = MAX(r->video_late, (int64_t)(r->last_video - frame.timestamp));
 }
 g_mutex_unlock(&r->lock);
 if (deliver) obs_source_output_video2(r->source, &frame);
 g_rec_mutex_unlock(&r->delivery);
 gst_video_frame_unmap(&mapped); gst_sample_unref(sample);
 return GST_FLOW_OK;
}
/* A fragmented READ map may allocate/coalesce its entire input. Bound the
 * advertised storage first (120 ms of 48 kHz stereo, Opus's largest packet);
 * validate the resulting mapping independently. */
#define PV_MAX_AUDIO_FRAMES 5760u
static bool map_audio(GstSample *sample, GstAudioInfo *info, GstMapInfo *mapped)
{
 GstBuffer *buffer=gst_sample_get_buffer(sample);
 GstCaps *caps=gst_sample_get_caps(sample);
 if (!buffer || !caps || !gst_audio_info_from_caps(info, caps) ||
     GST_AUDIO_INFO_FORMAT(info)!=GST_AUDIO_FORMAT_F32LE || info->rate!=48000 ||
     info->channels!=2 || info->bpf!=8 ||
     !gst_buffer_get_size(buffer) || gst_buffer_get_size(buffer)>PV_MAX_AUDIO_FRAMES*8u ||
     gst_buffer_get_size(buffer)%8) return false;
 if (!gst_buffer_map(buffer, mapped, GST_MAP_READ)) return false;
 if (!mapped->size || mapped->size>PV_MAX_AUDIO_FRAMES*8u || mapped->size%8) {
  gst_buffer_unmap(buffer, mapped); return false;
 }
 return true;
}
/* libobs raises its global audio buffering for any audio older than its mix
 * window and never lowers it again, so one startup burst (audio queued while
 * the first video frame decodes) would delay receive audio for the whole
 * session. Audio this far past its render time is withheld instead. 90 ms
 * passes ordinary jitter (median 14 ms on production receives) and bounds any
 * buffering growth to a lag viewers do not notice; startup floods and stalls
 * (hundreds of ms to seconds) are still withheld. */
#define PV_STALE_AUDIO_NS (90 * GST_MSECOND)
static GstFlowReturn audio_sample(GstAppSink *sink, gpointer opaque)
{
 struct receiver *r = opaque;
 GstSample *sample = gst_app_sink_pull_sample(sink);
 if (!sample) return GST_FLOW_EOS;
 GstAudioInfo info; GstMapInfo mapped;
 if (!map_audio(sample, &info, &mapped)) {
  gst_sample_unref(sample); return GST_FLOW_ERROR;
 }
 struct obs_source_audio audio = {0};
 audio.format = AUDIO_FORMAT_FLOAT; audio.speakers = SPEAKERS_STEREO;
 audio.samples_per_sec = info.rate; audio.frames = mapped.size / info.bpf;
 audio.data[0] = mapped.data; audio.timestamp = timestamp(sample, r->pipe, sink);
 g_rec_mutex_lock(&r->delivery);
 g_mutex_lock(&r->lock);
 bool deliver = !r->quit && !r->changed && r->accept_samples && r->generation == r->active_generation;
 if (deliver) {
  const int64_t late = (int64_t)(os_gettime_ns() - audio.timestamp);
  r->audio_late = MAX(r->audio_late, late);
  if (late > (int64_t)PV_STALE_AUDIO_NS) { r->audio_dropped += audio.frames; deliver = false; }
  else r->audio_frames += audio.frames;
 }
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
 bool hevc = caps->profiles & PV_PROFILE_HEVC_ANY;
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
 if (allowed) {
  /* rswebrtc asks for ULPFEC/RED on every transceiver after it is created,
   * which costs three payload types per offered codec (red, ulpfec, rtx of
   * red). The engine never answers with them, and with seven video entries
   * the 96-127 range ran out: the last codec was left without its RTX type.
   * Offer the codecs and their RTX only. */
  GArray *transceivers = NULL;
  g_signal_emit_by_name(rtc, "get-transceivers", &transceivers);
  for (guint i = 0; transceivers && i < transceivers->len; i++)
   gst_util_set_object_arg(G_OBJECT(g_array_index(transceivers, GObject *, i)), "fec-type", "none");
  if (transceivers) g_array_unref(transceivers);
  return;
 }
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
static GstElement *request_encoded_filter(GstElement *rx, const char *peer, const char *pad,
 GstCaps *caps, gpointer opaque)
{
 (void)rx; (void)peer; (void)caps;
 struct receive_attempt *a = opaque;
 if (!pad || !g_str_has_prefix(pad, "video_")) return NULL;
 g_mutex_lock(&a->gate);
 gboolean current = attempt_current(a);
 GstElement *filter = current && !a->failed ? pv_codec_route_new() : NULL;
 if (filter) {
  pv_codec_route_admit_main422(filter,(a->caps.profiles & PV_PROFILE_HEVC_MAIN422_10)!=0);
  pv_codec_route_admit_main444(filter,(a->caps.profiles & PV_PROFILE_HEVC_MAIN444_10)!=0);
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
  GST_ELEMENT_ERROR(rx, CORE, MISSING_PLUGIN, ("Receive codec route unavailable"), (NULL));
 }
 g_mutex_unlock(&a->gate);
 return filter;
}
static GstElement *make_pipeline(struct receiver *r, const char *endpoint)
{
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
  /* The resource Location carries no token, so the session DELETE (and PATCH)
   * authenticate with the standard WHEP Bearer header; same-origin is enforced
   * by the patched signaller. The engine keeps reading ?token= on POST. */
  char *token = endpoint_token(endpoint);
  if (token) { g_object_set(signaller, "auth-token", token, NULL); wipe(&token); }
  g_object_unref(signaller); gst_object_unref(rx);
 }
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
/* Worker only, no receiver lock: which stage claims the latency every clocked
 * sink waits for. Each value is the cumulative upstream minimum at that
 * element's source pad. Logged once per change; names are GStreamer factories. */
static void log_latency(struct receiver *r)
{
 GstElement *sink = gst_bin_get_by_name(GST_BIN(r->pipe), "video");
 if (!sink) return;
 GstClockTime latency = gst_base_sink_get_latency(GST_BASE_SINK(sink));
 gst_object_unref(sink);
 if (!GST_CLOCK_TIME_IS_VALID(latency) || latency == r->logged_latency) return;
 r->logged_latency = latency;
 static const char *stages[] = {"rtpjitterbuffer", "rtph264depay", "rtph265depay", "rtpvp9depay",
  "rtpopusdepay", "h264parse", "h265parse", "vp9parse", "vtdec", "vtdec_hw", "opusdec",
  "audioresample", NULL};
 GString *text = g_string_new(NULL);
 GstIterator *it = gst_bin_iterate_recurse(GST_BIN(r->pipe));
 GValue item = G_VALUE_INIT;
 for (GstIteratorResult next; (next = gst_iterator_next(it, &item)) != GST_ITERATOR_DONE && next != GST_ITERATOR_ERROR;) {
  if (next == GST_ITERATOR_RESYNC) { g_string_truncate(text, 0); gst_iterator_resync(it); continue; }
  GstElement *element = g_value_get_object(&item);
  GstElementFactory *factory = gst_element_get_factory(element);
  const char *name = factory ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) : NULL;
  bool wanted = false;
  for (int i = 0; name && stages[i]; i++) if (!strcmp(name, stages[i])) wanted = true;
  GstPad *pad = wanted ? gst_element_get_static_pad(element, "src") : NULL;
  if (pad) {
   GstQuery *query = gst_query_new_latency();
   gboolean live = FALSE; GstClockTime min = 0, max = 0;
   if (gst_pad_query(pad, query)) {
    gst_query_parse_latency(query, &live, &min, &max);
    g_string_append_printf(text, " %s=%" G_GUINT64_FORMAT, name, min / GST_MSECOND);
   }
   gst_query_unref(query); gst_object_unref(pad);
  }
  g_value_reset(&item);
 }
 g_value_unset(&item); gst_iterator_free(it);
 blog(LOG_INFO, "[pixelview-whep] sink latency %" G_GUINT64_FORMAT " ms; cumulative ms:%s",
  latency / GST_MSECOND, text->str);
 g_string_free(text, TRUE);
}
/* Worker only: worst lateness of delivered samples against their stamped render
 * time, per 5 s window. Positive means OBS received the sample after it was due.
 * Only abnormal windows are logged: OBS drops a line that repeats with similar
 * text, which hid a real stall behind 158 healthy windows. */
static void log_lateness(struct receiver *r)
{
 const uint64_t now = os_gettime_ns();
 if (!r->late_window) { r->late_window = now; return; }
 if (now - r->late_window < 5000000000ULL) return;
 r->late_window = now;
 g_mutex_lock(&r->lock);
 const bool playing = r->frames || r->audio_frames;
 const int64_t video = r->video_late, audio = r->audio_late;
 const uint64_t dropped = r->audio_dropped;
 r->video_late = r->audio_late = INT64_MIN; r->audio_dropped = 0;
 g_mutex_unlock(&r->lock);
 if (playing && (video > 200000000 || audio > 200000000 || dropped))
  blog(LOG_INFO, "[pixelview-whep] worst lateness (5 s): video %lld ms, audio %lld ms, stale audio dropped %llu ms",
   video == INT64_MIN ? -1LL : (long long)(video / 1000000), audio == INT64_MIN ? -1LL : (long long)(audio / 1000000),
   (unsigned long long)(dropped / 48));
}
static void stop_pipeline(struct receiver *r)
{
 g_mutex_lock(&r->lock); r->accept_samples=false; g_mutex_unlock(&r->lock);
 if (r->attempt) {
  g_mutex_lock(&r->attempt->gate); r->attempt->receiver = NULL; g_mutex_unlock(&r->attempt->gate);
  attempt_unref(r->attempt, NULL); r->attempt = NULL;
 }
 r->logged_latency = 0; r->late_window = 0;
 g_mutex_lock(&r->lock); r->video_late = r->audio_late = INT64_MIN; r->audio_dropped = 0; g_mutex_unlock(&r->lock);
 bool had_pipeline=r->pipe != NULL;
 if (r->pipe) {
  gst_element_set_state(r->pipe, GST_STATE_NULL);
  gst_object_unref(r->pipe); r->pipe = NULL;
 }
 /* NULL has drained all appsink callbacks: clear the last frame only now. */
 if(had_pipeline) obs_source_output_video(r->source,NULL);
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
   r->active_color = r->color;
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
   if (!msg) { log_latency(r); log_lateness(r); }
   g_mutex_lock(&r->lock);
   uint64_t last = r->last_video;
   unsupported_profile_locked(r,msg);
   /* The first frame waits for ICE and, in passthrough, the sender's next
    * keyframe (up to its GOP length). Once video has flowed, a 5 s gap is a
    * dead session: the engine does not re-attach a viewer after its input
    * reconnects, and the Receiving panel reconnects with a fresh session. */
   const unsigned limit = r->frames ? 5 : 15;
   bool stale = os_gettime_ns() - last > limit * 1000000000ULL;
   if ((msg || stale) && !r->changed) {
    r->accept_samples = false;
    r->state = msg && GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS ? "ended" : "error";
    log_media_stop(msg,stale ? limit : 0);
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
 r->video_late = r->audio_late = INT64_MIN;
 obs_source_set_async_unbuffered(source, true);
 g_mutex_init(&r->lock); g_rec_mutex_init(&r->delivery); g_cond_init(&r->wake);
 proc_handler_t *ph = obs_source_get_proc_handler(source);
 proc_handler_add(ph, "void connect(string endpoint, int latency, string color)", connect_proc, r);
 proc_handler_add(ph, "void disconnect()", disconnect_proc, r);
 proc_handler_add(ph, "void get_status(out bool ready, out string state, out int frames, out int audio_frames, out int latency, out int jitter_latency, out string failure)", status_proc, r);
 r->thread = g_thread_new("pixelview-whep", worker, r);
 return r;
}
static void destroy(void *opaque)
{
 struct receiver *r = opaque;
 g_rec_mutex_lock(&r->delivery);
 g_mutex_lock(&r->lock); r->quit = true; r->generation++;
 r->failure=NULL; r->accept_samples = false; g_cond_signal(&r->wake); g_mutex_unlock(&r->lock);
 g_rec_mutex_unlock(&r->delivery);
 g_thread_join(r->thread); wipe(&r->endpoint);
 g_cond_clear(&r->wake); g_rec_mutex_clear(&r->delivery); g_mutex_clear(&r->lock); g_free(r->widened); g_free(r);
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
