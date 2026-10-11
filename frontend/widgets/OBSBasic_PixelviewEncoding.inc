// Included by OBSBasic.cpp: native OBS encoding preferences, no separate output implementation.
#include <QAbstractItemView>
#include <QFormLayout>
#include <QPixmap>
#include <utility>
#include "utility/PixelviewP3Filter.hpp"
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
	// WebRTC viewers join mid-stream and the engine passes H.264/HEVC through,
	// so every keyframe must carry its parameter sets. The WHIP service asks for
	// this, but Pixelview skips service encoder settings (ApplyServiceSettings=false),
	// so enforce it here. Without it NVENC sends SPS/PPS only with the first IDR
	// and late browsers decode nothing; x264 repeats them by default and
	// VideoToolbox ignores the key.
	obs_data_set_bool(data, "repeat_headers", true);
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
	// NVENC (H.264 and HEVC): NVIDIA's ultra-low-latency recipe. The tuning
	// also selects a one-frame VBV and zero output delay in obs-nvenc;
	// look-ahead would wait for future frames. AQ helps flat gradients.
	if (strncmp(id, "obs_nvenc_", 10) == 0) {
		PixelviewDefaultList(data, props, "preset", "p4");
		PixelviewDefaultList(data, props, "tune", "ull");
		PixelviewDefaultList(data, props, "multipass", "qres");
		for (const auto &[key, value] : {std::pair{"lookahead", false}, std::pair{"adaptive_quantization", true}}) {
			auto *p = obs_properties_get(props, key);
			if (p && obs_property_get_type(p) == OBS_PROPERTY_BOOL)
				obs_data_set_bool(data, key, value);
		}
	}
	for (const auto *key : {"bitrate", "keyint_sec", "bf"}) {
		auto *p = obs_properties_get(props, key);
		// Every encoder and codec (HEVC Main, Main10 and Main 4:2:2 10 share one
		// configuration) defaults to two-second keyframes.
		const int value = strcmp(key, "bitrate") == 0 ? 6000 : strcmp(key, "keyint_sec") == 0 ? 2 : 0;
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
// BEGIN PROFILE LABELS
// HEVC guidance in the sender's profile list; the saved value is unchanged.
static QString PixelviewProfileLabel(const QString &name, const QString &value)
{
	if (value == QStringLiteral("main10")) return name + QStringLiteral(" (recommended)");
	if (value == QStringLiteral("main42210")) return name + QStringLiteral(" (transcoded for browsers)");
	if (value == QStringLiteral("main44410")) return name + QStringLiteral(" (transcoded for browsers)");
	return name;
}
static QString PixelviewProfileTip(const QString &value)
{
	if (value == QStringLiteral("main10"))
		return QStringLiteral("Recommended. Plays directly everywhere.");
	if (value == QStringLiteral("main42210"))
		return PIXELVIEW_PLATFORM_TEXT("Native in the Pixelview Player iOS app and in Pixelview Desktop on Apple silicon. Transcoded for browsers: more latency, lower quality.", "Native in the Pixelview Player iOS app and in Pixelview Desktop on a Mac with Apple silicon. Transcoded for browsers: more latency, lower quality.");
	if (value == QStringLiteral("main44410"))
		return PIXELVIEW_PLATFORM_TEXT("Full-resolution colour for RGB sources. Native in Pixelview Desktop on Apple silicon. Transcoded for browsers: more latency, and 4:2:0 except where a browser takes VP9 4:4:4. Needs an RGB 4:4:4 capture input and an RGB monitoring output to matter.", "Full-resolution colour for RGB sources. Native in Pixelview Desktop on a Mac with Apple silicon. Transcoded for browsers: more latency, and 4:2:0 except where a browser takes VP9 4:4:4. Needs an RGB 4:4:4 capture input and an RGB monitoring output to matter.");
	return QString();
}
// END PROFILE LABELS

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
	pixelviewColorLabel = new QLabel(sidebar);
	pixelviewColorLabel->setObjectName(QStringLiteral("pixelviewColorProfile"));
	pixelviewColorLabel->setWordWrap(true);
	pixelviewColorLabel->setToolTip(QStringLiteral("Set by this node's input colour profile in Pixelview admin. HDR streams at a 1000-nit nominal peak and needs an HEVC 10-bit profile."));
	pixelviewColorLabel->hide();
	layout->addWidget(pixelviewColorLabel);
	pixelviewProfile->setToolTip(QStringLiteral("Encoder profile. Main10 is recommended; 10-bit detail also needs a 10-bit capture input."));
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
				if (PixelviewProfileBlocked(obs_property_list_item_string(profile, i))) continue;
				const QString value = QString::fromUtf8(obs_property_list_item_string(profile, i));
				entries.emplace_back(PixelviewProfileLabel(QString::fromUtf8(obs_property_list_item_name(profile, i)), value), value);
			}
		}
		bool changed = pixelviewProfile->count() != static_cast<int>(entries.size());
		for (size_t i = 0; !changed && i < entries.size(); ++i)
			changed = pixelviewProfile->itemText(static_cast<int>(i)) != entries[i].first ||
				  pixelviewProfile->itemData(static_cast<int>(i)).toString() != entries[i].second;
		if (changed) {
			pixelviewProfile->clear();
			for (const auto &entry : entries) {
				pixelviewProfile->addItem(entry.first, entry.second);
				const QString tip = PixelviewProfileTip(entry.second);
				if (!tip.isEmpty()) pixelviewProfile->setItemData(pixelviewProfile->count() - 1, tip, Qt::ToolTipRole);
			}
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
	// A blocked profile met while streaming is switched once configuration unlocks.
	if (pixelviewProfilePolicyPending && idle)
		QTimer::singleShot(0, this, &OBSBasic::ApplyPixelviewProfilePolicy);
	// A colour profile changed while streaming or receiving applies once idle.
	if (pixelviewColorPending && idle)
		QTimer::singleShot(0, this, [this] { ApplyPixelviewColor(); });
}

