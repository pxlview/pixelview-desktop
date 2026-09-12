/* Synthetic loopback acceptance: actual source procs/worker/dynamic pads.
 * Test-only network isolation and OBS delivery audit; no production graph edits. */
#define GST_USE_UNSTABLE_API
#define PIXELVIEW_WHEP_TEST 1
#include <obs-module.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/webrtc/webrtc.h>
#include <nice/agent.h>
#include <math.h>
#include <stdio.h>
static GMutex audit_lock;
static unsigned videos, codes;
static bool seen[1024];
static double audio_energy;
static void audit_video(obs_source_t *s,const struct obs_source_frame2 *f)
{
 g_assert_cmpint(f->format,==,VIDEO_FORMAT_P010);
 g_assert_cmpint(f->range,==,VIDEO_RANGE_PARTIAL);
 g_assert_cmpint(f->trc,==,VIDEO_TRC_DEFAULT);
 g_mutex_lock(&audit_lock);
 if (!videos++) for(unsigned y=0;y<f->height;y++) {
  const uint16_t *p=(const uint16_t *)(f->data[0]+y*f->linesize[0]);
  for(unsigned x=0;x<f->width;x++) {unsigned c=p[x]>>6;if(!seen[c]){seen[c]=true;codes++;}}
 }
 g_mutex_unlock(&audit_lock);
 obs_source_output_video2(s,f);
}
static void audit_audio(obs_source_t *s,const struct obs_source_audio *a)
{
 const float *p=(const float *)a->data[0];double energy=0;
 for(unsigned i=0;i<a->frames*2;i++) energy+=p[i]*p[i];
 g_mutex_lock(&audit_lock);audio_energy+=energy;g_mutex_unlock(&audit_lock);
 obs_source_output_audio(s,a);
}
static void offline_ready(GObject *s,const char *peer,GstElement *rtc,gpointer data)
{
 (void)s;(void)peer;(void)data;
 GObject *ice=NULL;NiceAgent *agent=NULL;g_object_get(rtc,"ice-agent",&ice,NULL);
 g_object_get(ice,"agent",&agent,NULL);g_assert_nonnull(agent);
 NiceAddress address;nice_address_init(&address);g_assert_true(nice_address_set_from_string(&address,"127.0.0.1"));
 g_assert_true(nice_agent_add_local_address(agent,&address));
 g_object_set(agent,"ice-tcp",FALSE,NULL);g_object_unref(agent);g_object_unref(ice);
}
static GstBusSyncReply bus_audit(GstBus *bus,GstMessage *msg,gpointer data)
{
 (void)bus;(void)data;
 if(GST_MESSAGE_TYPE(msg)==GST_MESSAGE_ERROR){GError *e=NULL;char *d=NULL;gst_message_parse_error(msg,&e,&d);fprintf(stderr,"LOOPBACK_ERROR %s %s\n",e->message,d?d:"");g_clear_error(&e);g_free(d);}
 return GST_BUS_PASS;
}
static GstElement *offline_parse(const char *text,GError **error)
{
 GstElement *pipe=gst_parse_launch(text,error);if(!pipe)return NULL;
 GstElement *rx=gst_bin_get_by_name(GST_BIN(pipe),"rx");
 if(rx){GObject *s=NULL;g_object_set(rx,"stun-server",NULL,NULL);g_object_get(rx,"signaller",&s,NULL);
 g_signal_connect(s,"webrtcbin-ready",G_CALLBACK(offline_ready),NULL);g_object_unref(s);gst_object_unref(rx);}
 GstBus *bus=gst_element_get_bus(pipe);gst_bus_set_sync_handler(bus,bus_audit,NULL,NULL);gst_object_unref(bus);
 return pipe;
}
static GstSample *audit_pull(GstAppSink *sink)
{
 GstSample *sample=gst_app_sink_pull_sample(sink);
 static bool printed;
 if(sample&&!printed&&!strcmp(GST_OBJECT_NAME(sink),"video")){
  printed=true;char *c=gst_caps_to_string(gst_sample_get_caps(sample));fprintf(stderr,"RAW_VIDEO %s\n",c);g_free(c);
  GstObject *parent=gst_object_get_parent(GST_OBJECT(sink));g_assert_true(GST_IS_BIN(parent));
  GstElement *tap=gst_bin_get_by_name(GST_BIN(parent),"native-transform");g_assert_null(tap);
  GstElement *queue=gst_bin_get_by_name(GST_BIN(parent),"preview");g_assert_null(queue);
  GstPad *input=gst_element_get_static_pad(GST_ELEMENT(sink),"sink");GstPad *peer=gst_pad_get_peer(input);
  GstElement *upstream=gst_pad_get_parent_element(peer);
  g_assert_cmpstr(GST_OBJECT_NAME(upstream),==,"video-policy");
  gst_object_unref(upstream);gst_object_unref(peer);gst_object_unref(input);
  GstIterator *it=gst_bin_iterate_recurse(GST_BIN(parent));GValue value=G_VALUE_INIT;bool decoder=false;
  while(gst_iterator_next(it,&value)==GST_ITERATOR_OK){
   GstElement *element=g_value_get_object(&value);GstElementFactory *factory=gst_element_get_factory(element);
   if(factory && gst_element_factory_list_is_type(factory,GST_ELEMENT_FACTORY_TYPE_DECODER | GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO)) {
    fprintf(stderr,"ACTUAL_VIDEO_DECODER %s\n",gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)));decoder=true;
   }
   g_value_reset(&value);
  }
  g_value_unset(&value);gst_iterator_free(it);gst_object_unref(parent);g_assert_true(decoder);
  fprintf(stderr,"ORDINARY_GRAPH no-native-tap no-native-queue direct-clocked-appsink\n");
 }
 return sample;
}
#define gst_app_sink_pull_sample audit_pull
#define gst_parse_launch offline_parse
#define obs_source_output_video2 audit_video
#define obs_source_output_audio audit_audio
#include "../pixelview-whep.c"
#undef gst_parse_launch
#undef obs_source_output_video2
#undef obs_source_output_audio
int main(int argc,char **argv)
{
 g_assert_cmpint(argc,==,3);setbuf(stdout,NULL);gst_init(NULL,NULL);g_mutex_init(&audit_lock);
 g_assert_true(obs_startup("en-US",NULL,NULL));struct obs_audio_info ai={.samples_per_sec=48000,.speakers=SPEAKERS_STEREO};g_assert_true(obs_reset_audio(&ai));g_assert_true(obs_module_load());
 obs_source_t *source=obs_source_create_private("pixelview_whep_source","isolated-loopback",NULL);g_assert_nonnull(source);
 proc_handler_t *ph=obs_source_get_proc_handler(source);calldata_t cd;calldata_init(&cd);calldata_set_string(&cd,"endpoint",argv[1]);calldata_set_int(&cd,"latency",50);g_assert_true(proc_handler_call(ph,"connect",&cd));
 bool negative=!strcmp(argv[2],"negative"),passed=false;uint64_t frames=0,audio=0;int jitter=-1;
 for(unsigned i=0;i<180;i++){
  g_usleep(100000);g_assert_true(proc_handler_call(ph,"get_status",&cd));
  frames=calldata_int(&cd,"frames");audio=calldata_int(&cd,"audio_frames");jitter=(int)calldata_int(&cd,"jitter_latency");
  if(!strcmp(calldata_string(&cd,"state"),"error")){passed=negative&&!frames&&!audio;break;}
  if(!negative&&frames>=30&&audio>=24000){passed=true;break;}
 }
 g_assert_true(proc_handler_call(ph,"disconnect",&cd));obs_source_release(source);obs_wait_for_destroy_queue();
 g_mutex_lock(&audit_lock);printf("LOOPBACK_RESULT frames=%llu audio=%llu jitter=%d audited=%u codes=%u energy=%g negative=%d passed=%d\n",(unsigned long long)frames,(unsigned long long)audio,jitter,videos,codes,audio_energy,negative,passed);
 if(!negative)passed=passed&&jitter==50&&videos>=30&&audio_energy>1&&codes>(!strcmp(argv[2],"10")?256u:0u);
 g_mutex_unlock(&audit_lock);calldata_free(&cd);obs_shutdown();g_mutex_clear(&audit_lock);return passed?0:1;
}
