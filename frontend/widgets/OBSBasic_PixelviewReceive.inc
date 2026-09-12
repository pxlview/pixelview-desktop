// Pixelview modification: one native canvas, mutually exclusive send/receive controls.
#include <QToolButton>
#include <QTabBar>
#include <QLineEdit>
#include <QSysInfo>
#include <QUuid>
#include <utility/PixelviewKeychainTask.hpp>
#include <obs-module.h>

// In-memory exact-source binding; no source UUID or receive authority is saved.
static void PixelviewBindDeckLinkReceive(obs_source_t *source, bool receiving)
{
 calldata_t cd; calldata_init(&cd);
 calldata_set_ptr(&cd,"source",source); calldata_set_bool(&cd,"receiving",receiving);
 proc_handler_call(obs_get_proc_handler(),"pixelview_decklink_receive",&cd);
 calldata_free(&cd);
}

bool OBSBasic::PixelviewReceiveVideoBusy() const
{
 if (PixelviewSettingsBusy()) return true;
 // DeckLink can consume the main texture without obs_video_active().
 bool active = false;
 obs_enum_outputs([](void *opaque, obs_output_t *output) {
  if (obs_output_active(output)) *static_cast<bool *>(opaque) = true;
  return !*static_cast<bool *>(opaque);
 }, &active);
 return active;
}

bool OBSBasic::SetPixelviewReceivePrecision(bool enabled)
{
 if (PixelviewReceiveVideoBusy() || pixelviewReceivePrecisionFault) return false;
 if (enabled == pixelviewReceivePrecision) return true;
 obs_video_info current = {};
 if (!obs_get_video_info(&current)) return false;
 // Copy the module name: obs_reset_video tears down the previous video state.
 const std::string currentModule = current.graphics_module ? current.graphics_module : "";
 current.graphics_module = currentModule.c_str();
 obs_video_info target = enabled ? current : pixelviewSendVideo;
 if (enabled) {
  target.output_format = VIDEO_FORMAT_P010;
  target.colorspace = VIDEO_CS_709;
  target.range = VIDEO_RANGE_PARTIAL;
 } else {
  target.graphics_module = pixelviewSendRenderModule.c_str();
 }
 if (obs_reset_video(&target) != OBS_VIDEO_SUCCESS) {
  // Do not switch sources or mutate persisted sender settings on failure.
  if (obs_reset_video(&current) != OBS_VIDEO_SUCCESS) pixelviewReceivePrecisionFault = true;
  return false;
 }
 if (enabled) {
  pixelviewSendVideo = current;
  pixelviewSendRenderModule = currentModule;
  pixelviewSendVideo.graphics_module = pixelviewSendRenderModule.c_str();
 }
 pixelviewReceivePrecision = enabled;
 return true;
}

bool OBSBasic::PixelviewModeBusy() const
{
	return isClosing() || pixelviewShutdownPending || pixelviewReceiveCredentialBusy || pixelviewReceivePrecisionFault || pixelviewReceiveIntent || PixelviewSettingsBusy() ||
	       pixelviewLease.intent || pixelviewLease.pending || pixelviewLease.leased || pixelviewStopPending ||
	       pixelviewUnpairPending || (pixelviewDesktop && pixelviewDesktop->exchanging);
}

OBSSource OBSBasic::PixelviewManagedSource()
{
	if (pixelviewReceiving) return pixelviewReceiveSource;
	OBSSourceAutoRelease source = obs_get_source_by_name("Pixelview Capture");
	if (source && (obs_source_removed(source) || strcmp(obs_source_get_id(source), "decklink-input") != 0)) return nullptr;
	return OBSSource(source.Get());
}

void OBSBasic::SetPixelviewReceiveStatus(const QString &text, ReceiveStatusTone tone)
{
	const char *color = tone == ReceiveStatusTone::Receiving ? "#55b879" :
		tone == ReceiveStatusTone::Connecting ? "#d8ad55" :
		tone == ReceiveStatusTone::Error ? "#ed8585" : "palette(window-text)";
	pixelviewReceiveStatus->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(QString::fromLatin1(color)));
	pixelviewReceiveStatus->setText(text);
	pixelviewReceiveStatusTone = tone;
}

