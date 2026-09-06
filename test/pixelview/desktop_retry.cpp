#include "frontend/utility/PixelviewDesktop.hpp"
#include <cassert>
#include <iostream>
using pixelview::Desktop;
struct Fixture {
 Desktop d;
 qint64 now=0; // Injected monotonic clock: never sleep or access a network.
 int starts=0, stops=0, requests=0;
 QString bearer;
 Fixture() {
  d.monotonic=[&]{return now;};
  d.send=[&](QJsonObject o){if(o["type"]=="start") ++requests;};
  d.publish=[&](QString,QString token){++starts;bearer=token;};
  d.halt=[&]{++stops;};
 }
 void ready() { d.receive({{"type","ready"},{"heartbeat_interval",15},{"lease_seconds",45}},now); }
 void grant(QString token="first") {
  d.receive({{"type","started"},{"fence",1},{"lease_expires_at",9999999},
   {"config",QJsonObject{{"whip",QJsonObject{{"endpoint","https://fixture.invalid/whip"},{"bearer_token",token}}}}}},now);
 }
 void start() {ready();assert(d.requestStart(now));grant();assert(starts==1);}
};
int main() {
 Fixture f; f.start();
 f.now=30000; f.d.tick(f.now);
 assert(f.stops==1 && !f.d.authorized(f.now));
 assert(!f.d.takeRetry(f.now,true));
 f.now+=2000;assert(f.d.takeRetry(f.now,true));
 // A newly authenticated connection must request NEW authority, not publish cached ingest.
 f.ready();
 assert(f.requests==2 && f.starts==1);
 f.grant("fresh");
 assert(f.starts==2 && f.bearer=="fresh");
 std::cout << "deadline recovery requires a fresh lease: PASS\n";
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
  bounded.now+=30000;bounded.d.tick(bounded.now);
  bounded.now+=2000;bounded.d.takeRetry(bounded.now,true);bounded.ready();
 }
 assert(!bounded.d.intent && bounded.requests==21);
 std::cout << "retry exhaustion does not reset on authentication: PASS\n";
 Fixture wait; wait.start();wait.d.retryDelay=7;wait.d.maxRetries=1;
 wait.d.fail("control lost",true);
 assert(wait.d.retryAt==7000 && wait.d.retries==1);
 wait.d.fail("duplicate close",true);
 assert(wait.stops==1 && wait.d.retryAt==7000);
 assert(!wait.d.takeRetry(6999,true));
 assert(!wait.d.takeRetry(7000,false)); // Still draining output/setup.
 assert(wait.d.takeRetry(7000,true));
 assert(!wait.d.takeRetry(7000,true)); // Exactly one authentication per attempt.
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
 assert(!stale.d.acceptSetup(setup,stale.now)); // Old callback cannot use a new lease.
 assert(stale.d.acceptSetup(stale.d.generation,stale.now));
 Fixture success;success.start();success.d.fail("lost",true);success.now=2000;
 assert(success.d.takeRetry(success.now,true));success.ready();success.grant();
 assert(success.d.retries==1);success.d.outputStarted();assert(success.d.retries==0);
 for(const char *reason : {"Stop", "ForceStop", "Unpair", "revoked", "identity mismatch", "busy", "active_session_required", "node_paused", "subscription_required", "protocol", "Stopping before shutdown."}) {
  Fixture terminal;terminal.start();terminal.d.fail("lost",true);terminal.d.fail(reason);
  assert(!terminal.d.intent && !terminal.d.takeRetry(99999,true));
  terminal.ready();terminal.grant("delayed");assert(terminal.starts==1);
 }
 Fixture pending;pending.ready();assert(pending.d.requestStart(0));pending.d.fail("Stop awaiting lease");
 pending.grant("late");assert(pending.starts==0 && !pending.d.intent);
 assert(Desktop::transientClose(0) && Desktop::transientClose(1006) && Desktop::transientClose(1012));
 for(int code : {1000,1002,1003,1007,1008,4400,4401,4403,4999}) assert(!Desktop::transientClose(code));
 std::cout << "stale setup, awaiting lease Stop, terminal reasons and success reset: PASS\n";
 Fixture relaunched;relaunched.ready();assert(!relaunched.d.intent && relaunched.requests==0);
 Fixture zero;zero.start();zero.d.maxRetries=0;zero.d.fail("lost",true);
 assert(!zero.d.intent && !zero.d.takeRetry(99999,true));
 Fixture immediate;immediate.start();immediate.d.retryDelay=0;immediate.d.fail("lost",true);
 assert(immediate.d.takeRetry(0,true));
 for(const char *code : {"busy","active_session_required","node_paused","subscription_required","unknown"}) {
  Fixture denial;denial.start();denial.d.receive({{"type","error"},{"code",code}},0);
  assert(!denial.d.intent && !denial.d.authorized(0) && !denial.d.takeRetry(99999,true));
 }
 std::cout << "relaunch idle, zero settings, actual protocol denial messages: PASS\n";
}
