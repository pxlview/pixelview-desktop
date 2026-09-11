/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "native-422.h"
#include "native-422-rate.h"
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <gst/codecparsers/gsth265parser.h>
#include <gst/video/video-color.h>

struct pv_native422 {
 VTDecompressionSessionRef session;
 CMVideoFormatDescriptionRef format;
 GstH265Parser *parser;
 GstCaps *caps;
 GBytes *parameter_sets[3];
 guint width, height, stride;
 guint8 *packed;
 gboolean failed, hardware;
 guint rate_num,rate_den;
 struct pv_native422_timing timing;
};
struct decoded { CVPixelBufferRef image; OSStatus status; guint count; guint64 callback; };
struct pv_native422_timing pv_native422_get_timing(const struct pv_native422 *d) { return d->timing; }
static void decoded_frame(void *opaque, void *ref, OSStatus status,
 VTDecodeInfoFlags flags, CVImageBufferRef image, CMTime pts, CMTime duration)
{
 (void)opaque; (void)flags; (void)pts; (void)duration;
 struct decoded *d = ref;
 d->callback=(guint64)g_get_monotonic_time()*1000;
 d->count++; d->status = status;
 if (image && !d->image) d->image = CVPixelBufferRetain(image);
}
struct pv_native422 *pv_native422_create(void)
{
 struct pv_native422 *d = g_new0(struct pv_native422, 1);
 d->parser = gst_h265_parser_new();
 return d;
}
void pv_native422_require_initial_format(struct pv_native422 *d,guint n,guint den)
{
 if(d->caps || !pv422_rate_supported(n,den)) d->failed=TRUE;
 else { d->rate_num=n; d->rate_den=den; }
}
static gboolean timing_matches(guint n,guint d,guint32 scale,guint32 units,gboolean proportional,guint32 ticks)
{
 /* Compare in 128 bits: hostile 32-bit tick fields can overflow uint64. */
 return scale && units && (__uint128_t)scale*d==(__uint128_t)n*units*(proportional ? (guint64)ticks+1 : 1);
}
void pv_native422_destroy(struct pv_native422 *d)
{
 if (!d) return;
 if (d->session) {
  VTDecompressionSessionWaitForAsynchronousFrames(d->session);
  VTDecompressionSessionInvalidate(d->session); CFRelease(d->session);
 }
 if (d->format) CFRelease(d->format);
 if (d->caps) gst_caps_unref(d->caps);
 for (guint i = 0; i < 3; i++) if (d->parameter_sets[i]) g_bytes_unref(d->parameter_sets[i]);
 gst_h265_parser_free(d->parser); g_free(d->packed); g_free(d);
}
static gboolean valid_sps(const GstH265SPS *s)
{
 const GstH265ProfileTierLevel *p = &s->profile_tier_level;
 const GstH265VUIParams *v = &s->vui_params;
 if (p->profile_space || p->profile_idc != 4 || p->tier_flag ||
     !p->max_10bit_constraint_flag || p->max_8bit_constraint_flag ||
     !p->max_422chroma_constraint_flag || p->max_420chroma_constraint_flag ||
     p->interlaced_source_flag || !p->progressive_source_flag ||
     s->chroma_format_idc != 2 || s->separate_colour_plane_flag ||
     s->bit_depth_luma_minus8 != 2 || s->bit_depth_chroma_minus8 != 2 ||
     s->max_sub_layers_minus1 || s->max_num_reorder_pics[0] ||
     s->sps_multilayer_extension_flag || s->sps_3d_extension_flag || s->sps_scc_extension_flag ||
     !s->vui_parameters_present_flag || !v->parsed ||
     !v->video_signal_type_present_flag || v->video_full_range_flag ||
     !v->colour_description_present_flag || v->colour_primaries != 1 ||
     v->transfer_characteristics != 1 || v->matrix_coefficients != 1 ||
     v->field_seq_flag || v->default_display_window_flag)
  return FALSE;
 /* HEVC E.3.1 infers chroma_sample_loc_type 0 when absent. For 4:2:2
  * progressive pictures this is the left location; no resiting is performed. */
 if (v->chroma_loc_info_present_flag &&
     (v->chroma_sample_loc_type_top_field || v->chroma_sample_loc_type_bottom_field)) return FALSE;
 return TRUE;
}
/* H.265 7.3.3/Annex A: profile4 Main 4:2:2 10, not its Intra/Still
 * variants. The lower 35 constraint bits contain 34 reserved-zero bits and
 * general_inbld_flag; inbld is allowed. Source/nonpacked/frame-only flags are
 * separate from the range-extension profile constraints. */
