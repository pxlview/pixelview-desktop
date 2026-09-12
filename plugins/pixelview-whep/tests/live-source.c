/* SPDX-License-Identifier: GPL-2.0-or-later
 * Live production pipeline probe. Credentials arrive on stdin, never argv.
 * Raw bus diagnostics MUST be captured privately by live-source.py. */
#define PIXELVIEW_WHEP_TEST 1
#include "../pixelview-whep.c"
#include "../runtime.h"
#include <assert.h>
#include <stdio.h>
static void inspect_transceiver(GstElement *rtc, GObject *transceiver, gpointer data)
{
 (void)rtc; (void)data;
 GstCaps *caps=NULL; g_object_get(transceiver,"codec-preferences",&caps,NULL);
 if(caps) { char *text=gst_caps_to_string(caps); fprintf(stderr,"CODEC_CAPS %s\n",text); g_free(text); gst_caps_unref(caps); }
}
static void inspect_ready(GObject *signaller, const char *peer, GstElement *rtc, gpointer data)
{
 (void)signaller; (void)peer; (void)data;
 g_signal_connect(rtc,"on-new-transceiver",G_CALLBACK(inspect_transceiver),NULL);
}
int main(void)
{
 char endpoint[16386]; assert(fgets(endpoint,sizeof(endpoint),stdin));
 endpoint[strcspn(endpoint,"\r\n")]=0;
 assert(pixelview_gst_init()); assert(obs_startup("en-US",NULL,NULL));
 struct obs_audio_info ai={.samples_per_sec=48000,.speakers=SPEAKERS_STEREO}; assert(obs_reset_audio(&ai));
 assert(obs_module_load());
 obs_source_t *source=obs_source_create_private("pixelview_whep_source","live-test",NULL); assert(source);
 struct receiver r={.source=source,.accept_samples=true,.latency=50,.active_latency_override=true,.active_latency=50,.jitter_latency=-1};
 g_mutex_init(&r.lock); g_rec_mutex_init(&r.delivery);
 r.pipe=make_pipeline(&r,endpoint); memset(endpoint,0,sizeof(endpoint)); assert(r.pipe);
 GstElement *rx=gst_bin_get_by_name(GST_BIN(r.pipe),"rx"); GObject *signaller=NULL;
 g_object_get(rx,"signaller",&signaller,NULL);
 g_signal_connect(signaller,"webrtcbin-ready",G_CALLBACK(inspect_ready),NULL);
 g_object_unref(signaller); gst_object_unref(rx);
 GstBus *bus=gst_element_get_bus(r.pipe);
 bool failed=gst_element_set_state(r.pipe,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE;
 for(int i=0;!failed && i<300;i++) {
  GstMessage *msg=gst_bus_timed_pop_filtered(bus,100*GST_MSECOND,GST_MESSAGE_ERROR|GST_MESSAGE_EOS);
  if(msg) {
   if(GST_MESSAGE_TYPE(msg)==GST_MESSAGE_ERROR) {
    GError *e=NULL; gchar *debug=NULL; gst_message_parse_error(msg,&e,&debug);
    fprintf(stderr,"BUS_ERROR source=%s domain=%s code=%d message=%s debug=%s\n",GST_OBJECT_NAME(msg->src),g_quark_to_string(e->domain),e->code,e->message,debug?debug:"");
    g_clear_error(&e); g_free(debug);
   }
   gst_message_unref(msg); failed=true;
  }
  g_mutex_lock(&r.lock); bool enough=r.frames>=60 && r.audio_frames>=48000; g_mutex_unlock(&r.lock);
  if(enough) break;
 }
 gst_element_set_state(r.pipe,GST_STATE_NULL);
 printf("LIVE_RESULT video=%llu audio=%llu jitter=%d failed=%d\n",(unsigned long long)r.frames,(unsigned long long)r.audio_frames,r.jitter_latency,failed);
 bool passed=!failed && r.frames>=60 && r.audio_frames>=48000 && r.jitter_latency==50;
 gst_object_unref(bus); gst_object_unref(r.pipe); g_rec_mutex_clear(&r.delivery); g_mutex_clear(&r.lock);
 obs_source_release(source); obs_shutdown(); return passed?0:1;
}
