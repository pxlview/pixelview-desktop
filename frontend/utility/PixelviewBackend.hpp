#pragma once
#include <QtCore/QByteArray>
#include <QtCore/QString>

namespace pixelview {
// Runtime defaults for new pairing and the independent receiver. Never use
// these to rewrite the origin/development permission of a saved device pairing.
inline bool localDevelopmentEnabled()
{
	return qgetenv("PIXELVIEW_LOCAL_DEVELOPMENT") == "1";
}

inline QString defaultBackendOrigin()
{
	return localDevelopmentEnabled() ? QStringLiteral("http://localhost:8000")
	                                 : QStringLiteral("https://api4.pixelview.io");
}
}
