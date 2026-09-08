// Pixelview Desktop protocol v1: native controls, memory-only ingest service.
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>

void OBSBasic::InitPixelviewDesktop(QWidget *sidebar)
{
 pixelviewClock.start();
 pixelviewLease.monotonic=[this]{return pixelviewClock.elapsed();};
 pixelviewDesktop=std::make_unique<pixelview::DesktopConnection>();
 auto heading=pixelviewPairingHeading=new QLabel(QStringLiteral("Pairing / Connection"),sidebar);
 heading->setStyleSheet(QStringLiteral("font-weight: 600;")); sidebar->layout()->addWidget(heading);
 auto *pairingRow=new QWidget(sidebar);
 auto *identityRow=new QHBoxLayout(pairingRow); identityRow->setContentsMargins(0,0,0,0); identityRow->setSpacing(8);
 sidebar->layout()->addWidget(pairingRow);
 pixelviewIdentityStatus=new QLabel(QStringLiteral("Not paired"),pairingRow);
 pixelviewIdentityStatus->setObjectName(QStringLiteral("pixelviewIdentityStatus"));
 pixelviewIdentityStatus->setWordWrap(true); pixelviewIdentityStatus->setTextFormat(Qt::PlainText);
 identityRow->addWidget(pixelviewIdentityStatus,1);
 auto unpair=pixelviewUnpair=new QPushButton(QStringLiteral("Unpair"),pairingRow);
 unpair->setFlat(true);
 unpair->setStyleSheet(QStringLiteral("QPushButton { border: none; background: transparent; min-height: 20px; font-size: 11px; padding: 0; }"));
 unpair->setObjectName(QStringLiteral("pixelviewUnpair")); identityRow->addWidget(unpair,0,Qt::AlignVCenter);
 connect(unpair,&QPushButton::clicked,this,[this]{
  if(PixelviewPairingBusy()) return;
  pixelviewUnpairPending=true;
  pixelviewLease.fail("Stopping before unpairing…");
 });
 auto pair=pixelviewPair=new QPushButton(QStringLiteral("Pair with Pixelview"),sidebar);
 pair->setObjectName(QStringLiteral("pixelviewPair")); sidebar->layout()->addWidget(pair);
 pixelviewConnectionStatus=new QLabel(QStringLiteral("Not paired"),sidebar);
 pixelviewConnectionStatus->setWordWrap(true); sidebar->layout()->addWidget(pixelviewConnectionStatus);
 connect(pair,&QPushButton::clicked,this,&OBSBasic::PairPixelviewDesktop);
 pixelviewDesktop->status=[this](QString message){pixelviewConnectionStatus->setText(message);};
 pixelviewLease.error=[this](QString message){pixelviewConnectionStatus->setText(message);RefreshPixelviewReconnect();};
 pixelviewLease.send=[this](QJsonObject o){pixelviewDesktop->sendSocket(QJsonDocument(o).toJson(QJsonDocument::Compact));};
 pixelviewLease.halt=[this]{
  pixelviewStartPermit=false; pixelviewActualStreaming=false;
  if(!pixelviewLease.intent) ui->statusbar->clearMessage();
  pixelviewReconnectAt=0;
  if(pixelviewLease.transientFailure && !pixelviewLease.intent) {
   pixelviewReconnectAt=pixelviewClock.elapsed()+pixelviewBackoff;
   pixelviewBackoff=std::min(pixelviewBackoff*2,30000);
  }
  const bool alreadyStopping=pixelviewStopPending;
  pixelviewStopPending=true;
  if(!alreadyStopping) ++pixelviewStopGeneration;
  if(!pixelviewLease.ready) {
   pixelviewClosingSocket=true; pixelviewHeartbeat->stop(); pixelviewAuthDeadline=0;
  }
  // Never force-stop an already completed/unused output: libobs resets its stop event.
  // Pending setup has not started media and is invalidated by the generation guard.
  if(!alreadyStopping && pixelviewNativeAttempt && outputHandler && outputHandler->streamOutput &&
     (obs_output_active(outputHandler->streamOutput) || pixelviewStreamingBusy)) obs_output_force_stop(outputHandler->streamOutput);
  QueuePixelviewOutputStopped();
 };
 pixelviewLease.publish=[this](QString endpoint,QString bearer){
  if(!PixelviewLeaseValid() || PixelviewSettingsBusy()) {pixelviewLease.fail("Cannot start while native output is busy.");return;}
  pixelviewConnectionStatus->setText(QStringLiteral("Preparing WHIP stream…"));
  OBSDataAutoRelease settings=obs_data_create();
  obs_data_set_string(settings,"server",endpoint.toUtf8().constData());
  obs_data_set_string(settings,"bearer_token",bearer.toUtf8().constData());
  OBSServiceAutoRelease whip=obs_service_create("whip_custom","Pixelview leased WHIP",settings,nullptr);
  if(!whip) {pixelviewLease.fail("WHIP output is unavailable in this build. SRT is not supported.");return;}
  pixelviewPreviousService=service; service=whip;
  pixelviewStartPermit=true; StartStreaming();
 };
 pixelviewDesktop->message=[this](QByteArray body){
  if(isClosing() || pixelviewShutdownPending || pixelviewUnpairPending) return;
  QJsonParseError error; auto doc=QJsonDocument::fromJson(body,&error);
  if(body.size()>16384 || error.error!=QJsonParseError::NoError || !doc.isObject()) {pixelviewLease.fail("Invalid Pixelview message.");return;}
  if(pixelviewClosingSocket) {
   // Ignore stale grants/readiness, but never ignore a queued terminal denial.
   if(doc.object()["type"]=="error") pixelviewLease.receive(doc.object(),pixelviewClock.elapsed());
   return;
  }
  auto candidate=pixelviewIdentity;
  if(candidate.nodeId.isEmpty()) candidate=pixelviewExpectedIdentity;
  if(doc.object()["type"]=="ready" && !candidate.accept(doc.object())) {
   pixelviewUnpairRetry=!pixelviewPairingDurable;
   pixelviewLease.fail("Paired identity mismatch. Unpair and pair again.");return;
  }
  const bool wasReady=pixelviewLease.ready;
  pixelviewLease.receive(doc.object(),pixelviewClock.elapsed());
  if(!wasReady && pixelviewLease.ready) {
   pixelviewIdentity=candidate;
   pixelviewPairingDurable=SavePixelviewIdentity();
   if(!pixelviewPairingDurable) {pixelviewUnpairRetry=true;pixelviewLease.fail("Could not save pairing identity. Retry cleanup, then pair again.");return;}
   pixelviewExpectedIdentity.clear();pixelviewUnpairRetry=false;
   pixelviewAuthDeadline=0; pixelviewReconnectAt=0;
   pixelviewBackoff=1000;
   pixelviewConnectionStatus->setText(pixelviewLease.pending ? QStringLiteral("Requesting exclusive WHIP lease…") : QStringLiteral("Connected · WHIP ready to request"));
   pixelviewHeartbeat->start();
  }
  // Keep the media failure detail available after release (also in the node tooltip).
  RefreshPixelviewReconnect();
 };
 pixelviewDesktop->disconnected=[this](int code){
  if(isClosing() || pixelviewShutdownPending || pixelviewUnpairPending) return;
  const bool transient=pixelview::Desktop::transientClose(code);
  // A definitive close (especially 4401) can follow NSURLSession's generic error.
  if(pixelviewClosingSocket && transient) return;
  pixelviewLease.fail(code==4401 ? "Device revoked. Pair again in Pixelview admin." : "Pixelview control disconnected. Stream stopped.",transient);

  if(code==4401) {pixelviewUnpairPending=true;pixelviewIdentity.clear();SavePixelviewIdentity();pixelviewReconnectAt=0;}
 };
 pixelviewDesktop->paired=[this]{
  if(isClosing() || pixelviewShutdownPending) return;
  pixelviewOrigin=pixelviewDesktop->origin; pixelviewDev=pixelviewDesktop->development;
  config_set_bool(App()->GetUserConfig(),"PixelviewDesktop","CleanupComplete",false);
  config_set_bool(App()->GetUserConfig(),"PixelviewDesktop","PairingDisabled",false);
  pixelviewIdentity.clear(); pixelviewPairingDurable=false;
  pixelviewExpectedIdentity=pixelviewDesktop->identity;
  config_set_string(App()->GetUserConfig(),"PixelviewDesktop","Origin",pixelviewOrigin.toString().toUtf8().constData());
  config_set_bool(App()->GetUserConfig(),"PixelviewDesktop","LocalDevelopment",pixelviewDev);
  SavePixelviewIdentity();
  ConnectPixelviewDesktop();
 };
 pixelviewHeartbeat=new QTimer(this); pixelviewHeartbeat->setInterval(15000);
 connect(pixelviewHeartbeat,&QTimer::timeout,this,[this]{
  if(isClosing() || pixelviewShutdownPending) return;
  if(pixelviewLease.ready && pixelviewLease.heartbeatSent(pixelviewClock.elapsed()))
   pixelviewLease.send({{"type","heartbeat"},{"streaming",pixelviewActualStreaming && PixelviewLeaseValid()}, {"settings",PixelviewReportedSettings()}});
 });
 pixelviewWatchdog=new QTimer(this); pixelviewWatchdog->setInterval(100);
 connect(pixelviewWatchdog,&QTimer::timeout,this,[this]{
  if(isClosing() || pixelviewShutdownPending) {pixelviewWatchdog->stop();pixelviewHeartbeat->stop();return;}
  RefreshPixelviewPairing();
  auto now=pixelviewClock.elapsed(); pixelviewLease.tick(now);
  if(pixelviewAuthDeadline && now>=pixelviewAuthDeadline) pixelviewLease.fail("Pixelview authentication timed out.",true);
  const bool drained=!pixelviewStopPending && !pixelviewClosingSocket && !PixelviewSettingsBusy();
  if(pixelviewLease.takeRetry(now,drained)) ConnectPixelviewDesktop();
  else if(pixelviewReconnectAt && now>=pixelviewReconnectAt && drained) {pixelviewReconnectAt=0;ConnectPixelviewDesktop();}
  RefreshPixelviewReconnect();
 }); pixelviewWatchdog->start();
 connect(this,&OBSBasic::StreamingStarted,this,[this](bool withDelay){
  if(!PixelviewLeaseValid()) {pixelviewLease.fail("Lease lost before output started.");return;}
  if(withDelay) {pixelviewConnectionStatus->setText(QStringLiteral("Starting WHIP stream (delay)…"));return;}
  pixelviewActualStreaming=true;
  pixelviewLease.outputStarted();
  pixelviewConnectionStatus->setText(QStringLiteral("Streaming"));
  ui->statusbar->clearMessage();
 });
 pixelviewOrigin=QUrl(QString::fromUtf8(config_get_string(App()->GetUserConfig(),"PixelviewDesktop","Origin")));
 pixelviewDev=config_get_bool(App()->GetUserConfig(),"PixelviewDesktop","LocalDevelopment");
 pixelviewIdentity.accept({{"node_id",QString::fromUtf8(config_get_string(App()->GetUserConfig(),"PixelviewDesktop","NodeId"))},
  {"desktop_id",QString::fromUtf8(config_get_string(App()->GetUserConfig(),"PixelviewDesktop","DesktopId"))}});
 pixelviewUnpairRetry=config_get_bool(App()->GetUserConfig(),"PixelviewDesktop","PairingDisabled") &&
  !config_get_bool(App()->GetUserConfig(),"PixelviewDesktop","CleanupComplete");
 pixelviewPairingDurable=!pixelviewIdentity.nodeId.isEmpty() && !pixelviewIdentity.desktopId.isEmpty() &&
  !config_get_bool(App()->GetUserConfig(),"PixelviewDesktop","PairingDisabled");
 RefreshPixelviewPairing();
 if(pixelview::Desktop::validOrigin(pixelviewOrigin,pixelviewDev)) ConnectPixelviewDesktop();
}

