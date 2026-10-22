// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace pixelview {
inline bool shouldChangeDevice(const std::string &current, const std::string &requested)
{
	return !requested.empty() && current != requested;
}
inline std::optional<int64_t> chooseOption(int64_t current, const std::vector<int64_t> &supported,
					 std::optional<int64_t> preferred = std::nullopt)
{
	for (auto value : supported)
		if (value == current)
			return current;
	for (auto value : supported)
		if (preferred && value == *preferred)
			return value;
	if (supported.empty())
		return std::nullopt;
	return supported.front();
}
struct Device {
	std::string id;
	std::string name;
};
// A generated test pattern stands in for the DeckLink input without hardware.
// It is a second managed source in the sender scene; exactly one of the two
// scene items is visible, and that one is the capture. In the device list and
// the remote-control state it appears as one entry per pattern.
inline constexpr const char *TestPatternSourceId = "pixelview_test_pattern";
inline constexpr const char *TestPatternSourceName = "Pixelview Test Pattern";
inline constexpr const char *TestPatternPrefix = "test-pattern:";
inline std::string testPatternId(int64_t pattern)
{
	return TestPatternPrefix + std::to_string(pattern);
}
inline std::optional<int64_t> testPatternFromId(const std::string &id)
{
	const std::string prefix = TestPatternPrefix;
	if (id.size() <= prefix.size() || id.compare(0, prefix.size(), prefix) != 0)
		return std::nullopt;
	int64_t value = 0;
	for (size_t i = prefix.size(); i < id.size(); ++i) {
		if (id[i] < '0' || id[i] > '9' || value > 1000)
			return std::nullopt;
		value = value * 10 + (id[i] - '0');
	}
	return value;
}

// An AJA input is a third managed source, created by the stock AJA plugin
// ("aja_source"). Its devices are listed after the DeckLink devices with the
// plugin's card id behind a prefix, so ids never collide with DeckLink hashes.
// The plugin only registers the source when a card is present at launch.
inline constexpr const char *AjaSourceId = "aja_source";
inline constexpr const char *AjaSourceName = "Pixelview AJA Capture";
inline constexpr const char *AjaDevicePrefix = "aja:";
inline constexpr const char *AjaDeviceKey = "ui_prop_device";
inline constexpr const char *AjaInputKey = "ui_prop_input";
inline constexpr const char *AjaPixelFormatKey = "ui_prop_pix_fmt";
inline constexpr const char *AjaDeactivateKey = "ui_prop_deactivate_when_not_showing";
inline constexpr int64_t AjaInputNone = 22;       // IOSelection::Invalid ("Select...")
inline constexpr int64_t AjaPixelFormat10Bit = 0; // NTV2_FBF_10BIT_YCBCR (v210)
inline std::string ajaDeviceId(const std::string &card)
{
	return AjaDevicePrefix + card;
}
inline std::optional<std::string> ajaCardFromId(const std::string &id)
{
	const std::string prefix = AjaDevicePrefix;
	if (id.size() <= prefix.size() || id.compare(0, prefix.size(), prefix) != 0)
		return std::nullopt;
	return id.substr(prefix.size());
}
// The saved input while it is still offered, else the first one; never "Select...".
inline std::optional<int64_t> chooseAjaInput(int64_t current, const std::vector<int64_t> &offered)
{
	std::vector<int64_t> inputs;
	for (auto value : offered)
		if (value != AjaInputNone)
			inputs.push_back(value);
	return chooseOption(current, inputs);
}
// AJA stays close to the stock plugin: SDR Rec.709, up to 10-bit 4:2:2. Empty when allowed.
inline std::string ajaStreamRefusal(bool hdr, const std::string &profile)
{
	if (hdr)
		return "HDR is not supported with an AJA input yet. Ask an admin to set the node to SDR, or use a Blackmagic input.";
	if (profile == "main44410")
		return "Main 4:4:4 10 is not supported with an AJA input yet. Choose Main, Main10 or Main 4:2:2 10.";
	return {};
}

enum class CaptureStatus { PluginUnavailable, NoDevices, NotSelected, Disconnected, AvailableUnverified, TestPattern };
inline CaptureStatus captureStatus(bool plugin, const std::vector<Device> &devices, const std::string &selected)
{
	if (testPatternFromId(selected))
		return CaptureStatus::TestPattern;
	if (!plugin)
		return CaptureStatus::PluginUnavailable;
	if (!selected.empty()) {
		for (const auto &device : devices)
			if (device.id == selected)
				return CaptureStatus::AvailableUnverified;
		return CaptureStatus::Disconnected;
	}
	return devices.empty() ? CaptureStatus::NoDevices : CaptureStatus::NotSelected;
}
inline constexpr unsigned CanvasWidth = 1920;
inline constexpr unsigned CanvasHeight = 1080;

// A fit is a one-shot scene transform, never a timer-driven constraint.
// Restored scenes already own their transform and do not call sourceCreated().
class FitPolicy {
	bool requested = false;

public:
	void sourceCreated() { requested = true; }
	void requestReset() { requested = true; }
	bool takeRequest()
	{
		const bool result = requested;
		requested = false;
		return result;
	}
};
} // namespace pixelview
