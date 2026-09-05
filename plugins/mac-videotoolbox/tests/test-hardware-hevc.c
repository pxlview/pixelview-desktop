/* Native hardware smoke test: no OBS/app launch, actual encoded frames.
 * xcrun clang -std=c11 -Wall -Wextra -Werror test-hardware-hevc.c
 *   -framework VideoToolbox -framework CoreFoundation -framework CoreVideo
 *   -framework CoreMedia -o /tmp/test-hardware-hevc
 */
#include "../vt-compat.h"
#include <CoreVideo/CoreVideo.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct result {
	int frames;
	int keys;
	size_t bytes;
};

static void encoded(void *context, void *frame, OSStatus status, VTEncodeInfoFlags flags,
		    CMSampleBufferRef sample)
{
	(void)frame;
	(void)flags;
	assert(status == noErr && sample && CMSampleBufferDataIsReady(sample));
	struct result *result = context;
	result->frames++;
	result->bytes += CMSampleBufferGetTotalSampleSize(sample);
	CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
	bool sync = !attachments || !CFDictionaryContainsKey(CFArrayGetValueAtIndex(attachments, 0),
							   kCMSampleAttachmentKey_NotSync);
	if (sync) {
		result->keys++;
		printf(" keyframe=%d", result->frames - 1);
	}
}

static void set_int(VTCompressionSessionRef session, CFStringRef key, int value)
{
	CFNumberRef number = CFNumberCreate(NULL, kCFNumberIntType, &value);
	assert(VTSessionSetProperty(session, key, number) == noErr);
	CFRelease(number);
}

static void readback(VTCompressionSessionRef session, CFStringRef key, CFTypeRef expected)
{
	CFTypeRef value = NULL;
	assert(VTSessionCopyProperty(session, key, NULL, &value) == noErr);
	assert(value && CFEqual(value, expected));
	CFRelease(value);
}

static void verify_int(VTCompressionSessionRef session, CFStringRef key, int expected)
{
	CFNumberRef value = CFNumberCreate(NULL, kCFNumberIntType, &expected);
	readback(session, key, value);
	CFRelease(value);
}

