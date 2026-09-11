// Compiles extracted, unmodified production frontend methods against offline boundaries.
#include "frontend/utility/PixelviewDesktop.hpp"
#include <QtCore/QJsonDocument>
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
struct Connection {std::function<void(QByteArray)> message;std::function<void(int)> disconnected;bool exchanging=false;QString authorizedToken;int closes=0;void closeSocket(){++closes;}};
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
constexpr int LOG_INFO=0;
#define SHUTDOWN_SEPARATOR "shutdown"
void blog(int,const char*){}
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
 QUrl pixelviewOrigin{"https://fixture.invalid"};bool pixelviewDev=false;
 config_t *activeConfiguration=nullptr;
 int authentications=0,teardowns=0,nativePreparations=0;
 void StartStreaming();
 void closeWindow();void ConnectPixelviewDesktop();bool RequestPixelviewStart();
 void Watchdog();void Heartbeat();
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
 Timer heartbeat,watchdog;Timer *pixelviewHeartbeat=&heartbeat,*pixelviewWatchdog=&watchdog;
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
 for (bool save : {false,true}) {
  OBSBasic first; first.bind(); first.pixelviewPairingDurable=false; first.identitySaveSucceeds=save;
  first.connection.message(R"({"type":"ready","node_id":"node","desktop_id":"desktop","heartbeat_interval":15,"lease_seconds":45})");
  assert(first.pixelviewPairingDurable==save);
  assert(first.pixelviewLease.ready==save);
  assert(first.pixelviewUnpairRetry==!save);
  Timer::run();
 }
 { OBSBasic mismatch;mismatch.bind();mismatch.pixelviewPairingDurable=false;
 mismatch.pixelviewExpectedIdentity.accept({{"node_id","expected"},{"desktop_id","desktop"}});
 mismatch.connection.message(R"({"type":"ready","node_id":"other","desktop_id":"desktop","heartbeat_interval":15,"lease_seconds":45})");
 assert(!mismatch.pixelviewPairingDurable && !mismatch.pixelviewLease.ready && mismatch.pixelviewUnpairRetry);
 Timer::run(); }
 // Drain callbacks deliberately run before the queued 100ms accepted close.
 for(int mode : {0,1,2,3}) {
  OBSBasic shutdown;shutdown.bind();Output output;
  std::promise<void> setup;
  std::function<void(bool)> finish;
  if(mode<2) {
   shutdown.pixelviewLease.ready=true;shutdown.pixelviewLease.deadline=30000;
   assert(shutdown.pixelviewLease.requestStart(0));
   shutdown.pixelviewLease.pending=false;shutdown.pixelviewLease.leased=true;
   shutdown.setupStreamingGuard=setup.get_future().share();
   shutdown.pixelviewStreamingBusy=true;finish=shutdown.MakeSetup();
   if(mode==1) {shutdown.handler.streamOutput=&output;output.active=true;shutdown.pixelviewNativeAttempt=true;}
   shutdown.pixelviewLease.retryDelay=0;
   shutdown.pixelviewClock.now=30000;shutdown.pixelviewLease.tick(30000);
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
  assert(!shutdown.pixelviewLease.pending && !shutdown.pixelviewLease.leased);
  assert(!shutdown.heartbeat.active && !shutdown.watchdog.active);
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
  shutdown.Watchdog();shutdown.Heartbeat();shutdown.ConnectPixelviewDesktop();
  shutdown.connection.disconnected(0);shutdown.connection.disconnected(4401);
  shutdown.connection.message(R"({"type":"error","code":"busy"})");
  shutdown.connection.message(R"({"type":"ready","node_id":"node","desktop_id":"desktop","heartbeat_interval":15,"lease_seconds":45})");
  assert(!shutdown.pixelviewLease.ready && !shutdown.heartbeat.active);
  assert(!shutdown.RequestPixelviewStart());
  shutdown.StartStreaming();
  shutdown.connection.message(R"({"type":"started","fence":1,"lease_expires_at":9999999,"config":{"whip":{"endpoint":"https://fixture.invalid/whip","bearer_token":"new"}}})");
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
  pending.pixelviewLease.ready=true;pending.pixelviewLease.leased=true;pending.pixelviewLease.deadline=30000;
  pending.pixelviewStreamingBusy=true;
  std::promise<void> setup;pending.setupStreamingGuard=setup.get_future().share();
  auto finish=pending.MakeSetup();
  tray.text="Basic.Main.PreparingStream";tray.enabled=false;
  pending.RefreshPixelviewReconnect();assert(pending.button.text.contains("Reconnecting 2/5"));
  if(terminal) pending.connection.message(R"({"type":"error","code":"busy"})");
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
 w.pixelviewLease.ready=true;w.pixelviewLease.deadline=30000;assert(w.pixelviewLease.requestStart(0));
 w.pixelviewLease.pending=false;w.pixelviewLease.leased=true;
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
 w.pixelviewLease.receive({{"type","ready"},{"heartbeat_interval",15},{"lease_seconds",45}},2000);
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
 OBSBasic callback;callback.bind();callback.pixelviewLease.ready=true;callback.pixelviewLease.deadline=30000;
 assert(callback.pixelviewLease.requestStart(0));callback.pixelviewLease.pending=false;callback.pixelviewLease.leased=true;
 auto finish=callback.MakeSetup();auto copy=finish;
 finish(true);copy(true);assert(callback.handler.starts==1);
 callback.CancelPixelviewStart();finish(true);assert(callback.handler.starts==1);
 // A newer lease cannot authorize the old callback, even after the old setup settles.
 callback.pixelviewStopPending=false;callback.pixelviewClosingSocket=false;
 callback.pixelviewLease.ready=true;callback.pixelviewLease.deadline=30000;
 assert(callback.pixelviewLease.requestStart(0));callback.pixelviewLease.pending=false;callback.pixelviewLease.leased=true;
 copy(true);assert(callback.handler.starts==1);
 auto fresh=callback.MakeSetup();fresh(true);assert(callback.handler.starts==2);
 callback.pixelviewClock.now=30000;auto expired=callback.MakeSetup();expired(true);assert(callback.handler.starts==2);
 std::cout<<"real native setup continuation: duplicate, Stop, new lease and deadline guards PASS\n";
 OBSBasic messages;messages.bind();messages.pixelviewLease.ready=true;messages.pixelviewLease.deadline=30000;
 assert(messages.pixelviewLease.requestStart(0));messages.label.text="Requesting exclusive WHIP lease…";
 messages.connection.message(R"({"type":"started","fence":1,"lease_expires_at":9999999,"config":{"whip":{"endpoint":"https://fixture.invalid/whip","bearer_token":"new"}}})");
 assert(messages.label.text=="Preparing WHIP stream…");
 messages.connection.disconnected(0);assert(messages.pixelviewLease.intent);
 messages.connection.message(R"({"type":"error","code":"busy"})");
 assert(!messages.pixelviewLease.intent);
 std::cout<<"started clears lease-request label; late terminal message cancels intent PASS\n";
 OBSBasic delayed;delayed.bind();delayed.pixelviewLease.ready=true;delayed.pixelviewLease.leased=true;
 delayed.pixelviewLease.intent=true;delayed.pixelviewLease.deadline=30000;delayed.pixelviewLease.retries=2;
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
 OBSBasic reentrant;reentrant.bind();reentrant.pixelviewLease.ready=true;reentrant.pixelviewLease.deadline=30000;
 assert(reentrant.pixelviewLease.requestStart(0));reentrant.pixelviewLease.pending=false;reentrant.pixelviewLease.leased=true;
 reentrant.startingHook=[&]{reentrant.CancelPixelviewStart();};
 reentrant.MakeSetup()(true);
 assert(reentrant.handler.starts==0 && !reentrant.pixelviewNativeAttempt);
 std::cout<<"Stop from native Starting subscribers is rechecked before media starts PASS\n";
 OBSBasic denied;denied.bind();denied.pixelviewLease.ready=true;denied.pixelviewLease.deadline=30000;
 assert(denied.pixelviewLease.requestStart(0));denied.pixelviewLease.pending=false;denied.pixelviewLease.leased=true;
 auto deniedSetup=denied.MakeSetup();denied.disableOutputsRef=1;deniedSetup(true);
 assert(denied.handler.starts==0 && !denied.pixelviewLease.intent);
 std::cout<<"lifecycle denial arriving during setup is terminal PASS\n";
 OBSBasic mismatch;mismatch.bind();mismatch.pixelviewIdentity.accept({{"node_id","original"},{"desktop_id","desktop"}});
 mismatch.pixelviewLease.intent=true;
 mismatch.connection.message(R"({"type":"ready","node_id":"other","desktop_id":"desktop","heartbeat_interval":15,"lease_seconds":45})");
 assert(!mismatch.pixelviewLease.intent && mismatch.pixelviewIdentity.nodeId=="original");
 std::cout<<"authenticated identity mismatch cannot replace pairing or resume PASS\n";
 OBSBasic idle;idle.bind();idle.pixelviewLease.ready=true;idle.pixelviewLease.deadline=30000;
 idle.pixelviewClock.now=30000;idle.pixelviewLease.tick(30000);
 assert(!idle.pixelviewLease.intent && idle.pixelviewReconnectAt==31000);
 idle.pixelviewLease.fail("terminal protocol denial");assert(idle.pixelviewReconnectAt==0);
 std::cout<<"idle control timeout reconnects without inventing streaming intent PASS\n";
}
