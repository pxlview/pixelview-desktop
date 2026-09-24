/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "video-format.h"
#include <string.h>
static GstVideoTransferFunction hdr_transfer(enum pixelview_color color)
{
 return color==PIXELVIEW_COLOR_PQ?GST_VIDEO_TRANSFER_SMPTE2084:GST_VIDEO_TRANSFER_ARIB_STD_B67;
}
static bool is_hdr_transfer(GstVideoTransferFunction t)
{
 return t==GST_VIDEO_TRANSFER_SMPTE2084 || t==GST_VIDEO_TRANSFER_ARIB_STD_B67;
}
/* Absent colorimetry reads as all-unknown; an unparseable string is refused. */
static bool caps_colorimetry(GstCaps *caps, GstVideoColorimetry *c)
{
 const char *color=gst_structure_get_string(gst_caps_get_structure(caps,0),"colorimetry");
 if (!color) { memset(c,0,sizeof(*c)); return true; }
 return gst_video_colorimetry_from_string(c,color);
}
/* HDR mode trusts the operator's transfer only where the stream does not
 * contradict it. VP9 cannot signal PQ versus HLG, so a BT.2020 stream with an
 * unsignalled/SDR-curve transfer, or wholly unsignalled colour, is accepted and
 * labelled; an explicit BT.709 gamut, full range or the other HDR transfer is not. */
