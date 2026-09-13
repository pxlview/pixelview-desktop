#pragma once
#include <QtCore/QUrl>
#include <QtCore/QJsonObject>
#include <functional>
#include <algorithm>
namespace pixelview {
struct DesktopIdentity {
 QString nodeId,desktopId;
 void clear() {nodeId.clear();desktopId.clear();}
 bool accept(QJsonObject object) {
  const auto node=object["node_id"].toString(), desktop=object["desktop_id"].toString();
  if(node.isEmpty() || desktop.isEmpty() || (!nodeId.isEmpty() && (nodeId!=node || desktopId!=desktop))) return false;
  nodeId=node;desktopId=desktop;return true;
 }
 QString label(bool connected,bool streaming) const {
  if(nodeId.isEmpty() || desktopId.isEmpty()) return QStringLiteral("Not paired");
  return QStringLiteral("Paired · Node %1\n%2").arg(nodeId,streaming ? QStringLiteral("Streaming") : connected ? QStringLiteral("Connected") : QStringLiteral("Disconnected"));
 }
};
// Desktop control socket policy (standard player envelope). The server pings
// every 20 s and the Desktop answers each ping with its streaming state; that
// pong is the only liveness report. There is no lease: the engine rejects a
// second publisher itself, and media outlives any control-socket loss.
class Desktop {
public:
 static constexpr qint64 PING_SILENCE_MS=60000;
 std::function<void(QJsonObject)> send = [](QJsonObject){};
 std::function<void(QString,QString)> publish = [](QString,QString){};
 std::function<void()> halt = []{};
 std::function<QJsonObject()> report = []{return QJsonObject{{"streaming",false},{"settings",QJsonValue::Null}};};
 std::function<void(QString)> error=[](QString){};
 std::function<qint64()> monotonic=[] {return qint64(0);};
 bool ready=false, pending=false, started=false;
 bool mediaDraining=false;
 // Terminal mutations arrive before the close that follows them.
 bool revoked=false, replaced=false;
 qint64 pingDeadline=0;
 // Media rejection is not a control failure. Invalidate setup immediately,
 // retaining start bookkeeping until native output/setup have safely drained.
 void mediaStopped(QString message) {
  ++generation;intent=false;retryAt=-1;transientFailure=false;mediaDraining=true;
  halt();error(message);
 }
 // Session-only intent: never persisted with pairing identity.
 bool intent=false;
 bool reconnect=true;
 bool transientFailure=false;
 int retries=0, maxRetries=20, retryDelay=2;
 qint64 retryAt=-1;
 quint64 generation=0;
 bool setupClaimed=false;
 bool acceptSetup(quint64 attempt, qint64 now) const {
  return attempt==generation && intent && authorized(now);
 }
 bool claimSetup(quint64 attempt, qint64 now) {
  if(setupClaimed || !acceptSetup(attempt,now)) return false;
  setupClaimed=true;return true;
 }
 void outputStarted() {if(intent && started) retries=0;}
 // Only an upgrade rejected for the token (HTTP 401/403 mapped by the
 // transport) is terminal by number. Revocation and replacement arrive as
 // mutations before their close, so every other close is a transient drop.
 static bool transientClose(int code) {
  return code!=4401 && code!=4403;
 }
 bool takeRetry(qint64 now, bool drained) {
  if(!intent || retryAt<0 || now<retryAt || !drained) return false;
  retryAt=-1;return true;
 }
 // A control drop while media runs is not a stream failure: forget the socket
 // and let the caller reconnect; the next DESKTOP_READY needs no new start.
 bool controlLost() {
  if(!started || !intent || mediaDraining) return false;
  ready=false; pending=false; pingDeadline=0;
  return true;
 }
 bool pingExpired(qint64 now) {
  if(!ready || !pingDeadline || now<pingDeadline) return false;
  pingDeadline=0; return true;
 }
 bool authorized(qint64) const { return started && !mediaDraining; }
 bool requestStart(qint64) {
  if (!ready || pending || started || mediaDraining) return false;
  if(!intent) retries=0;
  ++generation;setupClaimed=false;intent=true; pending=true;
  send({{"message","DESKTOP_START"},{"data",QJsonObject{}}}); return true;
 }
 void fail(QString message, bool transient=false) {
  if(transient && retryAt>=0) return; // Duplicate transport notifications.
  transientFailure=transient;
  ++generation;
  if(!transient || !reconnect || retries>=maxRetries) intent=false;
  retryAt=-1;
  if(intent) {++retries;retryAt=monotonic()+qint64(std::max(0,retryDelay))*1000;}
  ready=false; pending=false; started=false; mediaDraining=false; pingDeadline=0;
  halt(); error(message);
 }
 bool development=false;
 void receive(const QJsonObject &o, qint64 now) {
  const auto name=o["mutation"].toString();
  const auto data=o["data"].toObject();
  if(name=="DESKTOP_READY") {
   if(ready) return;
   ready=true; pingDeadline=now+PING_SILENCE_MS;
   if(intent && !started && retryAt<0) requestStart(now);
  } else if(name=="SOCKET_SEND_PING") {
   // The server's ping loop can run ahead of DESKTOP_READY; a pong is always
   // the right answer, and presence depends on it.
   if(ready) pingDeadline=now+PING_SILENCE_MS;
   send({{"message","PONG_RESPONSE"},{"data",report()}});
  } else if(name=="DESKTOP_STARTED") {
   if(!ready || !pending) return;
   pending=false;
   if(mediaDraining) return; // Cancelled attempt, not fresh authority.
   const auto whip=data["config"].toObject()["whip"].toObject();
   QUrl endpoint(whip["endpoint"].toString());
   QUrl origin=endpoint; origin.setPath("");
   if(!validOrigin(origin, development) || whip["bearer_token"].toString().isEmpty()) {
    fail("WHIP is unavailable or invalid for this node. SRT is not supported."); return;
   }
   started=true;
   publish(endpoint.toString(),whip["bearer_token"].toString());
  } else if(name=="DESKTOP_ERROR") {
   const QString code=data["code"].toString();
   const QStringList known={"active_session_required","node_paused","subscription_required"};
   fail(known.contains(code) ? "Pixelview: "+code+". Check admin and start manually." : "Pixelview protocol error. Stream stopped.");
  } else if(name=="SOCKET_DESKTOP_REVOKED") {
   revoked=true;
   fail("Device revoked. Pair again in Pixelview admin.");
  } else if(name=="SOCKET_DESKTOP_REPLACED") {
   replaced=true;
   if(started && intent && !mediaDraining) {
    ready=false; pending=false; pingDeadline=0;
    error("Another connection of this device took over control. Streaming continues; restart the app to reconnect.");
   } else fail("Another connection of this device took over control. Restart the app to reconnect.");
  }
  // DESKTOP_STOPPED and unknown mutations need no action.
 }
 void outputStopped() {
  ++generation;intent=false;retryAt=-1;mediaDraining=false;
  const bool release=ready && (started || pending);
  started=pending=false;
  if (release) send({{"message","DESKTOP_STOP"},{"data",QJsonObject{}}});
 }
 static bool validOrigin(const QUrl &u, bool dev) {
  const auto host = u.host();
  return u.isValid() && !host.isEmpty() && u.userInfo().isEmpty() && !u.hasQuery() && !u.hasFragment() &&
   (u.path().isEmpty() || u.path() == "/") &&
   (u.scheme() == "https" || (dev && u.scheme() == "http" && (host == "localhost" || host == "127.0.0.1" || host == "::1")));
 }
};
}
