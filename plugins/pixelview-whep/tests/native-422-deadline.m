/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <gst/gst.h>
static gint64 test_now;
static gint64 deadline_clock(void) { return test_now; }
#define g_get_monotonic_time deadline_clock
#include "../native-422-filter.c"
#undef g_get_monotonic_time
#include "../native-422.m"
#include <assert.h>
#include <stdio.h>
static gboolean event_ok(GstPad *p,GstObject *o,GstEvent *e)
{ (void)p;(void)o;gst_event_unref(e);return TRUE; }
static void check(guint64 age, gboolean acquired, gboolean delta, gboolean active)
{
 PvNativeTap *tap=g_object_new(pv_native_tap_get_type(),NULL);
 tap->require_rtp=TRUE;tap->native_started=TRUE;
 GstBus *bus=gst_bus_new();gst_element_set_bus(GST_ELEMENT(tap),bus);
 tap->rate=g_new0(struct rate_observer,1);tap->rate->refs=1;g_mutex_init(&tap->rate->lock);
 test_now=10000000;
 guint64 now=(guint64)test_now*1000;
 tap->rate->rate=(struct pv422_rate){.initialized=true,.num=acquired?24:0,.den=1,.started=now-age,.last=now};
 tap->caps=gst_caps_new_simple("video/x-h265","profile",G_TYPE_STRING,"main-422-10","width",G_TYPE_INT,1920,"height",G_TYPE_INT,1080,NULL);
 gst_segment_init(&tap->segment,GST_FORMAT_TIME);
 tap->queue=gst_element_factory_make("queue",NULL);assert(tap->queue);
 GstPad *sink=gst_pad_new("test-sink",GST_PAD_SINK);gst_pad_set_event_function(sink,event_ok);
 gst_pad_set_active(tap->src,TRUE);gst_pad_set_active(sink,TRUE);assert(gst_pad_link(tap->src,sink)==GST_PAD_LINK_OK);
 GstBuffer *b=gst_buffer_new_allocate(NULL,1,NULL);GST_BUFFER_PTS(b)=0;
 if(delta) GST_BUFFER_FLAG_SET(b,GST_BUFFER_FLAG_DELTA_UNIT);
 GstFlowReturn result=tap_chain(tap->sink,GST_OBJECT(tap),b);
 fprintf(stderr,"deadline age=%llu acquired=%d delta=%d active=%d expected=%d result=%d\n",(unsigned long long)age,acquired,delta,tap->rate_active,active,result);
 assert(tap->rate_active==active);
 assert((tap->decoder->rate_num!=0)==active);
 struct pv_native422_timing timing=pv_native422_get_timing(tap->decoder);
 assert(timing.session_begin==0 && timing.callback==0);
 if(age>3000000000ULL) { assert(tap->failed && result==GST_FLOW_NOT_NEGOTIATED); assert(!g_strcmp0(tap->failure_reason,"au-acquisition-expired")); }
 gst_pad_unlink(tap->src,sink);gst_pad_set_active(sink,FALSE);gst_object_unref(sink);
 if(age>3000000000ULL) {
  GstMessage *msg=gst_bus_pop_filtered(bus,GST_MESSAGE_ERROR);assert(msg);
  struct pv422_diagnostic diagnostic={0};assert(pv422_diagnostic_read(msg,&diagnostic));
  assert(!strcmp(diagnostic.reason,"au-acquisition-expired"));
  /* Actual producer details roundtrip, not a constructed surrogate. */
  assert(diagnostic.values[3]==now && diagnostic.values[4]==now-age);
  gst_message_unref(msg);
 }
 gst_element_set_bus(GST_ELEMENT(tap),NULL);gst_object_unref(bus);
 gst_object_unref(tap->queue);gst_object_unref(tap);
}
static void active_stale_boundary(guint64 age)
{
 PvNativeTap *tap=g_object_new(pv_native_tap_get_type(),NULL);
 tap->require_rtp=TRUE; tap->native_started=TRUE; tap->rate_active=TRUE;
 tap->decoded_count=1; tap->last_pts=0;
 tap->rate=g_new0(struct rate_observer,1);tap->rate->refs=1;g_mutex_init(&tap->rate->lock);
 test_now=10000000;guint64 now=(guint64)test_now*1000;
 tap->rate->rate=(struct pv422_rate){.initialized=true,.num=24,.den=1,.started=1,.last=now-age};
 /* Seeded active/decoded_count state, NOT a previously decoded frame.
  * Active phase is deliberately far beyond acquisition expiry. The invalid
  * AU may fail later native metadata validation, but cannot mask a rate reason. */
 GstBuffer *b=gst_buffer_new_allocate(NULL,1,NULL);GST_BUFFER_PTS(b)=41666666;
 assert(tap_chain(tap->sink,GST_OBJECT(tap),b)==GST_FLOW_NOT_NEGOTIATED);
 assert((tap->failure_reason!=NULL)==(age>500000000));
 if(age>500000000) assert(!strcmp(tap->failure_reason,"au-ordered-rtp-stale"));
 assert(tap->rate_active && tap->decoded_count==1);
 fprintf(stderr,"active stale boundary age=%llu reason=%s\n",(unsigned long long)age,tap->failure_reason?tap->failure_reason:"native-metadata-only");
 gst_object_unref(tap);
}
int main(void)
{
 gst_init(NULL,NULL);
 for(guint64 gap=499999999;gap<=500000001;gap++) active_stale_boundary(gap);
 /* Reviewer reproduction first; no native session is needed to see activation. */
 check(4000000000ULL,TRUE,FALSE,FALSE);
 const guint64 ages[]={2999999999ULL,3000000000ULL,3000000001ULL};
 for(unsigned i=0;i<G_N_ELEMENTS(ages);i++)
  for(int acquired=0;acquired<2;acquired++) for(int delta=0;delta<2;delta++)
   check(ages[i],acquired,delta,ages[i]<=3000000000ULL && acquired && !delta);
 return 0;
}
