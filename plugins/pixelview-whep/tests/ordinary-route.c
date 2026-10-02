/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../codec-route.h"
#include <gst/app/gstappsink.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* The route bin holds exactly its stock capsfilter: no tap, queue or decoder. */
static void assert_stock_route(GstElement *filter)
{
 assert(GST_BIN_NUMCHILDREN(filter)==1);
 GstElement *route=gst_bin_get_by_name(GST_BIN(filter),"codec-route");assert(route);
 assert(!g_strcmp0(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(gst_element_get_factory(route))),"capsfilter"));
 gst_object_unref(route);
}
static void test_profile(const char *profile)
{
 GstElement *pipe=gst_pipeline_new(NULL),*filter=pv_codec_route_new();
 GstElement *sink=gst_element_factory_make("appsink",NULL);g_object_set(sink,"sync",FALSE,"async",FALSE,NULL);
 gst_bin_add_many(GST_BIN(pipe),filter,sink,NULL);assert(gst_element_link(filter,sink));
 assert(gst_element_set_state(pipe,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 /* A probed Main 4:2:2 10 decoder admits that profile on the SAME stock route. */
 if(!strcmp(profile,"main-422-10")) pv_codec_route_admit_main422(filter,TRUE);
 /* And a probed Main 4:4:4 10 decoder admits that one. */
 if(!strcmp(profile,"main-444-10")) pv_codec_route_admit_main444(filter,TRUE);
 GstPad *input=gst_element_get_static_pad(filter,"sink");
 assert(gst_pad_send_event(input,gst_event_new_stream_start(profile)));
 GstCaps *caps=gst_caps_new_simple("video/x-h265","stream-format",G_TYPE_STRING,"hvc1","alignment",G_TYPE_STRING,"au","profile",G_TYPE_STRING,profile,NULL);
 assert(gst_pad_send_event(input,gst_event_new_caps(caps)));gst_caps_unref(caps);
 /* The production extension point is only a stock capsfilter and CAPS inspection. */
 assert_stock_route(filter);
 /* The selector pinned exactly this profile for the rest of the attempt. */
 GstElement *route=gst_bin_get_by_name(GST_BIN(filter),"codec-route");GstCaps *pinned=NULL;g_object_get(route,"caps",&pinned,NULL);
 assert(!g_strcmp0(gst_structure_get_string(gst_caps_get_structure(pinned,0),"profile"),profile));
 gst_caps_unref(pinned);gst_object_unref(route);
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
 GstElement *pipe=gst_pipeline_new(NULL),*filter=pv_codec_route_new();
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
/* Without a probed 4:2:2 (or 4:4:4) decoder, a sender on HEVC 4:2:2 (or 4:4:4)
 * 10-bit, and always any other non-Main profile, is refused before its first AU
 * with the typed reason the frontend maps to "use HEVC Main or Main10". Each
 * admission covers its own profile only. */
static void unsupported_profile_refusal(const char *profile,const char *reason,gboolean admit)
{
 GstElement *pipe=gst_pipeline_new(NULL),*filter=pv_codec_route_new();
 GstElement *sink=gst_element_factory_make("appsink",NULL);g_object_set(sink,"sync",FALSE,"async",FALSE,NULL);
 gst_bin_add_many(GST_BIN(pipe),filter,sink,NULL);assert(gst_element_link(filter,sink));gst_element_set_state(pipe,GST_STATE_PLAYING);
 if(admit) pv_codec_route_admit_main422(filter,admit);
 GstPad *input=gst_element_get_static_pad(filter,"sink");gst_pad_send_event(input,gst_event_new_stream_start(profile));
 char *text=g_strdup_printf("video/x-h265,stream-format=hvc1,alignment=au,profile=%s,width=1920,height=1080,framerate=25/1",profile);
 GstCaps *caps=gst_caps_from_string(text);g_free(text);
 gst_pad_send_event(input,gst_event_new_caps(caps));gst_caps_unref(caps); /* dropped by the probe: reported as handled */
 GstSegment segment;gst_segment_init(&segment,GST_FORMAT_TIME);gst_pad_send_event(input,gst_event_new_segment(&segment));
 assert(gst_pad_chain(input,gst_buffer_new_allocate(NULL,1,NULL))==GST_FLOW_NOT_NEGOTIATED);
 GstBus *bus=gst_element_get_bus(pipe);GstMessage *error=gst_bus_timed_pop_filtered(bus,GST_SECOND,GST_MESSAGE_ERROR);assert(error);
 GError *err=NULL;gchar *debug=NULL;gst_message_parse_error(error,&err,&debug);
 assert(err && err->domain==GST_STREAM_ERROR && err->code==GST_STREAM_ERROR_WRONG_TYPE);
 assert(strstr(err->message,"HEVC Main or Main10"));
 const GstStructure *details=NULL;gst_message_parse_error_details(error,&details);
 assert(details && gst_structure_has_name(details,PV_UNSUPPORTED_PROFILE_DETAILS));
 assert(!g_strcmp0(gst_structure_get_string(details,"reason"),reason));
 g_clear_error(&err);g_free(debug);gst_message_unref(error);gst_object_unref(bus);
 assert_stock_route(filter);
 assert(!gst_app_sink_try_pull_sample(GST_APP_SINK(sink),0));
 gst_object_unref(input);gst_element_set_state(pipe,GST_STATE_NULL);gst_object_unref(pipe);
}
int main(void)
{
 gst_init(NULL,NULL);test_profile("main");test_profile("main-10");test_profile("main-422-10");test_profile("main-444-10");missing_profile_failure();
 unsupported_profile_refusal("main-422-10",PV_UNSUPPORTED_HEVC_MAIN_422_10,FALSE);
 unsupported_profile_refusal("main-422-12",PV_UNSUPPORTED_HEVC_PROFILE,FALSE);
 unsupported_profile_refusal("main-444-10",PV_UNSUPPORTED_HEVC_MAIN_444_10,FALSE);
 unsupported_profile_refusal("main-444-10",PV_UNSUPPORTED_HEVC_MAIN_444_10,TRUE); /* the 4:2:2 admission does not cover 4:4:4 */
 unsupported_profile_refusal("main-444-12",PV_UNSUPPORTED_HEVC_PROFILE,TRUE);
 puts("Main/Main10 and probed Main 4:2:2 10 / Main 4:4:4 10: stock capsfilter route, all 128 compressed AUs unchanged, profile pinned; missing profile fails closed; unprobed 4:2:2, 4:4:4 and other profiles refused with typed Main/Main10 guidance");
}
