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
