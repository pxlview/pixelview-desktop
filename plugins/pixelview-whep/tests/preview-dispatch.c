/* SPDX-License-Identifier: GPL-2.0-or-later
 * Compile the actual worker; barriers wrap native boundaries only in this TU. */
#define PIXELVIEW_WHEP_TEST
#include <obs-module.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include "../video-format.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
static GMutex gate;
static GCond cond;
static gint boundary;
static bool entered, released, finished;
static unsigned outputs;
static bool sequence;
static bool destroying, destroy_join_entered, destroyed;
static GThread *joined_preview;
static bool preview_join_entered;
static gpointer held_join(GThread *thread);
static unsigned events[64], event_count;
static _Thread_local GstSample *injected;
static _Thread_local bool injecting;
static bool invalid_color;
static GstSample *pull_injected(GstAppSink *sink);
static void record(unsigned value)
{
 g_mutex_lock(&gate); assert(event_count<64); events[event_count++]=value;
 g_cond_broadcast(&cond); g_mutex_unlock(&gate);
}
static _Thread_local bool prepared;
static void pause_at(int point)
{
 if (g_atomic_int_get(&boundary) != point || injecting) return;
 g_mutex_lock(&gate); entered=true; g_cond_broadcast(&cond);
 while (!released) g_cond_wait(&cond,&gate);
 g_mutex_unlock(&gate);
}
static bool held_info(GstCaps *caps,GstVideoInfo *info)
{ pause_at(1); return pixelview_video_info(caps,info); }
static bool held_frame(GstVideoFrame *mapped,struct obs_source_frame2 *frame)
{ bool ok=pixelview_video_frame(mapped,frame); pause_at(2); prepared=ok&&!injecting; return ok; }
static void capture_video(obs_source_t *s,const struct obs_source_frame2 *f)
{
 (void)s; assert(f); pause_at(4);
 if (!sequence) { outputs++; return; }
 assert(f->range==VIDEO_RANGE_PARTIAL && f->trc==VIDEO_TRC_DEFAULT);
 float matrix[16],lo[3],hi[3];
 assert(video_format_get_parameters_for_format(VIDEO_CS_709,f->range,f->format,matrix,lo,hi));
 assert(!memcmp(matrix,f->color_matrix,sizeof matrix));
 assert(!memcmp(lo,f->color_range_min,sizeof lo) && !memcmp(hi,f->color_range_max,sizeof hi));
 if(f->format==VIDEO_FORMAT_P010) {
  assert(f->width==1024);
  for(unsigned x=0;x<877;x++) assert(GST_READ_UINT16_LE(f->data[0]+x*2)==(x+64)*64);
 } else assert(f->format==VIDEO_FORMAT_NV12 && f->data[0][0]==42);
 record((unsigned)f->timestamp);
}
static void capture_clear(obs_source_t *s,const struct obs_source_frame *f)
{ (void)s; assert(!f); pause_at(5); if(sequence) record(0); }
static void held_unlock(GMutex *mutex);
static void done_unmap(GstVideoFrame *mapped)
{
 gst_video_frame_unmap(mapped);
 g_mutex_lock(&gate); finished=true; g_cond_broadcast(&cond); g_mutex_unlock(&gate);
}
#define pixelview_video_info held_info
#define pixelview_video_frame held_frame
#define obs_source_output_video2 capture_video
#define obs_source_output_video capture_clear
#define g_mutex_unlock held_unlock
#define gst_video_frame_unmap done_unmap
#define gst_app_sink_pull_sample pull_injected
#define g_thread_join held_join
#include "../pixelview-whep.c"
#undef g_thread_join
#undef gst_app_sink_pull_sample
#undef g_mutex_unlock
#undef gst_video_frame_unmap
static GstSample *pull_injected(GstAppSink *sink)
{ if(injecting) { GstSample *s=injected; injected=NULL; return s; } return gst_app_sink_pull_sample(sink); }
static struct receiver *tested;
static gpointer held_join(GThread *thread)
{
 if(thread==joined_preview) {
  g_mutex_lock(&gate);preview_join_entered=true;g_cond_broadcast(&cond);g_mutex_unlock(&gate);
 }
 if(destroying && thread==joined_preview) {
  g_mutex_lock(&gate); destroy_join_entered=true; g_cond_broadcast(&cond); g_mutex_unlock(&gate);
 }
 return g_thread_join(thread);
}
static void held_unlock(GMutex *mutex)
{
 /* After reservation unlock, before the first instruction of OBS. */
 bool reserved=false;
#ifdef TEST_DISPATCH_RESERVATION
 reserved=prepared && tested && mutex==&tested->lock;
 if (reserved) prepared=false;
#endif
 g_mutex_unlock(mutex);
 if (reserved) pause_at(3);
}
static void run_case(int point,int cancel)
{
 g_atomic_int_set(&boundary,point); entered=released=finished=false; outputs=0;
 struct receiver r={.generation=1,.active_generation=1,.accept_samples=true,
  .preview_enabled=true,.preview_native=true,.preview_generation=1,.preview_timestamp=100,.preview_clear=point==5};
 tested=&r; g_mutex_init(&r.lock); g_rec_mutex_init(&r.delivery); g_cond_init(&r.wake);
 GstCaps *caps=gst_caps_from_string("video/x-raw,format=NV12,width=4,height=4,framerate=30/1,colorimetry=bt709,chroma-site=mpeg2,interlace-mode=progressive");
 GstBuffer *buffer=gst_buffer_new_allocate(NULL,24,NULL);
 r.preview_sample=gst_sample_new(buffer,caps,NULL,NULL); gst_buffer_unref(buffer); gst_caps_unref(caps);
 g_mutex_lock(&r.lock);
 r.preview_thread=g_thread_new("test-preview",preview_worker,&r);
 g_mutex_unlock(&r.lock);
 g_mutex_lock(&gate); while(!entered)g_cond_wait(&cond,&gate); g_mutex_unlock(&gate);
 assert(outputs==0);
 uint64_t start=os_gettime_ns();
 if(cancel==0) {
  disconnect_proc(&r,NULL);
  calldata_t cd; calldata_init(&cd); calldata_set_string(&cd,"endpoint","http://127.0.0.1:9/whep");
  connect_proc(&r,&cd); calldata_free(&cd);
  g_mutex_lock(&r.lock); r.active_generation=r.generation; r.changed=false; r.accept_samples=true; g_mutex_unlock(&r.lock);
 } else if(cancel==1) {
  calldata_t cd; calldata_init(&cd); calldata_set_bool(&cd,"enabled",false);
  native_preview_proc(&r,&cd); calldata_free(&cd);
 } else {
  g_mutex_lock(&r.lock);
  if(cancel==2) r.accept_samples=false;
  if(cancel==3) r.quit=true;
  if(cancel==4) r.active_generation++;
  if(cancel==5) r.changed=true;
  g_mutex_unlock(&r.lock);
 }
 assert(os_gettime_ns()-start<500000000ULL);
 g_mutex_lock(&gate); released=true; g_cond_broadcast(&cond);
 while(!finished)g_cond_wait(&cond,&gate); g_mutex_unlock(&gate);
 g_mutex_lock(&r.lock); r.quit=true; g_cond_broadcast(&r.wake); g_mutex_unlock(&r.lock);
 g_thread_join(r.preview_thread);
 unsigned expected=(point==3 || point==4) ? 1 : 0;
 printf("boundary=%d cancel=%d outputs=%u expected=%u generation=%llu counter=%llu\n",point,cancel,outputs,expected,(unsigned long long)r.generation,(unsigned long long)r.frames); fflush(stdout);
 assert(outputs==expected);
 if(cancel==0)assert(r.frames==0);
 wipe(&r.endpoint); pv_feed_reset(&r.feed); tested=NULL;
 g_cond_clear(&r.wake); g_rec_mutex_clear(&r.delivery); g_mutex_clear(&r.lock);
}
static void submit(struct receiver *r,bool native,unsigned identity)
{
 GstCaps *caps=gst_caps_from_string(native?
  "video/x-raw,format=NV12,width=4,height=4,framerate=30/1,colorimetry=bt709,chroma-site=mpeg2,interlace-mode=progressive,pixelview-native422=true":
  "video/x-raw,format=P010_10LE,width=1024,height=4,framerate=30/1,colorimetry=bt709,chroma-site=mpeg2,interlace-mode=progressive");
 GstVideoInfo info; assert(gst_video_info_from_caps(&info,caps));
 if(invalid_color) {
  info.colorimetry.range=GST_VIDEO_COLOR_RANGE_0_255;
  char *color=gst_video_colorimetry_to_string(&info.colorimetry);
  gst_caps_set_simple(caps,"colorimetry",G_TYPE_STRING,color,NULL); g_free(color);
 }
 GstBuffer *buffer=gst_buffer_new_allocate(NULL,info.size,NULL); GstMapInfo map;
 assert(gst_buffer_map(buffer,&map,GST_MAP_WRITE)); memset(map.data,42,map.size);
 if(!native) for(unsigned x=0;x<877;x++) GST_WRITE_UINT16_LE(map.data+x*2,(x+64)*64);
 gst_buffer_unmap(buffer,&map); GST_BUFFER_PTS(buffer)=identity;
 GstSegment segment; gst_segment_init(&segment,GST_FORMAT_TIME);
 injected=gst_sample_new(buffer,caps,&segment,NULL); gst_buffer_unref(buffer); gst_caps_unref(caps);
 injecting=true; GstFlowReturn result=video_sample(NULL,r); injecting=false;
 if(invalid_color) { printf("ordinary full-range callback result=%d expected rejection\n",result); fflush(stdout); assert(result!=GST_FLOW_OK); }
 else assert(result==GST_FLOW_OK);
}
static void reconnect(struct receiver *r)
{
 uint64_t start=os_gettime_ns(); disconnect_proc(r,NULL); stop_pipeline(r);
 calldata_t cd; calldata_init(&cd); calldata_set_string(&cd,"endpoint","http://127.0.0.1:9/whep");
 connect_proc(r,&cd); calldata_free(&cd);
 r->pipe=gst_pipeline_new(NULL); gst_element_set_base_time(r->pipe,0);
 g_mutex_lock(&r->lock); r->active_generation=r->generation; r->changed=false; r->accept_samples=true; g_mutex_unlock(&r->lock);
 assert(os_gettime_ns()-start<500000000ULL);
}
static void wait_events(unsigned count)
{ g_mutex_lock(&gate); while(event_count<count)g_cond_wait(&cond,&gate); g_mutex_unlock(&gate); }
static bool reconnected;
static gpointer reconnect_thread(gpointer opaque)
{
 reconnect(opaque);
 g_mutex_lock(&gate);reconnected=true;g_cond_broadcast(&cond);g_mutex_unlock(&gate);
 return NULL;
}
static void mixed_case(int point)
{
 sequence=true;g_atomic_int_set(&boundary,point);entered=released=finished=false;event_count=0;
 struct receiver r={.generation=1,.active_generation=1,.accept_samples=true,.preview_enabled=true};
 tested=&r;g_mutex_init(&r.lock);g_rec_mutex_init(&r.delivery);g_cond_init(&r.wake);
 r.pipe=gst_pipeline_new(NULL);gst_element_set_base_time(r.pipe,0);
 submit(&r,true,1);
 g_mutex_lock(&gate);while(!entered)g_cond_wait(&cond,&gate);reconnected=false;g_mutex_unlock(&gate);
 joined_preview=r.preview_thread;preview_join_entered=false;
 GThread *transition=g_thread_new("new-attempt",reconnect_thread,&r);
 /* Teardown must wait for reserved native OBS delivery before enabling direct
  * ordinary callbacks. Stop/connect procs themselves still only invalidate. */
 g_mutex_lock(&gate);while(!preview_join_entered)g_cond_wait(&cond,&gate);
 assert(!reconnected && "new generation started before old native worker drained");
 released=true;g_cond_broadcast(&cond);g_mutex_unlock(&gate);
 g_thread_join(transition);assert(!r.preview_thread && !r.preview_sample);
 submit(&r,false,3);assert(!r.preview_thread && !r.preview_sample);
 assert(event_count==3 && events[0]==1 && events[1]==0 && events[2]==3);
 for(unsigned i=0;i<4;i++) {
  unsigned before=event_count;reconnect(&r);submit(&r,i%2==0,5+i*2);wait_events(before+2);
  assert(events[before]==0 && events[before+1]==5+i*2);
  if(i%2) assert(!r.preview_thread && !r.preview_sample);
 }
 disconnect_proc(&r,NULL);stop_pipeline(&r);wipe(&r.endpoint);
 tested=NULL;g_cond_clear(&r.wake);g_rec_mutex_clear(&r.delivery);g_mutex_clear(&r.lock);
 sequence=false;puts("native join -> NULL clear -> direct ordinary, repeated reverse transitions passed");
}

