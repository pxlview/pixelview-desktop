// Pixelview Desktop: the session watermark burned into the sent picture.
// See utility/PixelviewWatermark.hpp for the engine contract.
#include <QImage>
#include <QTemporaryDir>
#include <utility/PixelviewWatermark.hpp>

static constexpr qint64 PIXELVIEW_WATERMARK_MAX_BYTES = 8 * 1024 * 1024;

void OBSBasic::InitPixelviewWatermark()
{
	obs_add_main_rendered_callback(pixelview::WatermarkOverlay::rendered, &pixelviewWatermark);
	pixelviewWatermarkRetry = new QTimer(this);
	pixelviewWatermarkRetry->setSingleShot(true);
	connect(pixelviewWatermarkRetry, &QTimer::timeout, this, &OBSBasic::RefreshPixelviewWatermark);
	pixelviewLease.watermarkChanged = [this] {
		blog(LOG_INFO, "Pixelview: session watermark %s",
		     !pixelviewLease.watermarkKnown     ? "unknown (left to the engine)"
		     : pixelviewLease.watermarkEnabled ? QStringLiteral("on (%1)").arg(pixelviewLease.watermarkId).toUtf8().constData()
						       : "off");
		pixelviewWatermarkRetry->stop();
		// Queued: DESKTOP_STARTED publishes right after it changes.
		QTimer::singleShot(0, this, &OBSBasic::RefreshPixelviewWatermark);
	};
}

void OBSBasic::ShutdownPixelviewWatermark()
{
	obs_remove_main_rendered_callback(pixelview::WatermarkOverlay::rendered, &pixelviewWatermark);
	pixelviewWatermark.show(false);
	OBSSource previous = pixelviewWatermark.set(nullptr);
	previous = nullptr;
	++pixelviewWatermarkFetchSeq;
	pixelviewWatermarkDir.reset();
}

// Decided once per WHIP connection, before it starts (the header cannot change).
bool OBSBasic::DeclarePixelviewWatermark()
{
	pixelviewWatermarkDeclared = pixelview::WatermarkPolicy::declare(
		pixelviewLease.watermarkKnown, pixelviewLease.watermarkEnabled, pixelviewLease.watermarkId,
		pixelviewWatermarkLoaded);
	blog(LOG_INFO, "Pixelview: this stream %s",
	     pixelviewWatermarkDeclared ? "burns the session watermark in (sender watermark)"
					: "leaves the session watermark to the engine");
	RefreshPixelviewWatermark();
	return pixelviewWatermarkDeclared;
}

// The WHIP answer said whether the engine honours the declaration.
void OBSBasic::PixelviewWatermarkStarted()
{
	if (!pixelviewWatermarkDeclared || !outputHandler || !outputHandler->streamOutput)
		return;
	calldata_t cd = {};
	bool acknowledged = false;
	if (proc_handler_call(obs_output_get_proc_handler(outputHandler->streamOutput), "pixelview_watermark", &cd))
		acknowledged = calldata_bool(&cd, "declared") && calldata_bool(&cd, "acknowledged");
	calldata_free(&cd);
	if (!acknowledged) {
		blog(LOG_WARNING, "Pixelview: the engine did not acknowledge the sender watermark; it watermarks this stream itself");
		pixelviewWatermarkDeclared = false;
	}
	RefreshPixelviewWatermark();
}

void OBSBasic::PixelviewWatermarkStopped()
{
	pixelviewWatermarkDeclared = false;
	RefreshPixelviewWatermark();
}

void OBSBasic::RefreshPixelviewWatermark()
{
	if (isClosing() || pixelviewShutdownPending)
		return;
	const bool wanted = pixelviewLease.watermarkKnown && pixelviewLease.watermarkEnabled;
	if (wanted && pixelviewWatermarkLoaded != pixelviewLease.watermarkId &&
	    pixelviewWatermarkFetching != pixelviewLease.watermarkId && !pixelviewWatermarkRetry->isActive())
		FetchPixelviewWatermark();
	const bool show = pixelview::WatermarkPolicy::show(pixelviewWatermarkDeclared, pixelviewLease.watermarkKnown,
							   pixelviewLease.watermarkEnabled, pixelviewWatermarkLoaded);
	if (show != pixelviewWatermark.showing())
		blog(LOG_INFO, "Pixelview: sender watermark %s", show ? "shown" : "hidden");
	pixelviewWatermark.show(show);
	if (pixelviewWatermarkDeclared && wanted && pixelviewWatermarkLoaded != pixelviewLease.watermarkId)
		blog(LOG_INFO, "Pixelview: sender watermark %s is not loaded yet%s",
		     pixelviewLease.watermarkId.toUtf8().constData(), show ? "; the previous one stays" : "");
}