void OBSBasic::InitPixelviewReceive(QWidget *sidebar)
{
	// Equal halves, including native styles with unequal text-size hints.
	class ModeTabs final : public QTabBar {
	public:
		using QTabBar::QTabBar;
		QSize tabSizeHint(int index) const override
		{
			QSize size = QTabBar::tabSizeHint(index);
			for (int tab = 0; tab < count(); ++tab)
				size = size.expandedTo(QTabBar::tabSizeHint(tab));
			return size;
		}
	};
	pixelviewModeTabs = new ModeTabs(sidebar);
	pixelviewModeTabs->setExpanding(true);
	pixelviewModeTabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	pixelviewModeTabs->setObjectName(QStringLiteral("pixelviewModeTabs"));
	pixelviewModeTabs->addTab(QStringLiteral("Sending"));
	pixelviewModeTabs->addTab(QStringLiteral("Receiving"));
	// The brand is the first sidebar row; modes fill the next row, not the window.
	auto *sidebarLayout = qobject_cast<QVBoxLayout *>(sidebar->layout());
	sidebarLayout->insertWidget(1, pixelviewModeTabs);
	connect(pixelviewModeTabs, &QTabBar::currentChanged, this, &OBSBasic::SelectPixelviewMode);
	pixelviewReceivingPanel = new QWidget(sidebar);
	pixelviewReceivingPanel->setObjectName(QStringLiteral("pixelviewReceivingPanel"));
	sidebarLayout->addWidget(pixelviewReceivingPanel, 1);
	auto *rows = new QVBoxLayout(pixelviewReceivingPanel);
	rows->setContentsMargins(0, 0, 0, 0); rows->setSpacing(12);
	auto *connection = new QFrame(pixelviewReceivingPanel);
	connection->setObjectName(QStringLiteral("pixelviewReceiveConnection"));
	connection->setStyleSheet(QStringLiteral("QFrame#pixelviewReceiveConnection { background: palette(window); border: 1px solid palette(mid); border-radius: 6px; }"));
	auto *connectionRows = new QVBoxLayout(connection);
	connectionRows->setContentsMargins(12, 12, 12, 12); connectionRows->setSpacing(8);
	rows->addWidget(connection);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapAllRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	pixelviewReceiveId = new QLineEdit(pixelviewReceivingPanel);
	pixelviewReceivePassword = new QLineEdit(pixelviewReceivingPanel);
	pixelviewReceiveName = new QLineEdit(pixelviewReceivingPanel);

	pixelviewReceivePassword->setEchoMode(QLineEdit::Password);
	pixelviewReceivePassword->setInputMethodHints(Qt::ImhHiddenText | Qt::ImhNoPredictiveText);
	pixelviewReceiveId->setObjectName(QStringLiteral("pixelviewReceiveId"));
	pixelviewReceivePassword->setObjectName(QStringLiteral("pixelviewReceivePassword"));
	pixelviewReceiveName->setObjectName(QStringLiteral("pixelviewReceiveName"));
	auto *config = App()->GetUserConfig();
	pixelviewReceiveId->setText(QString::fromUtf8(config_get_string(config, "PixelviewReceive", "SessionId")));
	QString name = QString::fromUtf8(config_get_string(config, "PixelviewReceive", "Name"));
	pixelviewReceiveName->setText(name.isEmpty() ? QSysInfo::machineHostName() : name);
	auto *credentials = new QGridLayout;
	credentials->setHorizontalSpacing(12);
	credentials->setVerticalSpacing(4);
	credentials->setColumnStretch(0, 1);
	credentials->setColumnStretch(1, 1);
	credentials->addWidget(new QLabel(QStringLiteral("Session ID"), pixelviewReceivingPanel), 0, 0);
	auto *passwordHeader = new QWidget(pixelviewReceivingPanel);
	auto *passwordHeaderRow = new QHBoxLayout(passwordHeader);
	passwordHeaderRow->setContentsMargins(0, 0, 0, 0); passwordHeaderRow->setSpacing(6);
	passwordHeaderRow->addWidget(new QLabel(QStringLiteral("Password"), passwordHeader), 1);
	auto *reveal = new QToolButton(passwordHeader);
	reveal->setObjectName(QStringLiteral("pixelviewReceiveReveal"));
	reveal->setText(QStringLiteral("Show")); reveal->setCheckable(true); reveal->setAutoRaise(true);
	reveal->setToolTip(QStringLiteral("Show or hide the password while typing."));
	reveal->setStyleSheet(QStringLiteral("QToolButton { border: none; background: transparent; font-size: 11px; padding: 0; }"));
	connect(reveal, &QToolButton::toggled, this, [this, reveal](bool shown) {
		pixelviewReceivePassword->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password);
		reveal->setText(shown ? QStringLiteral("Hide") : QStringLiteral("Show"));
	});
	passwordHeaderRow->addWidget(reveal, 0, Qt::AlignVCenter);
	credentials->addWidget(passwordHeader, 0, 1);
	credentials->addWidget(pixelviewReceiveId, 1, 0);
	credentials->addWidget(pixelviewReceivePassword, 1, 1);
	connectionRows->addLayout(credentials);
	form->addRow(QStringLiteral("Receiver name"), pixelviewReceiveName);
	connectionRows->addLayout(form);
	pixelviewReceiveStatus = new QLabel(pixelviewReceivingPanel);
	pixelviewReceiveStatus->setTextFormat(Qt::PlainText);
	pixelviewReceiveStatus->setWordWrap(true);
	pixelviewReceiveStatus->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	// Separate durable-storage diagnostics from authentication/media errors.
	pixelviewReceiveStorageStatus = new QLabel(pixelviewReceivingPanel);
	pixelviewReceiveStorageStatus->setObjectName(QStringLiteral("pixelviewReceiveStorageStatus"));
	pixelviewReceiveStorageStatus->setTextFormat(Qt::PlainText);
	pixelviewReceiveStorageStatus->setWordWrap(true);
	pixelviewReceiveStorageStatus->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	pixelviewReceiveStorageStatus->hide();
	connectionRows->addWidget(pixelviewReceiveStorageStatus);
	if (!pixelviewReceiveStore) pixelviewReceiveStore = pixelview::makeReceiveCredentialStore();
	pixelviewReceivePasswordOrigin = PixelviewReceiveOrigin();
	const auto credential = pixelviewReceiveStore->load(pixelviewReceivePasswordOrigin,
		pixelviewReceiveId->text().trimmed(), QString::fromUtf8(config_get_string(config, "PixelviewReceive", "CredentialRevision")));
	if (credential.state == pixelview::ReceiveCredentialStore::State::Found) pixelviewReceivePassword->setText(credential.password);
	else if (credential.state == pixelview::ReceiveCredentialStore::State::Error) {
		pixelviewReceiveCredentialUnavailable = true;
		pixelviewReceiveStorageStatus->setText(QStringLiteral("The saved password could not be read. Click Start receiving so macOS can ask for access, or type it again."));
		pixelviewReceiveStorageStatus->show();
	}
	// Save edits without requiring Start. Deep links use a signal-blocked pair
	// replacement and one explicit save instead of triggering these handlers.
	connect(pixelviewReceiveId, &QLineEdit::textEdited, this, [this] {
		pixelviewReceiveCredentialUnavailable = false;
		pixelviewReceivePassword->clear();
		SavePixelviewReceive();
		RefreshPixelviewReceiveHint();
	});
	connect(pixelviewReceiveName, &QLineEdit::textEdited, this, [this] { SavePixelviewReceive(); RefreshPixelviewReceiveHint(); });
	connect(pixelviewReceivePassword, &QLineEdit::textEdited, this, [this] {
		pixelviewReceiveCredentialUnavailable = false;
		pixelviewReceivePasswordOrigin = PixelviewReceiveOrigin();
		pixelviewReceiveCredentialsDirty = true;
		RefreshPixelviewReceiveHint();
	});
	connect(pixelviewReceivePassword, &QLineEdit::editingFinished, this, [this] {
		if (!PixelviewModeBusy()) SavePixelviewReceive();
	});
	// The primary action follows the credentials; native output settings form
	// their own group below it (added after Start/status).
	auto *hardwareDivider = new QFrame(connection);
	hardwareDivider->setFrameShape(QFrame::HLine);
	auto *outputHint = new QLabel(QStringLiteral("Output"), pixelviewReceivingPanel);
	outputHint->setProperty("pixelviewHeading", true);
	outputHint->setStyleSheet(QStringLiteral("font-weight: 600;"));
	outputHint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	// Find the module's translated action in the retained Tools menu; never reimplement SDI output.
	auto *decklink = new QPushButton(QStringLiteral("DeckLink output settings…"), pixelviewReceivingPanel);
	decklink->setObjectName(QStringLiteral("pixelviewDecklinkOutputSettings"));
	decklink->ensurePolished(); decklink->setFixedHeight(36);
	connect(decklink, &QPushButton::clicked, this, [this] {
		if (isClosing() || pixelviewShutdownPending) return;
		auto *module = obs_get_module("decklink-output-ui");
		const char *label = module ? obs_module_get_locale_text(module, "Decklink Output") : nullptr;
		if (label) {
			for (auto *action : ui->menuTools->actions()) {
				if (action->text() == QString::fromUtf8(label)) { action->setEnabled(true); action->trigger(); return; }
			}
		}
		QMessageBox::information(this, QStringLiteral("DeckLink output"), QStringLiteral("Native DeckLink output settings are unavailable. Check the bundled module and Desktop Video installation."));
	});
	pixelviewReceiveButton = new QPushButton(QStringLiteral("Start receiving"), connection);
	pixelviewReceiveButton->setObjectName(QStringLiteral("pixelviewReceiveButton"));
	pixelviewReceiveButton->ensurePolished(); pixelviewReceiveButton->setFixedHeight(36);
	connectionRows->addWidget(pixelviewReceiveButton);
	connect(pixelviewReceiveButton, &QPushButton::clicked, this, [this] {
		if (pixelviewReceiveIntent) StopPixelviewReceive(true); else StartPixelviewReceive();
	});
	connectionRows->addWidget(pixelviewReceiveStatus);
	connectionRows->addWidget(hardwareDivider);
	connectionRows->addWidget(outputHint);
	connectionRows->addWidget(decklink);
	rows->addStretch(1);
	pixelviewReceiver = std::make_unique<pixelview::PixelviewReceiver>();
	pixelviewReceiver->onStopped = [this] { DisconnectPixelviewReceiveMedia(); };
	pixelviewReceiver->onChanged = [this] {
		if (isClosing() || pixelviewShutdownPending) return;
		using State = pixelview::PixelviewReceiver::State;
		if (pixelviewReceiver->state() == State::Idle || pixelviewReceiver->state() == State::Error) pixelviewReceiveIntent = false;
		pixelviewReceiveFrames = 0;
		SetPixelviewReceiveStatus(pixelviewReceiver->status(), pixelviewReceiver->state() == State::Error ? ReceiveStatusTone::Error :
			pixelviewReceiver->state() == State::Idle ? ReceiveStatusTone::Idle : ReceiveStatusTone::Connecting);
		RefreshPixelviewModes(); RefreshPixelviewAudio();
	};
	pixelviewReceiver->onEndpoint = [this](const QString &endpoint) {
		if (!pixelviewReceiveIntent || !pixelviewReceiving || isClosing() || pixelviewShutdownPending) return;
		if (!pixelviewReceiveSource) {
			pixelviewReceiveSource = OBSSourceAutoRelease(obs_source_create_private("pixelview_whep_source", "Pixelview Receive", nullptr));
			if (pixelviewReceiveSource) {
				auto *item = obs_scene_add(pixelviewReceiveScene, pixelviewReceiveSource);
				obs_transform_info info = {}; info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
				info.bounds_type = OBS_BOUNDS_SCALE_INNER; info.bounds_alignment = OBS_ALIGN_CENTER;
				vec2_set(&info.scale, 1.f, 1.f); vec2_set(&info.bounds, 1920.f, 1080.f);
				if (item) { obs_sceneitem_set_info2(item, &info); obs_sceneitem_set_locked(item, true); }
			}
		}
		if (!pixelviewReceiveSource) { StopPixelviewReceive(true); SetPixelviewReceiveStatus(QStringLiteral("Native WHEP receive module unavailable."), ReceiveStatusTone::Error); return; }
		calldata_t data; calldata_init(&data);
		PixelviewBindDeckLinkReceive(pixelviewReceiveSource, true);
		calldata_set_string(&data, "endpoint", endpoint.toUtf8().constData());
		const bool accepted = proc_handler_call(obs_source_get_proc_handler(pixelviewReceiveSource), "connect", &data);
		calldata_free(&data);
		if (!accepted) { StopPixelviewReceive(true); SetPixelviewReceiveStatus(QStringLiteral("Native receive connection unavailable."), ReceiveStatusTone::Error); return; }
		SetPixelviewReceiveStatus(QStringLiteral("Authorized · connecting media…"), ReceiveStatusTone::Connecting); RefreshPixelviewAudio();
	};
	pixelviewReceiveTimer = new QTimer(this);
	pixelviewReceiveTimer->setInterval(500);
	connect(pixelviewReceiveTimer, &QTimer::timeout, this, [this] {
		if (!pixelviewReceiveIntent || !pixelviewReceiveSource || isClosing() || pixelviewShutdownPending) return;
		calldata_t data; calldata_init(&data);
		if (proc_handler_call(obs_source_get_proc_handler(pixelviewReceiveSource), "get_status", &data)) {
			// Only fixed local labels: never render plugin errors or credential-bearing URLs.
			const char *state = calldata_string(&data, "state");
			const int64_t nativeFrames = calldata_int(&data, "native422_frames");
			const int64_t frames = nativeFrames ? nativeFrames : calldata_int(&data, "frames");
			const bool fresh = frames > pixelviewReceiveFrames;
			pixelviewReceiveFrames = frames;
			if (state && (strcmp(state, "error") == 0 || strcmp(state, "ended") == 0)) {
				StopPixelviewReceive(true);
				SetPixelviewReceiveStatus(QStringLiteral("Receive media failed. Check connection and retry."), ReceiveStatusTone::Error);
			} else if (state && strcmp(state, "playing") == 0 && fresh) {
				SetPixelviewReceiveStatus(QStringLiteral("Receiving"), ReceiveStatusTone::Receiving);
			} else {
				SetPixelviewReceiveStatus(QStringLiteral("Waiting for video…"), ReceiveStatusTone::Connecting);
			}
		} else {
			SetPixelviewReceiveStatus(QStringLiteral("Media status unavailable. Waiting for video…"), ReceiveStatusTone::Connecting);
		}
		calldata_free(&data);
	});
	// Reuse the native projector, including its Escape action; no second renderer.
	auto *view = menuBar()->addMenu(QStringLiteral("View"));
	auto *fullscreenMenu = view->addMenu(QStringLiteral("Fullscreen canvas"));
	connect(fullscreenMenu, &QMenu::aboutToShow, this, [this, fullscreenMenu] {
		fullscreenMenu->clear(); AddProjectorMenuMonitors(fullscreenMenu, this, &OBSBasic::OpenPreviewProjector);
	});
	// Fullscreen is shared canvas chrome, available only through native View.

	RefreshPixelviewReceiveHint();
	RefreshPixelviewModes();
}

