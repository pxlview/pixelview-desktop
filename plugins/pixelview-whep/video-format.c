/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "video-format.h"
#include <string.h>
bool pixelview_video_info(GstCaps *caps, GstVideoInfo *info)
{
 if (!caps || !gst_caps_is_fixed(caps) || gst_caps_get_size(caps)!=1) return false;
 const char *color=gst_structure_get_string(gst_caps_get_structure(caps,0),"colorimetry");
 GstVideoColorimetry c;
 if (!color || !gst_video_colorimetry_from_string(&c,color) ||
     c.range==GST_VIDEO_COLOR_RANGE_UNKNOWN || c.matrix==GST_VIDEO_COLOR_MATRIX_UNKNOWN ||
     c.transfer==GST_VIDEO_TRANSFER_UNKNOWN || c.primaries==GST_VIDEO_COLOR_PRIMARIES_UNKNOWN)
  return false;
 return gst_video_info_from_caps(info,caps) && gst_video_colorimetry_is_equal(&c,&info->colorimetry);
}
bool pixelview_video_frame(const GstVideoFrame *m, struct obs_source_frame2 *f)
{
 memset(f,0,sizeof(*f));
 unsigned planes; bool rgb=false, planar=false; unsigned bytes=2;
 switch(GST_VIDEO_FRAME_FORMAT(m)) {
 case GST_VIDEO_FORMAT_P010_10LE: f->format=VIDEO_FORMAT_P010; planes=2; break;
 case GST_VIDEO_FORMAT_NV12: f->format=VIDEO_FORMAT_NV12; planes=2; bytes=1; break;
 case GST_VIDEO_FORMAT_BGRA: f->format=VIDEO_FORMAT_BGRA; planes=1; bytes=4; rgb=true; break;
 case GST_VIDEO_FORMAT_I422_10LE: f->format=VIDEO_FORMAT_I210; planes=3; planar=true; break;
 default: return false;
 }
 const GstVideoColorimetry *c=&m->info.colorimetry;
 if (c->primaries!=GST_VIDEO_COLOR_PRIMARIES_BT709 ||
     c->matrix!=(rgb?GST_VIDEO_COLOR_MATRIX_RGB:GST_VIDEO_COLOR_MATRIX_BT709) ||
     c->transfer!=(rgb?GST_VIDEO_TRANSFER_SRGB:GST_VIDEO_TRANSFER_BT709) ||
     c->range!=(rgb?GST_VIDEO_COLOR_RANGE_0_255:GST_VIDEO_COLOR_RANGE_16_235) ||
     m->info.interlace_mode!=GST_VIDEO_INTERLACE_MODE_PROGRESSIVE ||
     m->info.width<=0 || m->info.height<=0) return false;
 f->width=m->info.width; f->height=m->info.height;
 f->range=c->range==GST_VIDEO_COLOR_RANGE_0_255?VIDEO_RANGE_FULL:VIDEO_RANGE_PARTIAL;
 f->trc=rgb?VIDEO_TRC_SRGB:VIDEO_TRC_DEFAULT;
 for(unsigned p=0;p<planes;p++) {
  int stride=GST_VIDEO_FRAME_PLANE_STRIDE(m,p);
  size_t row=p?(((size_t)f->width+1)/2)*bytes*(planar?1:2):(size_t)f->width*bytes;
  size_t rows=p&&!planar?(f->height+1)/2:f->height;
  if(stride<=0 || (size_t)stride<row) return false;
  uintptr_t start=(uintptr_t)GST_VIDEO_FRAME_PLANE_DATA(m,p);
  size_t span=(rows-1)*(size_t)stride+row;
  bool inside=false;
  for(unsigned j=0;j<GST_VIDEO_MAX_PLANES;j++) {
   uintptr_t base=(uintptr_t)m->map[j].data;
   if(base && start>=base && start-base<=m->map[j].size && span<=m->map[j].size-(start-base)) inside=true;
  }
  if(!inside) return false;
  f->data[p]=(uint8_t*)start; f->linesize[p]=(uint32_t)stride;
 }
 return video_format_get_parameters_for_format(VIDEO_CS_709,f->range,f->format,f->color_matrix,f->color_range_min,f->color_range_max);
}
