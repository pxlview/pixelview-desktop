#pragma once
#include <QtCore/QUrl>
#include <QtCore/QJsonObject>
#include <functional>
#include <cmath>
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
 qint64 deadline=0, heartbeatRequest=-1;
 bool heartbeatSent(qint64 now) { if(heartbeatRequest>=0) return false; heartbeatRequest=now; return true; }
 bool authorized(qint64 now) const { return ready && leased && now < deadline; }
 bool requestStart(qint64 now) {
  if (!ready || pending || leased || stopping || now >= deadline) return false;
  pending=true; send({{"type","start"}}); return true;
 }
 void fail(QString message) {
  ready=false; pending=false; leased=false; stopping=false; deadline=0;
  halt(); error(message);
 }
 std::function<void(QString)> error=[](QString){};
 bool development=false;
 void tick(qint64 now) { if(ready && now>=deadline) fail("Connection acknowledgement expired. Stream stopped."); }
 void receive(const QJsonObject &o, qint64 now) {
  const auto type=o["type"].toString();
  if (type=="ready" && !ready && o["heartbeat_interval"].toInt()==15 && o["lease_seconds"].toInt()==45) {
   ready=true; deadline=now+30000;
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
  if (ready && (leased || pending)) {stopping=true;send({{"type","stop"}});}
  leased=pending=false;
 }
 static bool validOrigin(const QUrl &u, bool dev) {
  const auto host = u.host();
  return u.isValid() && !host.isEmpty() && u.userInfo().isEmpty() && !u.hasQuery() && !u.hasFragment() &&
   (u.path().isEmpty() || u.path() == "/") &&
   (u.scheme() == "https" || (dev && u.scheme() == "http" && (host == "localhost" || host == "127.0.0.1" || host == "::1")));
 }
};
}