static gpointer empty_worker(gpointer data) { stop_pipeline(data); return NULL; }
static gpointer destroy_test(gpointer data)
{
 destroy(data); g_mutex_lock(&gate); destroyed=true; g_cond_broadcast(&cond); g_mutex_unlock(&gate); return NULL;
}
static void ordinary_lifetime(void)
{
 sequence=true; g_atomic_int_set(&boundary,0); released=true; event_count=0;
 struct receiver *r=g_new0(struct receiver,1);
 r->generation=r->active_generation=1; r->accept_samples=r->preview_enabled=true;
 g_mutex_init(&r->lock); g_rec_mutex_init(&r->delivery); g_cond_init(&r->wake); tested=r;
 struct obs_source_info si={.id="dispatch-owner",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_ASYNC_VIDEO,.get_name=source_name};
 obs_register_source(&si); r->source=obs_source_create_private("dispatch-owner","dispatch-owner",NULL); assert(r->source);
 obs_source_t *source=r->source; source_controls_init(r);
 r->pipe=gst_pipeline_new(NULL); gst_element_set_base_time(r->pipe,0);
 /* First ordinary frame remains the legacy synchronous path, then native creates
  * the sole worker. This is a real reverse transition, not a generation edit. */
 submit(r,false,1); assert(!r->preview_thread); reconnect(r); submit(r,true,3); wait_events(3);
 invalid_color=true; submit(r,false,99); invalid_color=false;
 reconnect(r); wait_events(4);
 g_mutex_lock(&gate); g_atomic_int_set(&boundary,4); entered=released=false; g_mutex_unlock(&gate);
 submit(r,true,5);
 g_mutex_lock(&gate); while(!entered)g_cond_wait(&cond,&gate); g_mutex_unlock(&gate);
 /* Only native preview replaces pending RAW; disabling it clears that work. */
 for(unsigned i=6;i<20;i++) submit(r,true,i);
 g_mutex_lock(&r->lock);assert(r->preview_sample && r->preview_timestamp==19 && r->preview_native);g_mutex_unlock(&r->lock);
 calldata_t cd; calldata_init(&cd); calldata_set_bool(&cd,"enabled",false); native_preview_proc(r,&cd); calldata_free(&cd);
 g_mutex_lock(&r->lock); assert(!r->preview_sample); g_mutex_unlock(&r->lock);
 /* Destroy must join the blocked native preview through pipeline teardown. */
 destroying=true; destroy_join_entered=destroyed=false; joined_preview=r->preview_thread;
 r->thread=g_thread_new("completed-media",empty_worker,r);
 GThread *d=g_thread_new("destroy-source",destroy_test,r);
 g_mutex_lock(&gate); while(!destroy_join_entered)g_cond_wait(&cond,&gate);
 assert(!destroyed); released=true; g_cond_broadcast(&cond); g_mutex_unlock(&gate);
 g_thread_join(d); assert(destroyed); assert(event_count==6 && events[4]==5 && events[5]==0);
 /* No callbacks may retain receiver storage after actual destroy returns. */
 obs_source_release(source); destroying=false; tested=NULL; sequence=false;
 puts("ordinary-first P010, full-range refusal, native-only latest queue/disable, blocked native destroy join passed");
}
int main(void)
{
 alarm(30); gst_init(NULL,NULL); g_mutex_init(&gate); g_cond_init(&cond);
#ifdef TEST_DISPATCH_RESERVATION
 mixed_case(3);
#endif
 mixed_case(4);
 assert(obs_startup("en-US",NULL,NULL)); ordinary_lifetime(); obs_shutdown();
 for(int cancel=0;cancel<6;cancel++) {
  run_case(1,cancel); run_case(2,cancel); run_case(5,cancel);
#ifdef TEST_DISPATCH_RESERVATION
  run_case(3,cancel);
#endif
  run_case(4,cancel);
 }
 g_cond_clear(&cond); g_mutex_clear(&gate); puts("production preview dispatch boundaries passed");
}
