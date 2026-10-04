// Pixelview Desktop admin remote control (DESKTOP_CONTROL on the control socket).
//
// The backend relays an admin command unchanged and returns this Desktop's
// DESKTOP_CONTROL_RESULT {request_id, ok, error?, state}. Every command runs
// the same native path as the matching Sending control, under the same
// configuration lock, and failures that would show a dialog locally are
// returned as the error instead.
#include <QJsonArray>
#include <QUuid>

void OBSBasic::PixelviewWarn(const QString &title, const QString &message)
{
	if (pixelviewRemoteError) {
		if (pixelviewRemoteError->isEmpty()) *pixelviewRemoteError = message;
		blog(LOG_WARNING, "Pixelview remote control: %s", QT_TO_UTF8(message));
		return;
	}
	QMessageBox::warning(this, title, message);
}

void OBSBasic::HandlePixelviewControl(const QJsonObject &data)
{
	const QString requestId = data["request_id"].toString();
	if (requestId.isEmpty() || requestId.size() > 64) return;
	const QJsonObject command = data["command"].toObject();
	// Run outside the socket callback: commands may start output or reset video.
	QTimer::singleShot(0, this, [this, requestId, command] {
		if (isClosing() || pixelviewShutdownPending || pixelviewUnpairPending || !pixelviewDesktop) return;
		QString error = RunPixelviewControl(command);
		RefreshPixelviewDevices();
		RefreshPixelviewReconnect();
		QJsonObject result{{"request_id", requestId}, {"ok", error.isEmpty()}, {"state", PixelviewControlState()}};
		if (!error.isEmpty()) result["error"] = error;
		blog(error.isEmpty() ? LOG_INFO : LOG_WARNING, "Pixelview remote control: %s %s",
		     QT_TO_UTF8(command["action"].toString()), error.isEmpty() ? "applied" : QT_TO_UTF8(error));
		if (pixelviewLease.ready) pixelviewLease.send({{"message", "DESKTOP_CONTROL_RESULT"}, {"data", result}});
	});
}

// State is pushed on every change (not polled): after each sidebar, stream or
// audio refresh, coalesced to one event-loop turn and skipped when unchanged.
// Every socket gets a full state right after DESKTOP_READY.
void OBSBasic::QueuePixelviewStatePush()
{
	if (pixelviewStatePushQueued) return;
	pixelviewStatePushQueued = true;
	QTimer::singleShot(0, this, [this] {
		pixelviewStatePushQueued = false;
		PushPixelviewState();
	});
}

void OBSBasic::PushPixelviewState()
{
	if (isClosing() || pixelviewShutdownPending || pixelviewUnpairPending || !pixelviewDesktop || !pixelviewLease.ready ||
	    pixelviewClosingSocket)
		return;
	QJsonObject state = PixelviewControlState();
	QJsonObject content = state;
	content.remove("seq"); // The ordering stamp changes on every read; compare the content.
	const QByteArray snapshot = QJsonDocument(content).toJson(QJsonDocument::Compact);
	if (snapshot == pixelviewPushedState) return;
	pixelviewPushedState = snapshot;
	pixelviewLease.send({{"message", "DESKTOP_STATE"}, {"data", QJsonObject{{"state", state}}}});
}