bool OBSBasic::PixelviewConfigurationLocked() const
{
 return pixelviewReceiving || !pixelviewPairingDurable || pixelviewLease.intent || PixelviewSettingsBusy();
}

bool OBSBasic::PixelviewPairingBusy()
{
 return isClosing() || pixelviewShutdownPending || pixelviewUnpairPending || pixelviewClosingSocket ||
  pixelviewStopPending || pixelviewLease.intent || pixelviewLease.pending || pixelviewLease.leased ||
  PixelviewSettingsBusy() || !pixelviewDesktop || pixelviewDesktop->exchanging;
}

void OBSBasic::RefreshPixelviewPairing()
{
 if(!pixelviewPair) return;
 const bool paired=pixelviewPairingDurable;
 const bool busy=PixelviewPairingBusy();
 pixelviewPairingHeading->setVisible(!paired);
 pixelviewPair->setVisible(!paired);
 pixelviewPair->setEnabled(!paired && !busy && !pixelviewUnpairRetry);
 pixelviewUnpair->setVisible(paired || pixelviewUnpairRetry);
 pixelviewUnpair->setText(pixelviewUnpairRetry ? QStringLiteral("Retry cleanup") : QStringLiteral("Unpair"));
 pixelviewUnpair->setEnabled(!busy);
 const bool ordinaryConnected=pixelviewLease.ready && !pixelviewLease.pending && (!pixelviewLease.intent || pixelviewActualStreaming);
 pixelviewConnectionStatus->setVisible(!paired || !ordinaryConnected);
 pixelviewIdentityStatus->setToolTip(pixelviewConnectionStatus->text());
 pixelviewIdentityStatus->setText(paired ? QStringLiteral("Node %1 · %2").arg(pixelviewIdentity.nodeId,
  pixelviewLease.ready ? QStringLiteral("Connected") : pixelviewLease.intent ? QStringLiteral("Reconnecting…") : QStringLiteral("Offline")) :
  QStringLiteral("Pair first to unlock capture, audio and stream settings."));
 // Mid is a background/shading role (equal to Window in Yami), not readable text.
 pixelviewIdentityStatus->setStyleSheet(paired && pixelviewLease.ready ? QStringLiteral("color: #68b984;") : QStringLiteral("color: palette(window-text);"));
}

