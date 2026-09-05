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
enum class CaptureStatus { PluginUnavailable, NoDevices, NotSelected, Disconnected, AvailableUnverified };
inline CaptureStatus captureStatus(bool plugin, const std::vector<Device> &devices, const std::string &selected)
{
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