bool OBSBasic::PixelviewProfileBlocked(const std::string &profile) const
{
	if (profile == "main" && PixelviewColorHdr()) return true; // HDR needs ten bits
	return !profile.empty() && pixelviewLease.blockedProfiles.contains(QString::fromStdString(profile));
}

bool OBSBasic::PixelviewColorHdr() const
{
	const auto target = pixelview::nodeColor(pixelviewLease.colorProfile.toStdString());
	if (target.known || pixelviewLease.colorProfile.isEmpty()) return target.hdr;
	// A profile this version does not know keeps the canvas, and so its depth needs.
	const std::string space = config_get_string(activeConfiguration, "Video", "ColorSpace");
	return space == "2100PQ" || space == "2100HLG";
}

namespace {
// Every P3 conversion on a source, found by type: renamed or duplicated
// instances are included, so a second one can never double-convert.
std::vector<OBSSource> PixelviewP3Filters(obs_source_t *source)
{
	std::vector<OBSSource> filters;
	obs_source_enum_filters(
		source,
		[](obs_source_t *, obs_source_t *filter, void *param) {
			if (strcmp(obs_source_get_id(filter), pixelview::P3_FILTER_ID) == 0)
				static_cast<std::vector<OBSSource> *>(param)->emplace_back(filter);
		},
		&filters);
	return filters;
}
} // namespace

