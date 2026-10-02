/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "codec-route.h"

static GstStaticCaps route_caps = GST_STATIC_CAPS("video/x-h265,stream-format=hvc1,alignment=au;video/x-h264;video/x-vp9");
/* request-encoded-filter receives downstream RAW caps, not the selected codec.
 * Inspect the first parsed CAPS event using the supported pad-probe API. The
 * route is a stock capsfilter: no AU callback, decoder or queue, and compressed
 * buffers are never dropped or moved. A profile change requires a new attempt. */
struct route_selector {
 gint refs;
 GstElement *bin, *policy; /* borrowed while the event probe is installed */
 gboolean selected, admit_main422, admit_main444;
};
static void selector_unref(gpointer opaque)
{
 struct route_selector *s=opaque;
 if(g_atomic_int_dec_and_test(&s->refs)) g_free(s);
}
static GstPadProbeReturn refuse_route(GstPad *pad,GstPadProbeInfo *info,gpointer opaque)
{
 (void)pad;(void)opaque;
 gst_mini_object_unref(GST_MINI_OBJECT(GST_PAD_PROBE_INFO_DATA(info)));
 GST_PAD_PROBE_INFO_DATA(info)=NULL;
 GST_PAD_PROBE_INFO_FLOW_RETURN(info)=GST_FLOW_NOT_NEGOTIATED;
 return GST_PAD_PROBE_HANDLED;
}
static GstPadProbeReturn select_route(GstPad *pad,GstPadProbeInfo *info,gpointer opaque)
{
 if(GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info))!=GST_EVENT_CAPS) return GST_PAD_PROBE_OK;
 struct route_selector *s=opaque;
 const char *unsupported=NULL;
 GstCaps *caps;gst_event_parse_caps(GST_PAD_PROBE_INFO_EVENT(info),&caps);
 const GstStructure *wire=gst_caps_is_fixed(caps)?gst_caps_get_structure(caps,0):NULL;
 if(!wire) goto failed;
 if(s->selected) {
  GstCaps *policy=NULL;g_object_get(s->policy,"caps",&policy,NULL);
  gboolean accepted=gst_caps_is_subset(caps,policy);gst_caps_unref(policy);
  if(!accepted) goto failed;
  return GST_PAD_PROBE_OK;
 }
 s->selected=TRUE;
 GstCaps *policy=gst_caps_new_empty_simple(gst_structure_get_name(wire));
 if(gst_structure_has_name(wire,"video/x-h265")) {
  gst_caps_set_simple(policy,"stream-format",G_TYPE_STRING,"hvc1","alignment",G_TYPE_STRING,"au",NULL);
  /* Pin the actual profile: a later stream of another profile never reaches
   * the decoder chosen for this one. */
  const char *profile=gst_structure_get_string(wire,"profile");
  if(!profile) { gst_caps_unref(policy);goto failed; }
  gboolean main422=!g_strcmp0(profile,"main-422-10"),main444=!g_strcmp0(profile,"main-444-10");
  if(g_strcmp0(profile,"main") && g_strcmp0(profile,"main-10") && !(main422 && s->admit_main422) &&
     !(main444 && s->admit_main444)) {
   /* A sender on HEVC 4:2:2 or 4:4:4 10-bit that this Mac did not verify (or on
    * another non-Main profile) is refused with a canonical reason before any AU;
    * the profile string itself is never copied into the message. */
   unsupported=main422?PV_UNSUPPORTED_HEVC_MAIN_422_10:main444?PV_UNSUPPORTED_HEVC_MAIN_444_10:PV_UNSUPPORTED_HEVC_PROFILE;
   gst_caps_unref(policy);goto failed;
  }
  gst_caps_set_simple(policy,"profile",G_TYPE_STRING,profile,NULL);
 }
 g_object_set(s->policy,"caps",policy,NULL);gst_caps_unref(policy);
 return GST_PAD_PROBE_OK;
failed:
 gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST,refuse_route,NULL,NULL);
 if(unsupported)
  gst_element_message_full_with_details(s->bin,GST_MESSAGE_ERROR,GST_STREAM_ERROR,GST_STREAM_ERROR_WRONG_TYPE,
   g_strdup("Unsupported HEVC profile received: use the HEVC Main or Main10 profile"),g_strdup(unsupported),
   __FILE__,GST_FUNCTION,__LINE__,gst_structure_new(PV_UNSUPPORTED_PROFILE_DETAILS,"reason",G_TYPE_STRING,unsupported,NULL));
 else
  GST_ELEMENT_ERROR(s->bin,CORE,NEGOTIATION,("Receive codec route refused"),(NULL));
 return GST_PAD_PROBE_DROP;
}
GstElement *pv_codec_route_new(void)
{
 GstElement *policy=gst_element_factory_make("capsfilter","codec-route");
 if(!policy) return NULL;
 struct route_selector *s=g_new0(struct route_selector,1);
 s->refs=2;s->bin=gst_bin_new(NULL);s->policy=policy;
 GstCaps *caps=gst_static_caps_get(&route_caps);
 g_object_set(s->policy,"caps",caps,NULL);gst_caps_unref(caps);
 gst_bin_add(GST_BIN(s->bin),s->policy);
 GstPad *input=gst_element_get_static_pad(s->policy,"sink"),*output=gst_element_get_static_pad(s->policy,"src");
 gst_element_add_pad(s->bin,gst_ghost_pad_new("sink",input));gst_element_add_pad(s->bin,gst_ghost_pad_new("src",output));
 /* The bin owns the configuration; the probe independently retains it through
  * any external capsfilter reference surviving bin disposal. */
 g_object_set_data_full(G_OBJECT(s->bin),"pixelview-route",s,selector_unref);
 gst_pad_add_probe(input,GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,select_route,s,selector_unref);
 gst_object_unref(input);gst_object_unref(output);return s->bin;
}
void pv_codec_route_admit_main422(GstElement *filter, gboolean admit)
{
 struct route_selector *s=g_object_get_data(G_OBJECT(filter),"pixelview-route");
 if(s) s->admit_main422=admit;
}
void pv_codec_route_admit_main444(GstElement *filter, gboolean admit)
{
 struct route_selector *s=g_object_get_data(G_OBJECT(filter),"pixelview-route");
 if(s) s->admit_main444=admit;
}