static gboolean supported_ptl(const guint8 *p)
{
 return p[0]==4 && p[11]==120 && (p[5]&0x0d)==0x0d && !(p[5]&0x42) &&
  (p[6]&0xf8)==0x08 && !(p[6]&7) && !p[7] && !p[8] && !p[9] && !(p[10]&0xfe);
}
static gboolean parameter_ptl(const GstH265NalUnit *nal,const guint8 *hvcc)
{
 /* Only the bounded general PTL prefix is read here. Full VPS/SPS syntax is
  * validated by the public Gst parser; do not depend on its private reader. */
 guint8 prefix[16];guint count=0,zeros=0;
 const guint skip=nal->type==GST_H265_NAL_VPS?4:1;
 for(guint i=2;i<nal->size && count<skip+12;i++) {
  guint8 b=nal->data[nal->offset+i];
  if(zeros==2 && b==3) {
   if(i+1>=nal->size || nal->data[nal->offset+i+1]>3)return FALSE;
   zeros=0;continue;
  }
  prefix[count++]=b;zeros=b==0?zeros+1:0;
 }
 if(count!=skip+12 || !supported_ptl(prefix+skip) || !(prefix[skip+5]&0x80))return FALSE;
 /* ISO/IEC 14496-15 8.3.3.1: configuration compatibility/constraint bits may
  * only be set when all parameter sets set them. They may be omitted; never
  * demand byte equality for these summary masks. Required profile constraints
  * were independently checked in both declarations. */
 for(guint i=1;i<=10;i++) if(hvcc[1+i]&~prefix[skip+i])return FALSE;
 return TRUE;
}
static gboolean configure(struct pv_native422 *d, GstCaps *caps)
{
 if (!caps || !gst_caps_is_fixed(caps) || gst_caps_get_size(caps) != 1) return FALSE;
 const GstStructure *s = gst_caps_get_structure(caps, 0);
 int width, height, fps_n, fps_d;
 if (!gst_structure_has_name(s, "video/x-h265") ||
     g_strcmp0(gst_structure_get_string(s, "stream-format"), "hvc1") ||
     g_strcmp0(gst_structure_get_string(s, "alignment"), "au") ||
     !gst_structure_get_int(s, "width", &width) || width < 2 || width > 1920 || width % 2 ||
     !gst_structure_get_int(s, "height", &height) || height < 1 || height > 1080 ||
     !gst_structure_get_fraction(s, "framerate", &fps_n, &fps_d) || fps_n <= 0 || fps_d <= 0 ||
     (guint64)fps_n > 60ULL * fps_d) return FALSE;
 if(d->rate_num && (width!=1920 || height!=1080 || (guint64)fps_n*d->rate_den!=(guint64)fps_d*d->rate_num)) return FALSE;
 /* Optional parser declarations must not contradict the strict bitstream
  * contract. Missing fields are established from the parameter sets below. */
 if(d->rate_num) {
  const char *keys[]={"profile","tier","level","chroma-format","interlace-mode"};
  const char *values[]={"main-422-10","main","4","4:2:2","progressive"};
  for(guint i=0;i<G_N_ELEMENTS(keys);i++)
   if(gst_structure_has_field(s,keys[i]) && g_strcmp0(gst_structure_get_string(s,keys[i]),values[i])) return FALSE;
  const char *depths[]={"bit-depth-luma","bit-depth-chroma"};
  for(guint i=0;i<G_N_ELEMENTS(depths);i++) {
   int signed_depth;guint unsigned_depth;
   if(gst_structure_has_field(s,depths[i]) &&
      !((gst_structure_get_int(s,depths[i],&signed_depth) && signed_depth==10) ||
        (gst_structure_get_uint(s,depths[i],&unsigned_depth) && unsigned_depth==10)))return FALSE;
  }
  if(gst_structure_has_field(s,"colorimetry")) {
   const char *text=gst_structure_get_string(s,"colorimetry");GstVideoColorimetry color;
   if(!text || !gst_video_colorimetry_from_string(&color,text) || color.range!=GST_VIDEO_COLOR_RANGE_16_235 ||
      color.matrix!=GST_VIDEO_COLOR_MATRIX_BT709 || color.transfer!=GST_VIDEO_TRANSFER_BT709 ||
      color.primaries!=GST_VIDEO_COLOR_PRIMARIES_BT709)return FALSE;
  }
 }
 const GValue *value = gst_structure_get_value(s, "codec_data");
 if (!value || !GST_VALUE_HOLDS_BUFFER(value)) return FALSE;
 GstBuffer *configuration = gst_value_get_buffer(value);
 /* Mapping fragmented input may allocate and merge it. Bound before mapping,
  * and retain the mapped-size checks below as a separate parser boundary. */
 if (!configuration || gst_buffer_get_size(configuration) < 23 ||
     gst_buffer_get_size(configuration) > 65536) return FALSE;
 GstMapInfo map;
 if (!gst_buffer_map(configuration, &map, GST_MAP_READ)) return FALSE;
 gboolean valid = FALSE;
 const uint8_t *sets[3] = {0}; size_t sizes[3] = {0};
 if (map.size < 23 || map.size > 65536 || map.data[0] != 1 || (map.data[21] & 3) != 3) goto done;
 if(d->rate_num && (!supported_ptl(map.data+1) || (map.data[16]&3)!=2 ||
    (map.data[17]&7)!=2 || (map.data[18]&7)!=2 ||
    (map.data[13]&0xf0)!=0xf0 || (map.data[15]&0xfc)!=0xfc || (map.data[16]&0xfc)!=0xfc ||
    (map.data[17]&0xf8)!=0xf8 || (map.data[18]&0xf8)!=0xf8 ||
    (map.data[21]>>6)==3 || ((map.data[21]>>3)&7)>1 || map.data[22]!=3)) goto done;
 if(d->rate_num) {
  /* avgFrameRate is an optional unsigned fps*256 summary, not a cadence
   * authority. Permit one unit of representation rounding, never use it to
   * select or rewrite the exact finite-rate rational. */
  guint64 average=GST_READ_UINT16_BE(map.data+19),scaled=(guint64)d->rate_num*256;
  if(average && (average*d->rate_den+d->rate_den<scaled || average*d->rate_den>scaled+d->rate_den))goto done;
 }
 size_t off = 23;
 for (guint i = 0; i < map.data[22]; i++) {
  if (map.size - off < 3) goto done;
  if(d->rate_num && (map.data[off]&0x40))goto done; /* reserved_zero_1bit */
  guint type = map.data[off++] & 63;
  guint count = GST_READ_UINT16_BE(map.data + off); off += 2;
  for (guint n = 0; n < count; n++) {
   if (map.size - off < 2) goto done;
   guint size = GST_READ_UINT16_BE(map.data + off);
   if (size < 2 || size > map.size - off - 2) goto done;
   if (type >= 32 && type <= 34) {
    if (sets[type - 32]) goto done;
    GstH265NalUnit nal = {0};
    if (gst_h265_parser_identify_nalu_hevc(d->parser, map.data, (guint)off, map.size, 2, &nal) != GST_H265_PARSER_OK ||
        nal.type != type || nal.layer_id || gst_h265_parser_parse_nal(d->parser, &nal) != GST_H265_PARSER_OK) goto done;
    if(d->rate_num && (nal.temporal_id_plus1!=1 || (type<=33 && !parameter_ptl(&nal,map.data)))) goto done;
    if(type==32 && d->rate_num) {
     GstH265VPS vps={0};
     if(gst_h265_parser_parse_vps(d->parser,&nal,&vps)!=GST_H265_PARSER_OK ||
        !vps.base_layer_internal_flag || !vps.base_layer_available_flag ||
        vps.max_layers_minus1 || vps.max_sub_layers_minus1 || vps.profile_tier_level.profile_space ||
        vps.profile_tier_level.profile_idc!=4 || vps.profile_tier_level.tier_flag || vps.profile_tier_level.level_idc!=120 ||
        (vps.timing_info_present_flag && !timing_matches(d->rate_num,d->rate_den,vps.time_scale,vps.num_units_in_tick,
          vps.poc_proportional_to_timing_flag,vps.num_ticks_poc_diff_one_minus1))) goto done;
    }
    if (type == 33) {
     GstH265SPS sps = {0};
     if (gst_h265_parser_parse_sps(d->parser, &nal, &sps, TRUE) != GST_H265_PARSER_OK || !valid_sps(&sps)) goto done;
     if(d->rate_num && (sps.profile_tier_level.level_idc!=120 || sps.width!=1920 || sps.height<1080 || sps.height>1088 ||
        (sps.vui_params.timing_info_present_flag && !timing_matches(d->rate_num,d->rate_den,sps.vui_params.time_scale,
          sps.vui_params.num_units_in_tick,sps.vui_params.poc_proportional_to_timing_flag,sps.vui_params.num_ticks_poc_diff_one_minus1)))) goto done;
     int display_width = sps.conformance_window_flag ? sps.crop_rect_width : sps.width;
     int display_height = sps.conformance_window_flag ? sps.crop_rect_height : sps.height;
     if (display_width != width || display_height != height) goto done;
    }
    sets[type - 32] = map.data + off + 2; sizes[type - 32] = size;
   } else {
    /* Out-of-band SEI or unknown configuration arrays need explicit policy. */
    goto done;
   }
   off += size + 2;
  }
 }
 if (off != map.size || !sets[0] || !sets[1] || !sets[2]) goto done;
 if (CMVideoFormatDescriptionCreateFromHEVCParameterSets(NULL, 3, sets, sizes, 4, NULL, &d->format)) goto done;
 CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(d->format);
 if (dimensions.width != width || dimensions.height != height) goto done;
 @autoreleasepool {
  NSDictionary *spec = @{(id)kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder:@YES};
  NSDictionary *attrs = @{(id)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_422YpCbCr10BiPlanarVideoRange)};
  VTDecompressionOutputCallbackRecord cb = {decoded_frame, NULL};
  d->timing.session_begin=(guint64)g_get_monotonic_time()*1000;
  OSStatus created=VTDecompressionSessionCreate(NULL, d->format, (CFDictionaryRef)spec, (CFDictionaryRef)attrs, &cb, &d->session);
  d->timing.session_end=(guint64)g_get_monotonic_time()*1000;
  if(created) goto done;
 }
 CFTypeRef hardware = NULL;
 OSStatus status = VTSessionCopyProperty(d->session, kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder, NULL, &hardware);
 d->hardware = !status && hardware && CFEqual(hardware, kCFBooleanTrue);
 if (hardware) CFRelease(hardware);
 if (!d->hardware) goto done;
 d->width = width; d->height = height; d->stride = ((width + 47) / 48) * 128;
 d->packed = g_malloc0((gsize)d->stride * height);
 d->caps = gst_caps_copy(caps); valid = TRUE;
 for (guint i = 0; i < 3; i++) d->parameter_sets[i] = g_bytes_new(sets[i], sizes[i]);
