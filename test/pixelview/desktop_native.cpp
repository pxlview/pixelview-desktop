#include "frontend/utility/PixelviewDesktop.hpp"
#include <cassert>
using pixelview::Desktop;
static QJsonObject mutation(const char *name, QJsonObject data = {}) { return {{"mutation", name}, {"data", data}}; }
static QJsonObject started(QJsonValue whip = QJsonObject{{"endpoint", "https://example.com/whip"}, {"bearer_token", "secret"}}) {
 return mutation("DESKTOP_STARTED", {{"config", QJsonObject{{"whip", whip}, {"srt", QJsonObject{{"host", "localhost"}}}}}});
}
static const QJsonObject READY = mutation("DESKTOP_READY", {{"desktop_id", "desktop-a"}, {"node_id", "node-a"}});
static const QJsonObject PING = mutation("SOCKET_SEND_PING");
struct Fixture {
 Desktop d; QString last; QJsonObject pong; int starts = 0, halts = 0, requests = 0;
 Fixture() {
  d.send = [&](QJsonObject o) { last = o["message"].toString(); if (last == "PONG_RESPONSE") pong = o["data"].toObject(); if (last == "DESKTOP_START") ++requests; };
  d.publish = [&](QString, QString) { ++starts; };
  d.halt = [&] { ++halts; };
  d.report = [&] { return QJsonObject{{"streaming", d.started}, {"settings", QJsonValue::Null}}; };
 }
 void stream() { d.receive(READY, 0); assert(d.requestStart(1)); d.receive(started(), 2); assert(d.started && starts == 1); }
};
int main() {
 assert(Desktop::validOrigin(QUrl("https://example.com"), false));
 assert(!Desktop::validOrigin(QUrl("http://localhost:18080"), false));
 assert(Desktop::validOrigin(QUrl("http://127.0.0.1:18080"), true));
 assert(!Desktop::validOrigin(QUrl("http://evil.test"), true));
 assert(!Desktop::validOrigin(QUrl("https://user:secret@example.com/path?q=x"), true));
 Fixture f;
 assert(!f.d.requestStart(0));
 f.d.receive(PING, 0); assert(f.last == "PONG_RESPONSE" && f.d.pingDeadline == 0 && !f.d.ready); // Ping may precede ready.
 f.d.receive(READY, 0);
 assert(f.d.ready && f.d.pingDeadline == Desktop::PING_SILENCE_MS && f.requests == 0);
 f.d.receive(PING, 1000);
 assert(f.last == "PONG_RESPONSE" && f.pong == QJsonObject({{"streaming", false}, {"settings", QJsonValue::Null}}));
 assert(f.d.pingDeadline == 1000 + Desktop::PING_SILENCE_MS);
 assert(f.d.requestStart(1)); assert(f.last == "DESKTOP_START" && f.d.pending && f.starts == 0);
 assert(!f.d.requestStart(1)); // One outstanding start.
 f.d.receive(started(), 2);
 assert(f.starts == 1 && f.d.started && !f.d.pending && f.d.authorized(3));
 f.d.receive(PING, 3); assert(f.pong["streaming"].toBool());
 f.d.receive(started(), 4); assert(f.starts == 1); // Only a pending request consumes a grant.
 f.d.outputStopped();
 assert(f.last == "DESKTOP_STOP" && !f.d.started && !f.d.intent && !f.d.authorized(5) && f.d.ready);
 f.d.receive(mutation("DESKTOP_STOPPED"), 6); assert(f.d.ready && f.halts == 0);
 // Stopping is local: a new start needs no acknowledgement.
 assert(f.d.requestStart(7) && f.requests == 2);
 f.d.receive(started(QJsonValue::Null), 8);
 assert(f.starts == 1 && f.halts == 1 && !f.d.ready && !f.d.intent && !f.d.authorized(9));
 // Server silence for PING_SILENCE_MS is a dead socket, reported once.
 f.d.receive(READY, 20000);
 assert(!f.d.pingExpired(20000 + Desktop::PING_SILENCE_MS - 1));
 assert(f.d.pingExpired(20000 + Desktop::PING_SILENCE_MS));
 assert(!f.d.pingExpired(20000 + Desktop::PING_SILENCE_MS + 1) && f.d.ready);
 assert(!f.d.pingExpired(99999999)); // Cleared until the next ping or ready.
 f.d.receive(READY, 90000); assert(f.d.pingDeadline == 0); // Already ready: ignored.
 for (const char *code : {"active_session_required", "node_paused", "subscription_required", "start_failed", "unknown_message", "other"}) {
  Fixture e; e.d.receive(READY, 0); assert(e.d.requestStart(1));
  e.d.receive(mutation("DESKTOP_ERROR", {{"code", code}}), 2);
  assert(!e.d.ready && !e.d.intent && !e.d.pending && e.halts == 1 && e.starts == 0);
 }
 Fixture u; u.d.receive(READY, 0); u.d.receive(mutation("SOMETHING_NEW"), 1); u.d.receive({{"type", "ready"}}, 2);
 assert(u.d.ready && u.halts == 0); // Unknown or legacy messages are ignored, never fatal.
 Fixture r; r.stream(); r.d.receive(mutation("SOCKET_DESKTOP_REVOKED"), 3);
 assert(r.d.revoked && !r.d.ready && !r.d.intent && !r.d.started && r.halts == 1);
 Fixture p; p.stream(); p.d.receive(mutation("SOCKET_DESKTOP_REPLACED"), 3);
 assert(p.d.replaced && !p.d.ready && p.d.started && p.d.intent && p.d.authorized(4) && p.halts == 0);
 Fixture q; q.d.receive(READY, 0); q.d.receive(mutation("SOCKET_DESKTOP_REPLACED"), 1);
 assert(q.d.replaced && !q.d.ready && q.halts == 1 && !q.d.intent);
 // A control drop while streaming keeps media and never re-requests a start.
 Fixture c; c.stream();
 assert(c.d.controlLost() && !c.d.ready && !c.d.pending && c.d.started && c.d.authorized(0) && c.halts == 0);
 c.d.receive(READY, 100); assert(c.d.ready && c.requests == 1 && c.d.pingDeadline == 100 + Desktop::PING_SILENCE_MS);
 c.d.receive(PING, 200); assert(c.pong["streaming"].toBool());
 Fixture n; n.d.receive(READY, 0); assert(n.d.requestStart(1));
 assert(!n.d.controlLost()); // No media yet: the caller fails the attempt and retries.
 Fixture m; m.stream(); m.d.mediaStopped("media failed");
 assert(!m.d.controlLost() && m.d.ready && !m.d.intent && m.halts == 1);
 pixelview::DesktopIdentity identity;
 assert(identity.label(false, false) == "Not paired");
 assert(identity.accept({{"desktop_id", "desktop-a"}, {"node_id", "node-a"}}));
 assert(identity.label(false, false).contains("Paired · Node node-a"));
 assert(identity.label(false, false).contains("Disconnected"));
 assert(identity.label(true, true).contains("Streaming"));
 assert(!identity.accept({{"desktop_id", "desktop-b"}, {"node_id", "node-b"}}));
 identity.clear(); assert(identity.nodeId.isEmpty() && identity.desktopId.isEmpty());
 assert(identity.label(false, false) == "Not paired");
}
