// Test-only dyld interposition: fail before Qt passes its already-released
// color-space reference to CoreGraphics. No replacement objects or data.
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>
#include <cstdlib>

static thread_local CGColorSpaceRef lastColor;
static thread_local int owned;
static CGColorSpaceRef createName(CFStringRef name)
{
    auto color = CGColorSpaceCreateWithName(name);
    lastColor = color;
    owned = 1;
    return color;
}
static CGColorSpaceRef createICC(CFDataRef data)
{
    auto color = CGColorSpaceCreateWithICCData(data);
    lastColor = color;
    owned = 1;
    return color;
}
static CFTypeRef retain(CFTypeRef value)
{
    if (value == lastColor)
        ++owned;
    return CFRetain(value);
}
static void release(CFTypeRef value)
{
    if (value == lastColor)
        --owned;
    CFRelease(value);
}
static CGImageRef createImage(size_t width, size_t height, size_t bpc, size_t bpp,
                             size_t row, CGColorSpaceRef color, CGBitmapInfo info,
                             CGDataProviderRef provider, const CGFloat *decode,
                             bool interpolate, CGColorRenderingIntent intent)
{
    if (color && color == lastColor && owned <= 0) {
        std::fputs("FAIL: color-space Create reference released before CGImageCreate\n", stderr);
        std::_Exit(91);
    }
    return CGImageCreate(width, height, bpc, bpp, row, color, info, provider,
                         decode, interpolate, intent);
}
#define INTERPOSE(replacement, original) \
    __attribute__((used, section("__DATA,__interpose"))) static const struct { \
        const void *replacement; const void *original; \
    } interpose_##original = {(const void *)&replacement, (const void *)&original}
INTERPOSE(createName, CGColorSpaceCreateWithName);
INTERPOSE(createICC, CGColorSpaceCreateWithICCData);
INTERPOSE(retain, CFRetain);
INTERPOSE(release, CFRelease);
INTERPOSE(createImage, CGImageCreate);