void OBSBasic::RefreshPixelviewReceiveHint()
{
	// Guidance only replaces idle guidance: errors stay visible until the next attempt.
	if (!pixelviewReceiveStatus || pixelviewReceiveIntent || pixelviewReceiveStatusTone != ReceiveStatusTone::Idle) return;
	const bool idMissing = pixelviewReceiveId->text().trimmed().isEmpty();
	const bool nameMissing = pixelviewReceiveName->text().trimmed().isEmpty();
	const bool passwordMissing = pixelviewReceivePassword->text().isEmpty() && !pixelviewReceiveCredentialUnavailable;
	SetPixelviewReceiveStatus(idMissing || nameMissing ? QStringLiteral("Enter the session ID and a receiver name, then click Start receiving.") :
		passwordMissing ? QStringLiteral("Enter the session password, then click Start receiving.") :
		QStringLiteral("Ready. Click Start receiving."), ReceiveStatusTone::Idle);
}

void OBSBasic::SelectPixelviewMode(int index)
{
	if (index < 0 || index > 1 || PixelviewModeBusy()) { RefreshPixelviewModes(); return; }
	if (pixelviewReceiving == (index == 1)) return;
 // Allocate before reset so allocation failure cannot leave sender on a receive format.
 if (index == 1 && !pixelviewReceiveScene)
  pixelviewReceiveScene = OBSSceneAutoRelease(obs_scene_create_private("Pixelview Receive Canvas"));
 if (index == 1 && !pixelviewReceiveScene) { RefreshPixelviewModes(); return; }
 if (!SetPixelviewReceivePrecision(index == 1)) {
  SetPixelviewReceiveStatus(QStringLiteral("Could not change canvas precision. Stop active outputs and retry; restart the app if video reset failed."), ReceiveStatusTone::Error);
  RefreshPixelviewModes(); return;
 }
	if (properties) properties->close();
	ClearPixelviewAudio();
	OBSSourceAutoRelease capture = obs_get_source_by_name("Pixelview Capture");
	if (capture) obs_source_set_monitoring_enabled(capture, false);
	if (index == 1) {
		if (capture) {
			OBSDataAutoRelease settings = obs_source_get_settings(capture);
			obs_data_set_bool(settings, "deactivate_when_not_showing", true);
			obs_source_update(capture, settings);
		}
		if (!pixelviewReceiveScene) pixelviewReceiveScene = OBSSceneAutoRelease(obs_scene_create_private("Pixelview Receive Canvas"));
		if (!pixelviewReceiveScene) return;
		pixelviewSendOutput = OBSSourceAutoRelease(obs_get_output_source(0));
		// GetCurrentScene remains the sender: suppress its editing overlays and mouse edits.
		pixelviewSendPreviewLocked = ui->preview->Locked();
		ui->preview->SetLocked(true);
		obs_set_output_source(0, obs_scene_get_source(pixelviewReceiveScene));
	} else {
		DisconnectPixelviewReceiveMedia();
		obs_set_output_source(0, pixelviewSendOutput);
		pixelviewSendOutput = nullptr;
		ui->preview->SetLocked(pixelviewSendPreviewLocked);
	}
	pixelviewReceiving = index == 1;
	PixelviewBindDeckLinkReceive(pixelviewReceiveSource, pixelviewReceiving);
	// Remember the operator's mode so the next launch opens on the same tab.
	// Shutdown leaves receive mode through StopPixelviewReceive, never through here.
	config_set_string(App()->GetUserConfig(), "PixelviewReceive", "Mode", pixelviewReceiving ? "receiving" : "sending");
	config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
	RefreshPixelviewDevices();
}

