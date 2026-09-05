// Pixelview modification: native capture audio controls; no extra audio pipeline.
#include <components/VolumeMeter.hpp>
#include <utility/PixelviewAudio.hpp>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QGridLayout>
#include <QScopedValueRollback>

namespace {
using PixelviewAudioDevices = std::vector<std::pair<QString, QString>>;
PixelviewAudioDevices PixelviewMonitoringDevices()
{
	PixelviewAudioDevices devices{{QStringLiteral("System default"), QStringLiteral("default")}};
	obs_enum_audio_monitoring_devices([](void *data, const char *name, const char *id) {
		auto &list = *static_cast<PixelviewAudioDevices *>(data);
		if (id && strcmp(id, "default") != 0)
			list.emplace_back(QString::fromUtf8(name), QString::fromUtf8(id));
		return true;
	}, &devices);
	return devices;
}
}

void OBSBasic::InitPixelviewAudio(QWidget *previewContainer)
{
	// Append to the native preview's vertical layout, after its video grid.
	// Keep one source meter and the same operational mute/listen connections.
	auto *layout = qobject_cast<QVBoxLayout *>(previewContainer->layout());
	auto *panel = new QWidget(previewContainer);
	panel->setObjectName(QStringLiteral("pixelviewAudioPanel"));
	panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	panel->setMinimumWidth(360);
	panel->setMaximumWidth(420);
	auto *rows = new QVBoxLayout(panel);
	rows->setContentsMargins(12, 8, 12, 8);
	rows->setSpacing(4);
	pixelviewMeterRow = new QHBoxLayout;
	pixelviewMeterHost = new QWidget(panel);
	auto *meterLayout = new QGridLayout(pixelviewMeterHost);
	meterLayout->setContentsMargins(0, 0, 0, 0);
	pixelviewMeterMuted = new QLabel(QStringLiteral("Audio muted"), pixelviewMeterHost);
	pixelviewMeterMuted->setObjectName(QStringLiteral("pixelviewAudioMuted"));
	pixelviewMeterMuted->setAccessibleName(QStringLiteral("Audio muted"));
	pixelviewMeterMuted->setAlignment(Qt::AlignCenter);
	pixelviewMeterMuted->setEnabled(false);
	auto labelPolicy = pixelviewMeterMuted->sizePolicy();
	labelPolicy.setRetainSizeWhenHidden(true);
	pixelviewMeterMuted->setSizePolicy(labelPolicy);
	pixelviewMeterMuted->hide();
	meterLayout->addWidget(pixelviewMeterMuted, 0, 0);
	pixelviewMeterHost->setMinimumWidth(100);
	pixelviewMeterRow->addWidget(pixelviewMeterHost, 1);
	pixelviewStreamMute = new QCheckBox(QStringLiteral("Mute audio"), panel);
	pixelviewStreamMute->setObjectName(QStringLiteral("pixelviewStreamMute"));
	pixelviewStreamMute->setAccessibleName(QStringLiteral("Mute audio"));
	pixelviewStreamMute->setToolTip(QStringLiteral("Mute this Blackmagic source in the stream and local monitoring. Unmuting leaves local listening off; enable Listen locally explicitly."));
	pixelviewMeterRow->addWidget(pixelviewStreamMute);
	rows->addLayout(pixelviewMeterRow);
	auto *listenRow = new QHBoxLayout;
	listenRow->setSpacing(8);
	pixelviewListen = new QCheckBox(QStringLiteral("Listen locally"), panel);
	pixelviewListen->setObjectName(QStringLiteral("pixelviewListen"));
	pixelviewListen->setToolTip(QStringLiteral("Local audio monitoring only; off does not mute the stream. Use headphones to avoid feedback. Device acceptance does not verify audible output."));
	// Reserve the themed indicator and full label before the device combo stretches.
	// The native macOS checkbox layout rect excludes outer widget margins.
	// Use its full rect so the protected label width also reserves layout space.
	pixelviewListen->setAttribute(Qt::WA_LayoutUsesWidgetRect);
	pixelviewListen->ensurePolished();
	pixelviewListen->setMinimumWidth(pixelviewListen->sizeHint().width());
	listenRow->addWidget(pixelviewListen);
	pixelviewMonitorDevice = new QComboBox(panel);
	pixelviewMonitorDevice->setObjectName(QStringLiteral("pixelviewMonitorDevice"));
	pixelviewMonitorDevice->setAccessibleName(QStringLiteral("Local audio output"));
	pixelviewMonitorDevice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	// Let long device names elide instead of taking space from Listen locally.
	pixelviewMonitorDevice->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	pixelviewMonitorDevice->setMinimumContentsLength(8);
	// Keep native layout-item margins from consuming the inter-control gap.
	pixelviewMonitorDevice->setAttribute(Qt::WA_LayoutUsesWidgetRect);
	pixelviewMonitorDevice->setToolTip(QStringLiteral("Native OBS monitoring output (profile-wide). System default uses the OS default output. Availability is not a playback test."));
	listenRow->addWidget(pixelviewMonitorDevice, 1);
	rows->addLayout(listenRow);
	pixelviewMonitorDevice->ensurePolished();
	pixelviewMonitorDevice->setFixedHeight(36);
	layout->addWidget(panel, 0, Qt::AlignLeft);
	connect(pixelviewStreamMute, &QCheckBox::clicked, this, [this](bool checked) { ChangePixelviewAudio(false, checked); });
	connect(pixelviewListen, &QCheckBox::clicked, this, [this](bool checked) { ChangePixelviewAudio(true, checked); });
	connect(pixelviewMonitorDevice, &QComboBox::activated, this, &OBSBasic::SelectPixelviewMonitorDevice);
	RefreshPixelviewAudio();
}

