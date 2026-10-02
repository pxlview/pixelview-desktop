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
      (GST_VIDEO_INFO_FORMAT(info)!=GST_VIDEO_FORMAT_P010_10LE && GST_VIDEO_INFO_FORMAT(info)!=GST_VIDEO_FORMAT_v210 &&
       GST_VIDEO_INFO_FORMAT(info)!=GST_VIDEO_FORMAT_AYUV64)) return false;
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
 unsigned planes; bool rgb=false, planar=false, v210=false, ayuv64=false; unsigned bytes=2;
 switch(GST_VIDEO_FRAME_FORMAT(m)) {
 case GST_VIDEO_FORMAT_P010_10LE: f->format=VIDEO_FORMAT_P010; planes=2; break;
 case GST_VIDEO_FORMAT_v210: f->format=VIDEO_FORMAT_V210; planes=1; v210=true; break;
 /* Packed A Y U V, 16 bits each: handed on as P416 by pixelview_video_unpack_ayuv64. */
 case GST_VIDEO_FORMAT_AYUV64: f->format=VIDEO_FORMAT_AYUV; planes=1; bytes=8; ayuv64=true; break;
 case GST_VIDEO_FORMAT_NV12: f->format=VIDEO_FORMAT_NV12; planes=2; bytes=1; break;
 case GST_VIDEO_FORMAT_BGRA: f->format=VIDEO_FORMAT_BGRA; planes=1; bytes=4; rgb=true; break;
 case GST_VIDEO_FORMAT_I422_10LE: f->format=VIDEO_FORMAT_I210; planes=3; planar=true; break;
 default: return false;
 }
 const GstVideoColorimetry *c=&m->info.colorimetry;
 const bool hdr=color!=PIXELVIEW_COLOR_SDR;
 if (hdr) {
  GstVideoColorimetry label; hdr_label(&label,color);
  if ((f->format!=VIDEO_FORMAT_P010 && f->format!=VIDEO_FORMAT_V210 && !ayuv64) || !gst_video_colorimetry_is_equal(c,&label)) return false;
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
  /* v210 packs six pixels into four 32-bit words; libobs uploads ((w+5)/6)*4 texels per row. */
  size_t row=v210?(((size_t)f->width+5)/6)*16:p?(((size_t)f->width+1)/2)*bytes*(planar?1:2):(size_t)f->width*bytes;
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
 /* The 4:4:4 frame is output as P416, so it takes that format's (ten-bit) matrix. */
 if(!video_format_get_parameters_for_format(space,f->range,ayuv64?VIDEO_FORMAT_P416:f->format,f->color_matrix,f->color_range_min,f->color_range_max)) return false;
 /* SDR Y'CbCr keeps sub-black and super-white (PLUGE, overshoots): the float
  * canvas carries them to the SDI output, so do not clamp to 64-940 on the way
  * in. HDR keeps the clamp; PQ and HLG are not defined below black. */
 if(!hdr && !rgb) for(int i=0;i<3;i++) { f->color_range_min[i]=0.f; f->color_range_max[i]=1.f; }
 return true;
}
/* Y'CbCr code c at eight bits is exactly c * 4 at ten (16 -> 64, 235 -> 940,
 * 128 -> 512): P010 stores it as c << 8. */
bool pixelview_video_widen_nv12(struct obs_source_frame2 *f, uint8_t **storage, size_t *capacity)
{
 if (f->format!=VIDEO_FORMAT_NV12) return true;
 if (!f->width || !f->height || !f->data[0] || !f->data[1] || f->linesize[0]<f->width ||
     f->linesize[1]<((f->width+1)/2)*2) return false;
 const size_t row=(((size_t)f->width+1)/2)*4; /* bytes: an even number of 16-bit samples */
 const size_t chroma_rows=((size_t)f->height+1)/2, need=row*((size_t)f->height+chroma_rows);
 if (*capacity<need) {
  uint8_t *grown=g_try_realloc(*storage,need);
  if (!grown) return false;
  *storage=grown; *capacity=need;
 }
 uint16_t *out=(uint16_t *)*storage;
 for (int plane=0;plane<2;plane++) {
  const size_t rows=plane?chroma_rows:f->height, samples=plane?((size_t)f->width+1)/2*2:f->width;
  for (size_t y=0;y<rows;y++) {
   const uint8_t *in=f->data[plane]+y*f->linesize[plane];
   uint16_t *line=out+(plane?(size_t)f->height:0)*(row/2)+y*(row/2);
   for (size_t x=0;x<samples;x++) line[x]=(uint16_t)(in[x]<<8);
   for (size_t x=samples;x<row/2;x++) line[x]=line[samples-1];
  }
 }
 f->data[0]=*storage; f->data[1]=*storage+row*f->height;
 f->linesize[0]=f->linesize[1]=(uint32_t)row;
 f->format=VIDEO_FORMAT_P010;
 /* The same matrix at ten-bit scale; the caller's range window is kept. */
 float min[3],max[3];
 return video_format_get_parameters_for_format(VIDEO_CS_709,f->range,f->format,f->color_matrix,min,max);
}
/* AYUV64 is A Y U V in 16-bit little-endian words. P416 is a luma plane and an
 * interleaved CbCr plane at full resolution, 16-bit words holding code << 6. */
bool pixelview_video_unpack_ayuv64(struct obs_source_frame2 *f, uint8_t **storage, size_t *capacity)
{
 if (f->format!=VIDEO_FORMAT_AYUV) return true;
 if (!f->width || !f->height || !f->data[0] || f->linesize[0]<(size_t)f->width*8) return false;
 const size_t luma_row=(size_t)f->width*2, chroma_row=(size_t)f->width*4;
 const size_t need=(luma_row+chroma_row)*f->height;
 if (*capacity<need) {
  uint8_t *grown=g_try_realloc(*storage,need);
  if (!grown) return false;
  *storage=grown; *capacity=need;
 }
 uint16_t *luma=(uint16_t *)*storage, *chroma=(uint16_t *)(*storage+luma_row*f->height);
 for (size_t y=0;y<f->height;y++) {
  const uint16_t *in=(const uint16_t *)(f->data[0]+y*f->linesize[0]);
  uint16_t *l=luma+y*f->width, *c=chroma+y*(size_t)f->width*2;
  for (size_t x=0;x<f->width;x++) {
   const uint32_t yy=((uint32_t)in[4*x+1]+32)>>6, u=((uint32_t)in[4*x+2]+32)>>6, v=((uint32_t)in[4*x+3]+32)>>6;
   l[x]=(uint16_t)((yy>1023?1023:yy)<<6);
   c[2*x]=(uint16_t)((u>1023?1023:u)<<6);
   c[2*x+1]=(uint16_t)((v>1023?1023:v)<<6);
  }
 }
 f->data[0]=(uint8_t *)luma; f->data[1]=(uint8_t *)chroma;
 f->linesize[0]=(uint32_t)luma_row; f->linesize[1]=(uint32_t)chroma_row;
 f->format=VIDEO_FORMAT_P416;
 return true;
}
