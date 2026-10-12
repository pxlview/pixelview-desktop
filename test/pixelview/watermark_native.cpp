// Sender watermark policy (utility/PixelviewWatermark.hpp); no graphics needed.
#include <utility/PixelviewWatermark.hpp>
#include <cassert>
using pixelview::WatermarkPolicy;
int main()
{
	// Declare only with a known state; when on, only with that exact image loaded.
	assert(!WatermarkPolicy::declare(false, false, "", ""));
	assert(!WatermarkPolicy::declare(false, true, "a1", "a1"));
	assert(WatermarkPolicy::declare(true, false, "", ""));
	assert(WatermarkPolicy::declare(true, false, "", "a1"));
	assert(WatermarkPolicy::declare(true, true, "a1", "a1"));
	assert(!WatermarkPolicy::declare(true, true, "b2", "a1"));
	assert(!WatermarkPolicy::declare(true, true, "a1", ""));
	assert(!WatermarkPolicy::declare(true, true, "", ""));
	// Shown only for a declared stream with the watermark on and an image loaded;
	// a newer image keeps the previous one until it is fetched.
	assert(WatermarkPolicy::show(true, true, true, "a1"));
	assert(!WatermarkPolicy::show(false, true, true, "a1"));
	assert(!WatermarkPolicy::show(true, true, false, "a1"));
	assert(!WatermarkPolicy::show(true, false, false, "a1"));
	assert(!WatermarkPolicy::show(true, true, true, ""));
	// Ids become file names: lowercase hex only.
	assert(WatermarkPolicy::validId("42cf3f83e3637a8a"));
	assert(!WatermarkPolicy::validId(""));
	assert(!WatermarkPolicy::validId("../x"));
	assert(!WatermarkPolicy::validId("ABC"));
	assert(!WatermarkPolicy::validId(QString(65, 'a')));
	return 0;
}
