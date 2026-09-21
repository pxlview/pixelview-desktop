// Pixelview Desktop control socket: native controls, memory-only ingest service.
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <utility/PixelviewBackend.hpp>

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
 // Every pairing/connection outcome shown on screen is also in the application log.
 pixelview::setKeychainLog([](const char *line){blog(LOG_WARNING,"%s",line);});
 pixelviewDesktop->log=[](QString message){blog(LOG_INFO,"Pixelview: %s",message.toUtf8().constData());};
 pixelviewDesktop->status=[this](QString message){
  blog(LOG_INFO,"Pixelview: pairing status: %s",message.toUtf8().constData());
  if(!isClosing() && !pixelviewShutdownPending) pixelviewConnectionStatus->setText(message);
 };
 pixelviewLease.error=[this](QString message){
  blog(LOG_WARNING,"Pixelview: control connection: %s",message.toUtf8().constData());
  pixelviewConnectionStatus->setText(message);RefreshPixelviewReconnect();
 };
 pixelviewLease.send=[this](QJsonObject o){pixelviewDesktop->sendSocket(QJsonDocument(o).toJson(QJsonDocument::Compact));};
 pixelviewLease.report=[this]{
  // Exactly the documented pong shape; anything the server cannot validate
  // would be read as a bare pong and hide an active stream.
  const bool streaming=pixelviewActualStreaming && PixelviewLeaseValid();
  QJsonObject settings=PixelviewReportedSettings();
  const auto resolution=settings["resolution"].toObject();
  const bool valid=resolution["width"].toInt()>0 && resolution["height"].toInt()>0 && settings["framerate"].toDouble()>0 &&
   settings["bitrate"].toDouble()>=1 && !settings["encoder_profile"].toString().isEmpty();
  return QJsonObject{{"streaming",streaming},{"settings",valid ? QJsonValue(settings) : QJsonValue::Null}};
 };
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
   pixelviewClosingSocket=true; pixelviewAuthDeadline=0;
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
  // Engines behind the backend's /ingress proxy name the backend origin in their WHIP Location.
  obs_data_set_string(settings,"pixelview_bearer_issuer_origin",pixelviewOrigin.toString().toUtf8().constData());
  OBSServiceAutoRelease whip=obs_service_create("whip_custom","Pixelview leased WHIP",settings,nullptr);
  if(!whip) {pixelviewLease.fail("WHIP output is unavailable in this build. SRT is not supported.");return;}
  pixelviewPreviousService=service; service=whip;
  pixelviewStartPermit=true; StartStreaming();
 };
 pixelviewDesktop->message=[this](QByteArray body){
  if(isClosing() || pixelviewShutdownPending || pixelviewUnpairPending) return;
  QJsonParseError error; auto doc=QJsonDocument::fromJson(body,&error);
  if(body.size()>16384 || error.error!=QJsonParseError::NoError || !doc.isObject()) {pixelviewLease.fail("Invalid Pixelview message.");return;}
  const QString mutation=doc.object()["mutation"].toString();
  if(pixelviewClosingSocket) {
   // Ignore stale grants/readiness, but never ignore a queued denial, revocation or replacement.
   if(mutation=="DESKTOP_ERROR" || mutation=="SOCKET_DESKTOP_REVOKED" || mutation=="SOCKET_DESKTOP_REPLACED") {
    pixelviewLease.receive(doc.object(),pixelviewClock.elapsed());
    if(pixelviewLease.revoked) PixelviewDeviceRevoked();
    if(pixelviewLease.replaced) pixelviewReconnectAt=0;
   }
   return;
  }
  auto candidate=pixelviewIdentity;
  if(candidate.nodeId.isEmpty()) candidate=pixelviewExpectedIdentity;
  if(mutation=="DESKTOP_READY" && !candidate.accept(doc.object()["data"].toObject())) {
   pixelviewUnpairRetry=!pixelviewPairingDurable;
   pixelviewLease.fail("Paired identity mismatch. Unpair and pair again.");return;
  }
  const bool wasReady=pixelviewLease.ready;
  pixelviewLease.receive(doc.object(),pixelviewClock.elapsed());
  if(!wasReady && pixelviewLease.ready) {
   pixelviewIdentity=candidate;
   pixelviewPairingDurable=SavePixelviewIdentity();
   if(!pixelviewPairingDurable) {pixelviewUnpairRetry=true;pixelviewLease.fail("Could not save pairing identity. Click Unpair before pairing again.");return;}
   pixelviewExpectedIdentity.clear();pixelviewUnpairRetry=false;
   pixelviewAuthDeadline=0; pixelviewReconnectAt=0;
   pixelviewBackoff=1000;
   blog(LOG_INFO,"Pixelview: control socket ready (node %s, desktop %s)",pixelviewIdentity.nodeId.toUtf8().constData(),pixelviewIdentity.desktopId.toUtf8().constData());
   pixelviewConnectionStatus->setText(pixelviewLease.pending ? QStringLiteral("Requesting stream start…") :
    pixelviewLease.started ? QStringLiteral("Control reconnected · streaming") : QStringLiteral("Connected · ready to stream"));
  }
  if(pixelviewLease.revoked) PixelviewDeviceRevoked();
  else if(pixelviewLease.replaced) {pixelviewReconnectAt=0; pixelviewAuthDeadline=0; pixelviewDesktop->closeSocket();}
  // Keep the media failure detail available after release (also in the node tooltip).
  RefreshPixelviewReconnect();
 };
 pixelviewDesktop->disconnected=[this](int code){
  if(isClosing() || pixelviewShutdownPending || pixelviewUnpairPending) return;
  // The terminal mutation before this close already decided; the code after it is not meaningful.
  if(pixelviewLease.revoked || pixelviewLease.replaced) return;
  const bool transient=pixelview::Desktop::transientClose(code);
  blog(LOG_INFO,"Pixelview: control socket closed (code %d, %s%s)",code,transient ? "transient" : "terminal",
   pixelviewActualStreaming ? ", streaming continues" : "");
  // A definitive close (especially 4401) can follow NSURLSession's generic error.
  if(pixelviewClosingSocket && transient) return;
  if(transient && pixelviewActualStreaming && pixelviewLease.controlLost()) {
   pixelviewAuthDeadline=0;
   pixelviewDesktop->closeSocket();
   pixelviewReconnectAt=pixelviewClock.elapsed()+pixelviewBackoff;
   pixelviewBackoff=std::min(pixelviewBackoff*2,30000);
   pixelviewConnectionStatus->setText(QStringLiteral("Control disconnected. Reconnecting; streaming continues."));
   return;
  }
  // A refused upgrade (HTTP 403 or a redirect, e.g. an edge rule) ends control
  // only: running media is independent of it and must not be stopped.
  if(code==4403 && pixelviewActualStreaming && pixelviewLease.controlLost()) {
   pixelviewAuthDeadline=0; pixelviewReconnectAt=0;
   pixelviewDesktop->closeSocket();
   pixelviewConnectionStatus->setText(QStringLiteral("Pixelview refused the control connection. Streaming continues; if this device was revoked, unpair and pair again after stopping."));
   RefreshPixelviewReconnect();
   return;
  }
  pixelviewLease.fail(code==4401 ? "Device revoked. Pair again in Pixelview admin." :
   code==4403 ? "Pixelview refused the control connection. If this device was revoked, unpair and pair again." :
   "Pixelview control disconnected. Stream stopped.",transient);
  if(code==4401) PixelviewDeviceRevoked();
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
 pixelviewWatchdog=new QTimer(this); pixelviewWatchdog->setInterval(100);
 connect(pixelviewWatchdog,&QTimer::timeout,this,[this]{
  if(isClosing() || pixelviewShutdownPending) {pixelviewWatchdog->stop();return;}
  RefreshPixelviewPairing();
  auto now=pixelviewClock.elapsed();
  // No DESKTOP_READY within the connect budget, or no server ping for 60 s: the
  // socket is dead. Both are transient; media keeps running through them.
  if((pixelviewAuthDeadline && now>=pixelviewAuthDeadline) || pixelviewLease.pingExpired(now)) {
   pixelviewAuthDeadline=0; pixelviewDesktop->disconnected(0);
  }
  const bool settled=!pixelviewStopPending && !pixelviewClosingSocket;
  const bool drained=settled && !PixelviewSettingsBusy();
  if(pixelviewLease.takeRetry(now,drained)) ConnectPixelviewDesktop();
  // A running stream keeps native output busy; its control-only reconnect must not wait for it.
  else if(pixelviewReconnectAt && now>=pixelviewReconnectAt && (drained || (settled && pixelviewLease.started))) {pixelviewReconnectAt=0;ConnectPixelviewDesktop();}
  RefreshPixelviewReconnect();
 }); pixelviewWatchdog->start();
 connect(this,&OBSBasic::StreamingStarted,this,[this](bool withDelay){
  if(!PixelviewLeaseValid()) {pixelviewLease.fail("Stream authority lost before output started.");return;}
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
  pixelviewStopPending || pixelviewLease.intent || pixelviewLease.pending || pixelviewLease.started ||
  PixelviewSettingsBusy() || !pixelviewDesktop || pixelviewDesktop->exchanging;
}

void OBSBasic::RefreshPixelviewPairing()
{
 if(!pixelviewPair) return;
 const bool paired=pixelviewPairingDurable;
 const bool busy=PixelviewPairingBusy();
 pixelviewPairingHeading->setVisible(!paired);
 pixelviewPair->setText(QStringLiteral("Pair with Pixelview"));
 pixelviewPair->setToolTip(paired ? QStringLiteral("Reconnect this saved pairing. macOS may request authorization; no new pairing code is needed.") : QString());
 pixelviewPair->setVisible((!paired && !pixelviewUnpairRetry) ||
  (paired && !pixelviewLease.ready && pixelviewDesktop && pixelviewDesktop->authorizedToken.isEmpty()));
 pixelviewPair->setEnabled(!busy && !pixelviewUnpairRetry);
 pixelviewUnpair->setVisible(paired || pixelviewUnpairRetry);
 pixelviewUnpair->setText(QStringLiteral("Unpair"));
 pixelviewUnpair->setEnabled(!busy);
 const bool ordinaryConnected=pixelviewLease.ready && !pixelviewLease.pending && (!pixelviewLease.intent || pixelviewActualStreaming);
 pixelviewConnectionStatus->setVisible(!paired || !ordinaryConnected);
 pixelviewIdentityStatus->setToolTip(pixelviewConnectionStatus->text());
 pixelviewIdentityStatus->setText(paired ? QStringLiteral("Node %1 · %2").arg(pixelviewIdentity.nodeId,
  pixelviewLease.ready ? QStringLiteral("Connected") : pixelviewLease.intent ? QStringLiteral("Reconnecting…") : QStringLiteral("Offline")) :
  pixelviewUnpairRetry ? QStringLiteral("Unpair required before pairing again.") :
  QStringLiteral("Pair first to unlock capture, audio and stream settings."));
 // Mid is a background/shading role (equal to Window in Yami), not readable text.
 pixelviewIdentityStatus->setStyleSheet(paired && pixelviewLease.ready ? QStringLiteral("color: #68b984;") : QStringLiteral("color: palette(window-text);"));
}

void OBSBasic::PairPixelviewDesktop()
{
 if(PixelviewPairingBusy()) return;
 if(pixelviewPairingDurable) {
  const auto origin=pixelviewOrigin;
  pixelviewDesktop->exchanging=true;
  RefreshPixelviewPairing();
  pixelview::runKeychainUserAction(this,[origin]{return pixelview::loadDevice(origin.toString());},[this,origin](QString token){
   pixelviewDesktop->exchanging=false;
   if(isClosing() || pixelviewShutdownPending || origin!=pixelviewOrigin) return;
   if(token.isEmpty()) {pixelviewConnectionStatus->setText(QStringLiteral("Keychain access was canceled or failed. Saved pairing has not been removed. Click Pair with Pixelview to reconnect without a new code."));RefreshPixelviewPairing();return;}
   pixelviewDesktop->authorizedToken=token;
   ConnectPixelviewDesktop(); RefreshPixelviewPairing();
  });
  return;
 }
 if(pixelviewUnpairRetry) return;
 QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("Pair with Pixelview"));
 QFormLayout form(&dialog);
 // Runtime opt-in affects new pairing only, never a saved origin/credential.
 const bool development=pixelview::localDevelopmentEnabled();
 QLineEdit origin(pixelview::defaultBackendOrigin(),&dialog);
 QLineEdit code(&dialog); code.setEchoMode(QLineEdit::Password); code.setPlaceholderText(QStringLiteral("One-time admin pairing code"));
 QCheckBox dev(QStringLiteral("Local development (allow HTTP only on loopback)"),&dialog); dev.setChecked(development);
 if(development) form.addRow(QStringLiteral("Backend origin"),&origin); else origin.hide();
 form.addRow(QStringLiteral("Pairing code"),&code);
 if(development) form.addRow(&dev); else dev.hide();
 QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form.addRow(&buttons);
 connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
 if(dialog.exec()!=QDialog::Accepted) return;
 if(pixelviewPairingDurable || pixelviewUnpairRetry || PixelviewPairingBusy()) return;
 QUrl requested(origin.text());requested.setPath("");
 if(requested!=pixelviewOrigin && !pixelviewOrigin.isEmpty() && !config_get_bool(App()->GetUserConfig(),"PixelviewDesktop","CleanupComplete")) {
  pixelviewConnectionStatus->setText(QStringLiteral("Unpair this installation before changing backend origin."));return;
 }
 pixelviewDesktop->closeSocket();pixelviewLease.ready=false;pixelviewReconnectAt=0;pixelviewAuthDeadline=0;
 pixelviewDesktop->pair(QUrl(origin.text()),dev.isChecked(),code.text()); code.clear();
}
void OBSBasic::ConnectPixelviewDesktop()
{
 if(!pixelviewDesktop || pixelviewDesktop->exchanging || isClosing() || pixelviewShutdownPending || pixelviewStopPending || pixelviewClosingSocket || pixelviewUnpairPending || config_get_bool(App()->GetUserConfig(),"PixelviewDesktop","PairingDisabled") || !pixelview::Desktop::validOrigin(pixelviewOrigin,pixelviewDev)) return;
 // Keep a process-only credential for this pairing; transport reconnect is not
 // a credential mutation and must not consume successful native authorization.
 QString token=pixelviewDesktop->authorizedToken;
 if(token.isEmpty()) {
  token=pixelview::loadDevice(pixelviewOrigin.toString());
  if(!token.isEmpty()) pixelviewDesktop->authorizedToken=token;
 }
 if(token.isEmpty()) {
  // An inaccessible Keychain is not evidence of an unpaired installation.
  // Preserve durable identity and credentials; never reset them on read failure.
  pixelviewLease.fail("Pairing credential unavailable. Retrying without prompts; saved pairing has not been removed. Click Pair with Pixelview to authorize access without a new code.",pixelviewPairingDurable);
  return;
 }
 // A reconnect keeps running media authority; only socket-bound state resets.
 pixelviewLease.ready=false;pixelviewLease.pending=false;pixelviewLease.pingDeadline=0;
 pixelviewLease.revoked=false;pixelviewLease.replaced=false;
 pixelviewLease.development=pixelviewDev;
 QUrl ws=pixelviewOrigin;ws.setScheme(ws.scheme()=="https" ? "wss":"ws");ws.setPath("/desktop/ws");
 pixelviewAuthDeadline=pixelviewClock.elapsed()+10000;
 blog(LOG_INFO,"Pixelview: connecting control socket to %s",ws.toString().toUtf8().constData());
 pixelviewDesktop->openSocket(ws,token);
}
void OBSBasic::PixelviewDeviceRevoked()
{
 // Revocation is not permission to delete a local credential or display UI.
 pixelviewDesktop->authorizedToken.clear();
 config_set_bool(App()->GetUserConfig(),"PixelviewDesktop","PairingDisabled",true);
 pixelviewPairingDurable=false; pixelviewUnpairRetry=true; SavePixelviewIdentity(); pixelviewReconnectAt=0;
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
 if(pixelviewDesktop && pixelviewLease.requestStart(pixelviewClock.elapsed())) pixelviewConnectionStatus->setText(QStringLiteral("Requesting stream start…"));
 else if(pixelviewConnectionStatus) pixelviewConnectionStatus->setText(QStringLiteral("Connect/pair first, or wait for the pending start request."));
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
   .arg(pixelviewLease.retryAt>=0 ? QStringLiteral("retry in %1 s").arg(seconds) : pixelviewLease.pending ? QStringLiteral("requesting stream start") : QStringLiteral("connecting / preparing"));
  pixelviewConnectionStatus->setText(message);
  ui->statusbar->showMessage(message);
 }
 // Cancellation is terminal immediately, even while setup/native completion is outstanding.
 if(pixelviewShutdownPending || (!pixelviewLease.intent && pixelviewStopPending)) {
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
 pixelviewReconnectAt=0;pixelviewAuthDeadline=0;
 pixelviewDesktop->closeSocket();pixelviewLease.ready=false;
 pixelviewDesktop->authorizedToken.clear();
 config_set_bool(App()->GetUserConfig(),"PixelviewDesktop","PairingDisabled",true);
 // Persist reconnect-disabled intent, but retain identity on Cancel/denial.
 pixelviewPairingDurable=false;
 SavePixelviewIdentity();
 const auto origin=pixelviewOrigin;
 pixelview::runKeychainUserAction(this,[origin]{return pixelview::removeDevice(origin.toString());},[this](bool removed){
 if(isClosing() || pixelviewShutdownPending) return;
 if(removed) {pixelviewIdentity.clear();pixelviewExpectedIdentity.clear();}
 config_set_bool(App()->GetUserConfig(),"PixelviewDesktop","CleanupComplete",removed);
 bool saved=SavePixelviewIdentity();
 pixelviewUnpairRetry=!saved || !removed;
 if(!saved) {
  pixelviewConnectionStatus->setText(QStringLiteral("Unpair incomplete: identity could not be saved. Do not restart; retry Unpair."));
 } else if(!removed) {
  pixelviewConnectionStatus->setText(QStringLiteral("Unpair incomplete: Keychain removal failed. Check Keychain access, then click Unpair again. Connection remains disabled."));
 } else pixelviewConnectionStatus->setText(QStringLiteral("Not paired. This installation only; admin registration is not revoked."));
 pixelviewUnpairPending=false;
 RefreshPixelviewPairing();
 });
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
