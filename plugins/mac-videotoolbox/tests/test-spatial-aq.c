/* Compile with the selected SDK; exercise real framework exports, not mocks.
 * xcrun clang -std=c11 -Wall -Wextra -Werror test-spatial-aq.c
 *   -framework VideoToolbox -framework CoreFoundation -o /tmp/test-spatial-aq
 */
#include <VideoToolbox/VideoToolbox.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#if __has_include("../vt-compat.h")
#include "../vt-compat.h"
#else
#define vt_spatial_aq_key() kVTCompressionPropertyKey_SpatialAdaptiveQPLevel
#endif

int main(void)
{
	assert(kVTQPModulationLevel_Default == -1);
	assert(kVTQPModulationLevel_Disable == 0);
	if (__builtin_available(macOS 15.0, *)) {
		CFStringRef key = vt_spatial_aq_key();
		CFStringRef *exported = dlsym(RTLD_DEFAULT, "kVTCompressionPropertyKey_SpatialAdaptiveQPLevel");
		assert(exported && key && CFEqual(key, *exported));
		assert(CFEqual(key, CFSTR("SpatialAdaptiveQPLevel")));
		puts("PASS: real SpatialAdaptiveQPLevel export and QP enum values");
	} else {
		assert(vt_spatial_aq_key() == NULL);
		puts("PASS: spatial AQ unavailable before macOS 15");
	}
	return 0;
}
