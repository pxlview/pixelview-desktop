/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test TU at the existing native function boundary; never production-linked. */
#include "../../pixelview-whep/native-422.h"
extern void pv_campaign_record(guint64,guint64,const guint64 *,guint);
static enum pv_native422_result campaign_decode(struct pv_native422 *d,GstSample *s,struct pv_native422_frame *f,GstBuffer **raw)
{
 guint64 begin=(guint64)g_get_monotonic_time()*1000;
 enum pv_native422_result result=pv_native422_decode_preview(d,s,f,raw);
 guint64 end=(guint64)g_get_monotonic_time()*1000;
 struct pv_native422_timing t=pv_native422_get_timing(d);
 guint64 v[]={GST_BUFFER_PTS(gst_sample_get_buffer(s)),begin,end,t.session_begin,t.session_end,t.submitted,t.returned,t.waited,t.callback,t.packed,t.previewed,t.first_callback};
 pv_campaign_record(3,0,v,G_N_ELEMENTS(v));return result;
}
#define pv_native422_decode_preview campaign_decode
#include "../../pixelview-whep/native-422-filter.c"