namespace {
QJsonValue PixelviewPropertyValue(obs_data_t *settings, obs_property_t *prop)
{
	const char *key = obs_property_name(prop);
	if (obs_property_get_type(prop) == OBS_PROPERTY_BOOL) return obs_data_get_bool(settings, key);
	switch (obs_property_list_format(prop)) {
	case OBS_COMBO_FORMAT_INT: return (double)obs_data_get_int(settings, key);
	case OBS_COMBO_FORMAT_FLOAT: return obs_data_get_double(settings, key);
	case OBS_COMBO_FORMAT_STRING: return QString::fromUtf8(obs_data_get_string(settings, key));
	default: return QJsonValue::Null;
	}
}

QJsonValue PixelviewListItemValue(obs_property_t *prop, size_t i)
{
	switch (obs_property_list_format(prop)) {
	case OBS_COMBO_FORMAT_INT: return (double)obs_property_list_item_int(prop, i);
	case OBS_COMBO_FORMAT_FLOAT: return obs_property_list_item_float(prop, i);
	case OBS_COMBO_FORMAT_STRING: return QString::fromUtf8(obs_property_list_item_string(prop, i));
	default: return QJsonValue::Null;
	}
}

// Remote capture settings: the selected source's own boolean and list
// properties (DeckLink input or test pattern), never free text; the device
// itself is chosen by select_device.
bool PixelviewRemoteProperty(obs_property_t *prop)
{
	const auto type = obs_property_get_type(prop);
	if (!obs_property_visible(prop) || strcmp(obs_property_name(prop), "device_hash") == 0) return false;
	return type == OBS_PROPERTY_BOOL ||
	       (type == OBS_PROPERTY_LIST && obs_property_list_format(prop) != OBS_COMBO_FORMAT_INVALID &&
		obs_property_list_format(prop) != OBS_COMBO_FORMAT_BOOL);
}
} // namespace

bool OBSBasic::PixelviewCaptureSelected()
{
	// Same condition as the "Choose a capture device to stream." hint.
	return pixelviewDevices && pixelviewDevices->currentIndex() > 0 && PixelviewActiveCaptureItem();
}

