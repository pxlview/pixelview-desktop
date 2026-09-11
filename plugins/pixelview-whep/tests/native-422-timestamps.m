/* SPDX-License-Identifier: GPL-2.0-or-later */
#define main deadline_fixture_main
#include "native-422-deadline.m"
#undef main
#include <gst/app/gstappsink.h>
static guint delivered;
static guint64 expected_pts;
static gboolean accept_frame(void *unused,GstSample *sample,const struct pv_native422_frame *frame)
{
 (void)unused; (void)frame;delivered++;
 assert(GST_BUFFER_PTS(gst_sample_get_buffer(sample))==expected_pts);
 assert(GST_BUFFER_DURATION(gst_sample_get_buffer(sample))==41666666);
 return TRUE;
}
static GstFlowReturn discard_raw(GstPad *pad,GstObject *obj,GstBuffer *b)
{ (void)pad;(void)obj;gst_buffer_unref(b);return GST_FLOW_OK; }
static void boundary(GstSample *sample,guint64 pts,guint64 duration,const char *reason)
{
 PvNativeTap *tap=g_object_new(pv_native_tap_get_type(),NULL);
 tap->require_rtp=TRUE;tap->native_started=TRUE;tap->rate_active=TRUE;
 tap->last_pts=GST_SECOND;tap->delivery=accept_frame;
 tap->rate=g_new0(struct rate_observer,1);tap->rate->refs=1;g_mutex_init(&tap->rate->lock);
 test_now=10000000;
 tap->rate->rate=(struct pv422_rate){.initialized=true,.num=24,.den=1,.started=1,.last=(guint64)test_now*1000};
 tap->decode_caps=gst_caps_copy(gst_sample_get_caps(sample));
 gst_caps_set_simple(tap->decode_caps,"framerate",GST_TYPE_FRACTION,24,1,NULL);
 pv_native422_require_initial_format(tap->decoder,24,1);
 gst_segment_init(&tap->segment,GST_FORMAT_TIME);
 GstPad *sink=gst_pad_new("timing-sink",GST_PAD_SINK);gst_pad_set_event_function(sink,event_ok);gst_pad_set_chain_function(sink,discard_raw);
 gst_pad_set_active(tap->src,TRUE);gst_pad_set_active(sink,TRUE);assert(gst_pad_link(tap->src,sink)==GST_PAD_LINK_OK);
 gst_pad_push_event(tap->src,gst_event_new_stream_start("timing-fixture"));
 gst_pad_push_event(tap->src,gst_event_new_caps(tap->decode_caps));gst_pad_push_event(tap->src,gst_event_new_segment(&tap->segment));
 GstBuffer *b=gst_buffer_copy(gst_sample_get_buffer(sample));GST_BUFFER_PTS(b)=pts;GST_BUFFER_DURATION(b)=duration;
 delivered=0;expected_pts=pts;
 GstFlowReturn flow=tap_chain(tap->sink,GST_OBJECT(tap),b);
 fprintf(stderr,"actual AU timing pts=%llu duration=%llu reason=%s delivered=%u\n",pts,duration,tap->failure_reason?tap->failure_reason:"none",delivered);
 if(reason) assert(flow==GST_FLOW_NOT_NEGOTIATED && !g_strcmp0(reason,tap->failure_reason) && !delivered && !tap->decoded_count);
 else assert(flow==GST_FLOW_OK && !tap->failed && !tap->failure_reason && delivered==1 && tap->decoded_count==1);
 gst_pad_unlink(tap->src,sink);gst_object_unref(sink);gst_object_unref(tap);
}
int main(int argc,char **argv)
{
 assert(argc==2);gst_init(NULL,NULL);
 GstElement *pipe=gst_parse_launch("filesrc name=file ! h265parse ! video/x-h265,stream-format=hvc1,alignment=au ! appsink name=sink sync=false",NULL);assert(pipe);
 GstElement *file=gst_bin_get_by_name(GST_BIN(pipe),"file");g_object_set(file,"location",argv[1],NULL);gst_object_unref(file);
 GstAppSink *sink=GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipe),"sink"));
 assert(gst_element_set_state(pipe,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 GstSample *sample=gst_app_sink_try_pull_sample(sink,5*GST_SECOND);assert(sample);
 for(guint64 delta=99999999;delta<=100000001;delta++) boundary(sample,GST_SECOND+delta,GST_CLOCK_TIME_NONE,delta>100000000?"au-pts-gap":NULL);
 boundary(sample,GST_CLOCK_TIME_NONE,GST_CLOCK_TIME_NONE,"au-pts-missing");
 boundary(sample,GST_SECOND,GST_CLOCK_TIME_NONE,"au-pts-nonmonotonic");
 boundary(sample,GST_SECOND-1,GST_CLOCK_TIME_NONE,"au-pts-nonmonotonic");
 for(guint64 duration=41666664;duration<=41666668;duration++) boundary(sample,GST_SECOND+41666666,duration,duration<41666665 || duration>41666667?"au-duration-conflict":NULL);
 gst_sample_unref(sample);gst_element_set_state(pipe,GST_STATE_NULL);gst_object_unref(sink);gst_object_unref(pipe);
 puts("PASS actual production AU PTS 100ms +/-1ns and duration +/-1 inclusive, +/-2 refusal; successful native delivery preserves PTS");
}