void OBSBasic::FetchPixelviewWatermark()
{
	if (!pixelviewDesktop || pixelviewDesktop->authorizedToken.isEmpty() ||
	    !pixelview::Desktop::validOrigin(pixelviewOrigin, pixelviewDev))
		return;
	const QString wanted = pixelviewLease.watermarkId;
	pixelviewWatermarkFetching = wanted;
	const quint64 seq = ++pixelviewWatermarkFetchSeq;
	QUrl url = pixelviewOrigin;
	url.setPath(QStringLiteral("/desktop/watermark"));
	QNetworkRequest request(url);
	request.setRawHeader("Authorization", "Bearer " + pixelviewDesktop->authorizedToken.toUtf8());
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
	request.setTransferTimeout(10000);
	auto *reply = pixelviewDesktop->http.get(request);
	reply->setReadBufferSize(PIXELVIEW_WATERMARK_MAX_BYTES + 1);
	connect(reply, &QNetworkReply::finished, this, [this, reply, seq, wanted] {
		reply->deleteLater();
		if (seq != pixelviewWatermarkFetchSeq || isClosing() || pixelviewShutdownPending)
			return;
		pixelviewWatermarkFetching.clear();
		const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QByteArray body = reply->read(PIXELVIEW_WATERMARK_MAX_BYTES + 1);
		const QString id = QString::fromLatin1(reply->rawHeader("X-Pixelview-Watermark-Id"));
		const bool still = pixelviewLease.watermarkKnown && pixelviewLease.watermarkEnabled;
		auto retry = [this, still](const char *why, int code) {
			blog(LOG_WARNING, "Pixelview: session watermark download failed (%s, HTTP %d)", why, code);
			if (still)
				pixelviewWatermarkRetry->start(5000);
		};
		if (http == 404) {
			// Turned off since it was announced; DESKTOP_WATERMARK follows.
			blog(LOG_INFO, "Pixelview: session watermark is off on the backend");
			return;
		}
		if (reply->error() != QNetworkReply::NoError || http != 200)
			return retry("request", http);
		if (body.size() > PIXELVIEW_WATERMARK_MAX_BYTES || !pixelview::WatermarkPolicy::validId(id))
			return retry("response", http);
		const QImage image = QImage::fromData(body, "PNG");
		if (image.isNull() || image.width() > 7680 || image.height() > 4320)
			return retry("not a PNG", http);
		if (!pixelviewWatermarkDir)
			pixelviewWatermarkDir = std::make_unique<QTemporaryDir>();
		if (!pixelviewWatermarkDir->isValid())
			return retry("no temporary directory", http);
		const QString path = pixelviewWatermarkDir->filePath(QStringLiteral("watermark-%1.png").arg(id));
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(body) != body.size())
			return retry("write", http);
		file.close();
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, "file", path.toUtf8().constData());
		// Premultiplied in linear light: the canvas blends linearly, and alpha
		// premultiplied in gamma space turned the white text into a grey that
		// vanished over mid-grey picture.
		obs_data_set_bool(settings, "linear_alpha", true);
		OBSSourceAutoRelease source =
			obs_source_create_private("image_source", "Pixelview Watermark", settings);
		if (!source || !obs_source_get_width(source))
			return retry("image source", http);
		OBSSource previous = pixelviewWatermark.set(source.Get());
		const QString previousId = pixelviewWatermarkLoaded;
		previous = nullptr;
		if (!previousId.isEmpty() && previousId != id)
			QFile::remove(pixelviewWatermarkDir->filePath(QStringLiteral("watermark-%1.png").arg(previousId)));
		pixelviewWatermarkLoaded = id;
		blog(LOG_INFO, "Pixelview: session watermark %s loaded (%dx%d%s)", id.toUtf8().constData(), image.width(),
		     image.height(), id == wanted ? "" : ", newer than announced");
		RefreshPixelviewWatermark();
	});
}
