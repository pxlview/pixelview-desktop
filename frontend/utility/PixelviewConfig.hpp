// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>

namespace pixelview {
// Keep upstream relative paths intact beneath a separate application root.
// nullptr must also resolve inside Pixelview: profile and scene paths append
// "obs-studio" themselves. This avoids reading/migrating stock OBS settings.
inline std::string configName(const char *name)
{
	return name && *name ? std::string("pixelview/") + name : "pixelview";
}
} // namespace pixelview
