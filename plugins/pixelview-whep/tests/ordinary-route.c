/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../native-422-filter.h"
#include <gst/app/gstappsink.h>
#include <assert.h>
#include <stdio.h>
static unsigned released;
static gboolean deliver(void *p,GstSample *s,const struct pv_native422_frame *f)
{ (void)p;(void)s;(void)f;assert(!"ordinary media entered native decode");return FALSE; }
static void release(void *p) { (void)p;released++; }
static void test_profile(const char *profile)
{
 GstElement *pipe=gst_pipeline_new(NULL),*filter=pv_native422_filter_new(deliver,NULL,release);
 GstElement *sink=gst_element_factory_make("appsink",NULL);g_object_set(sink,"sync",FALSE,"async",FALSE,NULL);
 gst_bin_add_many(GST_BIN(pipe),filter,sink,NULL);assert(gst_element_link(filter,sink));
 assert(gst_element_set_state(pipe,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 GstPad *input=gst_element_get_static_pad(filter,"sink");
 assert(gst_pad_send_event(input,gst_event_new_stream_start(profile)));
 GstCaps *caps=gst_caps_new_simple("video/x-h265","stream-format",G_TYPE_STRING,"hvc1","alignment",G_TYPE_STRING,"au","profile",G_TYPE_STRING,profile,NULL);
 assert(gst_pad_send_event(input,gst_event_new_caps(caps)));gst_caps_unref(caps);
 /* The actual production extension point must not allocate a native tap/queue
  * for ordinary profiles. Only stock capsfilter and CAPS inspection remain. */
 GstElement *queue=gst_bin_get_by_name(GST_BIN(filter),"preview");
 assert(!queue && "ordinary route still allocates native422 queue");
 GstElement *tap=gst_bin_get_by_name(GST_BIN(filter),"native-transform");
 assert(!tap && "ordinary route still allocates native422 tap");
 GstSegment segment;gst_segment_init(&segment,GST_FORMAT_TIME);
 assert(gst_pad_send_event(input,gst_event_new_segment(&segment)));
 for(unsigned i=0;i<32;i++) {
  GstBuffer *b=gst_buffer_new_allocate(NULL,1,NULL);GST_BUFFER_PTS(b)=i*GST_MSECOND;
  GstBuffer *original=gst_buffer_ref(b);
  assert(gst_pad_chain(input,b)==GST_FLOW_OK);
  GstSample *s=gst_app_sink_try_pull_sample(GST_APP_SINK(sink),GST_SECOND);assert(s);
  assert(gst_sample_get_buffer(s)==original && GST_BUFFER_PTS(original)==i*GST_MSECOND);
  gst_sample_unref(s);gst_buffer_unref(original);
 }
 gst_object_unref(input);gst_element_set_state(pipe,GST_STATE_NULL);gst_object_unref(pipe);
}
static void missing_profile_failure(void)
{
 GstElement *pipe=gst_pipeline_new(NULL),*filter=pv_native422_filter_new(deliver,NULL,release);
 GstElement *sink=gst_element_factory_make("appsink",NULL);g_object_set(sink,"sync",FALSE,"async",FALSE,NULL);
 gst_bin_add_many(GST_BIN(pipe),filter,sink,NULL);assert(gst_element_link(filter,sink));gst_element_set_state(pipe,GST_STATE_PLAYING);
 GstPad *input=gst_element_get_static_pad(filter,"sink");gst_pad_send_event(input,gst_event_new_stream_start("unknown-profile"));
 GstCaps *caps=gst_caps_from_string("video/x-h265,stream-format=hvc1,alignment=au");
 gst_pad_send_event(input,gst_event_new_caps(caps));gst_caps_unref(caps);
 GstSegment segment;gst_segment_init(&segment,GST_FORMAT_TIME);gst_pad_send_event(input,gst_event_new_segment(&segment));
 assert(gst_pad_chain(input,gst_buffer_new_allocate(NULL,1,NULL))==GST_FLOW_NOT_NEGOTIATED);
 assert(!gst_app_sink_try_pull_sample(GST_APP_SINK(sink),0));
 gst_object_unref(input);gst_element_set_state(pipe,GST_STATE_NULL);gst_object_unref(pipe);
}
static void native_dependency_failure(void)
{
 GstElement *pipe=gst_pipeline_new(NULL),*filter=pv_native422_filter_new(deliver,NULL,release);
 GstElement *sink=gst_element_factory_make("appsink",NULL);g_object_set(sink,"sync",FALSE,"async",FALSE,NULL);
 gst_bin_add_many(GST_BIN(pipe),filter,sink,NULL);assert(gst_element_link(filter,sink));
 gst_element_set_state(pipe,GST_STATE_PLAYING);
 GstPluginFeature *queue=GST_PLUGIN_FEATURE(gst_element_factory_find("queue"));assert(queue);
 gst_registry_remove_feature(gst_registry_get(),queue);gst_object_unref(queue);
 GstPad *input=gst_element_get_static_pad(filter,"sink");
 gst_pad_send_event(input,gst_event_new_stream_start("native-missing-queue"));
 GstCaps *caps=gst_caps_from_string("video/x-h265,profile=main-422-10,stream-format=hvc1,alignment=au,width=16,height=16,framerate=25/1");
 gst_pad_send_event(input,gst_event_new_caps(caps));gst_caps_unref(caps);
 GstSegment segment;gst_segment_init(&segment,GST_FORMAT_TIME);gst_pad_send_event(input,gst_event_new_segment(&segment));
 assert(gst_pad_chain(input,gst_buffer_new_allocate(NULL,1,NULL))==GST_FLOW_NOT_NEGOTIATED);
 GstBus *bus=gst_element_get_bus(pipe);GstMessage *error=gst_bus_timed_pop_filtered(bus,GST_SECOND,GST_MESSAGE_ERROR);assert(error);
 gst_message_unref(error);gst_object_unref(bus);
 assert(!gst_app_sink_try_pull_sample(GST_APP_SINK(sink),0));
 gst_object_unref(input);gst_element_set_state(pipe,GST_STATE_NULL);gst_object_unref(pipe);
}
int main(void) { gst_init(NULL,NULL);test_profile("main");test_profile("main-10");missing_profile_failure();native_dependency_failure();assert(released==4);puts("Main/Main10: no native tap/queue, all 64 compressed AUs unchanged; missing native dependency fails closed"); }
