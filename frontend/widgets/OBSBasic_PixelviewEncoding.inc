// Included by OBSBasic.cpp: native OBS encoding preferences, no separate output implementation.
#include <QAbstractItemView>
#include <QFormLayout>
#include <QPixmap>
#include <utility>
namespace {
bool PixelviewListValue(obs_properties_t *props, const char *key, const char *value)
{
	auto *p = obs_properties_get(props, key);
	if (!p || obs_property_get_type(p) != OBS_PROPERTY_LIST ||
	    obs_property_list_format(p) != OBS_COMBO_FORMAT_STRING)
		return false;
	for (size_t i = 0; i < obs_property_list_item_count(p); ++i)
		if (!obs_property_list_item_disabled(p, i) && strcmp(obs_property_list_item_string(p, i), value) == 0)
			return true;
	return false;
}
void PixelviewDefaultList(obs_data_t *data, obs_properties_t *props, const char *key, const char *value)
{
	if (PixelviewListValue(props, key, value))
		obs_data_set_string(data, key, value);
}
// BEGIN B-FRAME POLICY
void PixelviewEnforceNoBFrames(const char *id, obs_data_t *data)
{
	// Native backends use either the bool bframes or integer bf setting.
	// Set both explicitly: saved user values must not bypass the low-latency policy.
	// QSV exposes an integer bframes count; VT exposes a boolean.
	if (strncmp(id, "obs_qsv11", 9) == 0)
		obs_data_set_int(data, "bframes", 0);
	else
		obs_data_set_bool(data, "bframes", false);
	obs_data_set_int(data, "bf", 0);
	if (strcmp(id, "obs_nvenc_h264_tex") == 0 || strcmp(id, "obs_nvenc_hevc_tex") == 0 ||
	    strcmp(id, "obs_nvenc_av1_tex") == 0) {
		const auto options = pixelview::withoutNvencBFrameOverrides(obs_data_get_string(data, "opts"));
		obs_data_set_string(data, "opts", options.c_str());
		// get_nv_tuning matches ASCII case-insensitively; UHQ forces bf >= 4
		// before custom options. HQ is the native default and always offered.
		const std::string tune = obs_data_get_string(data, "tune");
		if (tune.size() == 3 && (tune[0] == 'u' || tune[0] == 'U') &&
		    (tune[1] == 'h' || tune[1] == 'H') && (tune[2] == 'q' || tune[2] == 'Q'))
			obs_data_set_string(data, "tune", "hq");
	}
	// Only AVC and AV1 expose B-picture counts in this native AMF backend.
	// HEVC has no corresponding SDK count keys; do not invent or strip any.
	if (strcmp(id, "h264_texture_amf") == 0 || strcmp(id, "av1_texture_amf") == 0) {
		const auto options = pixelview::withoutAmfBFrameOverrides(
			obs_data_get_string(data, "ffmpeg_opts"), strcmp(id, "av1_texture_amf") == 0);
		obs_data_set_string(data, "ffmpeg_opts", options.c_str());
	}
	if (strcmp(id, "obs_x264") == 0) {
		const auto options = pixelview::withoutBFrameOverrides(obs_data_get_string(data, "x264opts"));
		obs_data_set_string(data, "x264opts", options.c_str());
	}
}
// END B-FRAME POLICY
OBSData PixelviewEncoderData(const std::filesystem::path &path, const char *id, bool fresh)
{
	OBSDataAutoRelease data = obs_encoder_defaults(id);
	if (!data) data = obs_data_create();
	if (!fresh) {
		OBSDataAutoRelease saved = obs_data_create_from_json_file_safe(path.u8string().c_str(), "bak");
		if (saved) {
			obs_data_apply(data, saved);
			PixelviewEnforceNoBFrames(id, data);
			return OBSData(data.Get());
		}
	}
	OBSProperties props = obs_get_encoder_properties(id);
	if (!props) { PixelviewEnforceNoBFrames(id, data); return OBSData(data.Get()); }
	PixelviewDefaultList(data, props, "rate_control", "CBR");
	PixelviewDefaultList(data, props, "profile", strcmp(obs_get_encoder_codec(id), "h264") == 0 ? "baseline" : "main");
	if (strcmp(id, "obs_x264") == 0) {
		PixelviewDefaultList(data, props, "tune", "zerolatency");
		PixelviewDefaultList(data, props, "preset", "ultrafast");
	}
	for (const auto *key : {"bitrate", "keyint_sec", "bf"}) {
		auto *p = obs_properties_get(props, key);
		const int value = strcmp(key, "bitrate") == 0 ? 6000 : strcmp(key, "keyint_sec") == 0 ? 1 : 0;
		if (p && obs_property_get_type(p) == OBS_PROPERTY_INT &&
		    value >= obs_property_int_min(p) && value <= obs_property_int_max(p))
			obs_data_set_int(data, key, value);
	}
	auto *bframes = obs_properties_get(props, "bframes");
	if (bframes && obs_property_get_type(bframes) == OBS_PROPERTY_BOOL)
		obs_data_set_bool(data, "bframes", false);
	PixelviewEnforceNoBFrames(id, data);
	// spatial_aq_mode remains the native default (VT AQ_AUTO), not a guessed numeric value.
	return OBSData(data.Get());
}
std::string PixelviewOpusEncoder()
{
	const char *id;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i) {
		const auto caps = obs_get_encoder_caps(id);
		if (obs_get_encoder_type(id) == OBS_ENCODER_AUDIO &&
		    !(caps & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL)) &&
		    strcmp(obs_get_encoder_codec(id), "opus") == 0)
			return id;
	}
	return {};
}
void PixelviewHideBFrameProperties(obs_properties_t *props)
{
	// Keep the native property objects and callbacks alive, including group trees.
	for (auto *p = obs_properties_first(props); p; obs_property_next(&p)) {
		const char *name = obs_property_name(p);
		if (strcmp(name, "bframes") == 0 || strcmp(name, "bf") == 0 || strcmp(name, "bframe_ref_mode") == 0)
			obs_property_set_visible(p, false);
		if (obs_property_get_type(p) == OBS_PROPERTY_GROUP)
			PixelviewHideBFrameProperties(obs_property_group_content(p));
	}
}
void PixelviewAlignProperties(OBSPropertiesView *view)
{
	auto align = [view] {
		for (auto *form : view->findChildren<QFormLayout *>()) {
			form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
			form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
		}
		for (auto *label : view->findChildren<QLabel *>())
			label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	};
	view->setStyleSheet(QStringLiteral("QPushButton { text-align: left; }"));
	QObject::connect(view, &OBSPropertiesView::PropertiesRefreshed, view, align);
	align();
}
} // namespace

