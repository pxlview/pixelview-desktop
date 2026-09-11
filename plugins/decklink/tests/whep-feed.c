/* SPDX-License-Identifier: GPL-2.0-or-later */
/* NONSHIPPING Main422 transport admission. Never linked by production CMake. */
#define GST_USE_UNSTABLE_API
#define PIXELVIEW_WHEP_TEST 1
#include <obs-module.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/webrtc/webrtc.h>
#include <nice/agent.h>
#include "../../pixelview-whep/capability-probe.h"
#include "../../pixelview-whep/profile-offer.h"
#include <assert.h>
#include <stdio.h>
#include <util/platform.h>
#include "whep-campaign.h"
static gboolean test_probe(struct pixelview_receive_capabilities *caps,
                            pixelview_capability_cancel_fn cancelled, void *opaque)
{
 if (cancelled && cancelled(opaque)) return FALSE;
 *caps = (struct pixelview_receive_capabilities){.profiles=PV_PROFILE_HEVC_MAIN, .hevc_level_id=120};
 return TRUE; /* Injection, NOT capability evidence. */
}
static GstCaps *test_offer(const GstCaps *input, const struct pixelview_receive_limits *limits)
{
 GstCaps *caps = pixelview_profile_offer_caps_limited(input, limits);
 if (!caps) return NULL;
 for (guint i=0; i<gst_caps_get_size(caps); ++i) {
  GstStructure *s=gst_caps_get_structure(caps,i);
  if (!g_strcmp0(gst_structure_get_string(s,"encoding-name"),"H265"))
   gst_structure_set(s,"profile-id",G_TYPE_STRING,"4","interop-constraints",G_TYPE_STRING,"1d0800000000",NULL);
 }
 return caps;
}
static void offline_ready(GObject *s,const char *peer,GstElement *rtc,gpointer data)
{
 (void)s;(void)peer;(void)data;
 GObject *ice=NULL;NiceAgent *agent=NULL;g_object_get(rtc,"ice-agent",&ice,NULL);
 g_object_get(ice,"agent",&agent,NULL);assert(agent);
 NiceAddress address;nice_address_init(&address);assert(nice_address_set_from_string(&address,"127.0.0.1"));
 assert(nice_agent_add_local_address(agent,&address));g_object_set(agent,"ice-tcp",FALSE,NULL);
 g_object_unref(agent);g_object_unref(ice);
}
static GstBusSyncReply bus_audit(GstBus *bus,GstMessage *msg,gpointer data)
{
 (void)bus;(void)data;
 if(g_getenv("PV_TRACE_MESSAGES") && GST_MESSAGE_SRC(msg)) fprintf(stderr,"BUS_TRACE %p source=%p %s type=%s refs=%d\n",(void*)msg,(void*)GST_MESSAGE_SRC(msg),GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)),GST_MESSAGE_TYPE_NAME(msg),GST_OBJECT_REFCOUNT_VALUE(GST_MESSAGE_SRC(msg)));
 if(GST_MESSAGE_TYPE(msg)==GST_MESSAGE_ERROR){pv_campaign_flush();GError *e=NULL;char *d=NULL;gst_message_parse_error(msg,&e,&d);fprintf(stderr,"MAIN422_ERROR %s %s\n",e->message,d?d:"");g_clear_error(&e);g_free(d);}
 return GST_BUS_PASS;
}
static gint video_decoders, raw_decodebin_buffers, decodebins;
static GstPadProbeReturn decodebin_input(GstPad *pad, GstPadProbeInfo *info, gpointer opaque)
{
 (void)opaque;
 if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
  GstCaps *caps=gst_pad_get_current_caps(pad);
  if (caps && gst_structure_has_name(gst_caps_get_structure(caps,0),"video/x-raw"))
   g_atomic_int_inc(&raw_decodebin_buffers);
  if (caps) gst_caps_unref(caps);
 }
 return GST_PAD_PROBE_OK;
}
static GstPadProbeReturn cadence_parsed_audit(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
 (void)data;
 if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
  GstBuffer *buffer=GST_PAD_PROBE_INFO_BUFFER(info);
  if(g_getenv("PV_MATRIX_TRACE")) {
   fprintf(stderr,"MATRIX_PARSED wall=%llu pts=%llu duration=%llu bytes=%zu delta=%d\n",(unsigned long long)os_gettime_ns(),(unsigned long long)GST_BUFFER_PTS(buffer),(unsigned long long)GST_BUFFER_DURATION(buffer),gst_buffer_get_size(buffer),GST_BUFFER_FLAG_IS_SET(buffer,GST_BUFFER_FLAG_DELTA_UNIT));
   return GST_PAD_PROBE_OK;
  }
  GstCaps *caps=gst_pad_get_current_caps(pad); char *text=caps?gst_caps_to_string(caps):g_strdup("none");
  fprintf(stderr,"VT_PARSED pts=%llu duration=%llu caps=%s\n",(unsigned long long)GST_BUFFER_PTS(buffer),(unsigned long long)GST_BUFFER_DURATION(buffer),text);
  gpointer state=NULL; GstMeta *meta;
  while ((meta=gst_buffer_iterate_meta(buffer,&state))) fprintf(stderr,"VT_PARSED_META %s\n",g_type_name(meta->info->api));
  g_free(text);if(caps)gst_caps_unref(caps);
 }
 return GST_PAD_PROBE_OK;
}
#include <gst/rtp/gstrtpbuffer.h>
static GstPadProbeReturn matrix_rtp_audit(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
 (void)pad;(void)data;
 GstBuffer *b=GST_PAD_PROBE_INFO_BUFFER(info);GstRTPBuffer r=GST_RTP_BUFFER_INIT;
 if(b && gst_rtp_buffer_map(b,GST_MAP_READ,&r)) {
  fprintf(stderr,"MATRIX_RTP wall=%llu seq=%u ts=%u marker=%d discont=%d\n",(unsigned long long)os_gettime_ns(),gst_rtp_buffer_get_seq(&r),gst_rtp_buffer_get_timestamp(&r),gst_rtp_buffer_get_marker(&r),GST_BUFFER_FLAG_IS_SET(b,GST_BUFFER_FLAG_DISCONT));
  gst_rtp_buffer_unmap(&r);
 }
 return GST_PAD_PROBE_OK;
}
static void element_audit(GstBin *bin, GstBin *sub, GstElement *element, gpointer data)
{
 (void)bin;(void)sub;(void)data;
 campaign_element(element,GPOINTER_TO_UINT(data));
 GstElementFactory *factory=gst_element_get_factory(element); if(!factory)return;
 if (g_getenv("PV_MATRIX_TRACE") && !strcmp(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)),"rtph265depay")) {
  GstPad *pad=gst_element_get_static_pad(element,"sink");assert(pad);
  gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,matrix_rtp_audit,NULL,NULL);gst_object_unref(pad);
 }
 if ((g_getenv("PV_MATRIX_TRACE") || g_getenv("PV_DECKLINK_WHEP_CADENCE_AUDIT")) && !strcmp(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)),"h265parse")) {
  GstPad *pad=gst_element_get_static_pad(element,"src");assert(pad);
  gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,cadence_parsed_audit,NULL,NULL);gst_object_unref(pad);
 }
 const char *klass=gst_element_factory_get_metadata(factory,GST_ELEMENT_METADATA_KLASS);
 if (klass && strstr(klass,"Decoder/Video")) {
  g_atomic_int_inc(&video_decoders);
  char *path=gst_object_get_path_string(GST_OBJECT(element));
  fprintf(stderr,"DECODER_CONSTRUCTION factory=%s class=%s path=%s parent=%s\n",
   gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)),klass,path,GST_OBJECT_NAME(sub));g_free(path);
  /* Construction can precede negotiated caps. Record both current graph caps
   * and the factory templates without querying/re-entering negotiation. No
   * element property dump (which could contain an endpoint) is emitted. */
  for(const GList *p=gst_element_factory_get_static_pad_templates(factory);p;p=p->next) {
   GstStaticPadTemplate *t=p->data;GstCaps *caps=gst_static_caps_get(&t->static_caps);
   char *text=gst_caps_to_string(caps);fprintf(stderr,"DECODER_TEMPLATE pad=%s caps=%s\n",t->name_template,text);
   g_free(text);gst_caps_unref(caps);
  }
  char *topology=gst_debug_bin_to_dot_data(bin,GST_DEBUG_GRAPH_SHOW_MEDIA_TYPE|GST_DEBUG_GRAPH_SHOW_CAPS_DETAILS);
  fprintf(stderr,"DECODER_TOPOLOGY\n%s\n",topology);g_free(topology);fflush(stderr);
  /* Fail at the old encoded-decoder boundary, rather than letting it decode or
   * hiding a hang by releasing a consumer. Native VT is not a Gst decoder. */
  assert(!"encoded preview video decoder must never be constructed");
 }
 if (!strcmp(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)),"decodebin3")) {
  g_atomic_int_inc(&decodebins);
  GstPad *pad=gst_element_get_static_pad(element,"sink");assert(pad);
  gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,decodebin_input,NULL,NULL);gst_object_unref(pad);
 }
}
static GstElement *offline_parse(const char *text,GError **error)
{
 const char *mode=g_getenv("PV_WHEP_PREVIEW");
 char **parts=g_strsplit(text,"P010_10LE",-1);
 char *description=g_strjoinv(mode && !strcmp(mode,"nv12") ? "NV12" : "P010_10LE",parts);g_strfreev(parts);
 GstElement *pipe=gst_parse_launch(description,error);g_free(description);if(!pipe)return NULL;
 g_signal_connect(pipe,"deep-element-added",G_CALLBACK(element_audit),GUINT_TO_POINTER(++campaign_generation));
 GstElement *rx=gst_bin_get_by_name(GST_BIN(pipe),"rx");
 if(rx){GObject *s=NULL;g_object_set(rx,"stun-server",NULL,NULL);g_object_get(rx,"signaller",&s,NULL);
 g_signal_connect(s,"webrtcbin-ready",G_CALLBACK(offline_ready),NULL);g_object_unref(s);gst_object_unref(rx);}
 GstBus *bus=gst_element_get_bus(pipe);gst_bus_set_sync_handler(bus,bus_audit,NULL,NULL);gst_object_unref(bus);
 return pipe;
}
#include "../../pixelview-whep/source-feed-queue.h"
static void trace_push(struct pv_feed_queue *q, uint64_t generation, const struct pv_feed_request *r, bool audio)
{
 if(!g_getenv("PV_BOUNDED_CAMPAIGN")) fprintf(stderr,"FEED %c pts=%llu duration=%llu count=%u token=%llu\n",audio?'A':'V',(unsigned long long)r->timestamp_ns,(unsigned long long)r->duration_ns,r->audio_frames,(unsigned long long)q->token);
 if(g_getenv("PV_WHEP_APPROVED") && !g_getenv("PV_BOUNDED_CAMPAIGN")) fprintf(stderr,"ARRIVAL %c age_ns=%lld\n",audio?'A':'V',(long long)(os_gettime_ns()-r->timestamp_ns));
 /* Explicit negative fault: real elapsed blocking, never a fabricated clock. */
 if(!audio && g_getenv("PV_TEST_DOWNSTREAM_STALL")) {
  static unsigned videos;
  if(++videos==10) {
   fprintf(stderr,"TEST_DOWNSTREAM_STALL enter=%llu\n",(unsigned long long)os_gettime_ns());
   os_sleep_ms(650);
   fprintf(stderr,"TEST_DOWNSTREAM_STALL exit=%llu\n",(unsigned long long)os_gettime_ns());
  }
 }
 guint64 started=(guint64)g_get_monotonic_time()*1000;
 pv_feed_push(q,generation,r,audio);
 if(!audio) { guint64 v[]={r->timestamp_ns,started,(guint64)g_get_monotonic_time()*1000};pv_campaign_record(4,generation,v,G_N_ELEMENTS(v)); }
}
static void trace_request(struct pv_feed_queue *q,uint64_t generation,struct pv_feed_request *r)
{
 pv_feed_request(q,generation,r);
 if(g_getenv("PV_WHEP_APPROVED") && !g_getenv("PV_BOUNDED_CAMPAIGN") && r->command==PV_FEED_VIDEO && r->status==PV_FEED_OK) {
  guint32 first=GST_READ_UINT32_LE(r->data);
  fprintf(stderr,"POP_VIDEO pts=%llu y=%u drops=%llu remaining=%u\n",(unsigned long long)r->timestamp_ns,(first>>10)&1023,(unsigned long long)q->dropped_video,q->videos);
 }
}
#define pv_feed_request(...) trace_request(__VA_ARGS__)
static gint preview_released, preview_calls;
static void preview_video(obs_source_t *source, const struct obs_source_frame2 *frame)
{
 g_atomic_int_inc(&preview_calls);
 const char *mode = g_getenv("PV_WHEP_PREVIEW");

 if (mode && !strcmp(mode,"blocked"))
  while (!g_atomic_int_get(&preview_released)) g_usleep(1000);
 if (mode && !strcmp(mode,"nv12")) assert(frame->format == VIDEO_FORMAT_NV12);
 obs_source_output_video2(source,frame);
}
void pv_whep_preview_release(void) { g_atomic_int_set(&preview_released,1); }
void pv_whep_preview_still_blocked(void) {
 const char *mode=g_getenv("PV_WHEP_PREVIEW");
 if(mode && !strcmp(mode,"blocked")) assert(g_atomic_int_get(&preview_calls)==1 && !g_atomic_int_get(&preview_released));
}
void pv_whep_audit(void) {
 const char *mode=g_getenv("PV_WHEP_PREVIEW");
 if(mode && !strcmp(mode,"disabled")) assert(!g_atomic_int_get(&preview_calls));
 assert(!g_atomic_int_get(&video_decoders));
 assert(g_atomic_int_get(&decodebins)>=4 && g_atomic_int_get(&raw_decodebin_buffers)>30);
 fprintf(stderr,"RAW_PASSTHROUGH decodebin3=%d raw_input=%d video_decoders=%d\n",decodebins,raw_decodebin_buffers,video_decoders);
}
#define obs_source_output_video2 preview_video
#define pv_feed_push trace_push
#define pixelview_capability_probe_get test_probe
#define pixelview_profile_offer_caps_limited test_offer
#define gst_parse_launch offline_parse
#include "../../pixelview-whep/native-422-filter.h"
static GstPadProbeReturn conflicting_caps(GstPad *pad,GstPadProbeInfo *info,gpointer data)
{
 (void)pad;(void)data;GstEvent *event=GST_PAD_PROBE_INFO_EVENT(info);
 if(GST_EVENT_TYPE(event)==GST_EVENT_CAPS) {
  GstCaps *caps=NULL;gst_event_parse_caps(event,&caps);caps=gst_caps_copy(caps);
  gst_caps_set_simple(caps,"framerate",GST_TYPE_FRACTION,25,1,NULL);
  GstEvent *replacement=gst_event_new_caps(caps);gst_caps_unref(caps);
  gst_event_unref(event);GST_PAD_PROBE_INFO_DATA(info)=replacement;
  fprintf(stderr,"TEST_CAPS_CONFLICT injected=25/1 observed_rtp=24/1\n");
 }
 return GST_PAD_PROBE_OK;
}
static void test_require_rtp(GstElement *filter,GstElement *rx)
{
 if(g_getenv("PV_TEST_CAPS_CONFLICT")) {
  GstPad *pad=gst_element_get_static_pad(filter,"sink");assert(pad);
  gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,conflicting_caps,NULL,NULL);gst_object_unref(pad);
 }
 /* Historical small level255 precision fixture is NOT the new HD admission test. */
 if(g_getenv("PV_WHEP_APPROVED")) fprintf(stderr,"RTP_OBSERVER_ATTACHED %d\n",pv_native422_filter_require_rtp(filter,rx));
}
#define pv_native422_filter_require_rtp test_require_rtp
#include "../../pixelview-whep/pixelview-whep.c"
#undef pv_native422_filter_require_rtp
#undef gst_parse_launch
#undef pixelview_capability_probe_get
#undef pixelview_profile_offer_caps_limited
obs_source_t *pv_whep_create(const char *endpoint)
{
 gst_init(NULL,NULL);assert(obs_module_load());
 if(g_getenv("PV_TEST_DECODER_CONSTRUCTION")) {
  GstElement *bin=gst_pipeline_new("construction-negative");
  g_signal_connect(bin,"deep-element-added",G_CALLBACK(element_audit),NULL);
  GstElement *decoder=gst_element_factory_make("vtdec_hw","forbidden-preview");assert(decoder);
  gst_bin_add(GST_BIN(bin),decoder);abort();
 }
 obs_source_t *s=obs_source_create_private("pixelview_whep_source","test-only-main422",NULL);assert(s);
 const char *mode=g_getenv("PV_WHEP_PREVIEW");
 if(mode && !strcmp(mode,"disabled")) {
  calldata_t preview;calldata_init(&preview);calldata_set_bool(&preview,"enabled",false);
  assert(proc_handler_call(obs_source_get_proc_handler(s),"set_native_preview",&preview));calldata_free(&preview);
 }
 calldata_t cd;calldata_init(&cd);calldata_set_string(&cd,"endpoint",endpoint);calldata_set_int(&cd,"latency",50);
 assert(proc_handler_call(obs_source_get_proc_handler(s),"connect",&cd));calldata_free(&cd);return s;
}
void pv_whep_reconnect(obs_source_t *s, const char *endpoint)
{
 g_atomic_int_set(&preview_released,0);
 calldata_t cd;calldata_init(&cd);calldata_set_string(&cd,"endpoint",endpoint);calldata_set_int(&cd,"latency",50);
 assert(proc_handler_call(obs_source_get_proc_handler(s),"connect",&cd));calldata_free(&cd);
}
void pv_whep_status(obs_source_t *s, uint64_t *video, uint64_t *audio, bool *error)
{
 calldata_t cd;calldata_init(&cd);assert(proc_handler_call(obs_source_get_proc_handler(s),"get_status",&cd));
 *video=calldata_int(&cd,"native422_frames");*audio=calldata_int(&cd,"native_audio_frames");
 *error=!strcmp(calldata_string(&cd,"state"),"error");
 calldata_free(&cd);
}
void pv_whep_disconnect(obs_source_t *s)
{
 calldata_t cd;calldata_init(&cd);assert(proc_handler_call(obs_source_get_proc_handler(s),"disconnect",&cd));calldata_free(&cd);
}
/* Legacy owner fixture symbols are unused in this executable. */
obs_source_t *pv_owner_fixture_create(void) { abort(); }
void pv_owner_fixture_produce(obs_source_t *s,const char *path) { (void)s;(void)path;abort(); }
