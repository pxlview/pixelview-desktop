// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <filesystem>

namespace pixelview {
// Set once during command-line parsing, before any configuration consumers.
// This is an application-file root, never a replacement HOME or Keychain domain.
inline std::string appConfigRoot;
inline bool setConfigRoot(const char *root)
{
	if (!root || !*root || !std::filesystem::u8path(root).is_absolute())
		return false;
	appConfigRoot = std::filesystem::u8path(root).lexically_normal().u8string();
	while (appConfigRoot.size() > 1 && appConfigRoot.back() == '/')
		appConfigRoot.pop_back();
	return true;
}
inline std::string configOverridePath(const char *name)
{
	return appConfigRoot + (name && *name ? (appConfigRoot.back() == '/' ? "" : "/") + std::string(name) : "");
}
// Keep upstream relative paths intact beneath a separate application root.
// nullptr must also resolve inside Pixelview: profile and scene paths append
// "obs-studio" themselves. This avoids reading/migrating stock OBS settings.
inline std::string configName(const char *name)
{
	return name && *name ? std::string("pixelview/") + name : "pixelview";
}
} // namespace pixelview
