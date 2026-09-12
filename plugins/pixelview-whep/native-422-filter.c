/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native-422-filter.h"
#include "native-422-rate.h"
#include "native-422-diagnostic.h"
#include "main422-25p.h"
#include <gst/rtp/gstrtpbuffer.h>

struct rate_observer { gint refs; GMutex lock; struct pv422_rate rate; gboolean attached, ordinary; };
static void rate_unref(gpointer p)
{
 struct rate_observer *r=p;
 if(g_atomic_int_dec_and_test(&r->refs)) { g_mutex_clear(&r->lock); g_free(r); }
}
static GstPadProbeReturn rate_probe(GstPad *pad,GstPadProbeInfo *info,gpointer opaque)
{
 (void)pad;
 struct rate_observer *r=opaque; GstBuffer *b=GST_PAD_PROBE_INFO_BUFFER(info);
 GstRTPBuffer packet=GST_RTP_BUFFER_INIT;
 g_mutex_lock(&r->lock);
 if(r->ordinary) { g_mutex_unlock(&r->lock);return GST_PAD_PROBE_REMOVE; }
 if(!b || gst_buffer_get_size(b)>65536 || !gst_rtp_buffer_map(b,GST_MAP_READ,&packet)) {
  if(!r->rate.failed) r->rate.reason="ordered-rtp-map-or-size";
  r->rate.failed=true;
 }
 else {
  pv422_rate_packet(&r->rate,gst_rtp_buffer_get_ssrc(&packet),gst_rtp_buffer_get_seq(&packet),
   gst_rtp_buffer_get_timestamp(&packet),gst_rtp_buffer_get_marker(&packet),
   GST_BUFFER_FLAG_IS_SET(b,GST_BUFFER_FLAG_DISCONT),g_get_monotonic_time()*1000ULL);
  gst_rtp_buffer_unmap(&packet);
 }
 g_mutex_unlock(&r->lock); return GST_PAD_PROBE_OK;
}

/* Only the selected native branch instantiates this decoder/queue. Its
 * serialized input chain pushes owned RAW preview buffers to the leaky queue. */
