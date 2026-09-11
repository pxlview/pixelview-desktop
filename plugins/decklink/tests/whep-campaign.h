/* SPDX-License-Identifier: GPL-2.0-or-later */
/* NONSHIPPING bounded campaign observations. No payload, URL, raw SSRC or
 * per-frame disk writes. GLib ns units have microsecond resolution. */
#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <util/platform.h>
#include <assert.h>
#include <stdio.h>
#include <dlfcn.h>
#define PV_CAMPAIGN_RECORDS 2048
struct campaign_record { guint64 before,mono,after,kind,generation,v[12]; };
static struct campaign_record campaign_records[PV_CAMPAIGN_RECORDS];
static GMutex campaign_lock;
static guint campaign_count,campaign_generation;
static gboolean campaign_flushed;
void pv_campaign_record(guint64 kind,guint64 generation,const guint64 *v,guint n)
{
 if(!g_getenv("PV_BOUNDED_CAMPAIGN"))return;
 struct campaign_record r={.before=os_gettime_ns(),.kind=kind,.generation=generation};
 r.mono=(guint64)g_get_monotonic_time()*1000;r.after=os_gettime_ns();
 assert(n<=G_N_ELEMENTS(r.v));for(guint i=0;i<n;i++)r.v[i]=v[i];
 g_mutex_lock(&campaign_lock);
 if(!r.generation)r.generation=campaign_generation;
 assert(campaign_count<PV_CAMPAIGN_RECORDS);campaign_records[campaign_count++]=r;
 g_mutex_unlock(&campaign_lock);
}
void pv_campaign_flush(void)
{
 if(!g_getenv("PV_BOUNDED_CAMPAIGN"))return;
 g_mutex_lock(&campaign_lock);
 if(!campaign_flushed) {
  campaign_flushed=TRUE;
  fprintf(stderr,"CAMPAIGN_RECORDS count=%u cap=%u\n",campaign_count,PV_CAMPAIGN_RECORDS);
  for(guint i=0;i<campaign_count;i++) {
   struct campaign_record *r=&campaign_records[i];
   fprintf(stderr,"CAMPAIGN %llu %llu %llu %llu %llu",r->kind,r->generation,r->before,r->mono,r->after);
   for(guint j=0;j<G_N_ELEMENTS(r->v);j++)fprintf(stderr," %llu",r->v[j]);
   fputc('\n',stderr);
  }
  void (*observer_flush)(void)=dlsym(RTLD_DEFAULT,"pv_native422_observer_flush");
  if(observer_flush)observer_flush();
  fflush(stderr);
 }
 g_mutex_unlock(&campaign_lock);
}
static GstPadProbeReturn campaign_rtp(GstPad *pad,GstPadProbeInfo *info,gpointer data)
{
 (void)pad;GstBuffer *b=GST_PAD_PROBE_INFO_BUFFER(info);GstRTPBuffer p=GST_RTP_BUFFER_INIT;
 if(b && gst_buffer_get_size(b)<=65536 && gst_rtp_buffer_map(b,GST_MAP_READ,&p)) {
  /* Marker sampling is bounded and correlates identical packet seq/timestamps.
   * Audio markers are excluded by the negotiated video PT (test offer 96). */
  if(gst_rtp_buffer_get_marker(&p) && gst_rtp_buffer_get_payload_type(&p)==96) {
   guint64 v[]={gst_rtp_buffer_get_seq(&p),gst_rtp_buffer_get_timestamp(&p),GST_BUFFER_FLAG_IS_SET(b,GST_BUFFER_FLAG_DISCONT)};
   guint tag=GPOINTER_TO_UINT(data);pv_campaign_record(tag&3,tag>>2,v,G_N_ELEMENTS(v));
  }
  gst_rtp_buffer_unmap(&p);
 }
 return GST_PAD_PROBE_OK;
}
static void campaign_element(GstElement *element,guint generation)
{
 if(!g_getenv("PV_BOUNDED_CAMPAIGN"))return;
 GstElementFactory *factory=gst_element_get_factory(element);if(!factory)return;
 const char *name=gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
 guint kind=!strcmp(name,"rtpjitterbuffer")?1:!strcmp(name,"rtph265depay")?2:0;
 if(!kind)return;
 GstPad *pad=gst_element_get_static_pad(element,"sink");assert(pad);
 gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,campaign_rtp,GUINT_TO_POINTER((generation<<2)|kind),NULL);gst_object_unref(pad);
}