void OBSBasic::InitPixelviewEncoding(QWidget *sidebar)
{
	auto *layout = qobject_cast<QVBoxLayout *>(sidebar->layout());
	auto *divider = new QFrame(sidebar);
	divider->setFrameShape(QFrame::HLine);
	layout->addWidget(divider);
	auto *encodingHeading = new QLabel(QStringLiteral("Encoding"), sidebar);
	encodingHeading->setProperty("pixelviewHeading", true);
	layout->addWidget(encodingHeading);
	pixelviewEncoder = new QComboBox(sidebar);
	pixelviewEncoder->setObjectName(QStringLiteral("pixelviewEncoder"));
	pixelviewEncoder->setAccessibleName(QStringLiteral("Video encoder"));
	pixelviewEncoder->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	pixelviewEncoder->setMinimumContentsLength(15);
	layout->addWidget(pixelviewEncoder);
	pixelviewEncoder->setToolTip(QStringLiteral("Video encoder • 1920 × 1080 output. Native hardware encoders are listed when registered."));
	std::vector<pixelview::EncoderChoice> encoders;
	const char *id;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i) {
		const auto caps = obs_get_encoder_caps(id);
		if (obs_get_encoder_type(id) != OBS_ENCODER_VIDEO ||
		    (caps & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL))) continue;
		const std::string codec = obs_get_encoder_codec(id);
		const bool hardware = pixelview::isHardwareEncoder(id);
		if ((codec != "h264" && codec != "hevc" && codec != "av1") || (!hardware && strcmp(id, "obs_x264") != 0)) continue;
		encoders.push_back({id, codec, hardware});
		pixelviewEncoder->addItem(QString::fromUtf8(obs_encoder_get_display_name(id)), QString::fromUtf8(id));
	}
	pixelviewBitrate = new QSpinBox(sidebar);
	pixelviewBitrate->setObjectName(QStringLiteral("pixelviewBitrate"));
	pixelviewBitrate->setAccessibleName(QStringLiteral("Video bitrate Mbps"));
	pixelviewBitrate->setRange(1, 12);
	pixelviewBitrate->setValue(6);
	pixelviewBitrate->setSuffix(QStringLiteral(" Mbps"));
	pixelviewBitrate->setToolTip(QStringLiteral("Video bitrate for the stream, in megabits per second."));
	pixelviewBitrate->setAlignment(Qt::AlignLeft);
	auto *rates = new QHBoxLayout;
	rates->setSpacing(8);
	rates->addWidget(pixelviewFPS, 1);
	rates->addWidget(pixelviewBitrate, 1);
	layout->addLayout(rates);
	auto *profileRow = new QHBoxLayout;
	profileRow->setSpacing(8);
	auto *profileLabel = new QLabel(QStringLiteral("Profile"), sidebar);
	profileLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	pixelviewProfile = new QComboBox(sidebar);
	pixelviewProfile->setObjectName(QStringLiteral("pixelviewProfile"));
	pixelviewProfile->setAccessibleName(QStringLiteral("Encoder profile"));
	pixelviewProfile->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	pixelviewProfile->setMinimumContentsLength(8);
	profileLabel->setBuddy(pixelviewProfile);
	profileRow->addWidget(profileLabel);
	profileRow->addWidget(pixelviewProfile, 1);
	layout->addLayout(profileRow);
	pixelviewAdvanced = new QPushButton(QStringLiteral("Advanced…"), sidebar);
	pixelviewAdvanced->setAccessibleName(QStringLiteral("Advanced encoding"));
	pixelviewAdvanced->setObjectName(QStringLiteral("pixelviewAdvancedEncoding"));
	pixelviewAdvanced->setFlat(true);
	layout->addWidget(pixelviewAdvanced, 0, Qt::AlignLeft);
	pixelviewEncodingStatus = new QLabel(sidebar);
	pixelviewEncodingStatus->setWordWrap(true);
	pixelviewEncodingStatus->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	pixelviewEncodingStatus->setObjectName(QStringLiteral("pixelviewEncodingStatus"));
	pixelviewEncodingStatus->hide();
	layout->addWidget(pixelviewEncodingStatus);
	pixelviewProfile->setToolTip(QStringLiteral("Profiles are reported by the native encoder; registration does not prove hardware support. Main10 uses P010; Main 4:2:2 10 uses P216. 10-bit source detail also requires compatible input and Device settings (Allow 10 Bit in Auto mode). SDR Rec.709 is retained."));
	const bool initialized = config_get_bool(activeConfiguration, "Pixelview", "EncodingInitialized");
	const QString saved = QString::fromUtf8(config_get_string(activeConfiguration, "AdvOut", "Encoder"));
	int selected = initialized ? pixelviewEncoder->findData(saved) : -1;
	// Missing explicit encoders remain unavailable, never silently overwrite
	// their saved settings with a different backend's first-launch defaults.
	if (!initialized) {
#ifdef __APPLE__
		selected = pixelview::preferredEncoder(encoders, true);
#else
		selected = pixelview::preferredEncoder(encoders, false);
#endif
		if (selected >= 0) {
			const auto selectedId = pixelviewEncoder->itemData(selected).toString().toUtf8();
			OBSData data = PixelviewEncoderData(GetCurrentProfile().path / "streamEncoder.json", selectedId.constData(), true);
			SavePixelviewEncoding(selectedId.constData(), data, true);
		}
	}
	if (initialized && pixelviewEncoder->findData(saved) >= 0) {
		// Normalize persisted overrides before the native output handler is reused.
		OBSData data = PixelviewEncoderData(GetCurrentProfile().path / "streamEncoder.json", saved.toUtf8().constData(), false);
		SavePixelviewEncoding(saved.toUtf8().constData(), data, true);
	}
	connect(pixelviewEncoder, &QComboBox::activated, this, [this](int index) {
		if (PixelviewConfigurationLocked()) return;
		const auto id = pixelviewEncoder->itemData(index).toString().toUtf8();
		if (id == config_get_string(activeConfiguration, "AdvOut", "Encoder")) return;
		OBSData data = PixelviewEncoderData(GetCurrentProfile().path / "streamEncoder.json", id.constData(), true);
		SavePixelviewEncoding(id.constData(), data);
		RefreshPixelviewEncoding();
	});
	connect(pixelviewBitrate, &QSpinBox::editingFinished, this, [this] {
		if (PixelviewConfigurationLocked()) return;
		const std::string id = config_get_string(activeConfiguration, "AdvOut", "Encoder");
		OBSData data = PixelviewEncoderData(GetCurrentProfile().path / "streamEncoder.json", id.c_str(), false);
		const int bitrate = pixelview::quickBitrateKbps(pixelviewBitrate->value());
		// A refresh or focus change must not clamp an advanced bitrate.
		if (!pixelviewBitrate->property("edited").toBool()) return;
		pixelviewBitrate->setProperty("edited", false);
		obs_data_set_int(data, "bitrate", bitrate);
		SavePixelviewEncoding(id.c_str(), data);
		RefreshPixelviewEncoding();
	});
	connect(pixelviewBitrate, &QSpinBox::valueChanged, this, [this] { pixelviewBitrate->setProperty("edited", true); });
	connect(pixelviewProfile, &QComboBox::activated, this, [this](int index) {
		if (PixelviewConfigurationLocked()) return;
		const std::string id = config_get_string(activeConfiguration, "AdvOut", "Encoder");
		OBSData data = PixelviewEncoderData(GetCurrentProfile().path / "streamEncoder.json", id.c_str(), false);
		const auto value = pixelviewProfile->itemData(index).toString().toUtf8();
		obs_data_set_string(data, "profile", value.constData());
		SavePixelviewEncoding(id.c_str(), data);
		RefreshPixelviewEncoding();
	});
	connect(pixelviewAdvanced, &QPushButton::clicked, this, &OBSBasic::AdvancedPixelviewEncoding);
	RefreshPixelviewEncoding();
}