void OBSBasic::ApplyPixelviewCaptureColor()
{
	// A DeckLink capture labels its frames with the node's transfer (frame HDR
	// metadata, when the signal carries it, still wins in the plugin).
	OBSSource source = PixelviewCaptureSource();
	const auto target = pixelview::nodeColor(pixelviewLease.colorProfile.toStdString());
	if (!source || pixelviewLease.colorProfile.isEmpty() || !target.known) return;
	if (strcmp(obs_source_get_id(source), "decklink-input") != 0) {
		// Only the DeckLink capture is converted; anything else would leave P3 code
		// values tagged as Rec.2100 PQ, with no LUT on the engine either.
		// Once per capture type and profile, not on every apply.
		static std::string warned;
		const std::string key = pixelviewLease.colorProfile.toStdString() + "/" + obs_source_get_id(source);
		if (target.p3 && warned != key) {
			warned = key;
			blog(LOG_WARNING, "Pixelview: P3-D65 conversion needs a DeckLink capture; %s is not converted", obs_source_get_id(source));
			PixelviewWarn(QStringLiteral("Pixelview colour"),
				      QStringLiteral("This node is set to P3-D65 PQ sent with Pixelview Desktop, which is converted only on a DeckLink capture. Select a DeckLink input, or ask an admin to set the node to P3-D65 PQ sent with OBS."));
		}
		return;
	}
	// P3-D65 sent with Pixelview Desktop: the capture's primaries are corrected here.
	bool changed = false;
	auto p3 = PixelviewP3Filters(source);
	for (size_t i = target.p3 ? 1 : 0; i < p3.size(); ++i) {
		obs_source_filter_remove(source, p3[i]);
		changed = true;
	}
	if (!target.p3 && !p3.empty()) blog(LOG_INFO, "Pixelview: P3-D65 conversion removed from the capture");
	if (target.p3 && p3.size() > 1) blog(LOG_WARNING, "Pixelview: removed %zu duplicate P3-D65 conversions", p3.size() - 1);
	if (target.p3 && p3.empty()) {
		OBSSourceAutoRelease filter = obs_source_create_private(pixelview::P3_FILTER_ID, pixelview::P3_FILTER_NAME, nullptr);
		if (filter) {
			obs_source_filter_add(source, filter);
			changed = true;
			blog(LOG_INFO, "Pixelview: P3-D65 to Rec.2020 conversion on the capture");
		} else {
			blog(LOG_ERROR, "Pixelview: P3-D65 conversion filter unavailable");
		}
	}
	const std::string space = target.space;
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	const long long current = obs_data_get_int(settings, "color_space");
	const bool currentHdr = current == VIDEO_CS_2100_PQ || current == VIDEO_CS_2100_HLG;
	const long long wanted = space == "2100PQ" ? VIDEO_CS_2100_PQ : space == "2100HLG" ? VIDEO_CS_2100_HLG
				: currentHdr ? VIDEO_CS_DEFAULT : current;
	if (wanted == current) {
		if (changed) SaveProject();
		return;
	}
	obs_data_set_int(settings, "color_space", wanted);
	obs_source_update(source, settings);
	SaveProject();
	blog(LOG_INFO, "Pixelview: capture colour space %lld -> %lld for the node's colour profile", current, wanted);
}

