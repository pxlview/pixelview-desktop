// Pixelview Desktop protocol v1: native controls, memory-only ingest service.
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>

void OBSBasic::InitPixelviewDesktop(QWidget *sidebar)
{
 pixelviewClock.start();
 pixelviewDesktop=std::make_unique<pixelview::DesktopConnection>();
 auto heading=new QLabel(QStringLiteral("Pairing / Connection"),sidebar);
 heading->setStyleSheet(QStringLiteral("font-weight: 600;")); sidebar->layout()->addWidget(heading);
 pixelviewIdentityStatus=new QLabel(QStringLiteral("Not paired"),sidebar);
 pixelviewIdentityStatus->setObjectName(QStringLiteral("pixelviewIdentityStatus"));
 pixelviewIdentityStatus->setWordWrap(true); pixelviewIdentityStatus->setTextFormat(Qt::PlainText);
 sidebar->layout()->addWidget(pixelviewIdentityStatus);
 auto unpair=new QPushButton(QStringLiteral("Unpair this installation"),sidebar);
 unpair->setObjectName(QStringLiteral("pixelviewUnpair")); sidebar->layout()->addWidget(unpair);
 connect(unpair,&QPushButton::clicked,this,[this]{
  if(pixelviewDesktop->exchanging || pixelviewUnpairPending) return;
  pixelviewUnpairPending=true;pixelviewStartPermit=false;pixelviewReconnectAt=0;
  pixelviewConnectionStatus->setText(QStringLiteral("Stopping before unpairing…"));
  if(outputHandler && outputHandler->streamOutput &&
     (obs_output_active(outputHandler->streamOutput) || pixelviewStreamingBusy)) obs_output_force_stop(outputHandler->streamOutput);
  QTimer::singleShot(0,this,&OBSBasic::PixelviewOutputStopped);
 });
 auto pair=new QPushButton(QStringLiteral("Pair with Pixelview"),sidebar);
 pair->setObjectName(QStringLiteral("pixelviewPair")); sidebar->layout()->addWidget(pair);
 pixelviewConnectionStatus=new QLabel(QStringLiteral("Not paired"),sidebar);
 pixelviewConnectionStatus->setWordWrap(true); sidebar->layout()->addWidget(pixelviewConnectionStatus);
 connect(pair,&QPushButton::clicked,this,&OBSBasic::PairPixelviewDesktop);
 pixelviewDesktop->status=[this](QString message){pixelviewConnectionStatus->setText(message);};
 pixelviewLease.error=[this](QString message){pixelviewConnectionStatus->setText(message);};
 pixelviewLease.send=[this](QJsonObject o){pixelviewDesktop->sendSocket(QJsonDocument(o).toJson(QJsonDocument::Compact));};
 pixelviewLease.halt=[this]{
  pixelviewStartPermit=false; pixelviewActualStreaming=false;
  // Force-stop even during connecting/delay. Async setup rechecks the lease.
  if(outputHandler && outputHandler->streamOutput &&
     (obs_output_active(outputHandler->streamOutput) || pixelviewStreamingBusy)) obs_output_force_stop(outputHandler->streamOutput);
  pixelviewClosingSocket=true; pixelviewHeartbeat->stop(); pixelviewAuthDeadline=0;
  pixelviewReconnectAt=pixelviewClock.elapsed()+pixelviewBackoff;
  pixelviewBackoff=std::min(pixelviewBackoff*2,30000);
  QTimer::singleShot(0,this,&OBSBasic::PixelviewOutputStopped);
 };
 pixelviewLease.publish=[this](QString endpoint,QString bearer){
  if(!PixelviewLeaseValid() || PixelviewSettingsBusy()) {pixelviewLease.fail("Cannot start while native output is busy.");return;}
  OBSDataAutoRelease settings=obs_data_create();
  obs_data_set_string(settings,"server",endpoint.toUtf8().constData());
  obs_data_set_string(settings,"bearer_token",bearer.toUtf8().constData());
  OBSServiceAutoRelease whip=obs_service_create("whip_custom","Pixelview leased WHIP",settings,nullptr);
  if(!whip) {pixelviewLease.fail("WHIP output is unavailable in this build. SRT is not supported.");return;}
  pixelviewPreviousService=service; service=whip;
  pixelviewStartPermit=true; StartStreaming();
 };
 pixelviewDesktop->message=[this](QByteArray body){
  QJsonParseError error; auto doc=QJsonDocument::fromJson(body,&error);
  if(body.size()>16384 || error.error!=QJsonParseError::NoError || !doc.isObject()) {pixelviewLease.fail("Invalid Pixelview message.");return;}
  auto candidate=pixelviewIdentity;
  if(doc.object()["type"]=="ready" && !candidate.accept(doc.object())) {pixelviewLease.fail("Paired identity mismatch. Unpair and pair again.");return;}
  const bool wasReady=pixelviewLease.ready;
  pixelviewLease.receive(doc.object(),pixelviewClock.elapsed());
  if(!wasReady && pixelviewLease.ready) {
   pixelviewIdentity=candidate;SavePixelviewIdentity();
   pixelviewAuthDeadline=0; pixelviewReconnectAt=0;
   pixelviewConnectionStatus->setText(QStringLiteral("Connected · WHIP ready to request"));
   pixelviewHeartbeat->start();
  }
 };
 pixelviewDesktop->disconnected=[this](int code){
  pixelviewLease.fail(code==4401 ? "Device revoked. Pair again in Pixelview admin." : "Disconnected. Stream stopped; reconnecting without resuming.");
  if(code==4401) {pixelviewUnpairPending=true;pixelviewIdentity.clear();SavePixelviewIdentity();pixelviewReconnectAt=0;}
 };
 pixelviewDesktop->paired=[this]{
  pixelviewOrigin=pixelviewDesktop->origin; pixelviewDev=pixelviewDesktop->development;
  config_set_bool(App()->GetUserConfig(),"PixelviewDesktop","CleanupComplete",false);
  config_set_bool(App()->GetUserConfig(),"PixelviewDesktop","PairingDisabled",false);
  pixelviewIdentity=pixelviewDesktop->identity;
  config_set_string(App()->GetUserConfig(),"PixelviewDesktop","Origin",pixelviewOrigin.toString().toUtf8().constData());
  config_set_bool(App()->GetUserConfig(),"PixelviewDesktop","LocalDevelopment",pixelviewDev);
  SavePixelviewIdentity();
  ConnectPixelviewDesktop();
 };
 pixelviewHeartbeat=new QTimer(this); pixelviewHeartbeat->setInterval(15000);
 connect(pixelviewHeartbeat,&QTimer::timeout,this,[this]{
  if(pixelviewLease.ready && pixelviewLease.heartbeatSent(pixelviewClock.elapsed()))
   pixelviewLease.send({{"type","heartbeat"},{"streaming",pixelviewActualStreaming && PixelviewLeaseValid()}, {"settings",PixelviewReportedSettings()}});
 });
 pixelviewWatchdog=new QTimer(this); pixelviewWatchdog->setInterval(100);
 connect(pixelviewWatchdog,&QTimer::timeout,this,[this]{
  if(isClosing()) {pixelviewWatchdog->stop();pixelviewHeartbeat->stop();return;}
  pixelviewIdentityStatus->setText(pixelviewIdentity.label(pixelviewLease.ready,pixelviewActualStreaming));
  auto now=pixelviewClock.elapsed(); pixelviewLease.tick(now);
  if(pixelviewAuthDeadline && now>=pixelviewAuthDeadline) pixelviewLease.fail("Pixelview authentication timed out.");
  if(pixelviewReconnectAt && now>=pixelviewReconnectAt && !pixelviewClosingSocket && !PixelviewSettingsBusy()) {pixelviewReconnectAt=0;ConnectPixelviewDesktop();}
 }); pixelviewWatchdog->start();
 connect(this,&OBSBasic::StreamingStarted,this,[this]{
  if(!PixelviewLeaseValid()) {pixelviewLease.fail("Lease lost before output started.");return;}
  pixelviewActualStreaming=true;
 });
 connect(this,&OBSBasic::StreamingStopped,this,[this](bool delay){if(!delay) QTimer::singleShot(0,this,&OBSBasic::PixelviewOutputStopped);});
 pixelviewOrigin=QUrl(QString::fromUtf8(config_get_string(App()->GetUserConfig(),"PixelviewDesktop","Origin")));
 pixelviewDev=config_get_bool(App()->GetUserConfig(),"PixelviewDesktop","LocalDevelopment");
 pixelviewIdentity.accept({{"node_id",QString::fromUtf8(config_get_string(App()->GetUserConfig(),"PixelviewDesktop","NodeId"))},
  {"desktop_id",QString::fromUtf8(config_get_string(App()->GetUserConfig(),"PixelviewDesktop","DesktopId"))}});
 pixelviewIdentityStatus->setText(pixelviewIdentity.label(false,false));
 if(pixelview::Desktop::validOrigin(pixelviewOrigin,pixelviewDev)) ConnectPixelviewDesktop();
}

