/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <gst/gst.h>
static gint64 test_clock(void) { return 10000000; }
#define g_get_monotonic_time test_clock
#include "../native-422-filter.c"
#undef g_get_monotonic_time
#include "../native-422.m"
#include <assert.h>
#include <stdio.h>
static gboolean event_ok(GstPad *p,GstObject *o,GstEvent *e)
{ (void)p;(void)o;gst_event_unref(e);return TRUE; }
static void check(unsigned num,unsigned den)
{
 PvNativeTap *tap=g_object_new(pv_native_tap_get_type(),NULL);
 tap->require_rtp=TRUE;tap->native_started=TRUE;
 tap->rate=g_new0(struct rate_observer,1);tap->rate->refs=1;g_mutex_init(&tap->rate->lock);
 tap->rate->rate=(struct pv422_rate){.initialized=true,.num=num,.den=den,.started=9990000000ULL,.last=10000000000ULL};
 tap->caps=gst_caps_new_simple("video/x-h265","profile",G_TYPE_STRING,"main-422-10","width",G_TYPE_INT,1920,"height",G_TYPE_INT,1080,NULL);
 gst_segment_init(&tap->segment,GST_FORMAT_TIME);
 tap->queue=gst_element_factory_make("queue",NULL);assert(tap->queue);
 GstPad *sink=gst_pad_new("test-sink",GST_PAD_SINK);gst_pad_set_event_function(sink,event_ok);
 gst_pad_set_active(tap->src,TRUE);gst_pad_set_active(sink,TRUE);assert(gst_pad_link(tap->src,sink)==GST_PAD_LINK_OK);
 GstBuffer *b=gst_buffer_new_allocate(NULL,1,NULL);GST_BUFFER_PTS(b)=0;
 assert(tap_chain(tap->sink,GST_OBJECT(tap),b)==GST_FLOW_NOT_NEGOTIATED);
 gboolean allowed=(guint64)num==25ULL*den;
 assert(tap->rate_active==allowed);
 if(!allowed) assert(!g_strcmp0(tap->failure_reason,"au-caps-rate-conflict"));
 /* 25 passes only the rate gate: malformed metadata still fails before VT. */
 assert(pv_native422_get_timing(tap->decoder).session_begin==0);
 gst_pad_unlink(tap->src,sink);gst_pad_set_active(sink,FALSE);gst_object_unref(sink);
 gst_object_unref(tap->queue);gst_object_unref(tap);
 printf("PASS normal-build actual native filter rate %u/%u: admission=%d, no VT session\n",num,den,allowed);
}
int main(void)
{
 gst_init(NULL,NULL);assert(pv_main422_25p_enabled());
 check(24000,1001);check(24,1);check(25,1);check(30000,1001);check(30,1);
 return 0;
}