typedef struct {
 GstElement parent;
 GstPad *sink, *src;
 GstElement *queue; /* bin-owned, used only while streaming */
 struct pv_native422 *decoder;
 pv_native422_delivery delivery;
 void *opaque;
 GDestroyNotify destroy;
 GstCaps *caps;
 GstSegment segment;
 gboolean native_started, failed, preview_nv12;
 gboolean require_rtp, rate_active;
 struct rate_observer *rate;
 GstCaps *decode_caps;
 GstClockTime last_pts;
 const char *failure_reason;
 guint64 au_count, decoded_count, chain_enter, decode_ns, delivery_ns, push_ns;
} PvNativeTap;
typedef struct { GstElementClass parent; } PvNativeTapClass;
G_DEFINE_TYPE(PvNativeTap, pv_native_tap, GST_TYPE_ELEMENT)
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
 GST_STATIC_CAPS("video/x-h265,stream-format=hvc1,alignment=au;video/x-h264;video/x-vp9"));
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static gboolean tap_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
 (void)pad;
 PvNativeTap *tap = (PvNativeTap *)parent;
 if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
  GstCaps *caps; gst_event_parse_caps(event, &caps);
  const GstStructure *s = gst_caps_is_fixed(caps) ? gst_caps_get_structure(caps, 0) : NULL;
  gboolean native = s && gst_structure_has_name(s, "video/x-h265") &&
   !g_strcmp0(gst_structure_get_string(s, "profile"), "main-422-10");
  /* Switching a populated compressed queue to leaky could drop reference AUs.
   * Native admission therefore starts only on a new filter/receive attempt. */
  if (native && tap->caps && !tap->native_started) tap->failed = TRUE;
  if (tap->native_started && (!native || !gst_caps_is_equal(tap->caps, caps))) tap->failed = TRUE;
  if (tap->failed) { gst_event_unref(event); return FALSE; }
  gst_caps_replace(&tap->caps, caps);
  if (native) {
   int w=0,h=0,n=0,d=0;
   tap->native_started = TRUE;
   if (tap->require_rtp) {
    if(!tap->rate || !gst_structure_get_int(s,"width",&w) || w!=1920 ||
       !gst_structure_get_int(s,"height",&h) || h!=1080 ||
       (gst_structure_has_field(s,"framerate") &&
        (!gst_structure_get_fraction(s,"framerate",&n,&d) || n<=0 || d<=0 || !pv422_rate_supported(n,d)))) tap->failed=TRUE;
    gst_event_unref(event); return !tap->failed;
   }
   if (!gst_structure_get_int(s,"width",&w) || !gst_structure_get_int(s,"height",&h) ||
       !gst_structure_get_fraction(s,"framerate",&n,&d) || w<2 || w>1920 || h<2 || h>1080 || (w&1) || (h&1) || n<=0 || d<=0) {
    tap->failed=TRUE; gst_event_unref(event); return FALSE;
   }
   /* Drop-oldest is enabled only AFTER selecting the native RAW route. */
   g_object_set(tap->queue, "leaky", 2, NULL);
   GstCaps *raw = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, tap->preview_nv12 ? "NV12" : "P010_10LE",
    "width", G_TYPE_INT, w, "height", G_TYPE_INT, h, "framerate", GST_TYPE_FRACTION, n,d,
    "colorimetry", G_TYPE_STRING, "bt709", "chroma-site", G_TYPE_STRING, "mpeg2",
    "interlace-mode", G_TYPE_STRING, "progressive", "pixelview-native422", G_TYPE_BOOLEAN, TRUE, NULL);
   gst_event_unref(event); event = gst_event_new_caps(raw); gst_caps_unref(raw);
  }
 } else if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT) {
  const GstSegment *segment; gst_event_parse_segment(event, &segment); tap->segment = *segment;
  if(tap->require_rtp && tap->native_started && !tap->rate_active) { gst_event_unref(event); return TRUE; }
 }
 return gst_pad_push_event(tap->src, event);
}
static GstFlowReturn tap_chain(GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
 (void)pad;
 PvNativeTap *tap = (PvNativeTap *)parent;
 if (tap->failed) { gst_buffer_unref(buffer); return GST_FLOW_NOT_NEGOTIATED; }
 if (!tap->native_started) return gst_pad_push(tap->src, buffer);
 struct pv422_rate rate={0};
 guint64 now=0;
#define TAP_REJECT(why) do { tap->failure_reason=(why); goto rate_failed; } while(0)
 if(tap->require_rtp) {
  tap->au_count++;
  g_mutex_lock(&tap->rate->lock); rate=tap->rate->rate; g_mutex_unlock(&tap->rate->lock);
  now=(guint64)g_get_monotonic_time()*1000; tap->chain_enter=now;
  if(rate.failed) TAP_REJECT(rate.reason ? rate.reason : "ordered-rtp-observer-failed");
  if(!rate.initialized) TAP_REJECT("au-rtp-uninitialized");
  if(now<rate.last) TAP_REJECT("au-clock-backward");
  if(now-rate.last>500000000ULL) TAP_REJECT("au-ordered-rtp-stale");
  /* The acquisition deadline includes the first random-access AU, even when
   * RTP classification already completed. Exactly three seconds is admitted. */
  if(!tap->rate_active && now<rate.started) TAP_REJECT("au-acquisition-clock-backward");
  if(!tap->rate_active && now-rate.started>3000000000ULL) TAP_REJECT("au-acquisition-expired");
  if(!rate.num || (!tap->rate_active && GST_BUFFER_FLAG_IS_SET(buffer,GST_BUFFER_FLAG_DELTA_UNIT))) {
   gst_buffer_unref(buffer); return GST_FLOW_OK;
  }
  /* Normal-build 25p restriction, never a bypass of finite RTP,
   * strict headers/color/geometry, native hardware or owner admission. */
  if(pv_main422_25p_enabled() && (guint64)rate.num != 25ULL*rate.den)
   TAP_REJECT("au-caps-rate-conflict");
  if(!tap->rate_active) {
   int n=0,d=0;
   const GstStructure *wire=gst_caps_get_structure(tap->caps,0);
   if(gst_structure_has_field(wire,"framerate") &&
      (!gst_structure_get_fraction(wire,"framerate",&n,&d) || (guint64)n*rate.den!=(guint64)d*rate.num)) TAP_REJECT("au-caps-rate-conflict");
   tap->decode_caps=gst_caps_copy(tap->caps);
   gst_caps_set_simple(tap->decode_caps,"framerate",GST_TYPE_FRACTION,rate.num,rate.den,NULL);
   pv_native422_require_initial_format(tap->decoder,rate.num,rate.den);
   GstCaps *raw_caps=gst_caps_new_simple("video/x-raw","format",G_TYPE_STRING,tap->preview_nv12?"NV12":"P010_10LE",
    "width",G_TYPE_INT,1920,"height",G_TYPE_INT,1080,"framerate",GST_TYPE_FRACTION,rate.num,rate.den,
    "colorimetry",G_TYPE_STRING,"bt709","chroma-site",G_TYPE_STRING,"mpeg2","interlace-mode",G_TYPE_STRING,"progressive",
    "pixelview-native422",G_TYPE_BOOLEAN,TRUE,NULL);
   gboolean accepted=gst_pad_push_event(tap->src,gst_event_new_caps(raw_caps)); gst_caps_unref(raw_caps);
   if(!accepted) TAP_REJECT("raw-caps-refused");
   if(tap->segment.format!=GST_FORMAT_TIME) TAP_REJECT("au-segment-not-time");
   if(!gst_pad_push_event(tap->src,gst_event_new_segment(&tap->segment))) TAP_REJECT("raw-segment-refused");
   g_object_set(tap->queue,"leaky",2,NULL); tap->rate_active=TRUE; tap->last_pts=GST_CLOCK_TIME_NONE;
  }
  GstClockTime duration=gst_util_uint64_scale(rate.den,GST_SECOND,rate.num);
  if(!GST_BUFFER_PTS_IS_VALID(buffer)) TAP_REJECT("au-pts-missing");
  if(GST_CLOCK_TIME_IS_VALID(tap->last_pts) && GST_BUFFER_PTS(buffer)<=tap->last_pts) TAP_REJECT("au-pts-nonmonotonic");
  if(GST_CLOCK_TIME_IS_VALID(tap->last_pts) && GST_BUFFER_PTS(buffer)-tap->last_pts>100000000ULL) TAP_REJECT("au-pts-gap");
  if(GST_BUFFER_DURATION_IS_VALID(buffer) && (GST_BUFFER_DURATION(buffer)+1<duration || GST_BUFFER_DURATION(buffer)>duration+1)) TAP_REJECT("au-duration-conflict");
  tap->last_pts=GST_BUFFER_PTS(buffer);
  buffer=gst_buffer_make_writable(buffer); GST_BUFFER_DURATION(buffer)=duration;
 }
 GstSample *sample = gst_sample_new(buffer, tap->decode_caps ? tap->decode_caps : tap->caps, &tap->segment, NULL);
 struct pv_native422_frame frame;
 GstBuffer *raw = NULL;
 guint64 decode_start=(guint64)g_get_monotonic_time()*1000;
 gboolean ok = pv_native422_decode_preview(tap->decoder, sample, &frame, &raw) == PV_NATIVE422_FRAME;
 guint64 delivery_start=(guint64)g_get_monotonic_time()*1000;
 tap->decode_ns=delivery_start-decode_start;
 if(ok) { tap->decoded_count++; ok=tap->delivery(tap->opaque, sample, &frame); }
 tap->delivery_ns=(guint64)g_get_monotonic_time()*1000-delivery_start;
 gst_sample_unref(sample); gst_buffer_unref(buffer);
 if (!ok) {
  tap->failed=TRUE; if (raw) gst_buffer_unref(raw);
  GST_ELEMENT_ERROR(tap, STREAM, FORMAT, ("Native 422 requires unchanged limited-range BT709 Main42210"), (NULL));
  return GST_FLOW_NOT_NEGOTIATED;
 }
 /* An optional preview allocation failure cannot invalidate native delivery. */
 if (raw && !tap->preview_nv12) {
  GstMapInfo input;
  GstBuffer *p010 = NULL;
  if (gst_buffer_map(raw, &input, GST_MAP_READ)) {
   guint8 *words = g_try_malloc(input.size * 2);
   if (words) {
    for (gsize i=0; i<input.size; i++) GST_WRITE_UINT16_LE(words+2*i, (guint16)input.data[i] << 8);
    p010 = gst_buffer_new_wrapped(words, input.size*2);
    GST_BUFFER_PTS(p010)=GST_BUFFER_PTS(raw); GST_BUFFER_DURATION(p010)=GST_BUFFER_DURATION(raw);
   }
   gst_buffer_unmap(raw, &input);
  }
  gst_buffer_unref(raw); raw=p010;
 }
 guint64 push_start=(guint64)g_get_monotonic_time()*1000;
 GstFlowReturn flow=raw ? gst_pad_push(tap->src, raw) : GST_FLOW_OK;
 tap->push_ns=(guint64)g_get_monotonic_time()*1000-push_start;
 return flow;
rate_failed:;
 struct pv_native422_timing timing=pv_native422_get_timing(tap->decoder);
 tap->failed=TRUE;
 struct pv422_diagnostic diagnostic={.reason=pv422_diagnostic_reason(tap->failure_reason),.values={
  tap->rate_active,tap->au_count,tap->decoded_count,now,rate.started,rate.last,rate.failure_now,rate.total_packets,
  rate.sequence,rate.failure_sequence,rate.failure_delta,rate.intervals,rate.candidates,rate.num,rate.den,
  GST_BUFFER_PTS(buffer),tap->last_pts,GST_BUFFER_DURATION(buffer),tap->decode_ns,tap->delivery_ns,tap->push_ns,
  timing.begin,timing.configured,timing.session_begin,timing.session_end,timing.submitted,timing.returned,timing.waited,
  timing.callback,timing.first_callback,timing.packed,timing.previewed}};
 gst_element_message_full_with_details(GST_ELEMENT(tap),GST_MESSAGE_ERROR,GST_STREAM_ERROR,GST_STREAM_ERROR_FORMAT,
  g_strdup_printf("Native 422 finite-rate refusal: %s",tap->failure_reason),
  g_strdup_printf("active=%d au=%" G_GUINT64_FORMAT " decoded=%" G_GUINT64_FORMAT " now=%" G_GUINT64_FORMAT
   " started=%" G_GUINT64_FORMAT " last_rtp=%" G_GUINT64_FORMAT " failure_now=%" G_GUINT64_FORMAT
   " packets=%" G_GUINT64_FORMAT " seq=%u failure_seq=%u marker_delta=%u intervals=%u candidates=%u rate=%u/%u"
   " pts=%" G_GUINT64_FORMAT " last_pts=%" G_GUINT64_FORMAT " duration=%" G_GUINT64_FORMAT
   " previous_decode_ns=%" G_GUINT64_FORMAT " previous_delivery_ns=%" G_GUINT64_FORMAT " previous_push_ns=%" G_GUINT64_FORMAT
   " native_begin=%" G_GUINT64_FORMAT " configured=%" G_GUINT64_FORMAT " session_begin=%" G_GUINT64_FORMAT " session_end=%" G_GUINT64_FORMAT
   " submitted=%" G_GUINT64_FORMAT " returned=%" G_GUINT64_FORMAT " waited=%" G_GUINT64_FORMAT " callback=%" G_GUINT64_FORMAT
   " first_callback=%" G_GUINT64_FORMAT " packed=%" G_GUINT64_FORMAT " previewed=%" G_GUINT64_FORMAT,
   tap->rate_active,tap->au_count,tap->decoded_count,now,rate.started,rate.last,rate.failure_now,rate.total_packets,
   rate.sequence,rate.failure_sequence,rate.failure_delta,rate.intervals,rate.candidates,rate.num,rate.den,
   GST_BUFFER_PTS(buffer),tap->last_pts,GST_BUFFER_DURATION(buffer),tap->decode_ns,tap->delivery_ns,tap->push_ns,
   timing.begin,timing.configured,timing.session_begin,timing.session_end,timing.submitted,timing.returned,timing.waited,
   timing.callback,timing.first_callback,timing.packed,timing.previewed),
  __FILE__,GST_FUNCTION,__LINE__,pv422_diagnostic_details(&diagnostic));
 gst_buffer_unref(buffer);
 return GST_FLOW_NOT_NEGOTIATED;
#undef TAP_REJECT
}
static void tap_finalize(GObject *object)
{
 PvNativeTap *tap = (PvNativeTap *)object;
 pv_native422_destroy(tap->decoder);
 gst_caps_replace(&tap->caps, NULL);
 gst_caps_replace(&tap->decode_caps,NULL);
 if(tap->rate) rate_unref(tap->rate);
 if (tap->destroy) tap->destroy(tap->opaque);
 G_OBJECT_CLASS(pv_native_tap_parent_class)->finalize(object);
}
static void pv_native_tap_class_init(PvNativeTapClass *klass)
{
 G_OBJECT_CLASS(klass)->finalize = tap_finalize;
 gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass), "Pixelview native raw tap", "Filter/Video", "Owned optional native preview", "Pixelview");
 gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS(klass), &sink_template);
 gst_element_class_add_static_pad_template(GST_ELEMENT_CLASS(klass), &src_template);
}
static void pv_native_tap_init(PvNativeTap *tap)
{
 tap->decoder = pv_native422_create(); tap->preview_nv12=TRUE; gst_segment_init(&tap->segment, GST_FORMAT_UNDEFINED);
 tap->sink = gst_pad_new_from_static_template(&sink_template, "sink");
 tap->src = gst_pad_new_from_static_template(&src_template, "src");
 gst_pad_set_chain_function(tap->sink, tap_chain); gst_pad_set_event_function(tap->sink, tap_event);
 gst_element_add_pad(GST_ELEMENT(tap), tap->sink); gst_element_add_pad(GST_ELEMENT(tap), tap->src);
}
static GstElement *native_filter_new(pv_native422_delivery delivery, void *opaque, GDestroyNotify destroy)
{
 PvNativeTap *tap = g_object_new(pv_native_tap_get_type(), "name", "native-transform", NULL);
 tap->delivery=delivery; tap->opaque=opaque; tap->destroy=destroy;
 GstElement *queue = gst_element_factory_make("queue", "preview");
 if (!delivery || !queue) { if (queue) gst_object_unref(queue); gst_object_unref(tap); return NULL; }
 tap->queue=queue;
 g_object_set(queue, "max-size-buffers", 3u, "max-size-bytes", 16777216u, "max-size-time", (guint64)200000000, "leaky", 0, NULL);
 GstElement *bin = gst_bin_new(NULL);
 gst_bin_add_many(GST_BIN(bin), GST_ELEMENT(tap), queue, NULL);
 if (!gst_element_link(GST_ELEMENT(tap), queue)) { gst_object_unref(bin); return NULL; }
 GstPad *out = gst_element_get_static_pad(queue, "src");
 gboolean ok = gst_element_add_pad(bin, gst_ghost_pad_new("sink", tap->sink)) &&
  gst_element_add_pad(bin, gst_ghost_pad_new("src", out));
 gst_object_unref(out);
 if (!ok) { gst_object_unref(bin); return NULL; }
 return bin;
}
/* request-encoded-filter receives downstream RAW caps, not the selected codec.
 * Inspect the first parsed CAPS event using the supported pad-probe API. The
 * ordinary branch is a stock capsfilter: no AU callback, native decoder or queue.
 * Native-only elements are inserted before the first AU, never by dropping or
 * moving compressed buffers. A profile-family change requires a new attempt. */
