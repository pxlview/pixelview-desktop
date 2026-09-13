// Compiles extracted, unmodified production frontend methods against offline boundaries.
#include "frontend/utility/PixelviewDesktop.hpp"
#include <QtCore/QJsonDocument>
#include <QtCore/QStringList>
#include <cassert>
#include <future>
#include <vector>
#include <iostream>
#include <map>
#define emit
#define QTStr(x) QStringLiteral(x)
namespace fixture {
class QTimer {
public:
 static inline std::vector<std::function<void()>> queue;
 bool active=true;
 void stop() {active=false;}
 void start() {active=true;}
 static inline std::vector<std::function<void()>> closes;
 template<class T> static void singleShot(int delay,T *w,void(T::*f)()) {assert(delay==100);closes.emplace_back([w,f]{(w->*f)();});}
 static void runClose() {auto batch=std::move(closes);closes.clear();for(auto &f:batch) f();}
 template<class T,class F> static void singleShot(int,T*,F f) {queue.emplace_back(f);}
 static void run() {auto batch=std::move(queue);queue.clear();for(auto &f:batch) f();}
};
using Timer=QTimer;
struct Output {bool active=false;int forceStops=0;};
bool obs_output_active(Output *o) {return o->active;}
void obs_output_force_stop(Output *o) {++o->forceStops;o->active=false;}
struct Handler {bool StreamingActive()const{return streamOutput && streamOutput->active;} Output *streamOutput=nullptr;int starts=0;bool StartStreaming(int*){++starts;return true;}};
struct Connection {std::function<void(QByteArray)> message;std::function<void(int)> disconnected;bool exchanging=false;QString authorizedToken;int closes=0;void closeSocket(bool=true){++closes;}};
struct Label {QString text;void setText(QString s){text=s;}};
struct Status {QString text;void showMessage(QString s){text=s;}void clearMessage(){text.clear();}void StreamDelayStarting(int){}};
struct UI {Status *statusbar=nullptr;};
struct QPushButton : Label {void *menu=nullptr;bool enabled=false;void setEnabled(bool b){enabled=b;}void setMenu(void *p){menu=p;}};
struct Clock {qint64 now=0;qint64 elapsed()const{return now;}};
struct Tray : Label {bool enabled=false;void setEnabled(bool value){enabled=value;}};
// Stateful offline config boundary matching libobs/util/config-file.h. Reads
// must observe revocation writes, rather than silently returning false forever.
struct config_t {std::map<std::pair<std::string,std::string>,bool> booleans;};
struct Application {config_t config;config_t *GetUserConfig(){return &config;}};
Application application;
Application *App(){return &application;}
bool config_get_bool(config_t *config,const char *section,const char *name) {return config && config->booleans[{section,name}];}
void config_set_bool(config_t *config,const char *section,const char *name,bool value) {assert(config);config->booleans[{section,name}]=value;}
int config_get_int(config_t*,const char*,const char*) {return 2;}
constexpr int LOG_INFO=0, LOG_WARNING=2;
#define SHUTDOWN_SEPARATOR "shutdown"
#define PIXELVIEW_SHUTDOWN_WAIT_MS 10000
void blog(int,const char*,...){}
constexpr int OBS_FRONTEND_EVENT_STREAMING_STARTING=1;
class OBSBasic {
public:
 bool pixelviewReceiving=false; void *pixelviewSendOutput=nullptr, *pixelviewReceiveScene=nullptr;
 void StopPixelviewReceive() {}
 void obs_set_output_source(int,void*) {}
 pixelview::Desktop pixelviewLease;
 Clock pixelviewClock;
 bool pixelviewStartPermit=false,pixelviewActualStreaming=false,pixelviewStopPending=false;
 bool pixelviewNativeAttempt=false,pixelviewClosingSocket=false,pixelviewStreamingBusy=false;
 bool pixelviewUnpairPending=false,isClosing_=false,pixelviewShutdownPending=false;
 qint64 pixelviewShutdownDeadline=0; QString pixelviewShutdownWait; bool pixelviewForceClose=false;
 bool PixelviewShutdownReady();
 QUrl pixelviewOrigin{"https://fixture.invalid"};bool pixelviewDev=false;
 config_t *activeConfiguration=nullptr;
 int authentications=0,teardowns=0,nativePreparations=0;
 void StartStreaming();
 void closeWindow();void ConnectPixelviewDesktop();bool RequestPixelviewStart();
 void Watchdog();void PixelviewDeviceRevoked();
 QJsonObject PixelviewReportedSettings(){return {};}
 quint64 pixelviewStopGeneration=0;
 qint64 pixelviewReconnectAt=0,pixelviewAuthDeadline=0;
 int pixelviewBackoff=1000;
 pixelview::DesktopIdentity pixelviewIdentity;
 bool pixelviewPairingDurable=true;
 void RefreshPixelviewPairing(){} // Widget policy is compiled with actual Qt in test_pairing_ux.
 bool identitySaveSucceeds=true;
 bool pixelviewUnpairRetry=false;
 pixelview::DesktopIdentity pixelviewExpectedIdentity;
 bool SavePixelviewIdentity(){return identitySaveSucceeds;}
 Timer watchdog;Timer *pixelviewWatchdog=&watchdog;
 Handler handler;Handler *outputHandler=&handler;
 Connection connection;Connection *pixelviewDesktop=&connection;
 std::shared_future<void> setupStreamingGuard;
 int oldService=1,newService=2;int *service=&newService,*pixelviewPreviousService=&oldService;
 int stoppedSignals=0,unpairs=0;
 Label label,identityLabel;Label *pixelviewConnectionStatus=&label,*pixelviewIdentityStatus=&identityLabel;
 Status status;UI uiStorage{&status};UI *ui=&uiStorage;
 QPushButton button;
 Tray *sysTrayStream=nullptr;
 bool autoStartBroadcast=false,autoStopBroadcast=false,broadcastActive=false;
 int disableOutputsRef=0;
 std::function<void()> startingHook;
 void OnEvent(int){}void SaveProject(){}void StreamingStarting(bool){if(startingHook) startingHook();}
 void BroadcastStreamStarted(bool){}void StartRecording(){}void StartReplayBuffer(){}
 void DisplayStreamStartError(){pixelviewLease.fail("setup error");}
 bool PixelviewLeaseValid()const;
 bool PixelviewSettingsBusy()const{return pixelviewStreamingBusy;}
 std::function<void(bool)> MakeSetup();
 void OutputStartedSignal(bool withDelay);
 void StreamDelayStarting(int sec);
 void StreamingStarted(bool delay){OutputStartedSignal(delay);}
 void OnActivate(){}
 template<class T> T findChild(QString) {return &button;}
 bool isClosing()const{return isClosing_;}
 void StreamingStopped(){++stoppedSignals;pixelviewStreamingBusy=false;}
 void FinishPixelviewUnpair(){++unpairs;pixelviewUnpairPending=false;}
 void PixelviewOutputStopped();
 void QueuePixelviewOutputStopped(int delay=0);
 void CancelPixelviewStart();
 void RefreshPixelviewReconnect();
 void bind();
};
// PRODUCTION_METHODS
} // namespace fixture: avoid interposing real Qt symbols at link time.
using namespace fixture;
int main() {
 const QByteArray READY=R"({"mutation":"DESKTOP_READY","data":{"node_id":"node","desktop_id":"desktop"}})";
 const QByteArray PING=R"({"mutation":"SOCKET_SEND_PING","data":{}})";
 const QByteArray STARTED=R"({"mutation":"DESKTOP_STARTED","data":{"config":{"whip":{"endpoint":"https://fixture.invalid/whip","bearer_token":"new"},"srt":{}}}})";
 const QByteArray DENIED=R"({"mutation":"DESKTOP_ERROR","data":{"code":"active_session_required"}})";
 auto grant=[](OBSBasic &w){w.pixelviewLease.ready=true;assert(w.pixelviewLease.requestStart(0));w.pixelviewLease.pending=false;w.pixelviewLease.started=true;};
 { OBSBasic live; live.bind(); Output output; output.active=true; live.handler.streamOutput=&output;
   int requests=0; std::vector<QJsonObject> pongs;
   auto send=live.pixelviewLease.send;
   live.pixelviewLease.send=[&](QJsonObject o){if(o["message"]=="DESKTOP_START") ++requests; if(o["message"]=="PONG_RESPONSE") pongs.push_back(o["data"].toObject()); send(o);};
   live.connection.message(READY);
   live.pixelviewLease.requestStart(0); assert(requests==1);
   live.connection.message(STARTED);
   live.pixelviewActualStreaming=true; live.pixelviewNativeAttempt=true; live.pixelviewStreamingBusy=true;
   live.connection.message(PING);
   assert(pongs.size()==1 && pongs[0]["streaming"].toBool() && pongs[0]["settings"].isNull());
   // Transient control loss while streaming: media continues, reconnect is scheduled.
   live.connection.disconnected(1006);
   assert(output.forceStops==0 && output.active && live.PixelviewLeaseValid() && !live.pixelviewLease.ready);
   assert(!live.pixelviewStopPending && live.connection.closes==1 && live.pixelviewReconnectAt==1000);
   live.pixelviewClock.now=999; live.Watchdog(); assert(live.authentications==0);
   live.pixelviewClock.now=1000; live.Watchdog(); assert(live.authentications==1);
   live.pixelviewClock.now=2000;
   live.connection.message(READY);
   assert(requests==1 && live.pixelviewLease.ready && live.pixelviewLease.started && live.label.text=="Control reconnected · streaming");
   assert(output.forceStops==0 && output.active && live.pixelviewBackoff==1000);
   live.connection.message(PING); assert(pongs.size()==2 && pongs[1]["streaming"].toBool());
   // Server silence for the ping budget reconnects again without stopping media.
   live.pixelviewClock.now=2000+pixelview::Desktop::PING_SILENCE_MS; live.Watchdog();
   assert(!live.pixelviewLease.ready && live.connection.closes==2 && output.forceStops==0 && live.pixelviewLease.intent);
   live.pixelviewClock.now+=1000; live.Watchdog(); assert(live.authentications==2);
   live.connection.message(READY); assert(requests==1 && live.pixelviewLease.ready);
   live.connection.disconnected(4401); assert(output.forceStops==1 && !live.pixelviewLease.intent);
   assert(live.connection.authorizedToken.isEmpty() && config_get_bool(App()->GetUserConfig(),"PixelviewDesktop","PairingDisabled"));
   Timer::run(); application.config.booleans.clear();
 }
 { OBSBasic replaced; replaced.bind(); Output output; output.active=true; replaced.handler.streamOutput=&output;
   replaced.connection.message(READY); replaced.pixelviewLease.requestStart(0); replaced.connection.message(STARTED);
   replaced.pixelviewActualStreaming=true; replaced.pixelviewNativeAttempt=true;
   replaced.connection.message(R"({"mutation":"SOCKET_DESKTOP_REPLACED","data":{}})");
   assert(replaced.pixelviewLease.replaced && !replaced.pixelviewLease.ready && replaced.PixelviewLeaseValid());
   assert(output.forceStops==0 && replaced.connection.closes==1 && replaced.pixelviewReconnectAt==0);
   replaced.connection.disconnected(1000);
   assert(replaced.pixelviewLease.intent && output.forceStops==0 && !replaced.pixelviewStopPending);
   replaced.pixelviewClock.now=99999; replaced.Watchdog(); assert(replaced.authentications==0);
   assert(replaced.label.text.contains("took over control"));
   Timer::run();
 }
 { OBSBasic revoked; revoked.bind(); Output output; output.active=true; revoked.handler.streamOutput=&output;
   revoked.connection.authorizedToken="synthetic-token";
   revoked.connection.message(READY); revoked.pixelviewLease.requestStart(0); revoked.connection.message(STARTED);
   revoked.pixelviewActualStreaming=true; revoked.pixelviewNativeAttempt=true;
   revoked.connection.message(R"({"mutation":"SOCKET_DESKTOP_REVOKED","data":{}})");
   assert(revoked.pixelviewLease.revoked && !revoked.pixelviewLease.intent && output.forceStops==1);
   assert(revoked.connection.authorizedToken.isEmpty() && revoked.pixelviewUnpairRetry && !revoked.pixelviewPairingDurable);
   assert(config_get_bool(App()->GetUserConfig(),"PixelviewDesktop","PairingDisabled"));
   revoked.connection.disconnected(1000); assert(!revoked.pixelviewLease.transientFailure && revoked.pixelviewReconnectAt==0);
   Timer::run(); application.config.booleans.clear();
 }
 std::cout<<"control loss keeps media, replaced/revoked mutations decide before the close PASS\n";
 for (bool save : {false,true}) {
  OBSBasic first; first.bind(); first.pixelviewPairingDurable=false; first.identitySaveSucceeds=save;
  first.connection.message(READY);
  assert(first.pixelviewPairingDurable==save);
  assert(first.pixelviewLease.ready==save);
  assert(first.pixelviewUnpairRetry==!save);
  Timer::run();
 }
 { OBSBasic mismatch;mismatch.bind();mismatch.pixelviewPairingDurable=false;
 mismatch.pixelviewExpectedIdentity.accept({{"node_id","expected"},{"desktop_id","desktop"}});
 mismatch.connection.message(R"({"mutation":"DESKTOP_READY","data":{"node_id":"other","desktop_id":"desktop"}})");
 assert(!mismatch.pixelviewPairingDurable && !mismatch.pixelviewLease.ready && mismatch.pixelviewUnpairRetry);
 Timer::run(); }
 // Drain callbacks deliberately run before the queued 100ms accepted close.
 for(int mode : {0,1,2,3}) {
  OBSBasic shutdown;shutdown.bind();Output output;
  std::promise<void> setup;
  std::function<void(bool)> finish;
  if(mode<2) {
   grant(shutdown);
   shutdown.setupStreamingGuard=setup.get_future().share();
   shutdown.pixelviewStreamingBusy=true;finish=shutdown.MakeSetup();
   if(mode==1) {shutdown.handler.streamOutput=&output;output.active=true;shutdown.pixelviewNativeAttempt=true;}
   shutdown.pixelviewLease.retryDelay=0;
   shutdown.pixelviewClock.now=30000;shutdown.connection.disconnected(1006);
   assert(shutdown.pixelviewClosingSocket && shutdown.pixelviewStopPending);
  } else if(mode==2) {
   shutdown.pixelviewLease.intent=true;shutdown.pixelviewLease.fail("lost",true);
   Timer::run();assert(!shutdown.pixelviewStopPending);
   assert(shutdown.pixelviewLease.retryAt>0);
  }
  const auto generation=shutdown.pixelviewLease.generation;
  const auto forces=output.forceStops;
  shutdown.closeWindow();
  assert(!shutdown.pixelviewLease.intent && shutdown.pixelviewLease.retryAt==-1);
  assert(shutdown.pixelviewLease.generation>generation);
  assert(!shutdown.pixelviewLease.pending && !shutdown.pixelviewLease.started);
  assert(!shutdown.watchdog.active);
  assert(shutdown.pixelviewReconnectAt==0 && shutdown.pixelviewAuthDeadline==0);
  assert(output.forceStops==forces);
  if(mode<2) {
   assert(!shutdown.isClosing() && shutdown.teardowns==0);
   shutdown.Watchdog();shutdown.ConnectPixelviewDesktop();shutdown.StartStreaming();
   finish(true);assert(shutdown.handler.starts==0);
   shutdown.pixelviewNativeAttempt=false;setup.set_value();
  }
  Timer::run();
  assert(!shutdown.pixelviewStopPending);
  assert(!shutdown.button.enabled);
  shutdown.pixelviewClock.now=99999;
  const auto shutdownGeneration=shutdown.pixelviewLease.generation;
  shutdown.Watchdog();shutdown.ConnectPixelviewDesktop();
  shutdown.connection.disconnected(0);shutdown.connection.disconnected(4401);
  shutdown.connection.message(DENIED);
  shutdown.connection.message(READY);
  assert(!shutdown.pixelviewLease.ready);
  assert(!shutdown.RequestPixelviewStart());
  shutdown.StartStreaming();
  shutdown.connection.message(STARTED);
  if(finish) finish(true);
  assert(shutdown.pixelviewLease.generation==shutdownGeneration);
  assert(!shutdown.pixelviewUnpairPending && shutdown.nativePreparations==0);
  shutdown.CancelPixelviewStart();
  assert(shutdown.pixelviewReconnectAt==0 && !shutdown.pixelviewLease.intent);
  assert(shutdown.authentications==0 && shutdown.handler.starts==0);
  Timer::run();Timer::runClose();Timer::run();Timer::runClose();
  assert(shutdown.isClosing() && shutdown.teardowns==1 && output.forceStops==forces);
 }
 std::cout<<"accepted close gate: recovery/setup drain, countdown, idle, deferred callback gates PASS\n";
 for(bool terminal : {false,true}) {
  OBSBasic pending;pending.bind();Tray tray;pending.sysTrayStream=&tray;
  pending.pixelviewLease.intent=true;pending.pixelviewLease.retries=2;pending.pixelviewLease.maxRetries=5;
  pending.pixelviewLease.ready=true;pending.pixelviewLease.started=true;
  pending.pixelviewStreamingBusy=true;
  std::promise<void> setup;pending.setupStreamingGuard=setup.get_future().share();
  auto finish=pending.MakeSetup();
  tray.text="Basic.Main.PreparingStream";tray.enabled=false;
  pending.RefreshPixelviewReconnect();assert(pending.button.text.contains("Reconnecting 2/5"));
  if(terminal) pending.connection.message(DENIED);
  else pending.CancelPixelviewStart();
  assert(!pending.pixelviewLease.intent && pending.pixelviewStopPending);
  assert(pending.button.text=="Basic.Main.StoppingStreaming" && !pending.button.enabled);
  assert(tray.text=="Basic.Main.StoppingStreaming" && !tray.enabled);
  assert(!pending.status.text.contains("Reconnecting"));
  Timer::run();assert(pending.pixelviewStopPending);
  assert(pending.button.text=="Basic.Main.StoppingStreaming" && !pending.button.enabled);
  assert(tray.text=="Basic.Main.StoppingStreaming" && !tray.enabled);
  finish(true);assert(pending.handler.starts==0);
  setup.set_value();Timer::run();assert(!pending.pixelviewStopPending);
  assert(pending.button.text=="Basic.Main.StartStreaming" && !pending.button.enabled);
  assert(tray.text=="Basic.Main.StartStreaming" && !tray.enabled);
  assert(!pending.status.text.contains("Reconnecting") && !pending.pixelviewLease.intent);
  Timer::run();
 }
 std::cout<<"Stop and terminal denial: pending setup button/tray reconcile before and after drain PASS\n";
 OBSBasic w;w.bind();Output output;w.handler.streamOutput=&output;
 grant(w);
 w.pixelviewStreamingBusy=true;
 // A native failure has already completed. Halting must NOT force-stop it again:
 // libobs force_stop resets its completion event even on an unused output.
 w.pixelviewNativeAttempt=false;
 w.pixelviewLease.fail("media lost",true);
 assert(output.forceStops==0);
 auto oldCallbacks=Timer::queue;Timer::run();
 assert(w.connection.closes==1 && !w.pixelviewStopPending && w.pixelviewLease.intent);
 assert(w.stoppedSignals==1 && w.service==&w.oldService);
 w.pixelviewClock.now=2000;assert(w.pixelviewLease.takeRetry(2000,true));
 w.connection.message(READY);
 assert(w.pixelviewLease.pending);
 w.pixelviewStartPermit=true;w.pixelviewPreviousService=&w.oldService;w.service=&w.newService;
 for(auto &f:oldCallbacks) f();
 assert(w.pixelviewLease.pending && w.pixelviewStartPermit && w.service==&w.newService);
 assert(w.connection.closes==1); // Late completion cannot destroy the new attempt.
 // Actual running output stops immediately, but cleanup waits for native completion and setup.
 w.pixelviewNativeAttempt=true;output.active=true;w.pixelviewStreamingBusy=true;
 std::promise<void> setup;w.setupStreamingGuard=setup.get_future().share();
 w.pixelviewLease.fail("control lost",true);
 assert(output.forceStops==1 && !output.active);
 w.pixelviewLease.fail("Stop"); // Cancellation while already draining must not force-stop twice.
 assert(output.forceStops==1 && !w.pixelviewLease.intent);
 Timer::run();assert(w.pixelviewStopPending && w.connection.closes==1);
 w.pixelviewNativeAttempt=false;Timer::run();assert(w.pixelviewStopPending);
 setup.set_value();Timer::run();assert(!w.pixelviewStopPending && w.connection.closes==2);
 for(auto &f:oldCallbacks) f();assert(w.connection.closes==2);
 std::cout<<"real halt/drain: immediate stop, no double force, setup drain, stale completion PASS\n";
 OBSBasic ui;ui.bind();ui.pixelviewLease.intent=true;ui.pixelviewLease.retries=2;
 ui.pixelviewLease.maxRetries=5;ui.pixelviewLease.retryAt=7000;
 ui.RefreshPixelviewReconnect();
 assert(ui.status.text.contains("Reconnecting 2/5") && ui.button.enabled && ui.button.text.contains("Stop"));
 ui.CancelPixelviewStart();
 assert(!ui.pixelviewLease.intent && !ui.status.text.contains("Reconnecting"));
 Timer::run();assert(!ui.button.text.contains("Reconnecting"));
 std::cout<<"native status/Stop cancellation between attempts: PASS\n";
 OBSBasic revoke;revoke.bind();revoke.pixelviewLease.intent=true;
 revoke.pixelviewIdentity.accept({{"node_id","retained-node"},{"desktop_id","retained-desktop"}});
 revoke.connection.authorizedToken="synthetic-revoked-token";
 revoke.connection.disconnected(0);assert(revoke.pixelviewLease.intent && revoke.pixelviewClosingSocket);
 assert(revoke.connection.authorizedToken=="synthetic-revoked-token");
 // NSURLSession may report generic failure before the authoritative close code.
 revoke.connection.disconnected(4401);
 assert(!revoke.pixelviewLease.intent && !revoke.pixelviewUnpairPending);
 assert(!revoke.pixelviewPairingDurable && revoke.pixelviewUnpairRetry);
 assert(config_get_bool(App()->GetUserConfig(),"PixelviewDesktop","PairingDisabled"));
 assert(revoke.pixelviewIdentity.nodeId=="retained-node" && revoke.pixelviewIdentity.desktopId=="retained-desktop");
 assert(revoke.connection.authorizedToken.isEmpty());
 assert(!revoke.pixelviewLease.takeRetry(99999,true));
 Timer::run();assert(revoke.unpairs==0 && revoke.connection.closes==1);
 revoke.ConnectPixelviewDesktop();assert(revoke.authentications==0 && revoke.pixelviewReconnectAt==0);
 // The following scenarios represent independent installations.
 application.config.booleans.clear();
 std::cout<<"revocation after generic disconnect cancels queued retry: PASS\n";
 // An upgrade refused with HTTP 403 is terminal but not a revocation: no retry loop, pairing kept.
 OBSBasic refused;refused.bind();refused.connection.authorizedToken="kept";refused.pixelviewLease.intent=true;
 refused.connection.disconnected(4403);
 assert(!refused.pixelviewLease.intent && !refused.pixelviewLease.transientFailure && refused.pixelviewPairingDurable);
 assert(refused.connection.authorizedToken=="kept" && refused.label.text.contains("refused"));
 Timer::run();assert(refused.pixelviewReconnectAt==0);
 OBSBasic callback;callback.bind();grant(callback);
 auto finish=callback.MakeSetup();auto copy=finish;
 finish(true);copy(true);assert(callback.handler.starts==1);
 callback.CancelPixelviewStart();finish(true);assert(callback.handler.starts==1);
 // A newer grant cannot authorize the old callback, even after the old setup settles.
 callback.pixelviewStopPending=false;callback.pixelviewClosingSocket=false;
 grant(callback);
 copy(true);assert(callback.handler.starts==1);
 auto fresh=callback.MakeSetup();fresh(true);assert(callback.handler.starts==2);
 callback.pixelviewLease.started=false;auto lost=callback.MakeSetup();lost(true);assert(callback.handler.starts==2);
 std::cout<<"real native setup continuation: duplicate, Stop, new grant and lost-authority guards PASS\n";
 OBSBasic messages;messages.bind();messages.pixelviewLease.ready=true;
 assert(messages.pixelviewLease.requestStart(0));messages.label.text="Requesting stream start…";
 messages.connection.message(STARTED);
 assert(messages.label.text=="Preparing WHIP stream…");
 messages.connection.disconnected(0);assert(messages.pixelviewLease.intent);
 messages.connection.message(DENIED);
 assert(!messages.pixelviewLease.intent);
 std::cout<<"started clears start-request label; late terminal message cancels intent PASS\n";
 OBSBasic delayed;delayed.bind();delayed.pixelviewLease.ready=true;delayed.pixelviewLease.started=true;
 delayed.pixelviewLease.intent=true;delayed.pixelviewLease.retries=2;
 delayed.OutputStartedSignal(true);
 assert(!delayed.pixelviewActualStreaming && delayed.pixelviewLease.retries==2);
 delayed.OutputStartedSignal(false);assert(delayed.pixelviewActualStreaming && delayed.pixelviewLease.retries==0);
 delayed.pixelviewNativeAttempt=true;delayed.button.text="Native Stop";delayed.button.menu=&delayed;
 delayed.RefreshPixelviewReconnect();assert(delayed.button.text=="Native Stop" && delayed.button.menu==&delayed);
 std::cout<<"delay is not media success; native active controls retained PASS\n";
 OBSBasic queuedDelay;queuedDelay.bind();queuedDelay.pixelviewLease.intent=true;
 queuedDelay.pixelviewLease.fail("control lost",true);
 queuedDelay.StreamDelayStarting(5);
 assert(queuedDelay.pixelviewLease.intent && !queuedDelay.pixelviewActualStreaming);
 std::cout<<"queued native delay-start cannot cancel recovery after control loss PASS\n";
 OBSBasic reentrant;reentrant.bind();grant(reentrant);
 reentrant.startingHook=[&]{reentrant.CancelPixelviewStart();};
 reentrant.MakeSetup()(true);
 assert(reentrant.handler.starts==0 && !reentrant.pixelviewNativeAttempt);
 std::cout<<"Stop from native Starting subscribers is rechecked before media starts PASS\n";
 OBSBasic denied;denied.bind();grant(denied);
 auto deniedSetup=denied.MakeSetup();denied.disableOutputsRef=1;deniedSetup(true);
 assert(denied.handler.starts==0 && !denied.pixelviewLease.intent);
 std::cout<<"lifecycle denial arriving during setup is terminal PASS\n";
 OBSBasic mismatch;mismatch.bind();mismatch.pixelviewIdentity.accept({{"node_id","original"},{"desktop_id","desktop"}});
 mismatch.pixelviewLease.intent=true;
 mismatch.connection.message(R"({"mutation":"DESKTOP_READY","data":{"node_id":"other","desktop_id":"desktop"}})");
 assert(!mismatch.pixelviewLease.intent && mismatch.pixelviewIdentity.nodeId=="original");
 std::cout<<"authenticated identity mismatch cannot replace pairing PASS\n";
 OBSBasic idle;idle.bind();idle.pixelviewLease.ready=true;idle.pixelviewLease.pingDeadline=30000;
 idle.pixelviewClock.now=30000;idle.Watchdog();
 assert(!idle.pixelviewLease.intent && idle.pixelviewReconnectAt==31000);
 idle.pixelviewLease.fail("terminal protocol denial");assert(idle.pixelviewReconnectAt==0);
 std::cout<<"idle ping silence reconnects without inventing streaming intent PASS\n";
}