void OBSBasic::RefreshPixelviewEncoding()
{
	if (isClosing() || !pixelviewEncoder) return;
	const QSignalBlocker encoderBlock(pixelviewEncoder), profileBlock(pixelviewProfile), bitrateBlock(pixelviewBitrate);
	const std::string id = config_get_string(activeConfiguration, "AdvOut", "Encoder");
	const int index = pixelviewEncoder->findData(QString::fromStdString(id));
	if (!pixelviewEncoder->hasFocus() && !pixelviewEncoder->view()->isVisible() && pixelviewEncoder->currentIndex() != index)
		pixelviewEncoder->setCurrentIndex(index);
	const bool idle = !PixelviewConfigurationLocked();
	const bool ready = index >= 0 && !PixelviewOpusEncoder().empty();
	// Allow explicit recovery from an unavailable saved encoder without changing its settings.
	pixelviewEncoder->setEnabled(idle && pixelviewEncoder->count() > 0);
	pixelviewAdvanced->setEnabled(idle && ready);
	pixelviewBitrate->setEnabled(idle && ready);
	pixelviewProfile->setEnabled(idle && ready);
	pixelviewEncodingStatus->setVisible(!ready);
	if (!ready) {
		pixelviewEncodingStatus->setText(QStringLiteral("Encoding unavailable"));
		pixelviewEncodingStatus->setToolTip(QStringLiteral("A registered video encoder and Opus are required."));
		return;
	}
	OBSData data = PixelviewEncoderData(GetCurrentProfile().path / "streamEncoder.json", id.c_str(), false);
	OBSProperties props = obs_get_encoder_properties(id.c_str());
	obs_properties_apply_settings(props, data);
	// Do not reset the popup model or keyboard selection during an edit.
	if (!pixelviewProfile->hasFocus() && !pixelviewProfile->view()->isVisible()) {
		std::vector<std::pair<QString, QString>> entries;
		auto *profile = obs_properties_get(props, "profile");
		if (profile && obs_property_get_type(profile) == OBS_PROPERTY_LIST && obs_property_list_format(profile) == OBS_COMBO_FORMAT_STRING) {
			for (size_t i = 0; i < obs_property_list_item_count(profile); ++i) {
				if (obs_property_list_item_disabled(profile, i)) continue;
				entries.emplace_back(QString::fromUtf8(obs_property_list_item_name(profile, i)), QString::fromUtf8(obs_property_list_item_string(profile, i)));
			}
		}
		bool changed = pixelviewProfile->count() != static_cast<int>(entries.size());
		for (size_t i = 0; !changed && i < entries.size(); ++i)
			changed = pixelviewProfile->itemText(static_cast<int>(i)) != entries[i].first ||
				  pixelviewProfile->itemData(static_cast<int>(i)).toString() != entries[i].second;
		if (changed) {
			pixelviewProfile->clear();
			for (const auto &entry : entries) pixelviewProfile->addItem(entry.first, entry.second);
		}
		const int selected = pixelviewProfile->findData(QString::fromUtf8(obs_data_get_string(data, "profile")));
		if (pixelviewProfile->currentIndex() != selected) pixelviewProfile->setCurrentIndex(selected);
	}
	pixelviewProfile->setEnabled(idle && pixelviewProfile->count() > 0);
	const auto kbps = obs_data_get_int(data, "bitrate");
	if (!pixelviewBitrate->hasFocus()) { pixelviewBitrate->setValue(static_cast<int>(kbps / 1000)); pixelviewBitrate->setProperty("edited", false); }
	pixelviewAdvanced->setToolTip(QStringLiteral("%1 kbps • Opus (OBS defaults)\n%2 • SDR/input settings independent\nB-frames always off. Preferences only — no output started.")
		.arg(kbps).arg(QString::fromUtf8(config_get_string(activeConfiguration, "Video", "ColorFormat"))));
	pixelviewBitrate->setToolTip(QStringLiteral("Quick control: 1–12 Mbps. Actual saved bitrate: %1 kbps. Advanced values are retained until you edit this control.").arg(kbps));
}