QString OBSBasic::PixelviewReceiveOrigin() const
{
	QUrl origin = pixelviewOrigin.isEmpty() ? QUrl(QStringLiteral("https://api4.pixelview.io")) : pixelviewOrigin;
	if (origin.path() == QStringLiteral("/")) origin.setPath(QString());
	return origin.toString(QUrl::FullyEncoded);
}

bool OBSBasic::SavePixelviewReceive()
{
	if (pixelviewReceiveCredentialBusy) return false;
	const QString origin = PixelviewReceiveOrigin();
	if (pixelviewReceivePasswordOrigin != origin) {
		pixelviewReceivePassword->clear();
		pixelviewReceiveCredentialUnavailable = false;
	}
	pixelviewReceivePasswordOrigin = origin;
	auto *config = App()->GetUserConfig();
	if (pixelviewReceiveCredentialUnavailable) {
		// Metadata-only save: neither rotate the revision nor touch the unknown
		// secret, even when safe-save fails. Start/focus-out are not password edits.
		config_set_string(config, "PixelviewReceive", "Name", pixelviewReceiveName->text().trimmed().toUtf8().constData());
		const bool saved = config_save_safe(config, "tmp", nullptr) == CONFIG_SUCCESS;
		pixelviewReceiveCredentialsDirty = !saved;
		pixelviewReceiveStorageStatus->setText(saved ?
			QStringLiteral("The saved password could not be read. Click Start receiving so macOS can ask for access, or type it again.") :
			QStringLiteral("Changes could not be saved. Check the app's configuration folder permissions; the saved password is still unavailable."));
		pixelviewReceiveStorageStatus->show();
		return saved;
	}
	// Commit a new non-secret generation before replacing the Keychain record.
	// A failed write can therefore never reload the old password for this ID.
	const QString revision = QUuid::createUuid().toString(QUuid::WithoutBraces);
	config_set_string(config, "PixelviewReceive", "SessionId", pixelviewReceiveId->text().trimmed().toUtf8().constData());
	config_set_string(config, "PixelviewReceive", "Name", pixelviewReceiveName->text().trimmed().toUtf8().constData());
	config_set_string(config, "PixelviewReceive", "CredentialRevision", revision.toUtf8().constData());
	const bool configSaved = config_save_safe(config, "tmp", nullptr) == CONFIG_SUCCESS;
	const bool secretSaved = configSaved ? pixelviewReceiveStore->save(origin, pixelviewReceiveId->text().trimmed(), revision, pixelviewReceivePassword->text()) : pixelviewReceiveStore->clear();
	const bool saved = configSaved && secretSaved;
	pixelviewReceiveCredentialsDirty = !saved;
	pixelviewReceiveStorageStatus->setText(saved ? QString() : !configSaved ?
		QStringLiteral("Changes could not be saved. Check the app's configuration folder permissions; previous settings may return after reopening.") :
		QStringLiteral("The password could not be saved to the Keychain. Click Start receiving so macOS can ask for access; it may need typing again after reopening."));
	pixelviewReceiveStorageStatus->setVisible(!saved);
	return saved;
}