static void check_profile(CFStringRef encoder_id, CFStringRef profile, OSType format)
{
	CFTypeRef spec_keys[] = {kVTVideoEncoderSpecification_EncoderID,
				kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder};
	CFTypeRef spec_values[] = {encoder_id, kCFBooleanTrue};
	CFDictionaryRef spec = CFDictionaryCreate(NULL, spec_keys, spec_values, 2,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFNumberRef pixel_format = CFNumberCreate(NULL, kCFNumberSInt32Type, &format);
	CFTypeRef pixel_keys[] = {kCVPixelBufferPixelFormatTypeKey};
	CFTypeRef pixel_values[] = {pixel_format};
	CFDictionaryRef pixels = CFDictionaryCreate(NULL, pixel_keys, pixel_values, 1,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	struct result result = {0};
	VTCompressionSessionRef session = NULL;
	assert(VTCompressionSessionCreate(NULL, 1280, 720, kCMVideoCodecType_HEVC, spec, pixels,
		NULL, encoded, &result, &session) == noErr);
	readback(session, kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder, kCFBooleanTrue);
	assert(VTSessionSetProperty(session, kVTCompressionPropertyKey_ProfileLevel, profile) == noErr);
	set_int(session, kVTCompressionPropertyKey_ConstantBitRate, 6000000);
	set_int(session, kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, 1);
	set_int(session, kVTCompressionPropertyKey_MaxKeyFrameInterval, 60);
	set_int(session, kVTCompressionPropertyKey_ExpectedFrameRate, 60);
	assert(VTSessionSetProperty(session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse) == noErr);
	assert(vt_spatial_aq_key());
	set_int(session, vt_spatial_aq_key(), kVTQPModulationLevel_Disable); /* Auto + CBR */
	assert(VTSessionSetProperty(session, kVTCompressionPropertyKey_RealTime, kCFBooleanFalse) == noErr);
	assert(VTCompressionSessionPrepareToEncodeFrames(session) == noErr);
	readback(session, kVTCompressionPropertyKey_ProfileLevel, profile);
	verify_int(session, kVTCompressionPropertyKey_ConstantBitRate, 6000000);
	verify_int(session, kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, 1);
	verify_int(session, kVTCompressionPropertyKey_MaxKeyFrameInterval, 60);
	verify_int(session, vt_spatial_aq_key(), kVTQPModulationLevel_Disable);
	readback(session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
	char name[128];
	assert(CFStringGetCString(profile, name, sizeof(name), kCFStringEncodingUTF8));
	printf("%s hardware=true CBR=6000000 keyint=1 bframes=false spatialAQ=0:", name);
	for (int i = 0; i < 65; i++) {
		CVPixelBufferRef buffer = NULL;
		assert(CVPixelBufferPoolCreatePixelBuffer(NULL, VTCompressionSessionGetPixelBufferPool(session), &buffer) == kCVReturnSuccess);
		assert(CVPixelBufferGetPixelFormatType(buffer) == format);
		CVPixelBufferLockBaseAddress(buffer, 0);
		for (size_t plane = 0; plane < CVPixelBufferGetPlaneCount(buffer); plane++)
			memset(CVPixelBufferGetBaseAddressOfPlane(buffer, plane), plane ? 128 : 16,
				CVPixelBufferGetBytesPerRowOfPlane(buffer, plane) * CVPixelBufferGetHeightOfPlane(buffer, plane));
		CVPixelBufferUnlockBaseAddress(buffer, 0);
		assert(VTCompressionSessionEncodeFrame(session, buffer, CMTimeMake(i, 60), CMTimeMake(1, 60),
			NULL, NULL, NULL) == noErr);
		CVPixelBufferRelease(buffer);
	}
	assert(VTCompressionSessionCompleteFrames(session, kCMTimeInvalid) == noErr);
	assert(result.frames == 65 && result.keys >= 2 && result.bytes > 0);
	printf(" frames=%d keys=%d bytes=%zu PASS\n", result.frames, result.keys, result.bytes);
	VTCompressionSessionInvalidate(session);
	CFRelease(session);
	CFRelease(pixel_format);
	CFRelease(pixels);
	CFRelease(spec);
}

int main(void)
{
	CFArrayRef list = NULL;
	assert(VTCopyVideoEncoderList(NULL, &list) == noErr);
	CFStringRef encoder_id = NULL;
	for (CFIndex i = 0; i < CFArrayGetCount(list); i++) {
		CFDictionaryRef entry = CFArrayGetValueAtIndex(list, i);
		int codec = 0;
		CFNumberGetValue(CFDictionaryGetValue(entry, kVTVideoEncoderList_CodecType), kCFNumberIntType, &codec);
		if (codec == kCMVideoCodecType_HEVC &&
		    CFDictionaryGetValue(entry, kVTVideoEncoderList_IsHardwareAccelerated) == kCFBooleanTrue) {
			encoder_id = CFDictionaryGetValue(entry, kVTVideoEncoderList_EncoderID);
			break;
		}
	}
	assert(encoder_id);
	char id[256];
	assert(CFStringGetCString(encoder_id, id, sizeof(id), kCFStringEncodingUTF8));
	printf("Discovered hardware HEVC: %s\n", id);
	check_profile(encoder_id, kVTProfileLevel_HEVC_Main_AutoLevel, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange);
	check_profile(encoder_id, kVTProfileLevel_HEVC_Main10_AutoLevel, kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange);
	check_profile(encoder_id, kVTProfileLevel_HEVC_Main42210_AutoLevel, kCVPixelFormatType_422YpCbCr16BiPlanarVideoRange);
	CFRelease(list);
	return 0;
}