// The node's input colour profile decides the sender canvas and its max
// luminance the HDR nominal peak. HDR also needs an HEVC 10-bit profile, so
// 8-bit Main (and a non-HEVC encoder, or an 8-bit canvas for an encoder without
// a profile property) is replaced first.
// Starting: called from publish, before the output exists, past the lease lock.
bool OBSBasic::ApplyPixelviewColor(bool starting)
{
	pixelviewColorPending = false;
	if (isClosing() || !pixelviewEncoder) return true;
	const std::string profile = pixelviewLease.colorProfile.toStdString();
	const auto target = pixelview::nodeColor(profile);
	const uint32_t nits = pixelview::hdrPeakNits(pixelviewLease.colorPeakNits, target.space);
	const auto label = [&](bool pending) {
		if (!pixelviewColorLabel) return;
		pixelviewColorLabel->setVisible(!profile.empty());
		QString text = !target.known ? QStringLiteral("Colour: %1 (%2)").arg(QString::fromStdString(profile), QString::fromUtf8(target.label))
			       : target.hdr ? QStringLiteral("Colour: %1 · %2 nits").arg(QString::fromUtf8(target.label)).arg(nits)
					    : QStringLiteral("Colour: %1").arg(QString::fromUtf8(target.label));
		// The live stream keeps its canvas; say so instead of claiming the new one.
		if (pending) text += QStringLiteral(" · after this stream");
		pixelviewColorLabel->setText(text);
	};
	label(false);
	if (profile.empty()) return true; // A backend without colour profiles: keep the canvas.
	if (!target.known) {
		blog(LOG_WARNING, "Pixelview: node colour profile %s is not supported by this version; the canvas is kept", profile.c_str());
		return true;
	}
	std::string id = config_get_string(activeConfiguration, "AdvOut", "Encoder");
	const char *idCodec = obs_get_encoder_codec(id.c_str());
	const std::string space = config_get_string(activeConfiguration, "Video", "ColorSpace");
	const uint64_t peak = config_get_uint(activeConfiguration, "Video", "HdrNominalPeakLevel");
	const bool canvasDone = space == target.space && (!target.hdr || peak == nits);
	const bool locked = starting ? (pixelviewReceiving || PixelviewSettingsBusy()) : PixelviewConfigurationLocked();
	if (locked) {
		// Already applied (publish applies it before this queued call runs): nothing waits.
		if (!starting && canvasDone && (!target.hdr || (idCodec && strcmp(idCodec, "hevc") == 0))) return true;
		blog(LOG_INFO, "Pixelview: node colour profile %s applies when the sender is idle", profile.c_str());
		pixelviewColorPending = true;
		label(true);
		return !starting;
	}
	if (target.hdr) {
		std::string encoder = id;
		if (!idCodec || strcmp(idCodec, "hevc") != 0) {
			encoder.clear();
			for (int i = 0; i < pixelviewEncoder->count() && encoder.empty(); ++i) {
				const auto candidate = pixelviewEncoder->itemData(i).toString().toStdString();
				const char *codec = obs_get_encoder_codec(candidate.c_str());
				if (codec && strcmp(codec, "hevc") == 0) encoder = candidate;
			}
			if (encoder.empty()) {
				PixelviewWarn(QStringLiteral("Pixelview colour"), QStringLiteral("This node streams HDR (%1), which needs an HEVC encoder. None is available.").arg(QString::fromUtf8(target.label)));
				return false;
			}
		}
		OBSData data = PixelviewEncoderData(GetCurrentProfile().path / "streamEncoder.json", encoder.c_str(), encoder != id);
		const std::string current = obs_data_get_string(data, "profile");
		// An encoder without a profile property (AMD AMF) follows the canvas format:
		// an 8-bit canvas would stream 8-bit "HDR".
		const std::string format = config_get_string(activeConfiguration, "Video", "ColorFormat");
		const bool implicit8 = current.empty() && format != pixelview::implicitProfileFormat(format, true);
		if (encoder != id || current == "main" || implicit8) {
			if (current == "main") obs_data_set_string(data, "profile", "main10");
			blog(LOG_INFO, "Pixelview: node colour %s needs ten bits; encoder %s profile %s", profile.c_str(), encoder.c_str(),
			     current.empty() ? "(from a P010 canvas)" : obs_data_get_string(data, "profile"));
			if (!SavePixelviewEncoding(encoder.c_str(), data, starting)) return false;
			id = encoder;
		}
	}
	if (space != target.space || (target.hdr && peak != nits)) {
		if (pixelview::encodingBusy(obs_video_active(), outputHandler && outputHandler->Active(), setupStreamingGuard)) {
			pixelviewColorPending = true;
			label(true);
			return !starting;
		}
		const bool hadSpace = config_has_user_value(activeConfiguration, "Video", "ColorSpace");
		const bool hadPeak = config_has_user_value(activeConfiguration, "Video", "HdrNominalPeakLevel");
		config_set_string(activeConfiguration, "Video", "ColorSpace", target.space);
		if (target.hdr) config_set_uint(activeConfiguration, "Video", "HdrNominalPeakLevel", nits);
		if (ResetVideo() != OBS_VIDEO_SUCCESS) {
			if (hadSpace) config_set_string(activeConfiguration, "Video", "ColorSpace", space.c_str());
			else config_remove_value(activeConfiguration, "Video", "ColorSpace");
			if (hadPeak) config_set_uint(activeConfiguration, "Video", "HdrNominalPeakLevel", peak);
			else config_remove_value(activeConfiguration, "Video", "HdrNominalPeakLevel");
			const bool recovered = ResetVideo() == OBS_VIDEO_SUCCESS;
			PixelviewWarn(QStringLiteral("Pixelview colour"), recovered ? QStringLiteral("The %1 canvas could not be initialized; the previous colour space is kept.").arg(QString::fromUtf8(target.label))
										  : QStringLiteral("The colour space change and its rollback failed. Restart Pixelview."));
			return false;
		}
		config_save_safe(activeConfiguration, "tmp", nullptr);
		blog(LOG_INFO, "Pixelview: node colour profile %s: canvas %s -> %s%s", profile.c_str(), space.c_str(), target.space,
		     target.hdr ? QStringLiteral(" (%1-nit nominal peak)").arg(nits).toUtf8().constData() : "");
	}
	ApplyPixelviewCaptureColor();
	RefreshPixelviewEncoding();
	return true;
}