void OBSBasic::ClearPixelviewAudio()
{
	pixelviewAudioSignals.clear();
	delete pixelviewMeter;
	pixelviewMeter = nullptr;
	pixelviewAudioSource = nullptr;
}

void OBSBasic::PixelviewAudioChanged(void *data, calldata_t *)
{
	auto *main = static_cast<OBSBasic *>(data);
	// Signal may run on an audio thread and monitor signals precede state updates.
	// Context-bound queued delivery is cancelled when the QObject is destroyed.
	QMetaObject::invokeMethod(main, [main] { main->RefreshPixelviewAudio(); }, Qt::QueuedConnection);
}

void OBSBasic::RefreshPixelviewAudio()
{
	if (pixelviewAudioShuttingDown || isClosing() || !pixelviewListen || pixelviewAudioRefreshing)
		return;
	QScopedValueRollback<bool> refreshing(pixelviewAudioRefreshing, true);
	OBSSourceAutoRelease source = obs_get_source_by_name("Pixelview Capture");
	if (source && (obs_source_removed(source) || strcmp(obs_source_get_id(source), "decklink-input") != 0))
		source = nullptr;
	auto previous = OBSGetStrongRef(pixelviewAudioSource);
	if (previous != source || (!source && pixelviewMeter)) {
		ClearPixelviewAudio();
		pixelviewAudioNormalizationPending = false;
		pixelviewAudioNormalizationWarned = false;
		if (source) {
			pixelviewAudioSource = OBSGetWeakRef(source);
			pixelviewMeter = new VolumeMeter(pixelviewMeterHost, source);
			pixelviewMeter->setObjectName(QStringLiteral("pixelviewAudioMeter"));
			pixelviewMeter->setAccessibleName(QStringLiteral("Blackmagic audio levels"));
			auto meterPolicy = pixelviewMeter->sizePolicy();
			meterPolicy.setRetainSizeWhenHidden(true);
			pixelviewMeter->setSizePolicy(meterPolicy);
			qobject_cast<QGridLayout *>(pixelviewMeterHost->layout())->addWidget(pixelviewMeter, 0, 0);
			for (const char *signal : {"mute", "monitor", "remove", "destroy"})
				pixelviewAudioSignals.emplace_back(obs_source_get_signal_handler(source), signal, PixelviewAudioChanged, this);
		}
	}
	// Managed source only: old scenes (or external native changes) can have both
	// flags true. Fail closed locally, defer saving during scene load, and retry
	// persistence on polling without re-setting native flags or recursive saves.
	if (source) {
		const pixelview::SourceAudioState live{obs_source_muted(source), obs_source_get_monitoring_enabled(source)};
		const auto safe = pixelview::normalizeSourceAudio(live);
		if (safe.monitoring != live.monitoring) {
			obs_source_set_monitoring_enabled(source, false);
			pixelviewAudioNormalizationPending = true;
		}
		if (pixelviewAudioNormalizationPending && !disableSaving) {
			if (SavePixelviewAudioSource(source)) {
				pixelviewAudioNormalizationPending = false;
				pixelviewAudioNormalizationWarned = false;
			} else if (!pixelviewAudioNormalizationWarned) {
				pixelviewAudioNormalizationWarned = true;
				blog(LOG_WARNING, "Pixelview: muted capture local monitoring disabled live, but scene persistence could not be verified; will retry.");
			}
		}
	}
	const bool muted = source && obs_source_muted(source);
	const QSignalBlocker muteBlock(pixelviewStreamMute), listenBlock(pixelviewListen);
	pixelviewStreamMute->setEnabled(source != nullptr);
	pixelviewListen->setEnabled(source != nullptr && !muted && obs_audio_monitoring_available());
	pixelviewStreamMute->setChecked(muted);
	pixelviewListen->setChecked(source && !muted && obs_source_get_monitoring_enabled(source));
	if (pixelviewMeter) {
		pixelviewMeter->setMuted(muted);
		pixelviewMeter->setVisible(!muted);
	}
	pixelviewMeterMuted->setVisible(muted);

	// Never rebuild an open/focused combo or silently replace a missing explicit device.
	pixelviewMonitorDevice->setEnabled(!PixelviewSettingsBusy() && obs_audio_monitoring_available());
	if (pixelviewMonitorDevice->hasFocus() || pixelviewMonitorDevice->view()->isVisible())
		return;
	auto devices = PixelviewMonitoringDevices();
	const QString id = QString::fromUtf8(config_get_string(activeConfiguration, "Audio", "MonitoringDeviceId"));
	const QString name = QString::fromUtf8(config_get_string(activeConfiguration, "Audio", "MonitoringDeviceName"));
	const bool available = std::any_of(devices.begin(), devices.end(), [&](const auto &d) { return d.second == id; });
	if (!available)
		devices.emplace_back(QStringLiteral("Unavailable: ") + name, id);
	bool changed = pixelviewMonitorDevice->count() != int(devices.size());
	for (int i = 0; !changed && i < int(devices.size()); ++i)
		changed = pixelviewMonitorDevice->itemText(i) != devices[i].first || pixelviewMonitorDevice->itemData(i).toString() != devices[i].second;
	const QSignalBlocker deviceBlock(pixelviewMonitorDevice);
	if (changed) {
		pixelviewMonitorDevice->clear();
		for (const auto &device : devices)
			pixelviewMonitorDevice->addItem(device.first, device.second);
	}
	pixelviewMonitorDevice->setCurrentIndex(pixelviewMonitorDevice->findData(id));
}