bool OBSBasic::SavePixelviewEncoding(const char *id, obs_data_t *settings, bool initializing)
{
	if (PixelviewSettingsBusy() || (!initializing && PixelviewConfigurationLocked())) return false;
	auto fail = [this](const QString &message) { QMessageBox::warning(this, QStringLiteral("Pixelview encoding"), message); return false; };
	if (pixelview::encodingBusy(obs_video_active(), outputHandler && outputHandler->Active(), setupStreamingGuard))
		return fail(QStringLiteral("Stop active outputs and wait for stream setup before changing encoding."));
	const auto opus = PixelviewOpusEncoder();
	if (opus.empty()) return fail(QStringLiteral("No registered Opus encoder. Encoding preferences were not changed."));
	OBSProperties props = obs_get_encoder_properties(id);
	if (!props) return fail(QStringLiteral("The selected encoder is unavailable."));
	PixelviewEnforceNoBFrames(id, settings);
	obs_properties_apply_settings(props, settings);
	PixelviewEnforceNoBFrames(id, settings);
	const std::string profile = obs_data_get_string(settings, "profile");
	if (!profile.empty() && !PixelviewListValue(props, "profile", profile.c_str()))
		return fail(QStringLiteral("This profile is not enabled in the native encoder properties."));
	std::string format;
	if (strcmp(obs_get_encoder_codec(id), "hevc") == 0) {
		format = pixelview::profileFormat(profile);
		if (!pixelview::profileRangeSupported(profile, config_get_string(activeConfiguration, "Video", "ColorRange")))
			return fail(QStringLiteral("Main 4:2:2 10 requires limited-range P216. The current full-range video setting is incompatible; preferences were not changed."));
		if (format.empty()) return fail(QStringLiteral("This HEVC profile has no verified input-format mapping in Pixelview; select Main, Main10 or Main 4:2:2 10."));
	} else if (strcmp(obs_get_encoder_codec(id), "h264") == 0) {
		format = "NV12";
	}
	struct Field { const char *section; const char *key; bool present; std::string value; };
	std::vector<Field> old;
	auto set = [&](const char *section, const char *key, const std::string &value) {
		const char *previous = config_get_string(activeConfiguration, section, key);
		old.push_back({section, key, config_has_user_value(activeConfiguration, section, key), previous ? previous : ""});
		config_set_string(activeConfiguration, section, key, value.c_str());
	};
	auto restore = [&] {
		for (const auto &field : old) {
			if (field.present) config_set_string(activeConfiguration, field.section, field.key, field.value.c_str());
			else config_remove_value(activeConfiguration, field.section, field.key);
		}
	};
	const std::string previousFormat = config_get_string(activeConfiguration, "Video", "ColorFormat");
	if (!format.empty()) set("Video", "ColorFormat", format);
	const bool reset = !format.empty() && previousFormat != format;
	if (reset && ResetVideo() != OBS_VIDEO_SUCCESS) {
		restore();
		const bool recovered = ResetVideo() == OBS_VIDEO_SUCCESS;
		return fail(recovered ? QStringLiteral("The required video format could not be initialized; previous format restored.") : QStringLiteral("Video format failed and rollback failed. Restart Pixelview."));
	}
	const auto path = (GetCurrentProfile().path / "streamEncoder.json").u8string();
	OBSDataAutoRelease previous = obs_data_create_from_json_file_safe(path.c_str(), "bak");
	auto rollback = [&] {
		restore();
		bool jsonRestored;
		if (previous) {
			jsonRestored = obs_data_save_json_safe(previous, path.c_str(), "tmp", nullptr);
		} else {
			std::error_code error;
			std::filesystem::remove(path, error);
			jsonRestored = !error; // An already absent file is also restored.
		}
		const bool videoRestored = !reset || ResetVideo() == OBS_VIDEO_SUCCESS;
		return jsonRestored && videoRestored;
	};
	if (!obs_data_save_json_safe(settings, path.c_str(), "tmp", "bak")) {
		// A failed safe write may already have moved the canonical JSON to .bak.
		return fail(rollback() ? QStringLiteral("Could not save streamEncoder.json; preferences were not applied.") : QStringLiteral("JSON save and rollback failed. Check profile permissions and restart Pixelview."));
	}
	set("Output", "Mode", "Advanced");
	set("AdvOut", "Encoder", id); // This OBS branch uses Encoder, not StreamEncoder.
	set("AdvOut", "AudioEncoder", opus);
	set("AdvOut", "RecAudioEncoder", opus);
	set("AdvOut", "RecEncoder", "none");
	set("AdvOut", "ApplyServiceSettings", "false");
	set("AdvOut", "RescaleFilter", std::to_string(OBS_SCALE_DISABLE));
	set("SimpleOutput", "StreamAudioEncoder", "opus");
	set("Pixelview", "EncodingInitialized", "true");
	// Keep the old handler alive until the native factory and disk commit succeed.
	// ResetOutputs destroys it before construction, so cannot provide this transaction.
	std::unique_ptr<BasicOutputHandler> replacement;
	try {
		replacement.reset(CreateAdvancedOutputHandler(this));
	} catch (...) {
		return fail(rollback() ? QStringLiteral("Could not create native outputs; previous preferences and output handler retained.") : QStringLiteral("Output creation and rollback failed. Check profile permissions and restart Pixelview."));
	}
	// Keep OBS's existing advanced audio track defaults; there are no Pixelview audio knobs.
	if (config_save_safe(activeConfiguration, "tmp", nullptr) != CONFIG_SUCCESS) {
		replacement.reset();
		return fail(rollback() ? QStringLiteral("Could not save the profile; previous preferences restored.") : QStringLiteral("Profile save and rollback failed. Check profile permissions and restart Pixelview."));
	}
	outputHandler.swap(replacement);
	// Same UI synchronization as ResetOutputs, without a second destructive factory call.
	emit ReplayBufEnabled(outputHandler->replayBuffer);
	if (sysTrayReplayBuffer) sysTrayReplayBuffer->setEnabled(!!outputHandler->replayBuffer);
	UpdateIsRecordingPausable();
	return true;
}