struct route_selector {
 gint refs;
 GstElement *bin, *policy; /* borrowed while the event probe is installed */
 pv_native422_delivery delivery;
 void *opaque;
 GDestroyNotify destroy;
 struct rate_observer *rate;
 gboolean require_rtp, preview_nv12, selected;
};
static GstPadProbeReturn refuse_route(GstPad *pad,GstPadProbeInfo *info,gpointer opaque)
{
 (void)pad;(void)opaque;
 gst_mini_object_unref(GST_MINI_OBJECT(GST_PAD_PROBE_INFO_DATA(info)));
 GST_PAD_PROBE_INFO_DATA(info)=NULL;
 GST_PAD_PROBE_INFO_FLOW_RETURN(info)=GST_FLOW_NOT_NEGOTIATED;
 return GST_PAD_PROBE_HANDLED;
}
static void selector_unref(gpointer opaque)
{
 struct route_selector *s=opaque;
 if(!g_atomic_int_dec_and_test(&s->refs)) return;
 if(s->rate) rate_unref(s->rate);
 if(s->destroy) s->destroy(s->opaque);
 g_free(s);
}
static gboolean selector_delivery(void *opaque,GstSample *sample,const struct pv_native422_frame *frame)
{ struct route_selector *s=opaque;return s->delivery(s->opaque,sample,frame); }
static GstPadProbeReturn select_route(GstPad *pad,GstPadProbeInfo *info,gpointer opaque)
{
 (void)pad;
 if(GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info))!=GST_EVENT_CAPS) return GST_PAD_PROBE_OK;
 struct route_selector *s=opaque;
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
 gboolean native=gst_structure_has_name(wire,"video/x-h265") &&
  !g_strcmp0(gst_structure_get_string(wire,"profile"),"main-422-10");
 GstCaps *policy=gst_caps_new_empty_simple(gst_structure_get_name(wire));
 if(gst_structure_has_name(wire,"video/x-h265")) {
  gst_caps_set_simple(policy,"stream-format",G_TYPE_STRING,"hvc1","alignment",G_TYPE_STRING,"au",NULL);
  /* Pin the actual profile. Never route a later native stream into stock 420
   * decoding or silently enable native admission on a populated ordinary path. */
  const char *profile=gst_structure_get_string(wire,"profile");
  if(!profile || (g_strcmp0(profile,"main") && g_strcmp0(profile,"main-10") && !native)) {
   gst_caps_unref(policy);goto failed;
  }
  gst_caps_set_simple(policy,"profile",G_TYPE_STRING,profile,NULL);
 }
 g_object_set(s->policy,"caps",policy,NULL);gst_caps_unref(policy);
 if(!native && s->rate) { g_mutex_lock(&s->rate->lock);s->rate->ordinary=TRUE;g_mutex_unlock(&s->rate->lock); }
 if(native) {
  g_atomic_int_inc(&s->refs);
  GstElement *branch=native_filter_new(selector_delivery,s,selector_unref);
  if(!branch) goto failed;
  PvNativeTap *tap=(PvNativeTap *)gst_bin_get_by_name(GST_BIN(branch),"native-transform");
  tap->preview_nv12=s->preview_nv12;tap->require_rtp=s->require_rtp;
  tap->rate=s->rate;if(tap->rate) g_atomic_int_inc(&tap->rate->refs);
  gst_object_unref(tap);
  if(!gst_bin_add(GST_BIN(s->bin),branch)) { gst_object_unref(branch);goto failed; }
  GstPad *output=gst_element_get_static_pad(s->bin,"src");
  GstPad *target=gst_element_get_static_pad(branch,"src");
  gboolean ok=gst_ghost_pad_set_target(GST_GHOST_PAD(output),NULL) &&
   gst_element_link(s->policy,branch) && gst_ghost_pad_set_target(GST_GHOST_PAD(output),target) &&
   gst_element_sync_state_with_parent(branch);
  gst_object_unref(target);gst_object_unref(output);
  if(!ok) goto failed;
 }
 return GST_PAD_PROBE_OK;
