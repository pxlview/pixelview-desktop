/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../native-422-filter.c"
#include <assert.h>
static gboolean deliver(void *p,GstSample *s,const struct pv_native422_frame *f) {(void)p;(void)s;(void)f;return TRUE;}
int main(void) {
 gst_init(NULL,NULL);
 GstElement *filter=pv_native422_filter_new(deliver,NULL,NULL), *rx=gst_bin_new(NULL);
 assert(filter && pv_native422_filter_require_rtp(filter,rx));
 struct route_selector *tap=g_object_get_data(G_OBJECT(filter),"pixelview-route");
 assert(tap->require_rtp && !tap->rate->attached);
 GstElement *depay=gst_element_factory_make("rtph265depay",NULL);assert(depay);
 assert(gst_bin_add(GST_BIN(rx),depay));assert(tap->rate->attached);
 struct rate_observer *rate=tap->rate;
 for(guint i=0;i<=32;i++) {
  GstBuffer *b=gst_rtp_buffer_new_allocate(3,0,0);GstRTPBuffer r=GST_RTP_BUFFER_INIT;
  assert(gst_rtp_buffer_map(b,GST_MAP_WRITE,&r));gst_rtp_buffer_set_ssrc(&r,1234);
  gst_rtp_buffer_set_seq(&r,(guint16)i);gst_rtp_buffer_set_timestamp(&r,i*3750);gst_rtp_buffer_set_marker(&r,TRUE);gst_rtp_buffer_unmap(&r);
  GstPadProbeInfo info={0};info.type=GST_PAD_PROBE_TYPE_BUFFER;info.data=b;
  assert(rate_probe(NULL,&info,rate)==GST_PAD_PROBE_OK);gst_buffer_unref(b);
 }
 assert(rate->rate.num==24 && rate->rate.den==1);
 gst_object_unref(filter);
 /* Late pad callback retains only observer state, not freed source/filter. */
 GstPadProbeInfo invalid={0};assert(rate_probe(NULL,&invalid,rate)==GST_PAD_PROBE_OK);assert(rate->rate.failed);
 gst_object_unref(rx);return 0;
}
