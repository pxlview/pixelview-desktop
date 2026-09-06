#pragma once
#include <QtCore/QUrl>
#include <QtCore/QJsonObject>
#include <functional>
#include <cmath>
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
class Desktop {
public:
 std::function<void(QJsonObject)> send = [](QJsonObject){};
 std::function<void(QString,QString)> publish = [](QString,QString){};
 std::function<void()> halt = []{};
 bool ready=false, pending=false, leased=false, stopping=false;
 bool mediaDraining=false;
 qint64 stopDeadline=0;
 // Media rejection is not a control failure. Invalidate setup immediately,
 // retaining lease bookkeeping until native output/setup have safely drained.
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
 void outputStarted() {if(intent && leased) retries=0;}
 static bool transientClose(int code) {
  return code==0 || code==1001 || code==1006 || code==1011 || code==1012 || code==1013;
 }
 std::function<qint64()> monotonic=[] {return qint64(0);};
 bool takeRetry(qint64 now, bool drained) {
  if(!intent || retryAt<0 || now<retryAt || !drained) return false;
  retryAt=-1;return true;
 }
 qint64 deadline=0, heartbeatRequest=-1;
 bool heartbeatSent(qint64 now) { if(heartbeatRequest>=0) return false; heartbeatRequest=now; return true; }
 bool authorized(qint64 now) const { return ready && leased && !mediaDraining && now < deadline; }
 bool requestStart(qint64 now) {
  if (!ready || pending || leased || stopping || mediaDraining || now >= deadline) return false;
  if(!intent) retries=0;
  ++generation;setupClaimed=false;intent=true; pending=true; send({{"type","start"}}); return true;
 }
 void fail(QString message, bool transient=false) {
  if(transient && retryAt>=0) return; // Duplicate transport notifications.
  transientFailure=transient;
  ++generation;
  if(!transient || !reconnect || retries>=maxRetries) intent=false;
  retryAt=-1;
  if(intent) {++retries;retryAt=monotonic()+qint64(std::max(0,retryDelay))*1000;}
  ready=false; pending=false; leased=false; stopping=false; mediaDraining=false; deadline=0;
  halt(); error(message);
 }
 std::function<void(QString)> error=[](QString){};
 bool development=false;
 void tick(qint64 now) {
  if(ready && stopping && now>=stopDeadline) fail("Stream stop acknowledgement timed out. Reconnecting control; start manually.",true);
  else if(ready && now>=deadline) fail("Connection acknowledgement expired. Stream stopped.",true);
 }
 void receive(const QJsonObject &o, qint64 now) {
  const auto type=o["type"].toString();
  if(type=="started" && (mediaDraining || stopping)) return; // Cancelled attempt, not fresh authority.
  if (type=="ready" && !ready && o["heartbeat_interval"].toInt()==15 && o["lease_seconds"].toInt()==45) {
   ready=true; deadline=now+30000; heartbeatRequest=-1;
   if(intent && retryAt<0) requestStart(now);
  } else if(type=="started" && ready && pending && now<deadline) {
   pending=false; leased=true;
   const auto whip=o["config"].toObject()["whip"].toObject();
   QUrl endpoint(whip["endpoint"].toString());
   QUrl origin=endpoint; origin.setPath("");
   if(!validOrigin(origin, development) || whip["bearer_token"].toString().isEmpty() ||
      !o["fence"].isDouble() || o["fence"].toDouble()<=0 || std::floor(o["fence"].toDouble())!=o["fence"].toDouble() ||
      !o["lease_expires_at"].isDouble() || !std::isfinite(o["lease_expires_at"].toDouble()) || o["lease_expires_at"].toDouble()<=0) {
    fail("WHIP is unavailable or invalid for this node. SRT is not supported."); return;
   }
   // A response cannot move the deadline beyond the request/ack budget.
   publish(endpoint.toString(),whip["bearer_token"].toString());
  } else if(type=="heartbeat" && ready && now<deadline && heartbeatRequest>=0) {
   if(leased && !o["lease_expires_at"].isDouble()) { fail("Streaming lease lost."); return; }
   deadline=heartbeatRequest+30000; heartbeatRequest=-1;
  } else if(type=="stopped" && ready && stopping && !leased && !pending) {
   stopping=false;
  } else {
   const QString code=o["code"].toString();
   const QStringList known={"busy","active_session_required","node_paused","subscription_required"};
   fail(known.contains(code) ? "Pixelview: "+code+". Check admin and start manually." : "Pixelview protocol error. Stream stopped.");
  }
 }
 void outputStopped() {
  ++generation;intent=false;retryAt=-1;mediaDraining=false;
  const bool release=ready && (leased || pending);
  leased=pending=false;
  if (release) {stopping=true;stopDeadline=monotonic()+30000;send({{"type","stop"}});}
 }
 static bool validOrigin(const QUrl &u, bool dev) {
  const auto host = u.host();
  return u.isValid() && !host.isEmpty() && u.userInfo().isEmpty() && !u.hasQuery() && !u.hasFragment() &&
   (u.path().isEmpty() || u.path() == "/") &&
   (u.scheme() == "https" || (dev && u.scheme() == "http" && (host == "localhost" || host == "127.0.0.1" || host == "::1")));
 }
};
}