// The backend blocks some profiles per region (e.g. HEVC 4:2:2 in Sydney). A
// blocked current profile is replaced by Main10, else the first allowed one.
void OBSBasic::ApplyPixelviewProfilePolicy()
{
	pixelviewProfilePolicyPending = false;
	if (isClosing() || !pixelviewEncoder) return;
	const std::string id = config_get_string(activeConfiguration, "AdvOut", "Encoder");
	OBSData data = PixelviewEncoderData(GetCurrentProfile().path / "streamEncoder.json", id.c_str(), false);
	const std::string current = obs_data_get_string(data, "profile");
	if (PixelviewProfileBlocked(current)) {
		if (PixelviewConfigurationLocked()) {
			blog(LOG_INFO, "Pixelview: encoder profile %s is blocked in the node's region; switching after the stream", current.c_str());
			pixelviewProfilePolicyPending = true;
			return;
		}
		std::string fallback;
		OBSProperties props = obs_get_encoder_properties(id.c_str());
		auto *list = props ? obs_properties_get(props, "profile") : nullptr;
		if (list && obs_property_get_type(list) == OBS_PROPERTY_LIST && obs_property_list_format(list) == OBS_COMBO_FORMAT_STRING) {
			for (size_t i = 0; i < obs_property_list_item_count(list); ++i) {
				const std::string value = obs_property_list_item_string(list, i);
				if (obs_property_list_item_disabled(list, i) || PixelviewProfileBlocked(value)) continue;
				if (fallback.empty() || value == "main10") fallback = value;
			}
		}
		if (fallback.empty()) {
			blog(LOG_WARNING, "Pixelview: encoder profile %s is blocked in the node's region and no other profile is available", current.c_str());
		} else {
			blog(LOG_INFO, "Pixelview: encoder profile %s is blocked in the node's region; switching to %s", current.c_str(), fallback.c_str());
			obs_data_set_string(data, "profile", fallback.c_str());
			SavePixelviewEncoding(id.c_str(), data);
		}
	}
	RefreshPixelviewDevices();
}

bool OBSBasic::SavePixelviewEncoding(const char *id, obs_data_t *settings, bool initializing)
{
	if (PixelviewSettingsBusy() || (!initializing && PixelviewConfigurationLocked())) return false;
	auto fail = [this](const QString &message) { PixelviewWarn(QStringLiteral("Pixelview encoding"), message); return false; };
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
	if (profile == "main" && PixelviewColorHdr())
		return fail(QStringLiteral("This node streams HDR, which needs ten bits. Choose Main10, Main 4:2:2 10 or Main 4:4:4 10."));
	if (PixelviewProfileBlocked(profile))
		return fail(QStringLiteral("This profile is not available in the region of the paired Pixelview node."));
	if (PixelviewColorHdr() && strcmp(obs_get_encoder_codec(id), "hevc") != 0)
		return fail(QStringLiteral("This node streams HDR, which needs an HEVC encoder with a 10-bit profile."));
	std::string format;
	if (strcmp(obs_get_encoder_codec(id), "hevc") == 0) {
		format = profile.empty() && !obs_properties_get(props, "profile")
				 ? pixelview::implicitProfileFormat(config_get_string(activeConfiguration, "Video", "ColorFormat"), PixelviewColorHdr())
				 : pixelview::profileFormat(profile);
		if (!pixelview::profileRangeSupported(profile, config_get_string(activeConfiguration, "Video", "ColorRange")))
			return fail(profile == "main44410"
					    ? QStringLiteral("Main 4:4:4 10 requires limited-range P416. The current full-range video setting is incompatible; preferences were not changed.")
					    : QStringLiteral("Main 4:2:2 10 requires limited-range P216. The current full-range video setting is incompatible; preferences were not changed."));
		if (format.empty()) return fail(QStringLiteral("This HEVC profile has no verified input-format mapping in Pixelview; select Main, Main10, Main 4:2:2 10 or Main 4:4:4 10."));
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