void OBSBasic::StartPixelviewReceive()
{
	if (!pixelviewReceiving || !pixelviewReceiver || PixelviewModeBusy()) return;
	const QString id = pixelviewReceiveId->text().trimmed(), name = pixelviewReceiveName->text().trimmed();
	if (id.isEmpty() || name.isEmpty()) { pixelviewReceiveFrames = 0; SetPixelviewReceiveStatus(QStringLiteral("Enter a session ID and receiver name."), ReceiveStatusTone::Error); return; }
	if (!pixelviewReceiver->setOrigin(QUrl(PixelviewReceiveOrigin()), pixelviewDev)) { pixelviewReceiveFrames = 0; SetPixelviewReceiveStatus(QStringLiteral("Invalid backend origin."), ReceiveStatusTone::Error); return; }
	if (pixelviewReceivePasswordOrigin != PixelviewReceiveOrigin()) {
		SavePixelviewReceive(); // Apply the existing origin-bound password reset before snapshotting work.
		SetPixelviewReceiveStatus(QStringLiteral("Backend changed. Enter the receiver password again."), ReceiveStatusTone::Error);
		return;
	}
	auto start = [this, id, name] {
		pixelviewReceiveIntent = true;
		QString password = pixelviewReceivePassword->text();
		pixelviewReceiver->start(id, password, name); password.clear();
		pixelviewReceiveTimer->start(); RefreshPixelviewModes();
	};
	// Authorization belongs to the ordinary Start action, never a separate
	// Keychain control or an automatic reconnect/focus-out/polling callback.
	if (pixelviewReceiveCredentialUnavailable || pixelviewReceiveCredentialsDirty) {
		const bool reading = pixelviewReceiveCredentialUnavailable;
		const QString origin = PixelviewReceiveOrigin(), session = id;
		const QString password = pixelviewReceivePassword->text();
		auto *config = App()->GetUserConfig();
		QString revision = QString::fromUtf8(config_get_string(config, "PixelviewReceive", "CredentialRevision"));
		if (!reading) {
			revision = QUuid::createUuid().toString(QUuid::WithoutBraces);
			config_set_string(config, "PixelviewReceive", "SessionId", session.toUtf8().constData());
			config_set_string(config, "PixelviewReceive", "Name", name.toUtf8().constData());
			config_set_string(config, "PixelviewReceive", "CredentialRevision", revision.toUtf8().constData());
			if (config_save_safe(config, "tmp", nullptr) != CONFIG_SUCCESS) {
				pixelviewReceiveStorageStatus->setText(QStringLiteral("Receiver changes could not be saved. The saved password was not changed."));
				pixelviewReceiveStorageStatus->show(); start(); return;
			}
		}
		pixelviewReceiveCredentialBusy = true; RefreshPixelviewModes();
		const auto store = pixelviewReceiveStore;
		pixelview::runKeychainUserAction(this, [store, reading, origin, session, revision, password] {
			using State = pixelview::ReceiveCredentialStore::State;
			if (reading) return store->load(origin, session, revision);
			return pixelview::ReceiveCredentialStore::Result{store->save(origin, session, revision, password) ? State::Found : State::Error, {}};
		}, [this, reading, origin, start](pixelview::ReceiveCredentialStore::Result result) {
			pixelviewReceiveCredentialBusy = false;
			if (isClosing() || pixelviewShutdownPending) return;
			RefreshPixelviewModes();
			if (result.state == pixelview::ReceiveCredentialStore::State::Error) {
				pixelviewReceiveStorageStatus->setText(QStringLiteral("Password access was canceled or failed. The saved password was not removed. Click Start receiving to retry, or re-enter the password."));
				pixelviewReceiveStorageStatus->show();
				if (!reading) start(); // Current explicit input is still usable.
				return;
			}
			if (reading) pixelviewReceivePassword->setText(result.password);
			pixelviewReceivePasswordOrigin = origin;
			pixelviewReceiveCredentialUnavailable = false; pixelviewReceiveCredentialsDirty = false;
			pixelviewReceiveStorageStatus->clear(); pixelviewReceiveStorageStatus->hide();
			start();
		});
		return;
	}
	SavePixelviewReceive(); // Storage failure is reported separately; current input remains usable.
	start();
}

