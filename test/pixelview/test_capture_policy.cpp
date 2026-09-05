// Standalone tests: c++ -std=c++17 test/pixelview/test_capture_policy.cpp -o /tmp/pixelview-policy && /tmp/pixelview-policy
#include <cassert>
#include <iostream>
#if __has_include("../../frontend/utility/PixelviewCapturePolicy.hpp")
#include "../../frontend/utility/PixelviewCapturePolicy.hpp"
int main()
{
	pixelview::FitPolicy fit;
	assert(!fit.takeRequest());
	fit.sourceCreated();
	assert(fit.takeRequest());
	assert(!fit.takeRequest()); // timers, hotplug and manual edits must not refit
	fit.requestReset();
	assert(fit.takeRequest());
	assert(!fit.takeRequest());
	assert(pixelview::CanvasWidth == 1920);
	assert(pixelview::CanvasHeight == 1080);
	using pixelview::CaptureStatus;
	using pixelview::captureStatus;
	std::vector<pixelview::Device> devices{{"real-hash", "Native device name"}};
	assert(captureStatus(false, {}, "") == CaptureStatus::PluginUnavailable);
	assert(captureStatus(true, {}, "") == CaptureStatus::NoDevices);
	assert(captureStatus(true, devices, "") == CaptureStatus::NotSelected);
	assert(captureStatus(true, devices, "old-hash") == CaptureStatus::Disconnected);
	assert(captureStatus(true, devices, "real-hash") == CaptureStatus::AvailableUnverified);
	assert(captureStatus(true, {}, "real-hash") == CaptureStatus::Disconnected);
	// Reappearance is availability, never evidence of frames or a fit request.
	assert(captureStatus(true, devices, "real-hash") == CaptureStatus::AvailableUnverified);
	assert(!fit.takeRequest());
	assert(!pixelview::shouldChangeDevice("same-id", "same-id"));
	assert(!pixelview::shouldChangeDevice("same-id", ""));
	assert(pixelview::shouldChangeDevice("", "new-id"));
	assert(pixelview::shouldChangeDevice("old-id", "new-id"));
	assert(pixelview::chooseOption(2, {1, 2, 3}) == 2);
	assert(pixelview::chooseOption(9, {1, 2, 3}) == 1);
	assert(!pixelview::chooseOption(9, {}).has_value());
	// Native channel lists put None first, but unsupported layouts prefer stereo.
	assert(pixelview::chooseOption(8, {0, 2}, 2) == 2);
	assert(pixelview::chooseOption(0, {0, 2}, 2) == 0); // explicit None stays None
	assert(pixelview::chooseOption(2, {0, 2}, 2) == 2);
	assert(pixelview::chooseOption(8, {0, 2, 8}, 2) == 8);
	assert(pixelview::chooseOption(8, {0}, 2) == 0); // never select an unsupported default
	assert(!pixelview::chooseOption(8, {}, 2).has_value());
	std::cout << "Pixelview fit policy passed\n";
}
#else
int main() { std::cerr << "FAIL: Pixelview fit policy is missing\n"; return 1; }
#endif
