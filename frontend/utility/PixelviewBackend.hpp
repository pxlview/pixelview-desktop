#pragma once
#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <optional>

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

// Unattended pairing for automated tests: PIXELVIEW_PAIR_CODE (one-time admin
// code) and optional PIXELVIEW_PAIR_ORIGIN, honoured only with
// PIXELVIEW_LOCAL_DEVELOPMENT=1. Both are removed from the environment so the
// code never reaches child processes.
struct EnvironmentPairing {
	QUrl origin;
	QString code;
};

inline std::optional<EnvironmentPairing> takeEnvironmentPairing()
{
	const QString code = QString::fromUtf8(qgetenv("PIXELVIEW_PAIR_CODE")).trimmed();
	const QString origin = QString::fromUtf8(qgetenv("PIXELVIEW_PAIR_ORIGIN")).trimmed();
	qunsetenv("PIXELVIEW_PAIR_CODE");
	qunsetenv("PIXELVIEW_PAIR_ORIGIN");
	if (code.isEmpty() || !localDevelopmentEnabled())
		return std::nullopt;
	return EnvironmentPairing{QUrl(origin.isEmpty() ? defaultBackendOrigin() : origin), code};
}
}
