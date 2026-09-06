#pragma once

#include <Availability.h>
/* Use the SDK maximum, not AvailabilityMacros.h's deployment-derived maximum. */
#include <VideoToolbox/VideoToolbox.h>

#if __MAC_OS_X_VERSION_MAX_ALLOWED < 150000
#include <dlfcn.h>

/* Public macOS 15 VTQPModulationLevel values, also used by FFmpeg's
 * old-SDK compatibility path in libavcodec/videotoolboxenc.c.
 * These are enum constants, not exported symbols or preprocessor macros. */
enum { kVTQPModulationLevel_Default = -1, kVTQPModulationLevel_Disable = 0 };
#endif

static inline CFStringRef vt_spatial_aq_key(void)
{
	if (__builtin_available(macOS 15.0, *)) {
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
		return kVTCompressionPropertyKey_SpatialAdaptiveQPLevel;
#else
		/* Resolve the actual public CFString variable, not a guessed key.
		 * Missing exports remain unavailable; never dereference NULL. */
		const CFStringRef *key = dlsym(RTLD_DEFAULT, "kVTCompressionPropertyKey_SpatialAdaptiveQPLevel");
		return key ? *key : NULL;
#endif
	}
	return NULL;
}
