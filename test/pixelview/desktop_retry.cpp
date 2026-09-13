#include "frontend/utility/PixelviewDesktop.hpp"
#include <cassert>
#include <iostream>
using pixelview::Desktop;
static QJsonObject mutation(const char *name, QJsonObject data = {}) { return {{"mutation", name}, {"data", data}}; }
struct Fixture {
 Desktop d;
 qint64 now=0; // Injected monotonic clock: never sleep or access a network.
 int starts=0, stops=0, requests=0, pongs=0;
 QString bearer;
 Fixture() {
  d.monotonic=[&]{return now;};
  d.send=[&](QJsonObject o){if(o["message"]=="DESKTOP_START") ++requests; if(o["message"]=="PONG_RESPONSE") ++pongs;};
  d.publish=[&](QString,QString token){++starts;bearer=token;};
  d.halt=[&]{++stops;};
 }
 void ready() { d.receive(mutation("DESKTOP_READY",{{"desktop_id","d"},{"node_id","n"}}),now); }
 void ping() { d.receive(mutation("SOCKET_SEND_PING"),now); }
 void grant(QString token="first") {
  d.receive(mutation("DESKTOP_STARTED",{{"config",QJsonObject{{"whip",QJsonObject{{"endpoint","https://fixture.invalid/whip"},{"bearer_token",token}}}}}}),now);
 }
 void start() {ready();assert(d.requestStart(now));grant();assert(starts==1);}
};
int main() {
 Fixture f; f.start();
 f.d.fail("Media connection lost.",true);
 assert(f.stops==1 && !f.d.authorized(f.now) && f.d.intent);
 assert(!f.d.takeRetry(f.now,true));
 f.now+=2000;assert(f.d.takeRetry(f.now,true));
 // A reconnected socket must request NEW authority, not publish cached ingest.
 f.ready();
 assert(f.requests==2 && f.starts==1);
 f.grant("fresh");
 assert(f.starts==2 && f.bearer=="fresh");
 std::cout << "media loss recovery requests a fresh start: PASS\n";
 Fixture cancelled; cancelled.start();
 cancelled.d.outputStopped(); // Explicit Stop, not a transport failure.
 cancelled.d.fail("Disconnected",true);
 cancelled.ready();
 assert(cancelled.requests==1 && !cancelled.d.intent);
 Fixture shutdown; shutdown.start();shutdown.d.fail("Stopping before shutdown.");
 shutdown.ready();assert(shutdown.requests==1 && !shutdown.d.intent);
 std::cout << "Stop and shutdown cancel intent: PASS\n";
 Fixture bounded; bounded.start();
 // Native MaxRetries must bound consecutive unsuccessful recovery, not reset on ready.
 for(int i=0;i<21;++i) {
  bounded.d.fail("lost",true);
  bounded.now+=2000;bounded.d.takeRetry(bounded.now,true);bounded.ready();
 }
 assert(!bounded.d.intent && bounded.requests==21);
 std::cout << "retry exhaustion does not reset on reconnect: PASS\n";
 Fixture wait; wait.start();wait.d.retryDelay=7;wait.d.maxRetries=1;
 wait.d.fail("control lost",true);
 assert(wait.d.retryAt==7000 && wait.d.retries==1);
 wait.d.fail("duplicate close",true);
 assert(wait.stops==1 && wait.d.retryAt==7000);
 assert(!wait.d.takeRetry(6999,true));
 assert(!wait.d.takeRetry(7000,false)); // Still draining output/setup.
 assert(wait.d.takeRetry(7000,true));
 assert(!wait.d.takeRetry(7000,true)); // Exactly one connection per attempt.
 wait.now=7000;wait.ready();wait.d.fail("auth/media failed",true);
 assert(!wait.d.intent && !wait.d.takeRetry(99999,true));
 Fixture disabled;disabled.start();disabled.d.reconnect=false;disabled.d.fail("lost",true);
 assert(!disabled.d.intent && !disabled.d.takeRetry(99999,true));
 std::cout << "native retry delay, budget, drain gate and duplicate close: PASS\n";
 Fixture stale;stale.start();const auto setup=stale.d.generation;
 assert(stale.d.acceptSetup(setup,stale.now));
 stale.d.fail("Stop during setup");
 assert(!stale.d.acceptSetup(setup,stale.now));
 stale.ready();assert(stale.d.requestStart(stale.now));stale.grant("new-user-start");
 assert(!stale.d.acceptSetup(setup,stale.now)); // Old callback cannot use a new grant.
 assert(stale.d.acceptSetup(stale.d.generation,stale.now));
 Fixture success;success.start();success.d.fail("lost",true);success.now=2000;
 assert(success.d.takeRetry(success.now,true));success.ready();success.grant();
 assert(success.d.retries==1);success.d.outputStarted();assert(success.d.retries==0);
 for(const char *reason : {"Stop", "ForceStop", "Unpair", "revoked", "identity mismatch", "active_session_required", "node_paused", "subscription_required", "protocol", "Stopping before shutdown."}) {
  Fixture terminal;terminal.start();terminal.d.fail("lost",true);terminal.d.fail(reason);
  assert(!terminal.d.intent && !terminal.d.takeRetry(99999,true));
  terminal.ready();terminal.grant("delayed");assert(terminal.starts==1);
 }
 Fixture pending;pending.ready();assert(pending.d.requestStart(0));pending.d.fail("Stop awaiting start");
 pending.grant("late");assert(pending.starts==0 && !pending.d.intent);
 // Revoked/replaced arrive as mutations; only an upgrade rejected for the token is terminal by code.
 for(int code : {0,1000,1001,1002,1006,1008,1011,1012,1013,4400,4409,4999}) assert(Desktop::transientClose(code));
 for(int code : {4401,4403}) assert(!Desktop::transientClose(code));
 std::cout << "stale setup, awaiting-start Stop, terminal reasons and success reset: PASS\n";
 Fixture relaunched;relaunched.ready();assert(!relaunched.d.intent && relaunched.requests==0);
 Fixture zero;zero.start();zero.d.maxRetries=0;zero.d.fail("lost",true);
 assert(!zero.d.intent && !zero.d.takeRetry(99999,true));
 Fixture immediate;immediate.start();immediate.d.retryDelay=0;immediate.d.fail("lost",true);
 assert(immediate.d.takeRetry(0,true));
 for(const char *code : {"active_session_required","node_paused","subscription_required","unknown_message","unknown"}) {
  Fixture denial;denial.start();denial.d.receive(mutation("DESKTOP_ERROR",{{"code",code}}),0);
  assert(!denial.d.intent && !denial.d.authorized(0) && !denial.d.takeRetry(99999,true));
 }
 std::cout << "relaunch idle, zero settings, actual protocol denial messages: PASS\n";
 // Control loss while streaming: media continues, reconnect needs no new start,
 // pongs resume, and only server silence ends the reconnected socket again.
 Fixture live;live.start();
 assert(live.d.controlLost() && live.d.authorized(1000) && live.stops==0 && !live.d.ready);
 live.now=2000;live.ready();
 assert(live.d.ready && live.d.started && live.requests==1 && live.starts==1 && live.stops==0);
 live.now=2500;live.ping();assert(live.pongs==1 && live.d.pingDeadline==2500+Desktop::PING_SILENCE_MS);
 assert(!live.d.pingExpired(2500+Desktop::PING_SILENCE_MS-1));
 assert(live.d.pingExpired(2500+Desktop::PING_SILENCE_MS) && live.d.controlLost() && live.stops==0 && live.d.authorized(99999));
 Fixture mid;mid.ready();assert(mid.d.requestStart(0));
 assert(!mid.d.controlLost()); // A pending start is retried as a failed attempt.
 mid.d.fail("control lost",true);mid.now=2000;assert(mid.d.takeRetry(mid.now,true));mid.ready();
 assert(mid.requests==2 && mid.d.pending);
 for(bool drain : {false,true}) {
  Fixture ended;ended.start();assert(ended.d.controlLost());
  if(drain) ended.d.outputStopped(); else ended.d.mediaStopped("Native media failed");
  assert(!ended.d.intent && !ended.d.authorized(0));
  ended.ready();assert(ended.requests==1); // No automatic restart after Stop or media failure.
 }
 std::cout<<"control loss keeps media, reconnects without a new start, silence watchdog: PASS\n";
}
