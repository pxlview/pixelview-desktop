/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../video-format.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
 gst_init(NULL,NULL);
 GstVideoInfo i; gst_video_info_set_format(&i,GST_VIDEO_FORMAT_P010_10LE,64,32);
 gst_video_colorimetry_from_string(&i.colorimetry,"bt709");
 GstBuffer *b=gst_buffer_new_allocate(NULL,i.size,NULL);
 GstVideoFrame m={0}; assert(gst_video_frame_map(&m,&i,b,GST_MAP_READ));
 struct obs_source_frame2 f;
 assert(pixelview_video_frame(&m,&f));
 assert(f.format==VIDEO_FORMAT_P010 && f.range==VIDEO_RANGE_PARTIAL);
 assert(f.data[1]==GST_VIDEO_FRAME_PLANE_DATA(&m,1) && f.linesize[1]==128);
 m.info.colorimetry.transfer=GST_VIDEO_TRANSFER_SMPTE2084;
 assert(!pixelview_video_frame(&m,&f));
 m.info.colorimetry.transfer=GST_VIDEO_TRANSFER_BT709;
 m.info.colorimetry.range=GST_VIDEO_COLOR_RANGE_0_255;
 assert(!pixelview_video_frame(&m,&f)); // Full-range VT fidelity is not proven.
 m.info.colorimetry.range=GST_VIDEO_COLOR_RANGE_16_235;
 m.info.colorimetry.primaries=GST_VIDEO_COLOR_PRIMARIES_BT2020;
 assert(!pixelview_video_frame(&m,&f));
 m.info.colorimetry.primaries=GST_VIDEO_COLOR_PRIMARIES_BT709;
 m.info.stride[0]=-128; assert(!pixelview_video_frame(&m,&f));
 m.info.stride[0]=1; assert(!pixelview_video_frame(&m,&f));
 m.info.stride[0]=128;
 m.data[0]=m.map[0].data+m.map[0].size-1; assert(!pixelview_video_frame(&m,&f));
 gst_video_frame_unmap(&m);gst_buffer_unref(b);
 const GstVideoFormat formats[]={GST_VIDEO_FORMAT_NV12,GST_VIDEO_FORMAT_BGRA,GST_VIDEO_FORMAT_I422_10LE};
 const enum video_format expected[]={VIDEO_FORMAT_NV12,VIDEO_FORMAT_BGRA,VIDEO_FORMAT_I210};
 for(unsigned n=0;n<3;n++) {
  gst_video_info_set_format(&i,formats[n],65,33);
  gst_video_colorimetry_from_string(&i.colorimetry,n==1?"sRGB":"bt709");
  b=gst_buffer_new_allocate(NULL,i.size,NULL); memset(&m,0,sizeof(m));
  assert(gst_video_frame_map(&m,&i,b,GST_MAP_READ));
  assert(pixelview_video_frame(&m,&f) && f.format==expected[n]);
  m.info.width=1073741840; assert(!pixelview_video_frame(&m,&f)); m.info.width=65;
  m.info.colorimetry.range=GST_VIDEO_COLOR_RANGE_UNKNOWN; assert(!pixelview_video_frame(&m,&f));
  gst_video_frame_unmap(&m); gst_buffer_unref(b);
 }
 GstCaps *caps=gst_caps_from_string("video/x-raw,format=P010_10LE,width=64,height=32,colorimetry=bt709");
 GstVideoInfo parsed;
 assert(pixelview_video_info(caps,&parsed));
 gst_structure_set(gst_caps_get_structure(caps,0),"colorimetry",G_TYPE_STRING,"invalid",NULL);
 assert(!pixelview_video_info(caps,&parsed));
 gst_structure_set(gst_caps_get_structure(caps,0),"colorimetry",G_TYPE_STRING,"0:3:5:1",NULL);
 assert(!pixelview_video_info(caps,&parsed));
 gst_structure_remove_field(gst_caps_get_structure(caps,0),"colorimetry");
 assert(!pixelview_video_info(caps,&parsed));gst_caps_unref(caps);
 puts("PASS P010/NV12/BGRA/I210, odd padded planes, SDR709 limited YUV/full RGB; rejects PQ/gamut/unknown range/invalid strides/bounds");
}