bool OBSBasic::SavePixelviewAudioSource(obs_source_t *source)
{
	if (disableSaving)
		return false;
	// Native SaveProjectNow has no success return. Read back the exact native source
	// fields rather than claiming persistence from its void return.
	SaveProjectNow();
	OBSDataAutoRelease expected = obs_save_source(source);
	OBSDataAutoRelease saved = obs_data_create_from_json_file(GetCurrentSceneCollection().getFilePathString().c_str());
	if (!saved)
		return false;
	OBSDataArrayAutoRelease sources = obs_data_get_array(saved, "sources");
	for (size_t i = 0; i < obs_data_array_count(sources); ++i) {
		OBSDataAutoRelease entry = obs_data_array_item(sources, i);
		if (strcmp(obs_data_get_string(entry, "uuid"), obs_source_get_uuid(source)) == 0) {
			// The loader consumes the canonical boolean, not just the compatibility integer.
			obs_data_item_t *monitoring = obs_data_item_byname(entry, "monitoring");
			const bool canonicalMonitoring = monitoring && obs_data_item_gettype(monitoring) == OBS_DATA_BOOLEAN;
			obs_data_item_release(&monitoring);
			return canonicalMonitoring &&
			       obs_data_get_bool(entry, "monitoring") == obs_source_get_monitoring_enabled(source) &&
			       obs_data_get_bool(entry, "muted") == obs_data_get_bool(expected, "muted") &&
			       obs_data_get_int(entry, "monitoring_enabled") == obs_data_get_int(expected, "monitoring_enabled");
		}
	}
	return false;
}

void OBSBasic::ChangePixelviewAudio(bool monitoring, bool checked)
{
	if (isClosing() || pixelviewAudioShuttingDown || pixelviewAudioRefreshing)
		return;
	// Normalize legacy/external invalid flags before taking a rollback snapshot.
	RefreshPixelviewAudio();
	auto source = OBSGetStrongRef(pixelviewAudioSource);
	if (!source || obs_source_removed(source)) {
		RefreshPixelviewAudio();
		return;
	}
	if (monitoring && checked && obs_source_muted(source)) {
		RefreshPixelviewAudio();
		return;
	}
	if (monitoring && checked) {
		const auto devices = PixelviewMonitoringDevices();
		const QString id = QString::fromUtf8(config_get_string(activeConfiguration, "Audio", "MonitoringDeviceId"));
		if (std::none_of(devices.begin(), devices.end(), [&](const auto &d) { return d.second == id; })) {
			RefreshPixelviewAudio();
			QMessageBox::warning(this, QStringLiteral("Local audio"), QStringLiteral("Selected output device is unavailable. Choose an available output or System default."));
			return;
		}
	}
	const pixelview::SourceAudioState old{obs_source_muted(source), obs_source_get_monitoring_enabled(source)};
	const auto target = pixelview::sourceAudioTarget(old, monitoring, checked);
	const auto result = [&] {
		QScopedValueRollback<bool> changing(pixelviewAudioRefreshing, true);
		return pixelview::changeSourceAudio(old, target, [&](pixelview::SourceAudioState state) {
			// Stop local playback before unmuting; never touch source gain or mixers.
			obs_source_set_monitoring_enabled(source, state.monitoring);
			obs_source_set_muted(source, state.muted);
		}, [&] { return SavePixelviewAudioSource(source); });
	}();
	if (result != pixelview::AudioChangeResult::Applied) {
		QMessageBox::warning(this, QStringLiteral("Capture audio"), result != pixelview::AudioChangeResult::RollbackFailed ?
			QStringLiteral("Could not save audio settings. Previous settings were restored.") :
			QStringLiteral("Could not save audio settings. Live state was restored, but scene persistence could not be verified. Check scene-folder permissions before quitting."));
	}
	RefreshPixelviewAudio();
}