void OBSBasic::DisconnectPixelviewReceiveMedia()
{
	PixelviewBindDeckLinkReceive(nullptr, pixelviewReceiving);
	pixelviewReceiveFrames = 0;
	ClearPixelviewAudio();
	if (!pixelviewReceiveSource) return;
	obs_source_set_monitoring_enabled(pixelviewReceiveSource, false);
	calldata_t data; calldata_init(&data);
	proc_handler_call(obs_source_get_proc_handler(pixelviewReceiveSource), "disconnect", &data);
	calldata_free(&data);
	if (pixelviewReceiveScene) {
		if (auto *item = obs_scene_find_source(pixelviewReceiveScene, "Pixelview Receive")) obs_sceneitem_remove(item);
	}
	pixelviewReceiveSource = nullptr;
}

void OBSBasic::StopPixelviewReceive(bool keepReceiving)
{
	pixelviewReceiveFrames = 0;
	if (pixelviewReceiveStatus) SetPixelviewReceiveStatus(keepReceiving && !isClosing() && !pixelviewShutdownPending ?
		QStringLiteral("Stopped. Click Start receiving to reconnect.") : QStringLiteral("Not receiving."), ReceiveStatusTone::Idle);
	pixelviewReceiveIntent = false;
	if (pixelviewReceiveTimer) pixelviewReceiveTimer->stop();
	// UI Stop retains the masked form; native close/shutdown drops its copy.
	if (!keepReceiving && pixelviewReceivePassword) {
		// Cmd-Q can precede editingFinished. Flush the latest operator edit once,
		// never replace the durable password with the shutdown-cleared form.
		if (pixelviewReceiveCredentialsDirty) SavePixelviewReceive();
		pixelviewReceiveCredentialsDirty = false;
		pixelviewReceivePassword->clear();
	}
	if (pixelviewReceiver) pixelviewReceiver->stop();
	DisconnectPixelviewReceiveMedia();
	// No-argument callers are native close/shutdown. UI Stop/errors retain the
	// receive canvas and its editing lock so hidden sender framing stays protected.
	if (!keepReceiving && pixelviewReceiving && SetPixelviewReceivePrecision(false)) {
		obs_set_output_source(0, pixelviewSendOutput);
		pixelviewSendOutput = nullptr;
		ui->preview->SetLocked(pixelviewSendPreviewLocked);
		pixelviewReceiving = false;
		PixelviewBindDeckLinkReceive(nullptr, false);
	}
	RefreshPixelviewModes();
	RefreshPixelviewAudio();
}