QJsonObject OBSBasic::PixelviewControlState()
{
	const bool streaming = pixelviewActualStreaming && PixelviewLeaseValid();
	const bool starting = pixelviewLease.intent && !streaming;
	const bool stopping = !pixelviewLease.intent && pixelviewStopPending;
	QString lockReason;
	if (pixelviewReceiving) lockReason = QStringLiteral("receiving");
	else if (!pixelviewPairingDurable) lockReason = QStringLiteral("unpaired");
	else if (pixelviewLease.intent || PixelviewSettingsBusy()) lockReason = QStringLiteral("streaming");
	else if (properties) lockReason = QStringLiteral("local_dialog");
	const bool locked = !lockReason.isEmpty();

	QJsonObject stream{{"streaming", streaming}, {"starting", starting}, {"stopping", stopping},
			   {"can_start", !pixelviewReceiving && pixelviewPairingDurable && pixelviewLease.ready && !pixelviewLease.intent &&
						 !pixelviewStopPending && !PixelviewSettingsBusy() && PixelviewCaptureSelected()},
			   {"can_stop", pixelviewLease.intent || streaming},
			   {"retries", pixelviewLease.retries}, {"max_retries", pixelviewLease.maxRetries},
			   {"status", pixelviewConnectionStatus ? pixelviewConnectionStatus->text() : QString()},
			   {"hint", pixelviewStreamHint && pixelviewStreamHint->isVisible() ? pixelviewStreamHint->text() : QString()}};

	// Capture: discovered devices, then the test patterns ("test-pattern:<n>"
	// ids), and the selected source's own properties.
	QJsonArray devices;
	for (int i = 1; pixelviewDevices && i < pixelviewDevices->count(); ++i) {
		if (pixelviewDevices->itemData(i).toString().isEmpty()) continue; // Separator.
		devices.append(QJsonObject{{"id", pixelviewDevices->itemData(i).toString()}, {"name", pixelviewDevices->itemText(i)}});
	}
	QJsonObject capture{{"devices", devices}, {"device", QString()}, {"properties", QJsonArray()},
			    {"status", pixelviewDevices ? pixelviewDevices->toolTip() : QString()}};
	capture["device"] = QString::fromStdString(PixelviewSelectedCaptureId());
	OBSSource source = PixelviewCaptureSource();
	if (source) {
		OBSDataAutoRelease settings = obs_source_get_settings(source);
		QJsonArray list;
		OBSProperties props = obs_source_properties(source);
		for (obs_property_t *prop = props ? obs_properties_first(props) : nullptr; prop; obs_property_next(&prop)) {
			if (!PixelviewRemoteProperty(prop)) continue;
			const bool isList = obs_property_get_type(prop) == OBS_PROPERTY_LIST;
			QJsonObject entry{{"key", QString::fromUtf8(obs_property_name(prop))},
					  {"label", QString::fromUtf8(obs_property_description(prop))},
					  {"type", isList ? QStringLiteral("list") : QStringLiteral("bool")},
					  {"enabled", obs_property_enabled(prop)},
					  {"value", PixelviewPropertyValue(settings, prop)}};
			if (isList) {
				QJsonArray options;
				for (size_t i = 0; i < obs_property_list_item_count(prop); ++i)
					options.append(QJsonObject{{"label", QString::fromUtf8(obs_property_list_item_name(prop, i))},
								   {"value", PixelviewListItemValue(prop, i)},
								   {"disabled", obs_property_list_item_disabled(prop, i)}});
				entry["options"] = options;
			}
			list.append(entry);
		}
		capture["properties"] = list;
	}

	// Encoding: the same encoders, profiles and quick bitrate range as the sidebar.
	const std::string encoderId = config_get_string(activeConfiguration, "AdvOut", "Encoder");
	QJsonArray encoders, profiles, rates;
	for (int i = 0; pixelviewEncoder && i < pixelviewEncoder->count(); ++i)
		encoders.append(QJsonObject{{"id", pixelviewEncoder->itemData(i).toString()}, {"name", pixelviewEncoder->itemText(i)}});
	OBSData encoderData = PixelviewEncoderData(GetCurrentProfile().path / "streamEncoder.json", encoderId.c_str(), false);
	OBSProperties encoderProps = obs_get_encoder_properties(encoderId.c_str());
	if (encoderProps) {
		obs_properties_apply_settings(encoderProps, encoderData);
		auto *profile = obs_properties_get(encoderProps, "profile");
		if (profile && obs_property_get_type(profile) == OBS_PROPERTY_LIST &&
		    obs_property_list_format(profile) == OBS_COMBO_FORMAT_STRING) {
			for (size_t i = 0; i < obs_property_list_item_count(profile); ++i) {
				if (obs_property_list_item_disabled(profile, i)) continue;
				profiles.append(QJsonObject{{"id", QString::fromUtf8(obs_property_list_item_string(profile, i))},
							    {"name", QString::fromUtf8(obs_property_list_item_name(profile, i))}});
			}
		}
	}
	for (const auto &rate : pixelview::FrameRates)
		rates.append(QJsonObject{{"num", (int)rate.num}, {"den", (int)rate.den}, {"label", QString::fromLatin1(rate.label)}});
	uint32_t fpsNum = 0, fpsDen = 1;
	GetConfigFPS(fpsNum, fpsDen);
	QJsonObject encoding{{"encoders", encoders}, {"encoder", QString::fromStdString(encoderId)},
			     {"available", encoderProps != nullptr && !PixelviewOpusEncoder().empty()},
			     {"bitrate_kbps", (double)obs_data_get_int(encoderData, "bitrate")},
			     {"bitrate_mbps_min", 1}, {"bitrate_mbps_max", 12},
			     {"profiles", profiles}, {"profile", QString::fromUtf8(obs_data_get_string(encoderData, "profile"))},
			     {"fps_options", rates}, {"fps", QJsonObject{{"num", (int)fpsNum}, {"den", (int)fpsDen}}}};

	QJsonObject audio{{"available", false}, {"muted", false}};
	if (!pixelviewReceiving) {
		if (auto managed = PixelviewManagedSource()) audio = {{"available", true}, {"muted", obs_source_muted(managed)}};
	}
	if (pixelviewStateInstance.isEmpty()) pixelviewStateInstance = QUuid::createUuid().toString(QUuid::WithoutBraces);
	return {{"version", 1}, {"instance", pixelviewStateInstance}, {"seq", (double)++pixelviewStateSeq},
		{"paired", pixelviewPairingDurable}, {"receiving", pixelviewReceiving},
		{"locked", locked}, {"lock_reason", locked ? QJsonValue(lockReason) : QJsonValue::Null},
		{"stream", stream}, {"capture", capture}, {"encoding", encoding}, {"audio", audio}};
}