void OBSBasic::AdvancedPixelviewEncoding()
{
	if (PixelviewConfigurationLocked()) return;
	if (pixelview::encodingBusy(obs_video_active(), outputHandler && outputHandler->Active(), setupStreamingGuard)) return;
	const std::string id = config_get_string(activeConfiguration, "AdvOut", "Encoder");
	OBSData data = PixelviewEncoderData(GetCurrentProfile().path / "streamEncoder.json", id.c_str(), false);
	QDialog dialog(this);
	connect(this, &OBSBasic::StreamingPreparing, &dialog, &QDialog::reject);
	dialog.setStyleSheet(QStringLiteral("QPushButton { text-align: left; }"));
	dialog.setWindowTitle(QStringLiteral("Advanced encoding — %1").arg(QString::fromUtf8(obs_encoder_get_display_name(id.c_str()))));
	dialog.resize(620, 650);
	auto *layout = new QVBoxLayout(&dialog);
	auto *notice = new QLabel(QStringLiteral("Native encoder properties. B-frames are always off; other overrides are retained. HEVC profiles update the OBS input format, not the source bit depth. Hardware support must be validated by actual encoding."), &dialog);
	notice->setWordWrap(true);
	layout->addWidget(notice);
	auto *view = new OBSPropertiesView(data, id.c_str(), (PropertiesReloadCallback)obs_get_encoder_properties, 170);
	connect(view, &OBSPropertiesView::PropertiesAboutToRefresh, view, [view, id](obs_properties_t *props) {
		PixelviewEnforceNoBFrames(id.c_str(), view->GetSettings());
		PixelviewHideBFrameProperties(props);
	});
	PixelviewAlignProperties(view);
	view->ReloadProperties();
	layout->addWidget(view);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
		if (SavePixelviewEncoding(id.c_str(), view->GetSettings())) dialog.accept();
	});
	dialog.exec();
	RefreshPixelviewEncoding();
}