failed:
 gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST,refuse_route,NULL,NULL);
 GST_ELEMENT_ERROR(s->bin,CORE,NEGOTIATION,("Native receive branch unavailable"),(NULL));
 return GST_PAD_PROBE_DROP;
}
GstElement *pv_native422_filter_new(pv_native422_delivery delivery,void *opaque,GDestroyNotify destroy)
{
 struct route_selector *s=g_new0(struct route_selector,1);
 s->refs=1;s->delivery=delivery;s->opaque=opaque;s->destroy=destroy;s->preview_nv12=TRUE;
 s->bin=gst_bin_new(NULL);s->policy=gst_element_factory_make("capsfilter","codec-route");
 if(!delivery || !s->policy) { if(s->policy)gst_object_unref(s->policy);gst_object_unref(s->bin);selector_unref(s);return NULL; }
 GstCaps *caps=gst_static_caps_get(&sink_template.static_caps);
 g_object_set(s->policy,"caps",caps,NULL);gst_caps_unref(caps);
 gst_bin_add(GST_BIN(s->bin),s->policy);
 GstPad *input=gst_element_get_static_pad(s->policy,"sink"),*output=gst_element_get_static_pad(s->policy,"src");
 gst_element_add_pad(s->bin,gst_ghost_pad_new("sink",input));gst_element_add_pad(s->bin,gst_ghost_pad_new("src",output));
 /* The bin owns the configuration; a native child independently retains it
  * through any external references surviving bin disposal. */
 g_object_set_data_full(G_OBJECT(s->bin),"pixelview-route",s,selector_unref);
 g_atomic_int_inc(&s->refs);
 gst_pad_add_probe(input,GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,select_route,s,selector_unref);
 gst_object_unref(input);gst_object_unref(output);return s->bin;
}
static void rate_closure_unref(gpointer opaque,GClosure *closure) { (void)closure; rate_unref(opaque); }
static void rate_element_added(GstBin *bin,GstBin *sub,GstElement *element,gpointer opaque)
{
 (void)bin; (void)sub;
 GstElementFactory *f=gst_element_get_factory(element);
 if(!f || g_strcmp0(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(f)),"rtph265depay")) return;
 struct rate_observer *r=opaque;
 g_mutex_lock(&r->lock);
 if(r->attached) { if(!r->rate.failed) r->rate.reason="ordered-rtp-duplicate-depay"; r->rate.failed=true; g_mutex_unlock(&r->lock); return; }
 r->attached=TRUE; g_atomic_int_inc(&r->refs); g_mutex_unlock(&r->lock);
 GstPad *input=gst_element_get_static_pad(element,"sink");
 if(!input || !gst_pad_add_probe(input,GST_PAD_PROBE_TYPE_BUFFER,rate_probe,r,rate_unref)) {
  g_mutex_lock(&r->lock); if(!r->rate.failed) r->rate.reason="ordered-rtp-probe-attach"; r->rate.failed=true; g_mutex_unlock(&r->lock); rate_unref(r);
 }
 if(input) gst_object_unref(input);
}
gboolean pv_native422_filter_require_rtp(GstElement *filter, GstElement *receiver)
{
 struct route_selector *s=g_object_get_data(G_OBJECT(filter),"pixelview-route");
 if(!s || s->rate || !GST_IS_BIN(receiver)) return FALSE;
 s->require_rtp=TRUE;
 struct rate_observer *r=g_new0(struct rate_observer,1);r->refs=2;g_mutex_init(&r->lock);s->rate=r;
 /* rswebrtc emits request-encoded-filter BEFORE creating parsebin/depay.
  * Its public deep-element-added signal observes the later native topology. */
 gulong id=g_signal_connect_data(receiver,"deep-element-added",G_CALLBACK(rate_element_added),r,rate_closure_unref,0);
 if(!id) { rate_unref(r); r->rate.failed=true; }
 return id!=0;
}
void pv_native422_filter_preview_format(GstElement *filter, gboolean nv12)
{
 struct route_selector *s=g_object_get_data(G_OBJECT(filter),"pixelview-route");
 if(s) s->preview_nv12=nv12;
}