void OBSBasic::PairPixelviewDesktop()
{
 if(pixelviewPairingDurable || pixelviewUnpairRetry || PixelviewPairingBusy()) return;
 QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("Pair with Pixelview"));
 QFormLayout form(&dialog);
 QLineEdit origin(pixelviewOrigin.toString(),&dialog); origin.setPlaceholderText(QStringLiteral("https://your-backend-origin"));
 QLineEdit code(&dialog); code.setEchoMode(QLineEdit::Password); code.setPlaceholderText(QStringLiteral("One-time admin pairing code"));
 QCheckBox dev(QStringLiteral("Local development (allow HTTP only on loopback)"),&dialog); dev.setChecked(pixelviewDev);
 form.addRow(QStringLiteral("Backend origin"),&origin);form.addRow(QStringLiteral("Pairing code"),&code);form.addRow(&dev);
 QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form.addRow(&buttons);
 connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
 if(dialog.exec()!=QDialog::Accepted) return;
 if(pixelviewPairingDurable || pixelviewUnpairRetry || PixelviewPairingBusy()) return;
 QUrl requested(origin.text());requested.setPath("");
 if(requested!=pixelviewOrigin && !pixelviewOrigin.isEmpty() && !config_get_bool(App()->GetUserConfig(),"PixelviewDesktop","CleanupComplete")) {
  pixelviewConnectionStatus->setText(QStringLiteral("Unpair this installation before changing backend origin."));return;
 }
 pixelviewDesktop->closeSocket();pixelviewLease.ready=false;pixelviewHeartbeat->stop();pixelviewReconnectAt=0;pixelviewAuthDeadline=0;
 pixelviewDesktop->pair(QUrl(origin.text()),dev.isChecked(),code.text()); code.clear();
}
void OBSBasic::ConnectPixelviewDesktop()
{
 if(isClosing() || pixelviewShutdownPending || pixelviewStopPending || pixelviewClosingSocket || pixelviewUnpairPending || config_get_bool(App()->GetUserConfig(),"PixelviewDesktop","PairingDisabled") || !pixelview::Desktop::validOrigin(pixelviewOrigin,pixelviewDev)) return;
 QString token=pixelview::loadDevice(pixelviewOrigin.toString());
 if(token.isEmpty()) {pixelviewLease.fail("Pair with Pixelview to connect");pixelviewIdentity.clear();SavePixelviewIdentity();return;}
 pixelviewLease.ready=false;pixelviewLease.pending=false;pixelviewLease.leased=false;pixelviewLease.stopping=false;pixelviewLease.heartbeatRequest=-1;pixelviewLease.development=pixelviewDev;
 QUrl ws=pixelviewOrigin;ws.setScheme(ws.scheme()=="https" ? "wss":"ws");ws.setPath("/desktop/ws");
 pixelviewAuthDeadline=pixelviewClock.elapsed()+10000;
 pixelviewDesktop->openSocket(ws,token);
}
bool OBSBasic::PixelviewLeaseValid() const {return pixelviewDesktop && !pixelviewShutdownPending && !pixelviewUnpairPending && pixelviewLease.authorized(pixelviewClock.elapsed());}
bool OBSBasic::RequestPixelviewStart()
{
 if(pixelviewReceiving || isClosing() || pixelviewShutdownPending || pixelviewStopPending || pixelviewClosingSocket || pixelviewUnpairPending) return false;
 if(pixelviewStartPermit) {pixelviewStartPermit=false;return PixelviewLeaseValid();}
 if(!pixelviewPairingDurable || pixelviewLease.intent || PixelviewSettingsBusy()) return false;
 pixelviewLease.reconnect=config_get_bool(activeConfiguration, "Output", "Reconnect");
 pixelviewLease.retryDelay=std::max(0,(int)config_get_int(activeConfiguration, "Output", "RetryDelay"));
 pixelviewLease.maxRetries=std::max(0,(int)config_get_int(activeConfiguration, "Output", "MaxRetries"));
 if(pixelviewDesktop && pixelviewLease.requestStart(pixelviewClock.elapsed())) pixelviewConnectionStatus->setText(QStringLiteral("Requesting exclusive WHIP lease…"));
 else if(pixelviewConnectionStatus) pixelviewConnectionStatus->setText(QStringLiteral("Connect/pair first, or wait for the pending lease request."));
 RefreshPixelviewReconnect();
 return false;
}
void OBSBasic::PixelviewOutputStopped()
{
 if(!pixelviewStopPending) return;
 if(outputHandler && outputHandler->streamOutput && obs_output_active(outputHandler->streamOutput)) {
  QueuePixelviewOutputStopped(100);return;
 }
 if(pixelviewNativeAttempt || (setupStreamingGuard.valid() && setupStreamingGuard.wait_for(std::chrono::seconds(0))!=std::future_status::ready)) {
  QueuePixelviewOutputStopped(100);return;
 }
 pixelviewStopPending=false;
 pixelviewActualStreaming=false;pixelviewStartPermit=false;
 if(!pixelviewLease.intent) pixelviewLease.outputStopped();
 if(pixelviewStreamingBusy) {pixelviewStreamingBusy=false; emit StreamingStopped();}
 if(pixelviewUnpairPending) FinishPixelviewUnpair();
 if(pixelviewClosingSocket) {pixelviewDesktop->closeSocket();pixelviewClosingSocket=false;}
 if(pixelviewPreviousService) {service=pixelviewPreviousService;pixelviewPreviousService=nullptr;}
 RefreshPixelviewReconnect();
}

