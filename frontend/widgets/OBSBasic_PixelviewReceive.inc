// Pixelview modification: one native canvas, mutually exclusive send/receive controls.
#include <QTabBar>
#include <QLineEdit>
#include <QSysInfo>
#include <obs-module.h>

bool OBSBasic::PixelviewModeBusy() const
{
	return isClosing() || pixelviewShutdownPending || pixelviewReceiveIntent || PixelviewSettingsBusy() ||
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
	form->addRow(QStringLiteral("Session ID"), pixelviewReceiveId);
	form->addRow(QStringLiteral("Password"), pixelviewReceivePassword);
	form->addRow(QStringLiteral("Receiver name"), pixelviewReceiveName);
	rows->addLayout(form);
	pixelviewReceiveStatus = new QLabel(QStringLiteral("Enter session credentials to receive."), pixelviewReceivingPanel);
	pixelviewReceiveStatus->setTextFormat(Qt::PlainText);
	pixelviewReceiveStatus->setWordWrap(true);
	pixelviewReceiveStatus->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	rows->addWidget(pixelviewReceiveStatus);
	// Like Sending's source settings, hardware follows session setup, never transport.
	auto *hardwareDivider = new QFrame(pixelviewReceivingPanel);
	hardwareDivider->setFrameShape(QFrame::HLine); rows->addWidget(hardwareDivider);
	auto *outputHint = new QLabel(QStringLiteral("DeckLink output is optional. Receive on the canvas without output hardware."), pixelviewReceivingPanel);
	outputHint->setWordWrap(true);
	outputHint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	rows->addWidget(outputHint);
	// Find the module's translated action in the retained Tools menu; never reimplement SDI output.
	auto *decklink = new QPushButton(QStringLiteral("DeckLink output settings…"), pixelviewReceivingPanel);
	decklink->setObjectName(QStringLiteral("pixelviewDecklinkOutputSettings"));
	decklink->ensurePolished(); decklink->setFixedHeight(36);
	rows->addWidget(decklink);
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
	rows->addStretch(1);
	auto *separator = new QFrame(pixelviewReceivingPanel); separator->setFrameShape(QFrame::HLine); rows->addWidget(separator);
	pixelviewReceiveButton = new QPushButton(QStringLiteral("Start receiving"), pixelviewReceivingPanel);
	pixelviewReceiveButton->setObjectName(QStringLiteral("pixelviewReceiveButton"));
	pixelviewReceiveButton->ensurePolished(); pixelviewReceiveButton->setFixedHeight(36);
	rows->addWidget(pixelviewReceiveButton);
	connect(pixelviewReceiveButton, &QPushButton::clicked, this, [this] {
		if (pixelviewReceiveIntent) StopPixelviewReceive(true); else StartPixelviewReceive();
	});
	pixelviewReceiver = std::make_unique<pixelview::PixelviewReceiver>();
	pixelviewReceiver->onStopped = [this] { DisconnectPixelviewReceiveMedia(); };
	pixelviewReceiver->onChanged = [this] {
		if (isClosing() || pixelviewShutdownPending) return;
		using State = pixelview::PixelviewReceiver::State;
		if (pixelviewReceiver->state() == State::Idle || pixelviewReceiver->state() == State::Error) pixelviewReceiveIntent = false;
		pixelviewReceiveStatus->setText(pixelviewReceiver->status());
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
		if (!pixelviewReceiveSource) { StopPixelviewReceive(true); pixelviewReceiveStatus->setText(QStringLiteral("Native WHEP receive module unavailable.")); return; }
		calldata_t data; calldata_init(&data);
		calldata_set_string(&data, "endpoint", endpoint.toUtf8().constData());
		calldata_set_int(&data, "latency", 50);
		const bool accepted = proc_handler_call(obs_source_get_proc_handler(pixelviewReceiveSource), "connect", &data);
		calldata_free(&data);
		if (!accepted) { StopPixelviewReceive(true); pixelviewReceiveStatus->setText(QStringLiteral("Native receive connection unavailable.")); return; }
		pixelviewReceiveStatus->setText(QStringLiteral("Authorized · connecting media…")); RefreshPixelviewAudio();
	};
	pixelviewReceiveTimer = new QTimer(this);
	pixelviewReceiveTimer->setInterval(500);
	connect(pixelviewReceiveTimer, &QTimer::timeout, this, [this] {
		if (!pixelviewReceiveIntent || !pixelviewReceiveSource || isClosing() || pixelviewShutdownPending) return;
		calldata_t data; calldata_init(&data);
		if (proc_handler_call(obs_source_get_proc_handler(pixelviewReceiveSource), "get_status", &data)) {
			// Only fixed local labels: never render plugin errors or credential-bearing URLs.
			const char *state = calldata_string(&data, "state");
			if (state && strcmp(state, "playing") == 0) pixelviewReceiveStatus->setText(QStringLiteral("Receiving"));
			else if (state && (strcmp(state, "error") == 0 || strcmp(state, "ended") == 0)) { StopPixelviewReceive(true); pixelviewReceiveStatus->setText(QStringLiteral("Receive media failed. Check connection and retry.")); }
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

	RefreshPixelviewModes();
}

void OBSBasic::SelectPixelviewMode(int index)
{
	if (index < 0 || index > 1 || PixelviewModeBusy()) { RefreshPixelviewModes(); return; }
	if (pixelviewReceiving == (index == 1)) return;
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
	RefreshPixelviewDevices();
}

void OBSBasic::StartPixelviewReceive()
{
	if (!pixelviewReceiving || !pixelviewReceiver || PixelviewModeBusy()) return;
	const QString id = pixelviewReceiveId->text().trimmed(), name = pixelviewReceiveName->text().trimmed();
	if (id.isEmpty() || name.isEmpty()) { pixelviewReceiveStatus->setText(QStringLiteral("Enter a session ID and receiver name.")); return; }
	if (!pixelviewOrigin.isEmpty() && !pixelviewReceiver->setOrigin(pixelviewOrigin, pixelviewDev)) { pixelviewReceiveStatus->setText(QStringLiteral("Invalid backend origin.")); return; }
	auto *config = App()->GetUserConfig();
	config_set_string(config, "PixelviewReceive", "SessionId", id.toUtf8().constData());
	config_set_string(config, "PixelviewReceive", "Name", name.toUtf8().constData());
	if (config_save_safe(config, "tmp", nullptr) != CONFIG_SUCCESS) { pixelviewReceiveStatus->setText(QStringLiteral("Could not save receiver ID/name. Check configuration permissions.")); return; }
	pixelviewReceiveIntent = true;
	QString password = pixelviewReceivePassword->text(); pixelviewReceivePassword->clear();
	pixelviewReceiver->start(id, password, name); password.clear();
	pixelviewReceiveTimer->start(); RefreshPixelviewModes();
}

void OBSBasic::DisconnectPixelviewReceiveMedia()
{
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
	pixelviewReceiveIntent = false;
	if (pixelviewReceiveTimer) pixelviewReceiveTimer->stop();
	if (pixelviewReceivePassword) pixelviewReceivePassword->clear();
	if (pixelviewReceiver) pixelviewReceiver->stop();
	DisconnectPixelviewReceiveMedia();
	// No-argument callers are native close/shutdown. UI Stop/errors retain the
	// receive canvas and its editing lock so hidden sender framing stays protected.
	if (!keepReceiving && pixelviewReceiving) {
		obs_set_output_source(0, pixelviewSendOutput);
		pixelviewSendOutput = nullptr;
		ui->preview->SetLocked(pixelviewSendPreviewLocked);
		pixelviewReceiving = false;
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
	pixelviewSendingPanel->setVisible(!pixelviewReceiving);
	pixelviewReceivingPanel->setVisible(pixelviewReceiving);
	pixelviewReceiveId->setEnabled(!busy);
	pixelviewReceivePassword->setEnabled(!busy);
	pixelviewReceiveName->setEnabled(!busy);
	pixelviewReceiveButton->setText(pixelviewReceiveIntent ? QStringLiteral("Stop receiving") : QStringLiteral("Start receiving"));
	pixelviewReceiveButton->setEnabled(!isClosing() && !pixelviewShutdownPending && (pixelviewReceiveIntent || !busy));
}