void OBSBasic::RefreshPixelviewModes()
{
	if (!pixelviewModeTabs) return;
	const bool busy = PixelviewModeBusy();
	const QSignalBlocker blocker(pixelviewModeTabs);
	pixelviewModeTabs->setCurrentIndex(pixelviewReceiving ? 1 : 0);
	pixelviewModeTabs->setEnabled(!busy);
	// Say why the tabs are locked instead of leaving them silently grey.
	const QString lock = !busy ? QString() : pixelviewReceiveIntent ? QStringLiteral("Stop receiving to switch modes.") :
		QStringLiteral("Wait for the current operation to finish before switching modes.");
	pixelviewModeTabs->setTabToolTip(0, lock);
	pixelviewModeTabs->setTabToolTip(1, lock);
	pixelviewSendingPanel->setVisible(!pixelviewReceiving);
	pixelviewReceivingPanel->setVisible(pixelviewReceiving);
	pixelviewReceiveId->setEnabled(!busy);
	pixelviewReceivePassword->setEnabled(!busy);
	pixelviewReceiveName->setEnabled(!busy);
	pixelviewReceiveButton->setText(pixelviewReceiveIntent ? QStringLiteral("Stop receiving") : QStringLiteral("Start receiving"));
	pixelviewReceiveButton->setEnabled(!isClosing() && !pixelviewShutdownPending && (pixelviewReceiveIntent || !busy));
}
