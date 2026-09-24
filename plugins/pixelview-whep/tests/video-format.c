/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../video-format.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static GstCaps *p010(const char *colorimetry)
{
 GstCaps *caps=gst_caps_from_string("video/x-raw,format=P010_10LE,width=64,height=32,framerate=30/1");
 if(colorimetry) gst_structure_set(gst_caps_get_structure(caps,0),"colorimetry",G_TYPE_STRING,colorimetry,NULL);
 return caps;
}
static void expect(const char *colorimetry,enum pixelview_color color,const char *reason)
{
 GstCaps *caps=p010(colorimetry); GstVideoInfo info;
 const char *got=pixelview_video_color_mismatch(caps,color);
 if((got==NULL)!=(reason==NULL) || (got && strcmp(got,reason))) {
  fprintf(stderr,"%s mode %d: expected %s got %s\n",colorimetry?colorimetry:"(none)",color,reason?reason:"accept",got?got:"accept");
  assert(0);
 }
 /* SDR caps with any complete colorimetry parse; the SDR frame contract refuses them. */
 if((color!=PIXELVIEW_COLOR_SDR || !reason) && pixelview_video_info(caps,color,&info)!=(reason==NULL)) { fprintf(stderr,"%s mode %d: info disagrees with reason %s\n",colorimetry?colorimetry:"(none)",color,reason?reason:"accept"); assert(0); }
 if(!reason && color!=PIXELVIEW_COLOR_SDR) {
  GstBuffer *b=gst_buffer_new_allocate(NULL,info.size,NULL); GstVideoFrame m={0};
  assert(gst_video_frame_map(&m,&info,b,GST_MAP_READ));
  struct obs_source_frame2 f; assert(pixelview_video_frame(&m,color,&f));
  assert(f.format==VIDEO_FORMAT_P010 && f.range==VIDEO_RANGE_PARTIAL);
  assert(f.trc==(color==PIXELVIEW_COLOR_PQ?VIDEO_TRC_PQ:VIDEO_TRC_HLG));
  float matrix[16],min[3],max[3];
  assert(video_format_get_parameters_for_format(color==PIXELVIEW_COLOR_PQ?VIDEO_CS_2100_PQ:VIDEO_CS_2100_HLG,VIDEO_RANGE_PARTIAL,VIDEO_FORMAT_P010,matrix,min,max));
  assert(!memcmp(matrix,f.color_matrix,sizeof(matrix)) && !memcmp(min,f.color_range_min,sizeof(min)));
  /* The frame must not satisfy the SDR contract or the other HDR transfer. */
  assert(!pixelview_video_frame(&m,PIXELVIEW_COLOR_SDR,&f));
  assert(!pixelview_video_frame(&m,color==PIXELVIEW_COLOR_PQ?PIXELVIEW_COLOR_HLG:PIXELVIEW_COLOR_PQ,&f));
  gst_video_frame_unmap(&m); gst_buffer_unref(b);
 }
 gst_caps_unref(caps);
}
static void hdr_cases(void)
{
 const enum pixelview_color PQ=PIXELVIEW_COLOR_PQ, HLG=PIXELVIEW_COLOR_HLG, SDR=PIXELVIEW_COLOR_SDR;
 expect("bt2100-pq",PQ,NULL); expect("bt2100-hlg",HLG,NULL);
 expect("bt2100-pq",HLG,PV_COLOR_TRANSFER_MISMATCH); expect("bt2100-hlg",PQ,PV_COLOR_TRANSFER_MISMATCH);
 expect("bt2020",PQ,NULL); expect("bt2020-10",HLG,NULL); /* VP9: gamut only, transfer unsignalled */
 expect(NULL,PQ,NULL); expect("0:0:0:0",HLG,NULL);      /* wholly unsignalled: trust the operator */
 expect("bt709",PQ,PV_COLOR_SDR_SOURCE); expect("bt709",HLG,PV_COLOR_SDR_SOURCE);
 expect("2:9:14:9",PQ,PV_COLOR_UNSUPPORTED);            /* full-range BT.2020 */
 expect("1:9:14:0",PQ,PV_COLOR_UNSUPPORTED);            /* unsignalled gamut with SDR curve */
 expect("invalid",PQ,PV_COLOR_UNSUPPORTED);
 expect("bt2100-pq",SDR,PV_COLOR_HDR_SOURCE); expect("bt2100-hlg",SDR,PV_COLOR_HDR_SOURCE); expect("bt2020",SDR,PV_COLOR_HDR_SOURCE);
 expect("bt709",SDR,NULL); expect(NULL,SDR,PV_COLOR_UNSUPPORTED);
 GstCaps *nv12=gst_caps_from_string("video/x-raw,format=NV12,width=64,height=32,colorimetry=bt2100-pq"); GstVideoInfo info;
 assert(!pixelview_video_info(nv12,PQ,&info)); gst_caps_unref(nv12); /* HDR is ten-bit P010 only */
}
int main(void)
{
 gst_init(NULL,NULL);
 GstVideoInfo i; gst_video_info_set_format(&i,GST_VIDEO_FORMAT_P010_10LE,64,32);
 gst_video_colorimetry_from_string(&i.colorimetry,"bt709");
 GstBuffer *b=gst_buffer_new_allocate(NULL,i.size,NULL);
 GstVideoFrame m={0}; assert(gst_video_frame_map(&m,&i,b,GST_MAP_READ));
 struct obs_source_frame2 f;
 assert(pixelview_video_frame(&m,PIXELVIEW_COLOR_SDR,&f));
 assert(f.format==VIDEO_FORMAT_P010 && f.range==VIDEO_RANGE_PARTIAL);
 assert(f.data[1]==GST_VIDEO_FRAME_PLANE_DATA(&m,1) && f.linesize[1]==128);
 m.info.colorimetry.transfer=GST_VIDEO_TRANSFER_SMPTE2084;
 assert(!pixelview_video_frame(&m,PIXELVIEW_COLOR_SDR,&f));
 m.info.colorimetry.transfer=GST_VIDEO_TRANSFER_BT709;
 m.info.colorimetry.range=GST_VIDEO_COLOR_RANGE_0_255;
 assert(!pixelview_video_frame(&m,PIXELVIEW_COLOR_SDR,&f)); // Full-range VT fidelity is not proven.
 m.info.colorimetry.range=GST_VIDEO_COLOR_RANGE_16_235;
 m.info.colorimetry.primaries=GST_VIDEO_COLOR_PRIMARIES_BT2020;
 assert(!pixelview_video_frame(&m,PIXELVIEW_COLOR_SDR,&f));
 m.info.colorimetry.primaries=GST_VIDEO_COLOR_PRIMARIES_BT709;
 m.info.stride[0]=-128; assert(!pixelview_video_frame(&m,PIXELVIEW_COLOR_SDR,&f));
 m.info.stride[0]=1; assert(!pixelview_video_frame(&m,PIXELVIEW_COLOR_SDR,&f));
 m.info.stride[0]=128;
 m.data[0]=m.map[0].data+m.map[0].size-1; assert(!pixelview_video_frame(&m,PIXELVIEW_COLOR_SDR,&f));
 gst_video_frame_unmap(&m);gst_buffer_unref(b);
 const GstVideoFormat formats[]={GST_VIDEO_FORMAT_NV12,GST_VIDEO_FORMAT_BGRA,GST_VIDEO_FORMAT_I422_10LE};
 const enum video_format expected[]={VIDEO_FORMAT_NV12,VIDEO_FORMAT_BGRA,VIDEO_FORMAT_I210};
 for(unsigned n=0;n<3;n++) {
  gst_video_info_set_format(&i,formats[n],65,33);
  gst_video_colorimetry_from_string(&i.colorimetry,n==1?"sRGB":"bt709");
  b=gst_buffer_new_allocate(NULL,i.size,NULL); memset(&m,0,sizeof(m));
  assert(gst_video_frame_map(&m,&i,b,GST_MAP_READ));
  assert(pixelview_video_frame(&m,PIXELVIEW_COLOR_SDR,&f) && f.format==expected[n]);
  m.info.width=1073741840; assert(!pixelview_video_frame(&m,PIXELVIEW_COLOR_SDR,&f)); m.info.width=65;
  m.info.colorimetry.range=GST_VIDEO_COLOR_RANGE_UNKNOWN; assert(!pixelview_video_frame(&m,PIXELVIEW_COLOR_SDR,&f));
  gst_video_frame_unmap(&m); gst_buffer_unref(b);
 }
 GstCaps *caps=gst_caps_from_string("video/x-raw,format=P010_10LE,width=64,height=32,colorimetry=bt709");
 GstVideoInfo parsed;
 assert(pixelview_video_info(caps,PIXELVIEW_COLOR_SDR,&parsed));
 gst_structure_set(gst_caps_get_structure(caps,0),"colorimetry",G_TYPE_STRING,"invalid",NULL);
 assert(!pixelview_video_info(caps,PIXELVIEW_COLOR_SDR,&parsed));
 gst_structure_set(gst_caps_get_structure(caps,0),"colorimetry",G_TYPE_STRING,"0:3:5:1",NULL);
 assert(!pixelview_video_info(caps,PIXELVIEW_COLOR_SDR,&parsed));
 gst_structure_remove_field(gst_caps_get_structure(caps,0),"colorimetry");
 assert(!pixelview_video_info(caps,PIXELVIEW_COLOR_SDR,&parsed));gst_caps_unref(caps);
 hdr_cases();
 puts("PASS P010/NV12/BGRA/I210, odd padded planes, SDR709 limited YUV/full RGB; rejects PQ/gamut/unknown range/invalid strides/bounds");
 puts("PASS HDR PQ/HLG: BT.2100 caps and VP9-style unsignalled transfer labelled; SDR/full/other-transfer/non-P010 refused with typed reasons");
}
