/* SPDX-License-Identifier: GPL-2.0-or-later
 * Hardware HEVC Main 4:2:2 10 and Main 4:4:4 10 input rounding (see
 * run-422-rounding.py). The libobs P216/P416 canvas output holds ten-bit codes
 * as code << 6 plus float rounding noise in the low six bits. This encodes flat
 * patches of code << 6 + OFFSET from 16-bit 'sv22' (or, with 444, 'sv44') pixel
 * buffers, optionally rounded to the nearest ten-bit code first exactly as
 * plugins/mac-videotoolbox/encoder.c does, and writes Annex B for an
 * independent decode.
 * usage: test-422-rounding OFFSET OUTPUT.h265 [round] [444]
 */
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>
static NSMutableData *out;
static void cb(void *ctx, void *frame, OSStatus st, VTEncodeInfoFlags fl, CMSampleBufferRef s) {
 if (st || !s) { printf("encode status %d\n",(int)st); return; }
 CMFormatDescriptionRef fd=CMSampleBufferGetFormatDescription(s); static const uint8_t sc[4]={0,0,0,1};
 CFArrayRef att=CMSampleBufferGetSampleAttachmentsArray(s,false);
 bool key=!att||!CFDictionaryContainsKey(CFArrayGetValueAtIndex(att,0),kCMSampleAttachmentKey_NotSync);
 if(key){ size_t n=0; CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fd,0,NULL,NULL,&n,NULL);
  for(size_t i=0;i<n;i++){ const uint8_t *p; size_t len; CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fd,i,&p,&len,NULL,NULL); [out appendBytes:sc length:4]; [out appendBytes:p length:len]; } }
 CMBlockBufferRef bb=CMSampleBufferGetDataBuffer(s); size_t total=CMBlockBufferGetDataLength(bb); NSMutableData *d=[NSMutableData dataWithLength:total];
 CMBlockBufferCopyDataBytes(bb,0,total,d.mutableBytes); const uint8_t *b=d.bytes; size_t o=0;
 while(o+4<=total){ uint32_t len=(b[o]<<24)|(b[o+1]<<16)|(b[o+2]<<8)|b[o+3]; [out appendBytes:sc length:4]; [out appendBytes:b+o+4 length:len]; o+=4+len; }
}
int main(int argc,char **argv){ @autoreleasepool {
 int offset=atoi(argv[1]); bool round10=false,full=false; size_t W=1920,H=1080; out=[NSMutableData data];
 for(int i=3;i<argc;i++){ if(!strcmp(argv[i],"round")) round10=true; if(!strcmp(argv[i],"444")) full=true; }
 NSDictionary *spec=@{(id)kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder:@YES};
 VTCompressionSessionRef s=NULL; OSStatus st=VTCompressionSessionCreate(NULL,(int32_t)W,(int32_t)H,kCMVideoCodecType_HEVC,(__bridge CFDictionaryRef)spec,NULL,NULL,cb,NULL,&s);
 if(st){printf("create %d\n",(int)st);return 1;}
 /* Main 4:4:4 10 has no SDK constant; the encoder lists this value itself. */
 st=VTSessionSetProperty(s,kVTCompressionPropertyKey_ProfileLevel,full?CFSTR("HEVC_Main44410_AutoLevel"):kVTProfileLevel_HEVC_Main42210_AutoLevel);
 if(st){printf("profile %d\n",(int)st);return 1;}
 VTSessionSetProperty(s,kVTCompressionPropertyKey_RealTime,kCFBooleanTrue);
 VTSessionSetProperty(s,kVTCompressionPropertyKey_AllowFrameReordering,kCFBooleanFalse);
 VTSessionSetProperty(s,kVTCompressionPropertyKey_AverageBitRate,(__bridge CFNumberRef)@(40000000));
 VTSessionSetProperty(s,kVTCompressionPropertyKey_ColorPrimaries,kCVImageBufferColorPrimaries_ITU_R_709_2);
 VTSessionSetProperty(s,kVTCompressionPropertyKey_TransferFunction,kCVImageBufferTransferFunction_ITU_R_709_2);
 VTSessionSetProperty(s,kVTCompressionPropertyKey_YCbCrMatrix,kCVImageBufferYCbCrMatrix_ITU_R_709_2);
 for(int n=0;n<12;n++){ CVPixelBufferRef pb=NULL; CVPixelBufferCreate(NULL,W,H,full?kCVPixelFormatType_444YpCbCr16BiPlanarVideoRange:kCVPixelFormatType_422YpCbCr16BiPlanarVideoRange,(__bridge CFDictionaryRef)@{(id)kCVPixelBufferIOSurfacePropertiesKey:@{}},&pb);
  CVPixelBufferLockBaseAddress(pb,0);
  for(int pl=0;pl<2;pl++){ uint8_t *base=CVPixelBufferGetBaseAddressOfPlane(pb,pl); size_t bpr=CVPixelBufferGetBytesPerRowOfPlane(pb,pl);
   // The 4:4:4 chroma plane holds a CbCr pair for every pixel, twice as many words per row.
   const size_t words=pl&&full?W*2:W, per_patch=pl&&full?240:120;
   for(size_t y=0;y<H;y++){ uint16_t *row=(uint16_t*)(base+y*bpr); for(size_t x=0;x<words;x++){
     // 16 columns x 8 rows of flat patches; luma code varies per patch, chroma per patch too
     unsigned patch=(unsigned)(x/per_patch)+16*(unsigned)(y/135); unsigned code = pl==0 ? 64+patch*6+ (patch%7) : ((x&1)? 400+patch : 620-patch);
     int v=(int)code*64+offset; if(round10){ unsigned c=((unsigned)v+32)>>6; v=(int)(c>1023?1023:c)<<6; } row[x]=(uint16_t)v; } } }
  CVPixelBufferUnlockBaseAddress(pb,0);
  VTCompressionSessionEncodeFrame(s,pb,CMTimeMake(n,25),CMTimeMake(1,25),NULL,NULL,NULL); CFRelease(pb); }
 VTCompressionSessionCompleteFrames(s,kCMTimeInvalid);
 [out writeToFile:@(argv[2]) atomically:YES]; printf("wrote %lu bytes\n",(unsigned long)out.length);
}}
