/* SPDX-License-Identifier: GPL-2.0-or-later */
#define PIXELVIEW_WHEP_TEST 1
#include "../pixelview-whep.c"
#include "../runtime.h"
#include <gst/app/gstappsrc.h>
#include <assert.h>
#include <stdio.h>
static void check_codec(obs_source_t *source, const char *codec, bool audio)
{
 char spec[1024];
 snprintf(spec,sizeof(spec),"appsrc name=input format=time ! %s ! %s ! %s ! appsink name=output sync=false",
 audio ? "audio/x-raw,format=F32LE,rate=48000,channels=2,layout=interleaved" : "video/x-raw,format=NV12,colorimetry=bt709,width=320,height=180,framerate=30/1",
 audio ? "audioconvert ! opusenc ! opusdec ! audioconvert ! audio/x-raw,format=F32LE,layout=interleaved,channels=2" :
 !strcmp(codec,"H264") ? "vtenc_h264 realtime=true ! h264parse ! vtdec" :
 "vtenc_h265 realtime=true ! h265parse ! vtdec",
 audio ? "identity" : g_getenv("PIXELVIEW_TEST_P010") ? PIXELVIEW_RECEIVE_RAW_CAPS : "video/x-raw,format=NV12");
 GError *error=NULL;
 struct receiver r={.source=source,.accept_samples=true}; g_mutex_init(&r.lock); g_rec_mutex_init(&r.delivery);
 r.pipe=gst_parse_launch(spec,&error); assert(r.pipe && !error);
 GstElement *input=gst_bin_get_by_name(GST_BIN(r.pipe),"input");
 GstElement *output=gst_bin_get_by_name(GST_BIN(r.pipe),"output");
 GstAppSinkCallbacks cb={0};cb.new_sample=audio?audio_sample:video_sample;
 gst_app_sink_set_callbacks(GST_APP_SINK(output),&cb,&r,NULL);
 assert(gst_element_set_state(r.pipe,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 for(int i=0;i<30;i++) {
  size_t size=audio?960*2*sizeof(float):320*180*3/2;
  GstBuffer *b=gst_buffer_new_allocate(NULL,size,NULL);GstMapInfo map;assert(gst_buffer_map(b,&map,GST_MAP_WRITE));
  memset(map.data,audio?0:(32+i*4),map.size);gst_buffer_unmap(b,&map);
  GST_BUFFER_PTS(b)=i*(audio?20*GST_MSECOND:GST_SECOND/30);
  GST_BUFFER_DURATION(b)=audio?20*GST_MSECOND:GST_SECOND/30;
  assert(gst_app_src_push_buffer(GST_APP_SRC(input),b)==GST_FLOW_OK);
 }
 gst_app_src_end_of_stream(GST_APP_SRC(input));
 GstBus *bus=gst_element_get_bus(r.pipe);
 GstMessage *msg=gst_bus_timed_pop_filtered(bus,20*GST_SECOND,GST_MESSAGE_EOS|GST_MESSAGE_ERROR);
 assert(msg && GST_MESSAGE_TYPE(msg)==GST_MESSAGE_EOS);
 gst_element_set_state(r.pipe,GST_STATE_NULL);
 printf("PASS bundled %s encode/decode -> native OBS raw path: video=%llu audio=%llu\n",codec,(unsigned long long)r.frames,(unsigned long long)r.audio_frames);
 assert(audio?r.audio_frames>0:r.frames>0);
 gst_message_unref(msg);gst_object_unref(bus);gst_object_unref(input);gst_object_unref(output);gst_object_unref(r.pipe);g_mutex_clear(&r.lock); g_rec_mutex_clear(&r.delivery);
}
int main(void)
{
 assert(pixelview_gst_init());assert(obs_startup("en-US",NULL,NULL));
 struct obs_audio_info ai={.samples_per_sec=48000,.speakers=SPEAKERS_STEREO};assert(obs_reset_audio(&ai));
 assert(obs_module_load());obs_source_t *source=obs_source_create_private("pixelview_whep_source","codec-test",NULL);assert(source);
 check_codec(source,"H264",false);check_codec(source,"HEVC",false);check_codec(source,"Opus",true);
 obs_source_release(source);obs_shutdown();
}
