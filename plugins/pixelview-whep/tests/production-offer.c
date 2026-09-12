/* Offline actual production hooks, graph and worker regressions. */
#define GST_USE_UNSTABLE_API
#define PIXELVIEW_WHEP_TEST 1
#include <obs-module.h>
static void offline_video(obs_source_t *s,const struct obs_source_frame *f) {(void)s;(void)f;}
#define obs_source_output_video offline_video
#include "../pixelview-whep.c"
#undef obs_source_output_video
#include <gst/webrtc/webrtc.h>
#include <stdio.h>
static const char *video_caps="application/x-rtp,media=video,encoding-name=H265,clock-rate=90000,payload=96;application/x-rtp,media=video,encoding-name=H264,clock-rate=90000,payload=97;application/x-rtp,media=video,encoding-name=VP9,clock-rate=90000,payload=98";
static void negative_hook(bool null_caps, bool stale)
{
 struct receiver r={.active_latency_override=true,.active_latency=50,.generation=stale?1:0,.jitter_latency=-1};
 g_mutex_init(&r.lock);g_rec_mutex_init(&r.delivery);
 GstElement *rtc=gst_element_factory_make("webrtcbin",NULL);
 struct receive_attempt *a=attempt_new(&r);webrtc_ready(NULL,NULL,rtc,a);
 if (stale) g_assert_cmpint(r.jitter_latency,==,-1);
 GstCaps *caps=null_caps?NULL:gst_caps_from_string(video_caps);
 GstWebRTCRTPTransceiver *trans=NULL;
 g_signal_emit_by_name(rtc,"add-transceiver",GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY,caps,&trans);
 GstCaps *out=NULL;g_object_get(trans,"codec-preferences",&out,NULL);
 g_assert_nonnull(out);g_assert_true(gst_caps_is_empty(out));
 gst_caps_unref(out);if(caps)gst_caps_unref(caps);
 gst_element_set_state(rtc,GST_STATE_READY);
 GstPromise *promise=gst_promise_new();g_signal_emit_by_name(rtc,"create-offer",NULL,promise);
 g_assert_cmpint(gst_promise_wait(promise),==,GST_PROMISE_RESULT_REPLIED);
 const GstStructure *reply=gst_promise_get_reply(promise);
 g_assert_false(gst_structure_has_field(reply,"offer"));
 gst_promise_unref(promise);gst_element_set_state(rtc,GST_STATE_NULL);
 /* Hold rtc after receiver detach and clear, then run a late callback. */
 r.attempt=a;stop_pipeline(&r);
 g_mutex_clear(&r.lock);g_rec_mutex_clear(&r.delivery);
 g_signal_emit_by_name(rtc,"on-new-transceiver",trans);
 gst_object_unref(trans);gst_object_unref(rtc);
 puts("PASS empty/NULL/stale production hook: no valid offer; detached callback safe");
}
static void capture_ready(GObject *s,const char *peer,GstElement *rtc,gpointer opaque)
{ (void)s;(void)peer;g_async_queue_push(opaque,gst_object_ref(rtc)); }
/* Inject already-admitted snapshots: no decoder, device or network needed. */
static void admitted_envelope(unsigned profiles, unsigned level, unsigned fps)
{
 struct receiver r={.active_latency_override=true,.active_latency=50,.active_caps={profiles,level}};
 g_mutex_init(&r.lock);g_rec_mutex_init(&r.delivery);
 r.pipe=make_pipeline(&r,"http://127.0.0.1:9/offline-offer");g_assert_nonnull(r.pipe);
 GstElement *filter=gst_bin_get_by_name(GST_BIN(r.pipe),"video-policy");g_assert_nonnull(filter);
 GstCaps *raw=NULL;g_object_get(filter,"caps",&raw,NULL);g_assert_nonnull(raw);
 char *bounds=g_strdup_printf("video/x-raw,format=P010_10LE,width=(int)[1,1920],height=(int)[1,1080],framerate=(fraction)[0/1,%u/1]",fps);
 GstCaps *expected_raw=gst_caps_from_string(bounds);g_free(bounds);
 g_assert_true(gst_caps_is_equal(raw,expected_raw));
 gst_caps_unref(expected_raw);gst_caps_unref(raw);gst_object_unref(filter);

 /* Exercise the real production signal hook, not just the offer helper. */
 GstElement *rtc=gst_element_factory_make("webrtcbin",NULL);g_assert_nonnull(rtc);
 webrtc_ready(NULL,NULL,rtc,r.attempt);
 GstCaps *input=gst_caps_from_string(video_caps);
 GstWebRTCRTPTransceiver *trans=NULL;
 g_signal_emit_by_name(rtc,"add-transceiver",GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY,input,&trans);
 GstCaps *offered=NULL;g_object_get(trans,"codec-preferences",&offered,NULL);
 struct pixelview_receive_limits limits={profiles,level,1920,1080,fps};
 GstCaps *expected_offer=pixelview_profile_offer_caps_limited(input,&limits);
 g_assert_nonnull(expected_offer);g_assert_false(gst_caps_is_empty(expected_offer));
 g_assert_nonnull(offered);g_assert_true(gst_caps_is_equal(offered,expected_offer));
 g_assert_false(r.offer_failed);
 gst_element_set_state(rtc,GST_STATE_READY);
 GstPromise *promise=gst_promise_new();g_signal_emit_by_name(rtc,"create-offer",NULL,promise);
 g_assert_cmpint(gst_promise_wait(promise),==,GST_PROMISE_RESULT_REPLIED);
 g_assert_true(gst_structure_has_field(gst_promise_get_reply(promise),"offer"));
 gst_promise_unref(promise);gst_element_set_state(rtc,GST_STATE_NULL);
 gst_caps_unref(expected_offer);gst_caps_unref(offered);gst_caps_unref(input);
 stop_pipeline(&r);gst_object_unref(trans);gst_object_unref(rtc);
 g_mutex_clear(&r.lock);g_rec_mutex_clear(&r.delivery);
 printf("PASS admitted production hook + exact P010 HD capsfilter: mask=%u level=%u fps=%u\n",profiles,level,fps);
}
static void actual_graph(void)
{
 struct receiver r={.active_latency_override=true,.active_latency=50};
 g_mutex_init(&r.lock);g_rec_mutex_init(&r.delivery);
 g_assert_true(probe_attempt(&r,0));
 g_assert_cmpuint(r.active_caps.profiles,!=,0);
 g_assert_true(r.active_caps.hevc_level_id==0 || r.active_caps.hevc_level_id==120 || r.active_caps.hevc_level_id==123);
 r.pipe=make_pipeline(&r,"http://127.0.0.1:9/offline-offer");g_assert_nonnull(r.pipe);
 unsigned fps=(r.active_caps.profiles&(PV_PROFILE_HEVC_MAIN|PV_PROFILE_HEVC_MAIN10)) && r.active_caps.hevc_level_id==120 ? 30 : 60;
 char *bounds=g_strdup_printf("video/x-raw,format=P010_10LE,width=(int)[1,1920],height=(int)[1,1080],framerate=(fraction)[0/1,%u/1]",fps);
 GstCaps *envelope=gst_caps_from_string(bounds);g_free(bounds);
 GstIterator *it=gst_bin_iterate_elements(GST_BIN(r.pipe));GValue value=G_VALUE_INIT;bool bounded=false;
 while(gst_iterator_next(it,&value)==GST_ITERATOR_OK) {
  GObject *obj=g_value_get_object(&value);
  if(g_object_class_find_property(G_OBJECT_GET_CLASS(obj),"caps")) {
   GstCaps *c=NULL;g_object_get(obj,"caps",&c,NULL);
   if(c) { bounded|=gst_caps_is_subset(c,envelope);gst_caps_unref(c); }
  }
  g_value_reset(&value);
 }
 g_value_unset(&value);gst_iterator_free(it);gst_caps_unref(envelope);g_assert_true(bounded);
 GstElement *rx=gst_bin_get_by_name(GST_BIN(r.pipe),"rx");
 GAsyncQueue *ready=g_async_queue_new();
 GObject *signaller=NULL;g_object_get(rx,"signaller",&signaller,NULL);
 g_object_set(rx,"stun-server",NULL,NULL); /* No default Google STUN/DNS. */
 g_signal_connect(signaller,"webrtcbin-ready",G_CALLBACK(capture_ready),ready);
 gst_element_set_state(r.pipe,GST_STATE_PLAYING);
 GstElement *rtc=g_async_queue_timeout_pop(ready,5000000);g_assert_nonnull(rtc);
 GstWebRTCSessionDescription *offer=NULL;
 for(unsigned i=0;i<500&&!offer;i++) {
  g_usleep(10000);if(rtc)g_object_get(rtc,"local-description",&offer,NULL);
 }
 g_assert_nonnull(offer);
 char *sdp=gst_sdp_message_as_text(offer->sdp);
 char *hevc1=g_strdup_printf("level-id=%u;profile-id=1;tier-flag=0;tx-mode=SRST",r.active_caps.hevc_level_id);
 g_assert_cmpint(strstr(sdp,hevc1)!=NULL,==,(r.active_caps.profiles&PV_PROFILE_HEVC_MAIN)!=0);g_free(hevc1);
 char *hevc2=g_strdup_printf("level-id=%u;profile-id=2;tier-flag=0;tx-mode=SRST",r.active_caps.hevc_level_id);
 g_assert_cmpint(strstr(sdp,hevc2)!=NULL,==,(r.active_caps.profiles&PV_PROFILE_HEVC_MAIN10)!=0);g_free(hevc2);
 g_assert_nonnull(strstr(sdp,"packetization-mode=1"));
 g_assert_nonnull(strstr(sdp,"OPUS/48000/2"));
 g_assert_nonnull(strstr(sdp,"profile-id=0"));g_assert_nonnull(strstr(sdp,"profile-id=2"));
 gboolean payloads[128]={0};unsigned h265=0,vp9=0;
 for(unsigned m=0;m<gst_sdp_message_medias_len(offer->sdp);m++) {
  const GstSDPMedia *media=gst_sdp_message_get_media(offer->sdp,m);
  for(unsigned i=0;i<gst_sdp_media_attributes_len(media);i++) {
   const GstSDPAttribute *attr=gst_sdp_media_get_attribute(media,i);
   if(strcmp(attr->key,"rtpmap"))continue;
   unsigned pt=(unsigned)strtoul(attr->value,NULL,10);g_assert_cmpuint(pt,<,128);
   g_assert_false(payloads[pt]);payloads[pt]=TRUE;
   h265+=strstr(attr->value,"H265/")!=NULL;vp9+=strstr(attr->value,"VP9/")!=NULL;
  }
 }
 unsigned expected_hevc=!!(r.active_caps.profiles&PV_PROFILE_HEVC_MAIN)+!!(r.active_caps.profiles&PV_PROFILE_HEVC_MAIN10);
 expected_hevc += expected_hevc != 0; /* Explicit Main422 policy, not another probe bit. */
 unsigned expected_vp9=!!(r.active_caps.profiles&PV_PROFILE_VP9_0)+!!(r.active_caps.profiles&PV_PROFILE_VP9_2);
 g_assert_cmpuint(h265,==,expected_hevc);g_assert_cmpuint(vp9,==,expected_vp9);g_assert_cmpint(r.jitter_latency,==,50);
 /* No credentials; still avoid printing host candidates. */
 g_free(sdp);gst_webrtc_session_description_free(offer);
 stop_pipeline(&r);g_async_queue_unref(ready);gst_object_unref(rtc);gst_object_unref(rx);g_object_unref(signaller);
 g_mutex_clear(&r.lock);g_rec_mutex_clear(&r.delivery);
 printf("PASS production raw graph offer: actual probe mask=%u level=%u exact profile gates + envelope, H264mode1 Opus stereo unique BUNDLE PTs jitter50\n",r.active_caps.profiles,r.active_caps.hevc_level_id);
}
static void main422_offer(void)
{
 struct receiver r={.active_latency_override=true,.active_latency=50,.active_caps={PV_PROFILE_HEVC_MAIN|PV_PROFILE_HEVC_MAIN10,123}};
 g_mutex_init(&r.lock);g_rec_mutex_init(&r.delivery);
 struct receive_attempt *a=attempt_new(&r);
 GstElement *rtc=gst_element_factory_make("webrtcbin",NULL);
 webrtc_ready(NULL,NULL,rtc,a);
 GstCaps *input=gst_caps_from_string(video_caps);
 GstWebRTCRTPTransceiver *trans=NULL;
 g_signal_emit_by_name(rtc,"add-transceiver",GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY,input,&trans);
 gst_element_set_state(rtc,GST_STATE_READY);
 GstPromise *promise=gst_promise_new();g_signal_emit_by_name(rtc,"create-offer",NULL,promise);
 g_assert_cmpint(gst_promise_wait(promise),==,GST_PROMISE_RESULT_REPLIED);
 GstWebRTCSessionDescription *offer=NULL;
 g_assert_true(gst_structure_get(gst_promise_get_reply(promise),"offer",GST_TYPE_WEBRTC_SESSION_DESCRIPTION,&offer,NULL));
 char *sdp=gst_sdp_message_as_text(offer->sdp);
 /* Normal policy deliberately offers Main422; ordinary probe bits stay truthful. */
 g_assert_nonnull(strstr(sdp,"level-id=120;profile-id=4;tier-flag=0;tx-mode=SRST;interop-constraints=1d0800000000"));
 g_assert_nonnull(strstr(sdp,"level-id=123;profile-id=1"));
 g_assert_nonnull(strstr(sdp,"level-id=123;profile-id=2"));
 puts(sdp);
 puts("PASS normal-build Main422 25p policy: profile4 level120 constrained SDP via production hook; ordinary Main/Main10 unchanged");
 g_free(sdp);gst_webrtc_session_description_free(offer);gst_promise_unref(promise);
 gst_element_set_state(rtc,GST_STATE_NULL);r.attempt=a;stop_pipeline(&r);
 gst_object_unref(trans);gst_object_unref(rtc);gst_caps_unref(input);
 g_mutex_clear(&r.lock);g_rec_mutex_clear(&r.delivery);
}
/* Exercise production configuration and actual GObject jitter readback without
 * starting a receiver worker, decoder, network session or DeckLink device. */
