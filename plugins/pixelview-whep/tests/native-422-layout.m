/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Compile the actual production pack function, not a copy of its algorithm. */
#include "../native-422.m"
#include <assert.h>
#include <stdio.h>
static guint16 code(guint x, guint y, guint c) { return (x * (13 + 4 * c) + y * 97 + c * 193) % 1024; }
int main(int argc, char **argv)
{
 assert(argc == 2);
 const guint widths[] = {2,4,6,8,46,48,50,94,96,98,128,720,1024,1280,1920};
 for (guint j = 0; j < G_N_ELEMENTS(widths); j++) {
  guint w = widths[j], h = 8, stride = ((w + 47) / 48) * 128;
  size_t strides[2] = {w * 2 + 34, w * 2 + 62}, widths_[2] = {w, w / 2}, heights[2] = {h,h};
  guint8 *alloc[2] = {g_malloc0(strides[0] * h + 2), g_malloc0(strides[1] * h + 2)};
  void *planes[2] = {alloc[0] + 1, alloc[1] + 1};
  CVPixelBufferRef image = NULL;
  assert(!CVPixelBufferCreateWithPlanarBytes(NULL, w, h, kCVPixelFormatType_422YpCbCr10BiPlanarVideoRange,
   NULL, 0, 2, planes, widths_, heights, strides, NULL, NULL, NULL, &image));
  CVBufferSetAttachment(image, kCVImageBufferColorPrimariesKey, kCVImageBufferColorPrimaries_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
  CVBufferSetAttachment(image, kCVImageBufferTransferFunctionKey, kCVImageBufferTransferFunction_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
  CVBufferSetAttachment(image, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_709_2, kCVAttachmentMode_ShouldPropagate);
  CVBufferSetAttachment(image, kCVImageBufferChromaLocationTopFieldKey, kCVImageBufferChromaLocation_Left, kCVAttachmentMode_ShouldPropagate);
  gchar *path = g_strdup_printf("%s/layout-%u.yuv", argv[1], w); FILE *ref = fopen(path,"wb"); assert(ref); g_free(path);
  for (guint c = 0; c < 3; c++) for (guint y = 0; y < h; y++) for (guint x = 0; x < (c ? w / 2 : w); x++) {
   guint16 value = code(x,y,c); guint8 word[2]; GST_WRITE_UINT16_LE(word,value); assert(fwrite(word,2,1,ref)==1);
   guint8 *p = c ? (guint8 *)planes[1] + y * strides[1] + x * 4 + (c - 1) * 2 : (guint8 *)planes[0] + y * strides[0] + x * 2;
   GST_WRITE_UINT16_LE(p,value << 6);
  }
  fclose(ref);
  guint8 *guard = g_malloc(stride * h + 64); memset(guard,0xa5,stride * h + 64);
  struct pv_native422 d = {.width=w,.height=h,.stride=stride,.packed=guard+32};
  assert(pack(&d,image));
  for (guint i = 0; i < 32; i++) assert(guard[i]==0xa5 && guard[stride*h+32+i]==0xa5);
  path = g_strdup_printf("%s/layout-%u.v210",argv[1],w); FILE *out = fopen(path,"wb");assert(out);g_free(path);
  assert(fwrite(d.packed,stride*h,1,out)==1);fclose(out);
  /* Invalid low bits must not silently quantize, and a changed chroma site
   * must not silently resample. Reusing the same output allocation is tested. */
  ((guint8 *)planes[0])[0] |= 1; assert(!pack(&d,image)); ((guint8 *)planes[0])[0] &= ~1;
  CVBufferSetAttachment(image,kCVImageBufferChromaLocationTopFieldKey,kCVImageBufferChromaLocation_Center,kCVAttachmentMode_ShouldPropagate);
  assert(!pack(&d,image));
  CVPixelBufferRelease(image);g_free(alloc[0]);g_free(alloc[1]);g_free(guard);
 }
 puts("PASS native x422 production packing: 15 widths, padded unaligned planes, guard bytes, low-bit/siting rejection");
}