done:
 gst_buffer_unmap(gst_value_get_buffer(value), &map);
 return valid;
}
static gboolean valid_access_unit(struct pv_native422 *d, const GstMapInfo *map)
{
 if (!map->size || map->size > 16 * 1024 * 1024) return FALSE;
 guint off = 0; gboolean picture = FALSE;
 while (off < map->size) {
  GstH265NalUnit nal = {0};
  if (gst_h265_parser_identify_nalu_hevc(d->parser, map->data, off, map->size, 4, &nal) != GST_H265_PARSER_OK ||
      nal.layer_id || nal.temporal_id_plus1 != 1) return FALSE;
  /* h265parse may retain the initial parameter sets even with hvc1 caps.
   * Only byte-identical copies of the admitted configuration are accepted. */
  if (nal.type >= 32 && nal.type <= 34) {
   gsize size;
   const guint8 *bytes = g_bytes_get_data(d->parameter_sets[nal.type - 32], &size);
   if (size != nal.size || memcmp(bytes, map->data + nal.offset, size)) return FALSE;
   off = nal.offset + nal.size; continue;
  }
  if (nal.type <= 31) picture = TRUE;
  else if (nal.type == GST_H265_NAL_PREFIX_SEI || nal.type == GST_H265_NAL_SUFFIX_SEI) {
   GArray *messages = NULL;
   if (gst_h265_parser_parse_sei(d->parser, &nal, &messages) != GST_H265_PARSER_OK) {
    if (messages) g_array_unref(messages);
    return FALSE;
   }
   gboolean allowed = TRUE;
   for (guint i = 0; i < messages->len; i++) {
    GstH265SEIMessage *m = &g_array_index(messages, GstH265SEIMessage, i);
    /* Initial SDR/no-reorder contract. Unknown/dynamic HDR/alternative transfer
     * messages are rejected, not passed through as presumed SDR. */
    if (m->payloadType != GST_H265_SEI_USER_DATA_UNREGISTERED) allowed = FALSE;
   }
   g_array_unref(messages); if (!allowed) return FALSE;
  } else if(nal.type==GST_H265_NAL_FD) {
   /* Standard filler_data_rbsp used by Apple CBR: ff* followed by trailing
    * stop/alignment bits. No picture, color, timing or reference semantics. */
   if(nal.size<3 || map->data[nal.offset+nal.size-1]!=0x80) return FALSE;
   for(guint i=2;i+1<nal.size;i++) if(map->data[nal.offset+i]!=0xff) return FALSE;
  } else if (nal.type != GST_H265_NAL_AUD) return FALSE;
  off = nal.offset + nal.size;
 }
 return picture;
}
static gboolean attachment(CVPixelBufferRef image, CFStringRef key, CFStringRef expected)
{
 CFTypeRef value = CVBufferCopyAttachment(image, key, NULL);
 gboolean equal = value && CFEqual(value, expected);
 if (value) CFRelease(value);
 return equal;
}
static gboolean pack(struct pv_native422 *d, CVPixelBufferRef image)
{
 if (CVPixelBufferGetPixelFormatType(image) != kCVPixelFormatType_422YpCbCr10BiPlanarVideoRange ||
     CVPixelBufferGetWidth(image) != d->width || CVPixelBufferGetHeight(image) != d->height ||
     CVPixelBufferGetPlaneCount(image) != 2 ||
     !attachment(image, kCVImageBufferColorPrimariesKey, kCVImageBufferColorPrimaries_ITU_R_709_2) ||
     !attachment(image, kCVImageBufferTransferFunctionKey, kCVImageBufferTransferFunction_ITU_R_709_2) ||
     !attachment(image, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_709_2) ||
     !attachment(image, kCVImageBufferChromaLocationTopFieldKey, kCVImageBufferChromaLocation_Left)) return FALSE;
 for (guint p = 0; p < 2; p++)
  if (CVPixelBufferGetWidthOfPlane(image, p) != d->width / (p ? 2 : 1) ||
      CVPixelBufferGetHeightOfPlane(image, p) != d->height ||
      CVPixelBufferGetBytesPerRowOfPlane(image, p) < d->width * 2) return FALSE;
 if (CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly)) return FALSE;
 gboolean ok = TRUE;
 for (guint row = 0; row < d->height && ok; row++) {
  const guint8 *y = (const guint8 *)CVPixelBufferGetBaseAddressOfPlane(image, 0) + row * CVPixelBufferGetBytesPerRowOfPlane(image, 0);
  const guint8 *uv = (const guint8 *)CVPixelBufferGetBaseAddressOfPlane(image, 1) + row * CVPixelBufferGetBytesPerRowOfPlane(image, 1);
  guint8 *out = d->packed + row * d->stride;
  for (guint x = 0; x < d->stride / 16 * 6; x += 6) {
   guint16 a[6] = {64,64,64,64,64,64}, u[3] = {512,512,512}, v[3] = {512,512,512};
   for (guint i = 0; i < 6 && x + i < d->width; i++) {
    guint16 word = GST_READ_UINT16_LE(y + 2 * (x + i));
    if (word & 63) ok = FALSE;
    a[i] = word >> 6;
   }
   for (guint i = 0; i < 3 && x + i * 2 < d->width; i++) {
    guint16 cb = GST_READ_UINT16_LE(uv + 2 * x + 4 * i), cr = GST_READ_UINT16_LE(uv + 2 * x + 4 * i + 2);
    if ((cb | cr) & 63) ok = FALSE;
    u[i] = cb >> 6; v[i] = cr >> 6;
   }
   GST_WRITE_UINT32_LE(out, u[0] | (guint32)a[0] << 10 | (guint32)v[0] << 20);
   GST_WRITE_UINT32_LE(out + 4, a[1] | (guint32)u[1] << 10 | (guint32)a[2] << 20);
   GST_WRITE_UINT32_LE(out + 8, v[1] | (guint32)a[3] << 10 | (guint32)u[2] << 20);
   GST_WRITE_UINT32_LE(out + 12, a[4] | (guint32)v[2] << 10 | (guint32)a[5] << 20);
   out += 16;
  }
 }
 CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
 return ok;
}
/* Display-only 422 -> 420 conversion. Card codes bypass this conversion. */
static GstBuffer *preview_copy(struct pv_native422 *d, CVPixelBufferRef image)
{
 if (d->height & 1) return NULL;
 gsize bytes = (gsize)d->width * d->height * 3 / 2;
 guint8 *data = g_try_malloc(bytes);
 if (!data) return NULL;
 if (CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly)) { g_free(data); return NULL; }
 for (guint row=0; row<d->height; row++) {
  const guint8 *y = (const guint8 *)CVPixelBufferGetBaseAddressOfPlane(image, 0) + row * CVPixelBufferGetBytesPerRowOfPlane(image, 0);
  for (guint x=0; x<d->width; x++) data[(gsize)row*d->width+x] = GST_READ_UINT16_LE(y+2*x) >> 8;
 }
 guint8 *out = data + (gsize)d->width*d->height;
 for (guint row=0; row<d->height; row+=2) {
  const guint8 *a = (const guint8 *)CVPixelBufferGetBaseAddressOfPlane(image, 1) + row * CVPixelBufferGetBytesPerRowOfPlane(image, 1);
  const guint8 *b = a + CVPixelBufferGetBytesPerRowOfPlane(image, 1);
  for (guint x=0; x<d->width; x++) *out++ = ((guint)GST_READ_UINT16_LE(a+2*x) + GST_READ_UINT16_LE(b+2*x)) >> 9;
 }
 CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
 return gst_buffer_new_wrapped(data, bytes);
}
enum pv_native422_result pv_native422_decode(struct pv_native422 *d, GstSample *sample,
 struct pv_native422_frame *frame)
{
 return pv_native422_decode_preview(d, sample, frame, NULL);
}
enum pv_native422_result pv_native422_decode_preview(struct pv_native422 *d, GstSample *sample,
 struct pv_native422_frame *frame, GstBuffer **preview)
{
 if (preview) *preview = NULL;
 if (!d || !frame) return PV_NATIVE422_REJECTED;
 memset(frame, 0, sizeof(*frame));
 d->timing.begin=(guint64)g_get_monotonic_time()*1000;
 if (d->failed || !sample) goto failed;
 GstCaps *caps = gst_sample_get_caps(sample);
 if (!d->caps ? !configure(d, caps) : !caps || !gst_caps_is_equal(d->caps, caps)) goto failed;
 d->timing.configured=(guint64)g_get_monotonic_time()*1000;
 GstBuffer *buffer = gst_sample_get_buffer(sample);
 if (!buffer || !gst_buffer_get_size(buffer) || gst_buffer_get_size(buffer) > 16 * 1024 * 1024 ||
     !GST_BUFFER_PTS_IS_VALID(buffer) || GST_BUFFER_PTS(buffer) > G_MAXINT64 ||
     !GST_BUFFER_DURATION_IS_VALID(buffer) || GST_BUFFER_DURATION(buffer) > G_MAXINT64) goto failed;
 GstMapInfo map;
 if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) goto failed;
 if (!valid_access_unit(d, &map)) { gst_buffer_unmap(buffer, &map); goto failed; }
 CMBlockBufferRef block = NULL; CMSampleBufferRef compressed = NULL;
 OSStatus status = CMBlockBufferCreateWithMemoryBlock(NULL, NULL, map.size, NULL, NULL, 0, map.size, 0, &block);
 if (!status) status = CMBlockBufferReplaceDataBytes(map.data, block, 0, map.size);
 size_t size = map.size;
 gst_buffer_unmap(buffer, &map);
 CMSampleTimingInfo timing = {CMTimeMake(GST_BUFFER_DURATION(buffer), GST_SECOND),
  CMTimeMake(GST_BUFFER_PTS(buffer), GST_SECOND), kCMTimeInvalid};
 if (!status) status = CMSampleBufferCreateReady(NULL, block, d->format, 1, 1, &timing, 1, &size, &compressed);
 struct decoded decoded = {0};
 /* No asynchronous/temporal-processing flags. Still wait before releasing the
  * callback context and compressed bytes; no pointer into a Gst mapping escapes. */
 d->timing.submitted=(guint64)g_get_monotonic_time()*1000;
 if (!status) status = VTDecompressionSessionDecodeFrame(d->session, compressed, 0, &decoded, NULL);
 d->timing.returned=(guint64)g_get_monotonic_time()*1000;
 OSStatus waited = VTDecompressionSessionWaitForAsynchronousFrames(d->session);
 d->timing.waited=(guint64)g_get_monotonic_time()*1000;
 d->timing.callback=decoded.callback;
 if(!d->timing.first_callback) d->timing.first_callback=decoded.callback;
 if (compressed) CFRelease(compressed);
 if (block) CFRelease(block);
 gboolean ok = !status && !waited && !decoded.status && decoded.count == 1 && decoded.image && pack(d, decoded.image);
 d->timing.packed=(guint64)g_get_monotonic_time()*1000;
 if (ok && preview) {
  *preview = preview_copy(d, decoded.image);
  if (*preview) { GST_BUFFER_PTS(*preview) = GST_BUFFER_PTS(buffer); GST_BUFFER_DURATION(*preview) = GST_BUFFER_DURATION(buffer); }
 }
 d->timing.previewed=(guint64)g_get_monotonic_time()*1000;
 if (decoded.image) CVPixelBufferRelease(decoded.image);
 if (!ok) goto failed;
 *frame = (struct pv_native422_frame){d->packed, d->width, d->height, d->stride,
  GST_BUFFER_PTS(buffer), GST_BUFFER_DURATION(buffer), d->hardware};
 return PV_NATIVE422_FRAME;
failed:
 d->failed = TRUE;
 return PV_NATIVE422_REJECTED;
}