void OBSBasic::PairPixelviewDesktop()
{
 if(pixelviewUnpairPending || pixelviewClosingSocket || PixelviewSettingsBusy() || pixelviewDesktop->exchanging || pixelviewLease.pending || pixelviewLease.leased) return;
 QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("Pair with Pixelview"));
 QFormLayout form(&dialog);
 QLineEdit origin(pixelviewOrigin.toString(),&dialog); origin.setPlaceholderText(QStringLiteral("https://your-backend-origin"));
 QLineEdit code(&dialog); code.setEchoMode(QLineEdit::Password); code.setPlaceholderText(QStringLiteral("One-time admin pairing code"));
 QCheckBox dev(QStringLiteral("Local development (allow HTTP only on loopback)"),&dialog); dev.setChecked(pixelviewDev);
 form.addRow(QStringLiteral("Backend origin"),&origin);form.addRow(QStringLiteral("Pairing code"),&code);form.addRow(&dev);
 QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);form.addRow(&buttons);
 connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
 if(dialog.exec()!=QDialog::Accepted) return;
 QUrl requested(origin.text());requested.setPath("");
 if(requested!=pixelviewOrigin && !pixelviewOrigin.isEmpty() && !config_get_bool(App()->GetUserConfig(),"PixelviewDesktop","CleanupComplete")) {
  pixelviewConnectionStatus->setText(QStringLiteral("Unpair this installation before changing backend origin."));return;
 }
 pixelviewDesktop->closeSocket();pixelviewLease.ready=false;pixelviewHeartbeat->stop();pixelviewReconnectAt=0;pixelviewAuthDeadline=0;
 pixelviewDesktop->pair(QUrl(origin.text()),dev.isChecked(),code.text()); code.clear();
}
void OBSBasic::ConnectPixelviewDesktop()
{
 if(pixelviewUnpairPending || config_get_bool(App()->GetUserConfig(),"PixelviewDesktop","PairingDisabled") || !pixelview::Desktop::validOrigin(pixelviewOrigin,pixelviewDev)) return;
 QString token=pixelview::loadDevice(pixelviewOrigin.toString());
 if(token.isEmpty()) {pixelviewIdentity.clear();SavePixelviewIdentity();pixelviewConnectionStatus->setText(QStringLiteral("Pair with Pixelview to connect"));return;}
 pixelviewLease.ready=false;pixelviewLease.pending=false;pixelviewLease.leased=false;pixelviewLease.stopping=false;pixelviewLease.heartbeatRequest=-1;pixelviewLease.development=pixelviewDev;
 QUrl ws=pixelviewOrigin;ws.setScheme(ws.scheme()=="https" ? "wss":"ws");ws.setPath("/desktop/ws");
 pixelviewAuthDeadline=pixelviewClock.elapsed()+10000;
 pixelviewDesktop->openSocket(ws,token);
}
bool OBSBasic::PixelviewLeaseValid() const {return pixelviewDesktop && !pixelviewUnpairPending && pixelviewLease.authorized(pixelviewClock.elapsed());}
bool OBSBasic::RequestPixelviewStart()
{
 if(isClosing() || pixelviewClosingSocket || pixelviewUnpairPending) return false;
 if(pixelviewStartPermit) {pixelviewStartPermit=false;return PixelviewLeaseValid();}
 if(pixelviewDesktop && pixelviewLease.requestStart(pixelviewClock.elapsed())) pixelviewConnectionStatus->setText(QStringLiteral("Requesting exclusive WHIP lease…"));
 else if(pixelviewConnectionStatus) pixelviewConnectionStatus->setText(QStringLiteral("Connect/pair first, or wait for the pending lease request."));
 return false;
}
void OBSBasic::PixelviewOutputStopped()
{
 if(outputHandler && outputHandler->streamOutput && obs_output_active(outputHandler->streamOutput)) {
  QTimer::singleShot(100,this,&OBSBasic::PixelviewOutputStopped);return;
 }
 if(setupStreamingGuard.valid() && setupStreamingGuard.wait_for(std::chrono::seconds(0))!=std::future_status::ready) {
  QTimer::singleShot(100,this,&OBSBasic::PixelviewOutputStopped);return;
 }
 pixelviewActualStreaming=false;pixelviewStartPermit=false;pixelviewLease.outputStopped();
 if(pixelviewStreamingBusy) {pixelviewStreamingBusy=false; emit StreamingStopped();}
 if(pixelviewUnpairPending) FinishPixelviewUnpair();
 if(pixelviewClosingSocket) {pixelviewDesktop->closeSocket();pixelviewClosingSocket=false;}
 if(pixelviewPreviousService) {service=pixelviewPreviousService;pixelviewPreviousService=nullptr;}
}
bool OBSBasic::SavePixelviewIdentity()
{
 config_set_string(App()->GetUserConfig(),"PixelviewDesktop","NodeId",pixelviewIdentity.nodeId.toUtf8().constData());
 config_set_string(App()->GetUserConfig(),"PixelviewDesktop","DesktopId",pixelviewIdentity.desktopId.toUtf8().constData());
 const bool saved=config_save_safe(App()->GetUserConfig(),"tmp",nullptr)==CONFIG_SUCCESS;
 if(!saved)
  pixelviewConnectionStatus->setText(QStringLiteral("Could not save pairing identity."));
 pixelviewIdentityStatus->setText(pixelviewIdentity.label(pixelviewLease.ready,pixelviewActualStreaming));
 return saved;
}
void OBSBasic::FinishPixelviewUnpair()
{
 pixelviewHeartbeat->stop();pixelviewReconnectAt=0;pixelviewAuthDeadline=0;
 pixelviewDesktop->closeSocket();pixelviewLease.ready=false;
 config_set_bool(App()->GetUserConfig(),"PixelviewDesktop","PairingDisabled",true);
 pixelviewIdentity.clear();
 bool removed=pixelview::removeDevice(pixelviewOrigin.toString());
 config_set_bool(App()->GetUserConfig(),"PixelviewDesktop","CleanupComplete",removed);
 bool saved=SavePixelviewIdentity();
 if(!saved) {
  pixelviewConnectionStatus->setText(QStringLiteral("Unpair incomplete: identity could not be saved. Do not restart; retry Unpair."));
 } else if(!removed) {
  pixelviewConnectionStatus->setText(QStringLiteral("Keychain removal failed. Unlock Keychain and retry Unpair."));
 } else pixelviewConnectionStatus->setText(QStringLiteral("Not paired. This installation only; admin registration is not revoked."));
 pixelviewUnpairPending=false;
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
