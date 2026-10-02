// Pixelview modification, 2026-09-29: upload the application log to the
// backend for support and monitoring (docs/features.md, "Log upload").
// Policy lives in utility/PixelviewLogShipper.hpp; this is the Qt transport.
#include <QtCore/QCryptographicHash>
#include <QtCore/QSysInfo>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

// Which Mac this is ("Apple M1 Pro", "MacBookPro18,1"); empty when unknown.
static QString PixelviewLogSysctl(const char *name)
{
#ifdef __APPLE__
	char value[128] = {};
	size_t size = sizeof(value) - 1;
	if (sysctlbyname(name, value, &size, nullptr, 0) == 0) return QString::fromUtf8(value).trimmed();
#else
	(void)name;
#endif
	return {};
}

void OBSBasic::InitPixelviewLogs()
{
	auto *config = App()->GetUserConfig();
	config_set_default_bool(config, "PixelviewDiagnostics", "ShareLogs", true);
	auto &capture = pixelview::LogCapture::instance();
	pixelviewLogs = std::make_unique<pixelview::LogShipper>();
	pixelviewLogs->launchId = capture.launchId;
	pixelviewLogs->version = QString::fromUtf8(PIXELVIEW_VERSION);
	pixelviewLogs->build = QString::fromUtf8(PIXELVIEW_BUILD_NUMBER);
	pixelviewLogs->osVersion = QSysInfo::productVersion();
	pixelviewLogs->cpu = PixelviewLogSysctl("machdep.cpu.brand_string");
	pixelviewLogs->hardwareModel = PixelviewLogSysctl("hw.model");
	pixelviewLogs->home = QDir::homePath();
	BPtr<char> spool(GetAppConfigPathPtr("obs-studio/pixelview-log-spool.ndjson"));
	if (spool) pixelviewLogs->spoolPath = QString::fromUtf8(spool.Get());
	pixelviewLogHttp = new QNetworkAccessManager(this);
	pixelviewLogTimer = new QTimer(this);
	pixelviewLogTimer->setInterval(1000);
	connect(pixelviewLogTimer, &QTimer::timeout, this, &OBSBasic::PixelviewLogsTick);
	SetPixelviewLogSharing(config_get_bool(config, "PixelviewDiagnostics", "ShareLogs"));
}

void OBSBasic::SetPixelviewLogSharing(bool enabled)
{
	auto *config = App()->GetUserConfig();
	const bool changed = config_get_bool(config, "PixelviewDiagnostics", "ShareLogs") != enabled;
	config_set_bool(config, "PixelviewDiagnostics", "ShareLogs", enabled);
	if (changed) config_save_safe(config, "tmp", nullptr);
	pixelviewLogSharingOn = enabled;
	if (pixelviewLogSharing) pixelviewLogSharing->setChecked(enabled);
	auto &capture = pixelview::LogCapture::instance();
	if (!enabled) {
		// Off means nothing leaves the Mac: drop what is buffered and spooled.
		pixelviewLogTimer->stop();
		capture.setEnabled(false);
		pixelviewLogs->disable();
		blog(LOG_INFO, "Pixelview: log sharing with Pixelview support is off");
		return;
	}
	blog(LOG_INFO, "Pixelview: log sharing with Pixelview support is on (launch %s)",
	     pixelviewLogs->launchId.toUtf8().constData());
	capture.setEnabled(true);
	if (!pixelviewLogTimer->isActive()) {
		pixelviewLogs->loadSpool();
		pixelviewLogTimer->start();
	}
}

void OBSBasic::PixelviewLogsTick()
{
	if (!pixelviewLogs || !pixelview::LogCapture::instance().isEnabled()) return;
	qint64 dropped = 0;
	pixelviewLogs->ingest(pixelview::LogCapture::instance().take(dropped), dropped);
	pixelviewLogs->saveSpool(isClosing() || pixelviewShutdownPending);
	if (isClosing() || pixelviewShutdownPending) return;

	// A paired Desktop uploads with its device token and adds the viewer token
	// when it receives from the same backend; an unpaired receiver uses its
	// viewer token alone. Without either the lines stay spooled.
	QUrl origin;
	QString bearer, viewer;
	if (pixelviewPairingDurable && pixelviewDesktop && !pixelviewDesktop->authorizedToken.isEmpty() &&
	    pixelview::Desktop::validOrigin(pixelviewOrigin, pixelviewDev)) {
		origin = pixelviewOrigin;
		bearer = pixelviewDesktop->authorizedToken;
		if (QUrl(PixelviewReceiveOrigin()) == pixelviewOrigin) viewer = pixelviewLogViewerToken;
	} else if (!pixelviewLogViewerToken.isEmpty()) {
		origin = QUrl(PixelviewReceiveOrigin());
		bearer = pixelviewLogViewerToken;
		if (!pixelview::Desktop::validOrigin(origin, pixelview::localDevelopmentEnabled())) return;
	}
	const QString credential =
		bearer.isEmpty() ? QString()
				 : QString::fromLatin1(QCryptographicHash::hash((bearer + '|' + viewer).toUtf8(),
									       QCryptographicHash::Sha256)
								.toHex());
	if (!pixelviewLogs->due(credential)) return;
	const auto batch = pixelviewLogs->nextBatch();
	if (!batch.count) return;

	origin.setPath(QStringLiteral("/desktop/logs"));
	QNetworkRequest request(origin);
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setRawHeader("Authorization", "Bearer " + bearer.toUtf8());
	if (!viewer.isEmpty()) request.setRawHeader("X-Pixelview-Viewer-Token", viewer.toUtf8());
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
	request.setTransferTimeout(15000);
	pixelviewLogs->inFlight = true;
	const qint64 flight = ++pixelviewLogs->flight;
	auto *reply = pixelviewLogHttp->post(request, batch.body);
	reply->setReadBufferSize(65536);
	const qint64 lastUid = batch.lastUid;
	connect(reply, &QNetworkReply::finished, this, [this, reply, lastUid, flight, credential] {
		reply->deleteLater();
		if (!pixelviewLogs || pixelviewLogs->flight != flight) return;
		const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const int retryAfter = reply->rawHeader("Retry-After").toInt();
		using Result = pixelview::LogShipper::Result;
		const bool wasFailing = pixelviewLogs->failures > 0;
		const auto result = pixelviewLogs->finished(http, retryAfter, lastUid, credential);
		// Only transitions are logged: every line here is itself uploaded.
		if (result == Result::Sent && wasFailing)
			blog(LOG_INFO, "Pixelview: log upload recovered");
		else if (result == Result::Retry && pixelviewLogs->failures == 1)
			blog(LOG_WARNING, "Pixelview: log upload failed (HTTP %d); retrying with backoff", http);
		else if (result == Result::Paused || result == Result::Discarded)
			blog(LOG_WARNING, "Pixelview: log upload answered HTTP %d (%s)", http,
			     result == Result::Paused ? "paused" : "batch discarded");
	});
}
