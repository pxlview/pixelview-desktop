/* Synthetic compressed-fixture receive probe. No OBS, network or capture. */
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <VideoToolbox/VideoToolbox.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#ifdef DECODER_INTERPOSE_ONLY
/* Observe the actual decoder session, not VTIsHardwareDecodeSupported(). */
static OSStatus observe_create(CFAllocatorRef a, CMVideoFormatDescriptionRef f,
 CFDictionaryRef spec, CFDictionaryRef attrs,
 const VTDecompressionOutputCallbackRecord *cb, VTDecompressionSessionRef *out)
{
 OSStatus s = VTDecompressionSessionCreate(a,f,spec,attrs,cb,out);
 CFTypeRef hw = NULL;
 OSStatus q = s ? s : VTSessionCopyProperty(*out,
 kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder, kCFAllocatorDefault, &hw);
 fprintf(stderr,"HW_SESSION create=%d query=%d hardware=%s\n",(int)s,(int)q,
 hw && CFGetTypeID(hw)==CFBooleanGetTypeID() ? (CFBooleanGetValue(hw)?"true":"false") : "unknown");
 if(hw) CFRelease(hw);
 return s;
}
__attribute__((used)) static const struct { const void *replacement; const void *original; }
interpose[] __attribute__((section("__DATA,__interpose"))) = {
 {(const void *)observe_create,(const void *)VTDecompressionSessionCreate}
};

#else
int main(int argc, char **argv)
{
 if(argc != 5) { fprintf(stderr,"usage: probe fixture hevc|vp9 decoder format|auto\n"); return 2; }
 gst_init(&argc,&argv);
 gboolean vp9 = !strcmp(argv[2],"vp9");
 gchar *raw = !strcmp(argv[4],"auto") ? g_strdup("video/x-raw") : g_strdup_printf("video/x-raw,format=%s",argv[4]);
 gchar *desc = g_strdup_printf("%s ! %s ! %s name=decoder ! %s ! appsink name=out sync=false",vp9?"appsrc name=in format=time":"filesrc name=in",vp9?"vp9parse":"h265parse",argv[3],raw);
 GError *err=NULL;
 GstElement *pipe=gst_parse_launch(desc,&err);
 if(err) { fprintf(stderr,"PARSE_ERROR %s\n",err->message); return 3; }
 GstElement *src=gst_bin_get_by_name(GST_BIN(pipe),"in");
 GstElement *sink=gst_bin_get_by_name(GST_BIN(pipe),"out");
 if(!vp9) g_object_set(src,"location",argv[1],NULL);
 gchar *data=NULL; gsize length=0;
 if(vp9) {
  if(!g_file_get_contents(argv[1],&data,&length,&err) || length<32 || memcmp(data,"DKIF",4)) return 4;
  guint8 *d=(guint8*)data;
  GstCaps *caps=gst_caps_new_simple("video/x-vp9","width",G_TYPE_INT,(int)GST_READ_UINT16_LE(d+12),"height",G_TYPE_INT,(int)GST_READ_UINT16_LE(d+14),"framerate",GST_TYPE_FRACTION,24,1,NULL);
  gst_app_src_set_caps(GST_APP_SRC(src),caps); gst_caps_unref(caps);
 }
 gst_element_set_state(pipe,GST_STATE_PLAYING);
 if(vp9) {
  gsize p=32; guint n=0;
  while(p+12<=length) {
   guint size=GST_READ_UINT32_LE((guint8*)data+p); p+=12;
   if(size>length-p) return 4;
   GstBuffer *b=gst_buffer_new_allocate(NULL,size,NULL); gst_buffer_fill(b,0,data+p,size);
   GST_BUFFER_PTS(b)=gst_util_uint64_scale(n++,GST_SECOND,24); GST_BUFFER_DURATION(b)=GST_SECOND/24;
   gst_app_src_push_buffer(GST_APP_SRC(src),b); p+=size;
  }
  gst_app_src_end_of_stream(GST_APP_SRC(src)); g_free(data);
 }
 GstBus *bus=gst_element_get_bus(pipe); guint frames=0; gboolean failed=FALSE; guint64 checksum=0;
 gint64 deadline=g_get_monotonic_time()+15*G_TIME_SPAN_SECOND;
 while(g_get_monotonic_time()<deadline) {
  GstSample *sample=gst_app_sink_try_pull_sample(GST_APP_SINK(sink),GST_SECOND/10);
  if(sample) {
   GstCaps *caps=gst_sample_get_caps(sample); GstVideoInfo info;
   if(!frames && gst_video_info_from_caps(&info,caps)) {
    gchar *s=gst_caps_to_string(caps);
    printf("RAW caps=%s depth=%u chroma_w_sub=%u chroma_h_sub=%u\n",s,GST_VIDEO_INFO_COMP_DEPTH(&info,0),GST_VIDEO_FORMAT_INFO_W_SUB(info.finfo,1),GST_VIDEO_FORMAT_INFO_H_SUB(info.finfo,1)); g_free(s);
    GstElement *dec=gst_bin_get_by_name(GST_BIN(pipe),"decoder"); GstPad *pad=gst_element_get_static_pad(dec,"sink"); GstCaps *in=gst_pad_get_current_caps(pad);
    s=gst_caps_to_string(in); printf("COMPRESSED caps=%s\n",s);g_free(s);gst_caps_unref(in);gst_object_unref(pad);gst_object_unref(dec);
   }
   GstMapInfo map; if(gst_buffer_map(gst_sample_get_buffer(sample),&map,GST_MAP_READ)) {
    const gchar *dump=g_getenv("DECODER_FIRST_FRAME");
    if(!frames && dump && !g_file_set_contents(dump,(const gchar*)map.data,map.size,NULL)) return 5;
    for(gsize i=0;i<map.size;i++) checksum=checksum*33+map.data[i]; gst_buffer_unmap(gst_sample_get_buffer(sample),&map); }
   frames++;gst_sample_unref(sample);
  }
  GstMessage *msg=gst_bus_pop_filtered(bus,GST_MESSAGE_ERROR);
  if(msg) { gchar *debug=NULL;gst_message_parse_error(msg,&err,&debug);fprintf(stderr,"DECODE_ERROR %s (%s)\n",err->message,debug?debug:"");g_free(debug);g_error_free(err);gst_message_unref(msg);failed=TRUE;break; }
  if(!sample && gst_app_sink_is_eos(GST_APP_SINK(sink))) break;
 }
 printf("RESULT frames=%u checksum=%" G_GUINT64_FORMAT "\n",frames,checksum);
 gst_element_set_state(pipe,GST_STATE_NULL);gst_object_unref(bus);gst_object_unref(src);gst_object_unref(sink);gst_object_unref(pipe);g_free(raw);g_free(desc);
 return frames && !failed ? 0 : 1;
}
#endif
