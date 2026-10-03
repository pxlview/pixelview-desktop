// Pixelview updater on Windows: WinSparkle reading the Pixelview appcast with
// EdDSA-signed enclosures (Sparkle-compatible). Compiled only when the build
// is configured with a Pixelview appcast URL and public key.
#include "PixelviewSparkle.hpp"
#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>
#include <QAction>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <utility>
#include <winsparkle.h>

namespace {
// WinSparkle calls both callbacks on its own worker thread, never on main.
int __cdecl canShutdown()
{
	bool idle = false;
	QMetaObject::invokeMethod(
		qApp,
		[&idle] {
			auto *main = qobject_cast<OBSBasic *>(App()->GetMainWindow());
			idle = !main || !main->Active();
		},
		Qt::BlockingQueuedConnection);
	return idle ? 1 : 0;
}

void __cdecl requestShutdown()
{
	QMetaObject::invokeMethod(
		qApp,
		[] {
			if (auto *main = App()->GetMainWindow())
				main->close();
		},
		Qt::QueuedConnection);
}

void __cdecl updateError()
{
	blog(LOG_WARNING, "Pixelview updater: the update check or download failed");
}
} // namespace

PixelviewSparkle::PixelviewSparkle(QAction *checkForUpdatesAction) : observer(nullptr)
{
	Q_UNUSED(checkForUpdatesAction);
	win_sparkle_set_appcast_url(PIXELVIEW_SPARKLE_APPCAST_URL);
	if (!win_sparkle_set_eddsa_public_key(PIXELVIEW_SPARKLE_PUBLIC_KEY)) {
		blog(LOG_ERROR, "Pixelview updater: invalid update public key; updates are disabled");
		return;
	}
	win_sparkle_set_app_details(L"Pixelview", L"Pixelview Desktop", QStringLiteral(PIXELVIEW_VERSION).toStdWString().c_str());
	// The appcast's sparkle:version is the build number, as on macOS.
	win_sparkle_set_app_build_version(QStringLiteral(PIXELVIEW_BUILD_NUMBER).toStdWString().c_str());
	win_sparkle_set_registry_path("Software\\Pixelview\\Pixelview Desktop\\WinSparkle");
	win_sparkle_set_automatic_check_for_updates(
		config_get_bool(App()->GetAppConfig(), "General", "EnableAutoUpdates") ? 1 : 0);
	win_sparkle_set_update_check_interval(60 * 60 * 24);
	win_sparkle_set_can_shutdown_callback(canShutdown);
	win_sparkle_set_shutdown_request_callback(requestShutdown);
	win_sparkle_set_error_callback(updateError);
	win_sparkle_init();
	observer = this; // Marks a successful initialisation.
	connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
		if (std::exchange(observer, nullptr))
			win_sparkle_cleanup();
	});
}

PixelviewSparkle::~PixelviewSparkle()
{
	if (observer)
		win_sparkle_cleanup();
}

void PixelviewSparkle::checkForUpdates(bool manualCheck)
{
	if (!observer)
		return;
	if (manualCheck)
		win_sparkle_check_update_with_ui();
	else
		win_sparkle_check_update_without_ui();
}