static void latency_notified(GObject *object,GParamSpec *spec,gpointer data)
{ (void)object;(void)spec;(*(unsigned *)data)++; }
static void latency_contract(void)
{
 GstElement *fresh=gst_element_factory_make("webrtcbin",NULL);g_assert_nonnull(fresh);
 guint upstream=0;g_object_get(fresh,"latency",&upstream,NULL);gst_object_unref(fresh);
 const int requests[]={-999,0,50,100,2000,-1,2001}; /* -999: omitted argument */
 for(unsigned i=0;i<G_N_ELEMENTS(requests);i++) {
  struct receiver r={0};g_mutex_init(&r.lock);g_rec_mutex_init(&r.delivery);g_cond_init(&r.wake);
  calldata_t cd;calldata_init(&cd);calldata_set_string(&cd,"endpoint","https://example.invalid/whep");
  if(requests[i]!=-999)calldata_set_int(&cd,"latency",requests[i]);
  connect_proc(&r,&cd);
  bool valid=requests[i]==-999 || (requests[i]>=0 && requests[i]<=2000);
  unsigned expected=requests[i]==-999 || !valid ? upstream : (unsigned)requests[i];
  g_assert_cmpstr(r.state,==,valid?"connecting":"error");
  calldata_t status;calldata_init(&status);status_proc(&r,&status);
  g_assert_cmpint(calldata_int(&status,"latency"),==,valid && requests[i]!=-999 ? requests[i] : -1);
  g_assert_cmpint(calldata_int(&status,"jitter_latency"),==,-1);calldata_free(&status);
  if(!valid) { g_assert_null(r.endpoint);g_assert_null(r.pipe); }
  /* Same requested->active snapshot used by the production worker. */
  r.active_latency=r.latency;r.active_latency_override=r.latency_override;
  r.active_generation=r.generation;r.changed=false;
  struct receive_attempt *a=attempt_new(&r);
  GstElement *rtc=gst_element_factory_make("webrtcbin",NULL);g_assert_nonnull(rtc);
  unsigned writes=0;g_signal_connect(rtc,"notify::latency",G_CALLBACK(latency_notified),&writes);
  webrtc_ready(NULL,NULL,rtc,a);
  g_assert_cmpuint(writes,==,valid && requests[i]!=-999 ? 1 : 0);
  guint actual=0;g_object_get(rtc,"latency",&actual,NULL);
  g_assert_cmpuint(actual,==,expected);g_assert_cmpint(r.jitter_latency,==,(int)expected);
  calldata_init(&status);status_proc(&r,&status);
  g_assert_cmpint(calldata_int(&status,"jitter_latency"),==,(int)actual);calldata_free(&status);
  r.attempt=a;stop_pipeline(&r);gst_object_unref(rtc);wipe(&r.endpoint);calldata_free(&cd);
  g_cond_clear(&r.wake);g_mutex_clear(&r.lock);g_rec_mutex_clear(&r.delivery);
 }
 printf("PASS production latency contract: upstream default observed %ums, explicit0/50/100/2000 preserved, invalid rejected; actual webrtcbin readback\n",upstream);
}
int main(int argc,char **argv)
{
 setbuf(stdout,NULL);gst_init(&argc,&argv);
 latency_contract();
 if(argc>1 && !strcmp(argv[1],"--main422-only")) { main422_offer(); return 0; }
 main422_offer();
 negative_hook(false,false);negative_hook(true,false);negative_hook(false,true);
 admitted_envelope(PV_PROFILE_VP9_0,0,60);
 admitted_envelope(PV_PROFILE_H264,0,60);
 admitted_envelope(PV_PROFILE_VP9_2,0,60);
 admitted_envelope(PV_PROFILE_VP9_0|PV_PROFILE_VP9_2,0,60);
 admitted_envelope(PV_PROFILE_HEVC_MAIN|PV_PROFILE_HEVC_MAIN10,123,60);
 admitted_envelope(PV_PROFILE_HEVC_MAIN|PV_PROFILE_HEVC_MAIN10,120,30);
 admitted_envelope(PV_PROFILE_H264|PV_PROFILE_HEVC_MAIN|PV_PROFILE_VP9_0,120,30);
 if(argc<2 || strcmp(argv[1],"--deterministic-only")) actual_graph();
 return 0;
}
