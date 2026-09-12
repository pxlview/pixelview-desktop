/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../native-422-filter.h"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <assert.h>
#include <stdio.h>

struct fake_output { FILE *file; unsigned frames, released; };
static gboolean deliver(void *opaque, GstSample *sample, const struct pv_native422_frame *frame)
{
 struct fake_output *out = opaque;
 assert(frame->pts == GST_BUFFER_PTS(gst_sample_get_buffer(sample)));
 assert(frame->pts == gst_util_uint64_scale(out->frames, 1001 * GST_SECOND, 30000));
 assert(fwrite(frame->v210, frame->stride * frame->height, 1, out->file) == 1);
 out->frames++;
 return TRUE;
}
static void release(void *opaque) { ((struct fake_output *)opaque)->released++; }
static void verify_preview(GstSample *raw)
{
 const GstStructure *caps=gst_caps_get_structure(gst_sample_get_caps(raw),0);
 assert(gst_structure_has_name(caps,"video/x-raw") && !g_strcmp0(gst_structure_get_string(caps,"format"),"NV12"));
 GstBuffer *buffer=gst_sample_get_buffer(raw);assert(gst_buffer_get_size(buffer)==1024*64*3/2);
 guint64 index=gst_util_uint64_scale_round(GST_BUFFER_PTS(buffer),30000,1001*GST_SECOND);assert(index<3);
 GstMapInfo map;assert(gst_buffer_map(buffer,&map,GST_MAP_READ));
 for(unsigned y=0;y<64;y++)for(unsigned x=0;x<1024;x++)assert(map.data[y*1024+x]==(64+(x+index*7)%877)/4);
 for(size_t i=1024*64;i<map.size;i++)assert(map.data[i]==128);
 gst_buffer_unmap(buffer,&map);
}
static void route_switch_regression(void)
{
 struct fake_output out={0};
 GstElement *pipe=gst_pipeline_new(NULL), *filter=pv_native422_filter_new(deliver,&out,release);
 GstElement *sink=gst_element_factory_make("fakesink",NULL);g_object_set(sink,"async",FALSE,NULL);
 gst_bin_add_many(GST_BIN(pipe),filter,sink,NULL);assert(gst_element_link(filter,sink));
 gst_element_set_state(pipe,GST_STATE_PLAYING);
 GstPad *input=gst_element_get_static_pad(filter,"sink");
 GstCaps *allowed=gst_pad_query_caps(input,NULL), *byte_stream=gst_caps_from_string("video/x-h265,stream-format=byte-stream,alignment=au");
 assert(!gst_caps_can_intersect(allowed,byte_stream));
 gst_caps_unref(allowed);gst_caps_unref(byte_stream);
 assert(gst_pad_send_event(input,gst_event_new_stream_start("route-test")));
 GstCaps *ordinary=gst_caps_from_string("video/x-h264");
 assert(gst_pad_send_event(input,gst_event_new_caps(ordinary)));gst_caps_unref(ordinary);
 GstCaps *native=gst_caps_from_string("video/x-h265,stream-format=hvc1,alignment=au,profile=main-422-10,width=1024,height=64,framerate=30000/1001");
 gboolean accepted=gst_pad_send_event(input,gst_event_new_caps(native));gst_caps_unref(native);
 GstElement *queue=gst_bin_get_by_name(GST_BIN(filter),"preview");
 /* Stock capsfilter rejects a new codec family at CAPS negotiation. */
 assert(!queue); // Ordinary route never allocates a native queue.
 GstSegment segment;gst_segment_init(&segment,GST_FORMAT_TIME);
 gst_pad_send_event(input,gst_event_new_segment(&segment));
 assert(!accepted);
 gst_object_unref(input);
 gst_element_set_state(pipe,GST_STATE_NULL);gst_object_unref(pipe);
 assert(out.released==1 && !out.frames);
}
int main(int argc, char **argv)
{
 assert(argc == 3); gst_init(NULL, NULL); route_switch_regression();
 for (unsigned restart = 0; restart < 3; restart++) {
  struct fake_output out = {.file=fopen(argv[2], restart ? "ab" : "wb")}; assert(out.file);
  GstElement *reader = gst_parse_launch("filesrc name=file ! h265parse ! video/x-h265,stream-format=hvc1,alignment=au ! appsink name=read sync=false", NULL);
  GstElement *file = gst_bin_get_by_name(GST_BIN(reader), "file");
  g_object_set(file, "location", argv[1], NULL); gst_object_unref(file);
  GstAppSink *read = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(reader), "read"));
  GstElement *pipeline = gst_pipeline_new(NULL);
  GstElement *src = gst_element_factory_make("appsrc", NULL);
  GstElement *filter = pv_native422_filter_new(deliver, &out, release); assert(filter);
  GstElement *preview = gst_element_factory_make("appsink", NULL);
  g_object_set(src, "format", GST_FORMAT_TIME, NULL);
  g_object_set(preview, "sync", FALSE, NULL);
  gst_bin_add_many(GST_BIN(pipeline), src, filter, preview, NULL);
  assert(gst_element_link_many(src, filter, preview, NULL));
  assert(gst_element_set_state(reader, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
  assert(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
  unsigned submitted = 0;
  for (;;) {
   GstSample *sample = gst_app_sink_try_pull_sample(read, 5 * GST_SECOND);
   if (!sample) break;
   GstBuffer *buffer = gst_buffer_copy(gst_sample_get_buffer(sample));
   GST_BUFFER_PTS(buffer) = gst_util_uint64_scale(submitted++, 1001 * GST_SECOND, 30000);
   GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(1, 1001 * GST_SECOND, 30000);
   gst_app_src_set_caps(GST_APP_SRC(src), gst_sample_get_caps(sample));
   assert(gst_app_src_push_buffer(GST_APP_SRC(src), buffer) == GST_FLOW_OK);
   gst_sample_unref(sample);
  }
  assert(submitted == 3); gst_app_src_end_of_stream(GST_APP_SRC(src));
  GstSample *raw = gst_app_sink_try_pull_sample(GST_APP_SINK(preview), 5 * GST_SECOND);
  assert(raw);
  verify_preview(raw); gst_sample_unref(raw);
  while ((raw = gst_app_sink_try_pull_sample(GST_APP_SINK(preview), GST_SECOND))) { verify_preview(raw); gst_sample_unref(raw); }
  GstBus *bus = gst_element_get_bus(pipeline);
  GstMessage *message = gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
  assert(message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS);
  gst_message_unref(message); gst_object_unref(bus);
  gst_element_set_state(reader, GST_STATE_NULL); gst_object_unref(read); gst_object_unref(reader);
  gst_element_set_state(pipeline, GST_STATE_NULL); gst_object_unref(pipeline);
  assert(out.frames == 3 && out.released == 1); fclose(out.file);
 }
 puts("native422 encoded filter: three restarts, nine exact timed deliveries, owner released once per attempt");
}