static const char *hdr_mismatch(const GstVideoColorimetry *c, enum pixelview_color color)
{
 if (is_hdr_transfer(c->transfer) && c->transfer!=hdr_transfer(color)) return PV_COLOR_TRANSFER_MISMATCH;
 if (c->primaries==GST_VIDEO_COLOR_PRIMARIES_BT709 || c->matrix==GST_VIDEO_COLOR_MATRIX_BT709) return PV_COLOR_SDR_SOURCE;
 if (c->range==GST_VIDEO_COLOR_RANGE_0_255) return PV_COLOR_UNSUPPORTED;
 if (c->range!=GST_VIDEO_COLOR_RANGE_16_235 && c->range!=GST_VIDEO_COLOR_RANGE_UNKNOWN) return PV_COLOR_UNSUPPORTED;
 if (c->primaries!=GST_VIDEO_COLOR_PRIMARIES_BT2020 && c->primaries!=GST_VIDEO_COLOR_PRIMARIES_UNKNOWN) return PV_COLOR_UNSUPPORTED;
 if (c->matrix!=GST_VIDEO_COLOR_MATRIX_BT2020 && c->matrix!=GST_VIDEO_COLOR_MATRIX_UNKNOWN) return PV_COLOR_UNSUPPORTED;
 if (c->transfer==hdr_transfer(color)) return NULL;
 const bool unsignalled_transfer=c->transfer==GST_VIDEO_TRANSFER_UNKNOWN || c->transfer==GST_VIDEO_TRANSFER_BT709 ||
  c->transfer==GST_VIDEO_TRANSFER_BT2020_10 || c->transfer==GST_VIDEO_TRANSFER_BT2020_12;
 if (!unsignalled_transfer) return PV_COLOR_UNSUPPORTED;
 const bool wholly_unsignalled=c->transfer==GST_VIDEO_TRANSFER_UNKNOWN &&
  c->primaries==GST_VIDEO_COLOR_PRIMARIES_UNKNOWN && c->matrix==GST_VIDEO_COLOR_MATRIX_UNKNOWN;
 const bool bt2020=c->primaries==GST_VIDEO_COLOR_PRIMARIES_BT2020 || c->matrix==GST_VIDEO_COLOR_MATRIX_BT2020;
 return wholly_unsignalled || bt2020 ? NULL : PV_COLOR_UNSUPPORTED;
}
static void hdr_label(GstVideoColorimetry *c, enum pixelview_color color)
{
 c->range=GST_VIDEO_COLOR_RANGE_16_235; c->matrix=GST_VIDEO_COLOR_MATRIX_BT2020;
 c->transfer=hdr_transfer(color); c->primaries=GST_VIDEO_COLOR_PRIMARIES_BT2020;
}
const char *pixelview_video_color_mismatch(GstCaps *caps, enum pixelview_color color)
{
 if (!caps || !gst_caps_is_fixed(caps) || gst_caps_get_size(caps)!=1) return PV_COLOR_UNSUPPORTED;
 GstVideoColorimetry c;
 if (!caps_colorimetry(caps,&c)) return PV_COLOR_UNSUPPORTED;
 if (color!=PIXELVIEW_COLOR_SDR) return hdr_mismatch(&c,color);
 if (is_hdr_transfer(c.transfer) || c.primaries==GST_VIDEO_COLOR_PRIMARIES_BT2020 ||
     c.matrix==GST_VIDEO_COLOR_MATRIX_BT2020) return PV_COLOR_HDR_SOURCE;
 GstVideoInfo info;
 return pixelview_video_info(caps,color,&info)?NULL:PV_COLOR_UNSUPPORTED;
}
bool pixelview_video_info(GstCaps *caps, enum pixelview_color color, GstVideoInfo *info)
{
 if (!caps || !gst_caps_is_fixed(caps) || gst_caps_get_size(caps)!=1) return false;
 if (color!=PIXELVIEW_COLOR_SDR) {
  GstVideoColorimetry c;
  if (!caps_colorimetry(caps,&c) || hdr_mismatch(&c,color) || !gst_video_info_from_caps(info,caps) ||
      GST_VIDEO_INFO_FORMAT(info)!=GST_VIDEO_FORMAT_P010_10LE) return false;
  /* The label, not GstVideoInfo's resolution-based defaults, describes the frame. */
  hdr_label(&info->colorimetry,color);
  return true;
 }
 const char *color_text=gst_structure_get_string(gst_caps_get_structure(caps,0),"colorimetry");
 GstVideoColorimetry c;
 if (!color_text || !gst_video_colorimetry_from_string(&c,color_text) ||
     c.range==GST_VIDEO_COLOR_RANGE_UNKNOWN || c.matrix==GST_VIDEO_COLOR_MATRIX_UNKNOWN ||
     c.transfer==GST_VIDEO_TRANSFER_UNKNOWN || c.primaries==GST_VIDEO_COLOR_PRIMARIES_UNKNOWN)
  return false;
 return gst_video_info_from_caps(info,caps) && gst_video_colorimetry_is_equal(&c,&info->colorimetry);
}
bool pixelview_video_frame(const GstVideoFrame *m, enum pixelview_color color, struct obs_source_frame2 *f)
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
 const bool hdr=color!=PIXELVIEW_COLOR_SDR;
 if (hdr) {
  GstVideoColorimetry label; hdr_label(&label,color);
  if (f->format!=VIDEO_FORMAT_P010 || !gst_video_colorimetry_is_equal(c,&label)) return false;
 } else if (c->primaries!=GST_VIDEO_COLOR_PRIMARIES_BT709 ||
     c->matrix!=(rgb?GST_VIDEO_COLOR_MATRIX_RGB:GST_VIDEO_COLOR_MATRIX_BT709) ||
     c->transfer!=(rgb?GST_VIDEO_TRANSFER_SRGB:GST_VIDEO_TRANSFER_BT709) ||
     c->range!=(rgb?GST_VIDEO_COLOR_RANGE_0_255:GST_VIDEO_COLOR_RANGE_16_235)) return false;
 if (m->info.interlace_mode!=GST_VIDEO_INTERLACE_MODE_PROGRESSIVE ||
     m->info.width<=0 || m->info.height<=0) return false;
 f->width=m->info.width; f->height=m->info.height;
 f->range=c->range==GST_VIDEO_COLOR_RANGE_0_255?VIDEO_RANGE_FULL:VIDEO_RANGE_PARTIAL;
 f->trc=hdr?(color==PIXELVIEW_COLOR_PQ?VIDEO_TRC_PQ:VIDEO_TRC_HLG):rgb?VIDEO_TRC_SRGB:VIDEO_TRC_DEFAULT;
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
 const enum video_colorspace space=!hdr?VIDEO_CS_709:color==PIXELVIEW_COLOR_PQ?VIDEO_CS_2100_PQ:VIDEO_CS_2100_HLG;
 return video_format_get_parameters_for_format(space,f->range,f->format,f->color_matrix,f->color_range_min,f->color_range_max);
}
