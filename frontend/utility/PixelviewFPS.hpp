// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace pixelview {
struct FrameRate {
	const char *label;
	uint32_t num;
	uint32_t den;
};
inline constexpr std::array<FrameRate, 8> FrameRates = {{{"23.976", 24000, 1001}, {"24", 24, 1},
	{"25", 25, 1}, {"29.97", 30000, 1001}, {"30", 30, 1}, {"50", 50, 1},
	{"59.94", 60000, 1001}, {"60", 60, 1}}};
inline int frameRateIndex(uint32_t num, uint32_t den)
{
	if (!num || !den)
		return -1;
	for (size_t i = 0; i < FrameRates.size(); ++i)
		if (uint64_t(num) * FrameRates[i].den == uint64_t(FrameRates[i].num) * den)
			return static_cast<int>(i);
	return -1;
}
enum class FPSChangeResult { Success, Active, ResetFailed, SaveFailed, RollbackFailed };

// Real UI supplies OBS/config operations; tests exercise this same transaction.
// Save only after a successful reset. A safe-save failure leaves the old file intact.
template<typename Apply, typename Restore, typename Reset, typename Save>
FPSChangeResult changeFrameRate(bool active, Apply apply, Restore restore, Reset reset, Save save)
{
	if (active)
		return FPSChangeResult::Active;
	apply();
	const bool resetOK = reset();
	if (resetOK && save())
		return FPSChangeResult::Success;
	restore();
	if (!reset())
		return FPSChangeResult::RollbackFailed;
	return resetOK ? FPSChangeResult::SaveFailed : FPSChangeResult::ResetFailed;
}
} // namespace pixelview