void OBSBasic::QueuePixelviewOutputStopped(int delay)
{
 const auto generation=pixelviewStopGeneration;
 QTimer::singleShot(delay,this,[this,generation]{
  if(pixelviewStopPending && generation==pixelviewStopGeneration) PixelviewOutputStopped();
 });
}

void OBSBasic::CancelPixelviewStart()
{
 if(!pixelviewDesktop) return;
 pixelviewLease.fail("Stream stopped.");
 // Restore idle control connectivity only; Stop has already erased streaming intent.
 if(!isClosing() && !pixelviewShutdownPending && !pixelviewUnpairPending) pixelviewReconnectAt=pixelviewClock.elapsed()+1000;
}

void OBSBasic::RefreshPixelviewReconnect()
{
 if(isClosing()) return;
 RefreshPixelviewPairing();
 const bool recovering=pixelviewLease.intent && pixelviewLease.retries>0;
 if(recovering) {
  const auto seconds=std::max<qint64>(0,(pixelviewLease.retryAt-pixelviewClock.elapsed()+999)/1000);
  const QString message=QStringLiteral("Reconnecting %1/%2 · %3").arg(pixelviewLease.retries).arg(pixelviewLease.maxRetries)
   .arg(pixelviewLease.retryAt>=0 ? QStringLiteral("retry in %1 s").arg(seconds) : pixelviewLease.pending ? QStringLiteral("requesting fresh WHIP lease") : QStringLiteral("authenticating / preparing"));
  pixelviewConnectionStatus->setText(message);
  ui->statusbar->showMessage(message);
 }
 // Cancellation is terminal immediately, even while setup/native completion is outstanding.
 if(pixelviewShutdownPending || (!pixelviewLease.intent && (pixelviewStopPending || pixelviewLease.stopping))) {
  if(auto *button=findChild<QPushButton *>(QStringLiteral("streamButton"))) {
   button->setMenu(nullptr);button->setEnabled(false);button->setText(QTStr("Basic.Main.StoppingStreaming"));
  }
  if(sysTrayStream) {sysTrayStream->setEnabled(false);sysTrayStream->setText(QTStr("Basic.Main.StoppingStreaming"));}
  return;
 }
 if(!pixelviewLease.intent && !pixelviewNativeAttempt && sysTrayStream) {
  sysTrayStream->setEnabled(pixelviewPairingDurable && pixelviewLease.ready);sysTrayStream->setText(QTStr("Basic.Main.StartStreaming"));
 }
 // The existing native control remains the sole Start/Stop action, including setup.
 if(auto *button=findChild<QPushButton *>(QStringLiteral("streamButton"))) {
  if(pixelviewLease.intent && !pixelviewActualStreaming) {
   if(!pixelviewNativeAttempt) button->setMenu(nullptr);
   button->setEnabled(true);
   button->setText(recovering ? QStringLiteral("Stop (Reconnecting %1/%2)").arg(pixelviewLease.retries).arg(pixelviewLease.maxRetries) : QTStr("Basic.Main.StopStreaming"));
  } else if(!pixelviewNativeAttempt && !pixelviewStopPending) {
   button->setMenu(nullptr);button->setEnabled(pixelviewPairingDurable && pixelviewLease.ready);button->setText(QTStr("Basic.Main.StartStreaming"));
  }
 }
}
bool OBSBasic::SavePixelviewIdentity()
{
 if(pixelviewIdentity.nodeId.isEmpty() || pixelviewIdentity.desktopId.isEmpty()) pixelviewPairingDurable=false;
 config_set_string(App()->GetUserConfig(),"PixelviewDesktop","NodeId",pixelviewIdentity.nodeId.toUtf8().constData());
 config_set_string(App()->GetUserConfig(),"PixelviewDesktop","DesktopId",pixelviewIdentity.desktopId.toUtf8().constData());
 const bool saved=config_save_safe(App()->GetUserConfig(),"tmp",nullptr)==CONFIG_SUCCESS;
 if(!saved)
  pixelviewConnectionStatus->setText(QStringLiteral("Could not save pairing identity."));
 RefreshPixelviewPairing();
 return saved;
}
void OBSBasic::FinishPixelviewUnpair()
{
 pixelviewHeartbeat->stop();pixelviewReconnectAt=0;pixelviewAuthDeadline=0;
 pixelviewDesktop->closeSocket();pixelviewLease.ready=false;
 config_set_bool(App()->GetUserConfig(),"PixelviewDesktop","PairingDisabled",true);
 pixelviewIdentity.clear();pixelviewExpectedIdentity.clear();
 bool removed=pixelview::removeDevice(pixelviewOrigin.toString());
 config_set_bool(App()->GetUserConfig(),"PixelviewDesktop","CleanupComplete",removed);
 bool saved=SavePixelviewIdentity();
 pixelviewUnpairRetry=!saved || !removed;
 if(!saved) {
  pixelviewConnectionStatus->setText(QStringLiteral("Unpair incomplete: identity could not be saved. Do not restart; retry Unpair."));
 } else if(!removed) {
  pixelviewConnectionStatus->setText(QStringLiteral("Keychain removal failed. Unlock Keychain and retry Unpair."));
 } else pixelviewConnectionStatus->setText(QStringLiteral("Not paired. This installation only; admin registration is not revoked."));
 pixelviewUnpairPending=false;
 RefreshPixelviewPairing();
}
QJsonObject OBSBasic::PixelviewReportedSettings()
{
 obs_video_info video={}; obs_get_video_info(&video);
 const char *id=config_get_string(activeConfiguration,"AdvOut","Encoder");
 OBSData data=PixelviewEncoderData(GetCurrentProfile().path/"streamEncoder.json",id,false);
 QString encoder=pixelview::isHardwareEncoder(id) ? QStringLiteral("hardware"):QStringLiteral("software");
 if(encoder=="hardware" && QString::fromUtf8(id).startsWith("com.apple.") && strcmp(obs_get_encoder_codec(id),"hevc")==0) encoder="apple_hevc";
 // Allowlist only nonsecret native options, never arbitrary encoder free-form text.
 QJsonObject options{{"keyint_sec",(double)obs_data_get_int(data,"keyint_sec")},{"bframes",false}};
 return {{"resolution",QJsonObject{{"width",(int)video.output_width},{"height",(int)video.output_height}}},
  {"framerate",video.fps_den ? (double)video.fps_num/video.fps_den:0}, {"encoder",encoder},
  {"bitrate",(double)obs_data_get_int(data,"bitrate")*1000}, {"encoder_profile",QString::fromUtf8(obs_data_get_string(data,"profile"))},{"encoder_settings",options}};
}