QString OBSBasic::RunPixelviewControl(const QJsonObject &command)
{
	const QString action = command["action"].toString();
	if (action == "get_state") return {};
	if (!pixelviewPairingDurable) return QStringLiteral("This Mac is not paired.");
	if (pixelviewReceiving) return QStringLiteral("This Mac is receiving. Stop receiving on the Mac first.");

	if (action == "start") {
		if (pixelviewLease.intent) return {};
		if (pixelviewStopPending || PixelviewSettingsBusy()) return QStringLiteral("The previous stream is still stopping. Try again.");
		if (!pixelviewLease.ready) return QStringLiteral("The control connection is not ready.");
		// Locally an empty scene asks "stream a blank screen?"; nobody can answer that remotely.
		if (!PixelviewCaptureSelected()) return QStringLiteral("Choose a capture device to stream.");
		StartStreaming();
		if (!pixelviewLease.intent)
			return pixelviewConnectionStatus && !pixelviewConnectionStatus->text().isEmpty() ? pixelviewConnectionStatus->text()
												      : QStringLiteral("The stream could not be started.");
		return {};
	}
	if (action == "stop") {
		if (pixelviewLease.intent || pixelviewActualStreaming || pixelviewStreamingBusy) StopStreaming();
		return {};
	}

	QString error;
	QScopedValueRollback<QString *> quiet(pixelviewRemoteError, &error);
	if (action == "set_mute") {
		if (!command["muted"].isBool()) return QStringLiteral("muted must be true or false.");
		auto source = PixelviewManagedSource();
		if (!source) return QStringLiteral("No capture source is selected.");
		const bool muted = command["muted"].toBool();
		ChangePixelviewAudio(false, muted);
		if (error.isEmpty() && obs_source_muted(source) != muted) error = QStringLiteral("Audio could not be changed.");
		return error;
	}

	// Everything else changes sender configuration: same lock as the sidebar.
	if (PixelviewConfigurationLocked()) return QStringLiteral("Stop streaming before changing settings.");
	if (properties) return QStringLiteral("Device settings are open on the Mac. Close them first.");

	if (action == "select_device") {
		const QString id = command["device"].toString();
		const int index = pixelviewDevices ? pixelviewDevices->findData(id) : -1;
		if (id.isEmpty() || index <= 0) return QStringLiteral("That capture device is not connected.");
		SelectPixelviewDevice(index);
		if (error.isEmpty() && (!PixelviewActiveCaptureItem() || id != QString::fromStdString(PixelviewSelectedCaptureId())))
			error = QStringLiteral("The capture device could not be selected.");
		return error;
	}
	if (action == "set_capture") {
		OBSSource source = PixelviewCaptureSource();
		if (!source) return QStringLiteral("Select a capture device first.");
		const QByteArray key = command["key"].toString().toUtf8();
		const QJsonValue value = command["value"];
		OBSDataAutoRelease settings = obs_source_get_settings(source);
		OBSProperties props = obs_source_properties(source);
		obs_property_t *prop = props && !key.isEmpty() ? obs_properties_get(props, key.constData()) : nullptr;
		if (!prop || !PixelviewRemoteProperty(prop)) return QStringLiteral("Unknown capture setting.");
		if (!obs_property_enabled(prop)) return QStringLiteral("That capture setting is not available for this device.");
		if (obs_property_get_type(prop) == OBS_PROPERTY_BOOL) {
			if (!value.isBool()) return QStringLiteral("That capture setting takes true or false.");
			obs_data_set_bool(settings, key.constData(), value.toBool());
		} else {
			size_t i = 0;
			const size_t count = obs_property_list_item_count(prop);
			for (; i < count; ++i)
				if (!obs_property_list_item_disabled(prop, i) && PixelviewListItemValue(prop, i) == value) break;
			if (i == count) return QStringLiteral("That value is not offered for this capture setting.");
			switch (obs_property_list_format(prop)) {
			case OBS_COMBO_FORMAT_INT: obs_data_set_int(settings, key.constData(), obs_property_list_item_int(prop, i)); break;
			case OBS_COMBO_FORMAT_FLOAT: obs_data_set_double(settings, key.constData(), obs_property_list_item_float(prop, i)); break;
			default: obs_data_set_string(settings, key.constData(), obs_property_list_item_string(prop, i)); break;
			}
		}
		// Same order as the native properties dialog: modified callback, then update.
		obs_property_modified(prop, settings);
		obs_source_update(source, settings);
		SaveProject();
		return {};
	}
	if (action == "fit") {
		if (!PixelviewActiveCaptureItem()) return QStringLiteral("Select a capture device first.");
		pixelviewFitPolicy.requestReset();
		FitPixelviewCapture();
		return {};
	}
	if (action == "set_fps") {
		const int index = pixelview::frameRateIndex((uint32_t)command["num"].toInt(), (uint32_t)command["den"].toInt());
		if (command["num"].toInt() <= 0 || command["den"].toInt() <= 0 || index < 0) return QStringLiteral("Unsupported frame rate.");
		SelectPixelviewFPS(index);
		uint32_t num = 0, den = 1;
		GetConfigFPS(num, den);
		if (error.isEmpty() && pixelview::frameRateIndex(num, den) != index) error = QStringLiteral("The frame rate could not be changed.");
		return error;
	}
	if (action == "set_encoder") {
		const QByteArray id = command["encoder"].toString().toUtf8();
		if (!pixelviewEncoder || id.isEmpty() || pixelviewEncoder->findData(QString::fromUtf8(id)) < 0)
			return QStringLiteral("That encoder is not available on this Mac.");
		if (id == config_get_string(activeConfiguration, "AdvOut", "Encoder")) return {};
		OBSData data = PixelviewEncoderData(GetCurrentProfile().path / "streamEncoder.json", id.constData(), true);
		if (!SavePixelviewEncoding(id.constData(), data) && error.isEmpty()) error = QStringLiteral("The encoder could not be changed.");
		return error;
	}
	if (action == "set_bitrate" || action == "set_profile") {
		const std::string id = config_get_string(activeConfiguration, "AdvOut", "Encoder");
		if (!pixelviewEncoder || pixelviewEncoder->findData(QString::fromStdString(id)) < 0)
			return QStringLiteral("Select an available encoder first.");
		OBSData data = PixelviewEncoderData(GetCurrentProfile().path / "streamEncoder.json", id.c_str(), false);
		if (action == "set_bitrate") {
			const int kbps = command["mbps"].isDouble() && command["mbps"].toDouble() == command["mbps"].toInt()
						 ? pixelview::quickBitrateKbps(command["mbps"].toInt()) : 0;
			if (!kbps) return QStringLiteral("Bitrate must be a whole number from 1 to 12 Mbps.");
			obs_data_set_int(data, "bitrate", kbps);
		} else {
			const QByteArray profile = command["profile"].toString().toUtf8();
			if (profile.isEmpty()) return QStringLiteral("Choose an encoder profile.");
			obs_data_set_string(data, "profile", profile.constData());
		}
		if (!SavePixelviewEncoding(id.c_str(), data) && error.isEmpty()) error = QStringLiteral("The encoding setting could not be changed.");
		return error;
	}
	return QStringLiteral("Unknown command.");
}
