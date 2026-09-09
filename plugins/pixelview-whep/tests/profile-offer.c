/* Offline policy and actual webrtcbin SDP capture; no media/credentials. */
#define GST_USE_UNSTABLE_API
#include <gst/gst.h>
#include <gst/webrtc/webrtc.h>
#include <stdio.h>
#ifdef PROFILE_POLICY_LEGACY
static GstCaps *pixelview_profile_offer_caps(const GstCaps *caps, unsigned profiles, unsigned level)
{ (void)profiles; (void)level; return gst_caps_copy(caps); }
#define PV_PROFILE_H264 1u
#define PV_PROFILE_HEVC_MAIN 2u
#define PV_PROFILE_HEVC_MAIN10 4u
#define PV_PROFILE_VP9_0 8u
#define PV_PROFILE_VP9_2 16u
#else
#include "../profile-offer.h"
#endif
typedef struct { unsigned profiles, level; GMainLoop *loop; gboolean captured; GstElement *rtc; } Capture;
static void local_offer(GObject *rtc, GParamSpec *pspec, gpointer opaque)
{
 (void)pspec; Capture *c = opaque;
 GstWebRTCSessionDescription *offer = NULL;
 g_object_get(rtc,"local-description",&offer,NULL);
 if (!offer || c->captured) { if (offer) gst_webrtc_session_description_free(offer); return; }
 c->captured = TRUE;
 gchar *sdp = gst_sdp_message_as_text(offer->sdp); puts(sdp); g_free(sdp);
 gst_webrtc_session_description_free(offer); g_main_loop_quit(c->loop);
}
static void transceiver(GstElement *rtc, GObject *trans, gpointer opaque)
{
 (void)rtc; Capture *c = opaque; GstCaps *caps = NULL;
 g_object_get(trans,"codec-preferences",&caps,NULL); g_assert_nonnull(caps);
 gchar *text = gst_caps_to_string(caps); fprintf(stderr,"RAW_CAPS %s\n",text); g_free(text);
 if (c->profiles) {
  GstCaps *out = pixelview_profile_offer_caps(caps,c->profiles,c->level);
  g_assert_nonnull(out); g_object_set(trans,"codec-preferences",out,NULL); gst_caps_unref(out);
 }
 gst_caps_unref(caps);
}
static gboolean poll_offer(gpointer opaque)
{ Capture *c = opaque; if (c->rtc) local_offer(G_OBJECT(c->rtc),NULL,c); return G_SOURCE_CONTINUE; }
static void ready(GObject *signaller, const char *peer, GstElement *rtc, gpointer opaque)
{
 (void)signaller; (void)peer;
 Capture *c = opaque; c->rtc = gst_object_ref(rtc);
 g_signal_connect(rtc,"on-new-transceiver",G_CALLBACK(transceiver),opaque);
 g_signal_connect(rtc,"notify::local-description",G_CALLBACK(local_offer),opaque);
}
static gboolean stop_capture(gpointer opaque) { g_main_loop_quit(opaque); return G_SOURCE_REMOVE; }
int main(int argc, char **argv)
{
 gst_init(&argc, &argv);
 GstCaps *base = gst_caps_from_string("application/x-rtp,media=video,encoding-name=H265,clock-rate=90000,payload=96;application/x-rtp,media=video,encoding-name=H264,clock-rate=90000,payload=97;application/x-rtp,media=video,encoding-name=VP9,clock-rate=90000,payload=98");
 unsigned profiles = argc > 1 ? (unsigned)g_ascii_strtoull(argv[1],NULL,10) : 0;
 unsigned level = argc > 2 ? (unsigned)g_ascii_strtoull(argv[2],NULL,10) : 0;
 if (argc > 3) {
  Capture c = {profiles,level,g_main_loop_new(NULL,FALSE),FALSE,NULL};
  GError *error = NULL;
  GstElement *pipe = gst_parse_launch("whepclientsrc name=rx video-codecs=\"<H265,H264,VP9>\" audio-codecs=\"<OPUS>\" rx. ! video/x-raw ! queue ! fakesink sync=false rx. ! audio/x-raw ! queue ! fakesink sync=false",&error);
  g_assert_no_error(error); g_assert_nonnull(pipe);
  GstElement *rx = gst_bin_get_by_name(GST_BIN(pipe),"rx"); GObject *signaller = NULL;
  g_object_set(rx,"stun-server",NULL,NULL); /* rswebrtc otherwise defaults to Google STUN */
  g_object_get(rx,"signaller",&signaller,NULL);
  g_signal_connect(signaller,"webrtcbin-ready",G_CALLBACK(ready),&c);
  g_object_set(signaller,"whep-endpoint","http://127.0.0.1:9/offline-offer",NULL);
  gst_element_set_state(pipe,GST_STATE_PLAYING);
  guint poll = g_timeout_add(20,poll_offer,&c);
  guint timer = g_timeout_add_seconds(10,stop_capture,c.loop); g_main_loop_run(c.loop);
  g_source_remove(poll);
  if (c.captured) g_source_remove(timer);
  if (c.rtc) gst_object_unref(c.rtc);
  gst_element_set_state(pipe,GST_STATE_NULL);
  gst_object_unref(rx); g_object_unref(signaller); gst_object_unref(pipe); g_main_loop_unref(c.loop);
  gst_caps_unref(base); g_assert_true(c.captured); return 0;
 }
#ifndef PROFILE_POLICY_LEGACY
 struct pixelview_receive_limits limits = {31,123,1920,1080,60};
 GstCaps *limited = pixelview_profile_offer_caps_limited(base,&limits);
 g_assert_nonnull(limited); g_assert_cmpuint(gst_caps_get_size(limited),==,5); gst_caps_unref(limited);
 limits.max_width=3840; g_assert_null(pixelview_profile_offer_caps_limited(base,&limits));
 limits.max_width=1920; limits.hevc_level_id=93;
 g_assert_null(pixelview_profile_offer_caps_limited(base,&limits));
#endif
 GstCaps *caps = pixelview_profile_offer_caps(base, profiles, level);
 g_assert_nonnull(caps);
 if (!profiles) { g_assert_true(gst_caps_is_empty(caps)); puts("PASS no probe means no video profiles"); goto done; }
 GstElement *rtc = gst_element_factory_make("webrtcbin", NULL);
 g_assert_nonnull(rtc);
 GstWebRTCRTPTransceiver *trans = NULL;
 g_signal_emit_by_name(rtc,"add-transceiver",GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY,caps,&trans);
 g_assert_nonnull(trans); gst_object_unref(trans);
 GstCaps *audio = gst_caps_from_string("application/x-rtp,media=audio,encoding-name=OPUS,clock-rate=48000,encoding-params=(string)2,payload=111");
 g_signal_emit_by_name(rtc,"add-transceiver",GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY,audio,&trans);
 gst_object_unref(trans); gst_caps_unref(audio);
 g_assert_cmpint(gst_element_set_state(rtc,GST_STATE_READY),!=,GST_STATE_CHANGE_FAILURE);
 GstPromise *promise = gst_promise_new();
 g_signal_emit_by_name(rtc,"create-offer",NULL,promise);
 g_assert_cmpint(gst_promise_wait(promise),==,GST_PROMISE_RESULT_REPLIED);
 GstWebRTCSessionDescription *offer = NULL;
 g_assert_true(gst_structure_get(gst_promise_get_reply(promise),"offer",GST_TYPE_WEBRTC_SESSION_DESCRIPTION,&offer,NULL));
 gchar *sdp = gst_sdp_message_as_text(offer->sdp); puts(sdp); g_free(sdp);
 gst_webrtc_session_description_free(offer); gst_promise_unref(promise);
 gst_element_set_state(rtc,GST_STATE_NULL); gst_object_unref(rtc);
 done: gst_caps_unref(caps); gst_caps_unref(base); return 0;
}
