/* SPDX-License-Identifier: GPL-2.0-or-later */
#define PIXELVIEW_WHEP_TEST
#include <obs-module.h>
#include <gst/gst.h>
#include <assert.h>
#include <stdio.h>
static gint delivered;
static uint64_t expected_pts;
static void capture_audio(obs_source_t *source,const struct obs_source_audio *audio)
{
 (void)source; assert(audio->timestamp==expected_pts);
 assert(audio->format==AUDIO_FORMAT_FLOAT && audio->frames==480);
 for(unsigned i=0;i<960;i++) assert(((float *)audio->data[0])[i]==0.25f);
 g_atomic_int_inc(&delivered);
}
static void clear_video(obs_source_t *s,const struct obs_source_frame *f) {(void)s;assert(!f);}
#define obs_source_output_audio capture_audio
#define obs_source_output_video clear_video
#include "../pixelview-whep.c"
static void send_pcm(struct receiver *r,GstPad *input,unsigned index)
{
 int before=g_atomic_int_get(&delivered);
 GstBuffer *b=gst_buffer_new_allocate(NULL,480*8,NULL);GstMapInfo map;
 assert(gst_buffer_map(b,&map,GST_MAP_WRITE));
 for(unsigned i=0;i<960;i++) ((float *)map.data)[i]=0.25f;
 gst_buffer_unmap(b,&map);
 GST_BUFFER_PTS(b)=2*GST_SECOND+500*GST_MSECOND+index*10*GST_MSECOND;GST_BUFFER_DURATION(b)=10*GST_MSECOND;
 expected_pts=gst_element_get_base_time(r->pipe)+500*GST_MSECOND+index*10*GST_MSECOND;
 assert(gst_pad_chain(input,b)==GST_FLOW_OK);
 gint64 end=g_get_monotonic_time()+2000000;
 while(g_atomic_int_get(&delivered)==before && g_get_monotonic_time()<end) g_usleep(1000);
 assert(g_atomic_int_get(&delivered)==before+1);
}
static void test_profile(const char *profile)
{
 struct receiver r={.generation=1,.active_generation=1,.accept_samples=true,
  .active_caps={.profiles=PV_PROFILE_HEVC_MAIN|PV_PROFILE_HEVC_MAIN10|PV_PROFILE_HEVC_MAIN422_10,.hevc_level_id=123}};
 g_mutex_init(&r.lock);g_rec_mutex_init(&r.delivery);g_cond_init(&r.wake);
 r.pipe=make_pipeline(&r,"http://127.0.0.1:9/whep");assert(r.pipe);
 GstElement *rx=gst_bin_get_by_name(GST_BIN(r.pipe),"rx");
 GstCaps *raw=gst_caps_from_string("video/x-raw,format=P010_10LE");
 GstElement *filter=request_encoded_filter(rx,"fixture","video_0",raw,r.attempt);assert(filter);gst_caps_unref(raw);
 GstElement *selector=gst_pipeline_new(NULL),*fake=gst_element_factory_make("fakesink",NULL);
 g_object_set(fake,"sync",FALSE,"async",FALSE,NULL);
 gst_bin_add_many(GST_BIN(selector),filter,fake,NULL);assert(gst_element_link(filter,fake));gst_object_unref(filter);
 gst_element_set_state(selector,GST_STATE_PLAYING);
 GstPad *video=gst_element_get_static_pad(filter,"sink");
 /* Remove transport/video before PLAYING: production audio graph only, no HTTP,
  * ICE, decoder or device. All converter/queue/appsink properties stay intact. */
 const char *remove[]={"rx","video","video-policy"};
 for(unsigned i=0;i<3;i++) {GstElement *e=gst_bin_get_by_name(GST_BIN(r.pipe),remove[i]);assert(e);gst_bin_remove(GST_BIN(r.pipe),e);gst_object_unref(e);}
 gst_object_unref(rx);
 GstElement *audio=gst_bin_get_by_name(GST_BIN(r.pipe),"audio");gboolean sync=false;g_object_get(audio,"sync",&sync,NULL);assert(sync);gst_object_unref(audio);
 GstElement *entry=gst_bin_get_by_name(GST_BIN(r.pipe),"audio-input");GstPad *input=gst_element_get_static_pad(entry,"sink");gst_object_unref(entry);
 GstClock *clock=gst_system_clock_obtain();gst_pipeline_use_clock(GST_PIPELINE(r.pipe),clock);
 gst_element_set_start_time(r.pipe,GST_CLOCK_TIME_NONE);gst_element_set_base_time(r.pipe,gst_clock_get_time(clock));gst_object_unref(clock);
 assert(gst_element_set_state(r.pipe,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 assert(gst_pad_send_event(input,gst_event_new_stream_start("pcm")));
 GstCaps *caps=gst_caps_from_string("audio/x-raw,format=F32LE,layout=interleaved,channels=2,rate=48000");
 assert(gst_pad_send_event(input,gst_event_new_caps(caps)));gst_caps_unref(caps);
 GstSegment seg;gst_segment_init(&seg,GST_FORMAT_TIME);seg.start=2*GST_SECOND;seg.position=seg.start;
 assert(gst_pad_send_event(input,gst_event_new_segment(&seg)));
 send_pcm(&r,input,0);
 assert(gst_pad_send_event(video,gst_event_new_stream_start("selected")));
 caps=gst_caps_new_simple("video/x-h265","profile",G_TYPE_STRING,profile,"stream-format",G_TYPE_STRING,"hvc1","alignment",G_TYPE_STRING,"au",NULL);
 assert(gst_pad_send_event(video,gst_event_new_caps(caps)));gst_caps_unref(caps);
 /* The selected video profile never changes the audio route. */
 for(unsigned i=1;i<8;i++) send_pcm(&r,input,i);
 g_mutex_lock(&r.lock);
 assert(r.audio_frames==8*480);
 g_mutex_unlock(&r.lock);
 gst_object_unref(input);gst_object_unref(video);
 stop_pipeline(&r);
 gst_element_set_state(selector,GST_STATE_NULL);gst_object_unref(selector);
 g_cond_clear(&r.wake);g_rec_mutex_clear(&r.delivery);g_mutex_clear(&r.lock);
 printf("PASS %s: production single audio graph, exact OBS PCM/segment PTS and teardown\n",profile);
}
/* A fragmented oversized buffer is refused on its advertised size, before a
 * READ map could coalesce it. */
static void oversized_refusal(void)
{
 const size_t n=(PV_MAX_AUDIO_FRAMES+1)*8u;
 GstBuffer *b=gst_buffer_new();
 gst_buffer_append_memory(b,gst_allocator_alloc(NULL,n/2,NULL));
 gst_buffer_append_memory(b,gst_allocator_alloc(NULL,n-n/2,NULL));
 GstMemory *first=gst_buffer_peek_memory(b,0),*second=gst_buffer_peek_memory(b,1);
 GstCaps *caps=gst_caps_from_string("audio/x-raw,format=F32LE,layout=interleaved,channels=2,rate=48000");
 GstSegment segment;gst_segment_init(&segment,GST_FORMAT_TIME);GST_BUFFER_PTS(b)=0;
 GstSample *sample=gst_sample_new(b,caps,&segment,NULL);gst_buffer_unref(b);gst_caps_unref(caps);
 GstAudioInfo info;GstMapInfo mapped;
 assert(!map_audio(sample,&info,&mapped));
 b=gst_sample_get_buffer(sample);
 assert(gst_buffer_n_memory(b)==2 && gst_buffer_peek_memory(b,0)==first && gst_buffer_peek_memory(b,1)==second);
 gst_sample_unref(sample);
 puts("PASS fragmented oversized PCM refused before coalescing/map");
}
int main(void) {gst_init(NULL,NULL);test_profile("main");test_profile("main-10");test_profile("main-422-10");oversized_refusal();}