void OBSBasic::SelectPixelviewMonitorDevice(int index)
{
	if (isClosing() || index < 0)
		return;
	if (PixelviewSettingsBusy()) {
		const QSignalBlocker blocker(pixelviewMonitorDevice);
		pixelviewMonitorDevice->setCurrentIndex(pixelviewMonitorDevice->findData(QString::fromUtf8(config_get_string(activeConfiguration, "Audio", "MonitoringDeviceId"))));
		return;
	}
	const auto devices = PixelviewMonitoringDevices();
	const QString id = pixelviewMonitorDevice->itemData(index).toString();
	auto device = std::find_if(devices.begin(), devices.end(), [&](const auto &d) { return d.second == id; });
	if (device == devices.end()) {
		// Reject immediately even while focused; polling deliberately skips this combo.
		const QSignalBlocker blocker(pixelviewMonitorDevice);
		pixelviewMonitorDevice->setCurrentIndex(pixelviewMonitorDevice->findData(QString::fromUtf8(config_get_string(activeConfiguration, "Audio", "MonitoringDeviceId"))));
		QMessageBox::warning(this, QStringLiteral("Local audio"), QStringLiteral("That output device is unavailable. No output change was made."));
		return;
	}
	const std::string oldName = config_get_string(activeConfiguration, "Audio", "MonitoringDeviceName");
	const std::string oldId = config_get_string(activeConfiguration, "Audio", "MonitoringDeviceId");
	if (oldId == id.toStdString())
		return;
	const bool hadName = config_has_user_value(activeConfiguration, "Audio", "MonitoringDeviceName");
	const bool hadId = config_has_user_value(activeConfiguration, "Audio", "MonitoringDeviceId");
	const char *liveNamePtr, *liveIdPtr;
	obs_get_audio_monitoring_device(&liveNamePtr, &liveIdPtr);
	const std::string liveName = liveNamePtr ? liveNamePtr : "", liveId = liveIdPtr ? liveIdPtr : "default";
	const auto result = pixelview::changeAudio([&] {
		return obs_set_audio_monitoring_device(device->first.toUtf8().constData(), id.toUtf8().constData());
	}, [&] {
		config_set_string(activeConfiguration, "Audio", "MonitoringDeviceName", device->first.toUtf8().constData());
		config_set_string(activeConfiguration, "Audio", "MonitoringDeviceId", id.toUtf8().constData());
		return config_save_safe(activeConfiguration, "tmp", nullptr) == CONFIG_SUCCESS;
	}, [&] {
		if (hadName) config_set_string(activeConfiguration, "Audio", "MonitoringDeviceName", oldName.c_str());
		else config_remove_value(activeConfiguration, "Audio", "MonitoringDeviceName");
		if (hadId) config_set_string(activeConfiguration, "Audio", "MonitoringDeviceId", oldId.c_str());
		else config_remove_value(activeConfiguration, "Audio", "MonitoringDeviceId");
		const bool liveRestored = obs_set_audio_monitoring_device(liveName.c_str(), liveId.c_str());
		const bool saved = config_save_safe(activeConfiguration, "tmp", nullptr) == CONFIG_SUCCESS;
		return liveRestored && saved;
	});
	// Restore selection even while focused; periodic enumeration still leaves popups alone.
	const QSignalBlocker blocker(pixelviewMonitorDevice);
	pixelviewMonitorDevice->setCurrentIndex(pixelviewMonitorDevice->findData(QString::fromUtf8(config_get_string(activeConfiguration, "Audio", "MonitoringDeviceId"))));
	if (result != pixelview::AudioChangeResult::Applied)
		QMessageBox::warning(this, QStringLiteral("Local audio"), result == pixelview::AudioChangeResult::RollbackFailed ?
			QStringLiteral("Output change failed; restoration could not be verified. Check profile permissions and restart before relying on local monitoring.") :
			QStringLiteral("Could not apply or save that output device. Previous settings were restored."));
}
