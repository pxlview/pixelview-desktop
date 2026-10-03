#pragma once
#include <QtCore/QString>
// User-facing wording that names the host platform or its credential store.
// The macOS text stays verbatim; other platforms get neutral wording.
#ifdef __APPLE__
#define PIXELVIEW_PLATFORM_TEXT(mac, other) QStringLiteral(mac)
#else
#define PIXELVIEW_PLATFORM_TEXT(mac, other) QStringLiteral(other)
#endif
