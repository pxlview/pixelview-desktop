#include "frontend/utility/PixelviewDesktop.hpp"
#include <cassert>
#include <string>
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
 assert(f.starts == 1 && f.halts == 1 && f.d.ready && !f.d.intent && !f.d.authorized(9)); // Unusable WHIP config: still online.
 // Server silence for PING_SILENCE_MS is a dead socket, reported once.
 f.d.receive(PING, 20000);
 assert(!f.d.pingExpired(20000 + Desktop::PING_SILENCE_MS - 1));
 assert(f.d.pingExpired(20000 + Desktop::PING_SILENCE_MS));
 assert(!f.d.pingExpired(20000 + Desktop::PING_SILENCE_MS + 1) && f.d.ready);
 assert(!f.d.pingExpired(99999999)); // Cleared until the next ping or ready.
 f.d.receive(READY, 90000); assert(f.d.pingDeadline == 0); // Already ready: ignored.
 for (const char *code : {"active_session_required", "subscription_required", "start_failed", "unknown_message", "other"}) {
  Fixture e; e.d.receive(READY, 0); assert(e.d.requestStart(1));
  e.d.receive(mutation("DESKTOP_ERROR", {{"code", code}}), 2);
  if (std::string(code) == "unknown_message") { assert(e.d.ready && e.d.intent && e.d.pending && e.halts == 0); continue; }
  // A refused start keeps the socket ready (online), and Start works again at once.
  assert(e.d.ready && !e.d.intent && !e.d.pending && e.halts == 1 && e.starts == 0);
  assert(e.d.requestStart(3) && e.d.pending);
 }
 // An older backend's unknown_message (e.g. for DESKTOP_STATE) never stops a stream.
 Fixture old; old.stream(); old.d.receive(mutation("DESKTOP_ERROR", {{"code", "unknown_message"}}), 3);
 assert(old.d.ready && old.d.started && old.d.intent && old.halts == 0);
 Fixture idle; idle.d.receive(READY, 0); idle.d.receive(mutation("DESKTOP_ERROR", {{"code", "start_failed"}}), 1);
 assert(idle.d.ready && idle.halts == 0);
 // Stop on a ready socket (CancelPixelviewStart) ends only the stream: the
 // socket stays ready, DESKTOP_STOP follows the output stop, Start works again.
 Fixture stop; stop.stream(); stop.d.mediaStopped("Stream stopped.");
 assert(stop.d.ready && !stop.d.intent && !stop.d.authorized(3) && stop.halts == 1);
 stop.d.outputStopped(); assert(stop.last == "DESKTOP_STOP" && stop.d.ready && !stop.d.started);
 assert(stop.d.requestStart(4));
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
 // Region profile policy.
 {
  Fixture p; int policies = 0; QJsonObject start;
  p.d.policy = [&] { ++policies; };
  p.d.profile = [] { return QStringLiteral("main42210"); };
  const auto send = p.d.send;
  p.d.send = [&](QJsonObject o) { if (o["message"] == "DESKTOP_START") start = o["data"].toObject(); send(o); };
  const QJsonArray blocked{"main44410", "main42210", "main42210", 7};
  p.d.receive(mutation("DESKTOP_READY", {{"desktop_id", "desktop-a"}, {"node_id", "node-a"}, {"blocked_profiles", blocked}}), 0);
  assert(p.d.ready && policies == 1 && p.d.blockedProfiles == QStringList({"main42210", "main44410"}));
  // The start names the profile; a refusal keeps the socket ready and re-applies the known list.
  assert(p.d.requestStart(1) && start["encoder_profile"] == "main42210");
  p.d.receive(mutation("DESKTOP_ERROR", {{"code", "profile_blocked"}, {"profile", "main42210"}, {"blocked_profiles", blocked}}), 2);
  assert(p.d.ready && !p.d.intent && !p.d.pending && p.halts == 1 && policies == 2);
  // DESKTOP_STARTED refreshes the list (here: region moved, nothing blocked).
  p.d.profile = [] { return QString(); };
  assert(p.d.requestStart(3) && !start.contains("encoder_profile"));
  p.d.receive(mutation("DESKTOP_STARTED", {{"config", QJsonObject{{"whip", QJsonObject{{"endpoint", "https://example.com/whip"}, {"bearer_token", "secret"}}}}}, {"blocked_profiles", QJsonArray{}}}), 4);
  assert(p.d.started && p.d.blockedProfiles.isEmpty() && policies == 3);
  // A backend without the policy sends nothing: nothing is blocked, no callback.
  Fixture old; int oldPolicies = 0; old.d.policy = [&] { ++oldPolicies; };
  old.stream(); assert(old.d.blockedProfiles.isEmpty() && oldPolicies == 0);
 }
 // Node colour profile: READY, STARTED (before publish) and DESKTOP_COLOR; changes only.
 {
  Fixture p; QStringList seen; bool publishedAfterColor = false;
  p.d.colorChanged = [&] { seen.append(p.d.colorProfile); };
  const auto publish = p.d.publish;
  p.d.publish = [&](QString e, QString b) { publishedAfterColor = p.d.colorProfile == "REC_2020_HLG"; publish(e, b); };
  auto color = [](const char *profile) { return QJsonObject{{"profile", profile}, {"max_luminance", 1000}}; };
  p.d.receive(mutation("DESKTOP_READY", {{"desktop_id", "desktop-a"}, {"node_id", "node-a"}, {"color", color("REC_2020_PQ")}}), 0);
  assert(p.d.colorProfile == "REC_2020_PQ" && seen == QStringList({"REC_2020_PQ"}));
  p.d.receive(mutation("DESKTOP_COLOR", {{"color", color("REC_2020_PQ")}}), 1);
  assert(seen.size() == 1); // unchanged: no callback
  p.d.receive(mutation("DESKTOP_COLOR", {{"color", color("P3D65_PQ")}}), 2);
  assert(seen == QStringList({"REC_2020_PQ", "P3D65_PQ"}));
  assert(p.d.requestStart(3));
  p.d.receive(mutation("DESKTOP_STARTED", {{"config", QJsonObject{{"whip", QJsonObject{{"endpoint", "https://example.com/whip"}, {"bearer_token", "secret"}}}}}, {"color", color("REC_2020_HLG")}}), 4);
  assert(p.d.started && publishedAfterColor && seen.last() == "REC_2020_HLG");
  // Malformed or missing colour leaves the profile as it is.
  p.d.receive(mutation("DESKTOP_COLOR", {{"color", "REC_709"}}), 5);
  p.d.receive(mutation("DESKTOP_COLOR", {{"color", QJsonObject{{"profile", ""}}}}), 6);
  assert(p.d.colorProfile == "REC_2020_HLG" && seen.size() == 3);
  // The max luminance alone is a change; a missing one reads as 0 (1000 nits).
  assert(p.d.colorPeakNits == 1000);
  p.d.receive(mutation("DESKTOP_COLOR", {{"color", QJsonObject{{"profile", "REC_2020_HLG"}, {"max_luminance", 4000}}}}), 7);
  assert(p.d.colorPeakNits == 4000 && seen.size() == 4);
  p.d.receive(mutation("DESKTOP_COLOR", {{"color", QJsonObject{{"profile", "REC_2020_HLG"}}}}), 8);
  assert(p.d.colorPeakNits == 0 && seen.size() == 5);
  Fixture old; int oldColors = 0; old.d.colorChanged = [&] { ++oldColors; };
  old.stream(); assert(old.d.colorProfile.isEmpty() && oldColors == 0);
 }
 // Session watermark: READY, STARTED (before publish) and DESKTOP_WATERMARK.
 {
  Fixture p; int changes = 0; bool knownAtPublish = false, enabledAtPublish = false;
  p.d.watermarkChanged = [&] { ++changes; };
  const auto publish = p.d.publish;
  p.d.publish = [&](QString e, QString b) { knownAtPublish = p.d.watermarkKnown; enabledAtPublish = p.d.watermarkEnabled; publish(e, b); };
  auto on = [](const char *id) { return QJsonObject{{"enabled", true}, {"id", id}}; };
  const QJsonObject off{{"enabled", false}, {"id", ""}};
  p.d.receive(mutation("DESKTOP_READY", {{"desktop_id", "desktop-a"}, {"node_id", "node-a"}, {"watermark", off}}), 0);
  assert(p.d.watermarkKnown && !p.d.watermarkEnabled && changes == 1);
  p.d.receive(mutation("DESKTOP_WATERMARK", {{"watermark", off}}), 1);
  assert(changes == 1); // unchanged: no callback
  p.d.receive(mutation("DESKTOP_WATERMARK", {{"watermark", on("a1")}}), 2);
  assert(p.d.watermarkEnabled && p.d.watermarkId == "a1" && changes == 2);
  // A malformed push changes nothing: enabled without an id, or no object.
  p.d.receive(mutation("DESKTOP_WATERMARK", {{"watermark", QJsonObject{{"enabled", true}}}}), 3);
  p.d.receive(mutation("DESKTOP_WATERMARK", {{"watermark", "on"}}), 3);
  p.d.receive(mutation("DESKTOP_WATERMARK", QJsonObject{}), 3);
  assert(p.d.watermarkKnown && p.d.watermarkId == "a1" && changes == 2);
  assert(p.d.requestStart(4));
  p.d.receive(mutation("DESKTOP_STARTED", {{"config", QJsonObject{{"whip", QJsonObject{{"endpoint", "https://example.com/whip"}, {"bearer_token", "secret"}}}}}, {"watermark", on("b2")}}), 5);
  assert(p.d.started && knownAtPublish && enabledAtPublish && p.d.watermarkId == "b2" && changes == 3);
  // While streaming the push still applies (the stream follows it live).
  p.d.receive(mutation("DESKTOP_WATERMARK", {{"watermark", off}}), 6);
  assert(!p.d.watermarkEnabled && p.d.watermarkId.isEmpty() && changes == 4);
  // A STARTED without the field (lookup failed) is unknown: the engine keeps it.
  Fixture q; q.d.receive(mutation("DESKTOP_READY", {{"desktop_id", "desktop-a"}, {"node_id", "node-a"}, {"watermark", on("c3")}}), 0);
  assert(q.d.watermarkKnown && q.d.requestStart(1));
  q.d.receive(started(), 2);
  assert(q.d.started && !q.d.watermarkKnown && !q.d.watermarkEnabled);
  // An older backend never sends it.
  Fixture old; int oldChanges = 0; old.d.watermarkChanged = [&] { ++oldChanges; };
  old.stream(); assert(!old.d.watermarkKnown && oldChanges == 0);
 }
}
