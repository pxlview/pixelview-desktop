/* SPDX-License-Identifier: GPL-2.0-or-later */
#define PIXELVIEW_WHEP_TEST 1
#include <obs.h>
#include <assert.h>
static unsigned checked_frames;
static void checked_output(obs_source_t *source, const struct obs_source_frame2 *f)
{
 assert(f->format==VIDEO_FORMAT_P010 && f->range==VIDEO_RANGE_PARTIAL && f->trc==VIDEO_TRC_DEFAULT);
 const uint16_t *y=(const uint16_t *)f->data[0];
 for(unsigned x=0;x<877;x++) assert((y[x]>>6)==64+x);
 ++checked_frames;
 obs_source_output_video2(source,f); // Actual libobs copy/upload, not a fake sink.
}
#define obs_source_output_video2 checked_output
#include "../pixelview-whep.c"
#undef obs_source_output_video2
#include "../video-format.h"
static struct receiver probe;
static unsigned raw_frames, rejected_frames;
static GstFlowReturn audited_sample(GstAppSink *sink, gpointer opaque)
{
 GstFlowReturn result=video_sample(sink,opaque);
 if (result==GST_FLOW_NOT_NEGOTIATED || result==GST_FLOW_ERROR) rejected_frames++;
 return result;
}
static GstPadProbeReturn observe_raw(GstPad *pad, GstPadProbeInfo *unused, gpointer opaque)
{
 (void)unused; (void)opaque;
 if (!raw_frames++) {
  GstCaps *caps=gst_pad_get_current_caps(pad); gchar *s=gst_caps_to_string(caps);
  printf("DECODED %s\n",s);g_free(s);gst_caps_unref(caps);
 }
 return GST_PAD_PROBE_OK;
}
void pixelview_precision_start(obs_source_t *source, const char *path)
{
 gst_init(NULL,NULL);
 memset(&probe,0,sizeof(probe)); probe.source=source; probe.accept_samples=true;
 g_mutex_init(&probe.lock);g_rec_mutex_init(&probe.delivery);
 GError *error=NULL;
 probe.pipe=gst_parse_launch("filesrc name=file ! h265parse ! vtdec_hw ! " PIXELVIEW_RECEIVE_RAW_CAPS " ! appsink name=out sync=true",&error);
 assert(probe.pipe && !error);
 GstElement *file=gst_bin_get_by_name(GST_BIN(probe.pipe),"file");g_object_set(file,"location",path,NULL);gst_object_unref(file);
 GstElement *out=gst_bin_get_by_name(GST_BIN(probe.pipe),"out");
 GstPad *pad=gst_element_get_static_pad(out,"sink"); gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,observe_raw,NULL,NULL);gst_object_unref(pad);
 GstAppSinkCallbacks cb={0};cb.new_sample=audited_sample;
 gst_app_sink_set_callbacks(GST_APP_SINK(out),&cb,&probe,NULL);gst_object_unref(out);
 assert(gst_element_set_state(probe.pipe,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
}
unsigned pixelview_precision_stop(void)
{
 GstBus *bus=gst_element_get_bus(probe.pipe);
 GstMessage *msg=gst_bus_pop_filtered(bus,GST_MESSAGE_ERROR);
 if(msg) { GError *err=NULL; gchar *detail=NULL; gst_message_parse_error(msg,&err,&detail); fprintf(stderr,"OFFLINE ERROR %s %s\n",err->message,detail?detail:""); g_error_free(err);g_free(detail);gst_message_unref(msg); }
 gst_object_unref(bus);
 gst_element_set_state(probe.pipe,GST_STATE_NULL);
 gst_object_unref(probe.pipe);g_mutex_clear(&probe.lock);g_rec_mutex_clear(&probe.delivery);
 if (g_getenv("PIXELVIEW_EXPECT_REJECT")) { assert(raw_frames>0 && rejected_frames>0 && checked_frames==0 && probe.frames==0); puts("REJECT full-range encoded HEVC: zero OBS deliveries"); return 0; }
 assert(checked_frames==probe.frames && checked_frames>0);
 printf("CALLBACK codes=877 exact=1 checked=%u\n",checked_frames);
 return (unsigned)probe.frames;
}
