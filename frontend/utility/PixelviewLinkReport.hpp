#pragma once
// Pixelview modification, 2026-10-02: the Desktop's connection report.
//
// Pushed on the control socket as DESKTOP_STATS so the admin can show how
// good the link is: the round trip of the control socket at all times and,
// while streaming, the sent bitrate with the round trip, loss and jitter the
// media server reports back over RTCP. Measurements only; the admin decides
// how to present them. Pure policy, tested in test/pixelview/link_report.cpp.
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <algorithm>
#include <cmath>
namespace pixelview {
// The WHIP output's pixelview_link_stats answer.
struct MediaLink {
 bool reported=false;
 int ageMs=-1, rttMs=-1;
 double lossPct=0, jitterMs=0;
 qint64 lost=0, nacked=0;
};
class LinkReport {
public:
 static constexpr int INTERVAL_MS=2000;
 static constexpr qint64 LOG_INTERVAL_MS=60000;
 // One sample per interval. controlRttMs is -1 until a control ping was answered.
 QJsonObject sample(qint64 now,int controlRttMs,bool streaming,quint64 totalBytes,const MediaLink &link) {
  QJsonValue media=QJsonValue::Null;
  if(streaming) {
   // The first sample of a stream, or a counter that went back, only sets the baseline.
   double kbps=-1;
   if(wasStreaming && totalBytes>=bytes && now>at) kbps=double(totalBytes-bytes)*8.0/double(now-at);
   if(!wasStreaming) {startedAt=now;window={};window.since=now;}
   bytes=totalBytes;at=now;
   QJsonObject m{{"bitrate_kbps",kbps<0 ? QJsonValue(QJsonValue::Null) : QJsonValue(std::round(kbps))},
    {"uptime_s",double((now-startedAt)/1000)},{"reported",link.reported}};
   if(link.reported) {
    m["report_age_ms"]=link.ageMs;
    m["rtt_ms"]=link.rttMs<0 ? QJsonValue(QJsonValue::Null) : QJsonValue(link.rttMs);
    m["loss_pct"]=tenth(link.lossPct);m["jitter_ms"]=tenth(link.jitterMs);
    m["lost"]=double(link.lost);m["nacked"]=double(link.nacked);
   }
   media=m;
   if(kbps>=0) {window.kbps+=kbps;++window.samples;}
   if(link.reported) {
    window.reported=true;window.rtt=link.rttMs;window.maxRtt=std::max(window.maxRtt,link.rttMs);
    window.maxLoss=std::max(window.maxLoss,link.lossPct);window.maxJitter=std::max(window.maxJitter,link.jitterMs);
    window.lost=link.lost;window.nacked=link.nacked;
   }
  }
  wasStreaming=streaming;
  window.control=controlRttMs;
  return {{"version",1},{"control",QJsonObject{{"rtt_ms",controlRttMs<0 ? QJsonValue(QJsonValue::Null) : QJsonValue(controlRttMs)}}},{"media",media}};
 }
 // True when this report differs from the last one sent: every interval while
 // streaming, and otherwise only when the control round trip changed.
 bool take(const QJsonObject &stats) {
  const QByteArray snapshot=QJsonDocument(stats).toJson(QJsonDocument::Compact);
  if(snapshot==pushed) return false;
  pushed=snapshot;return true;
 }
 // A new control socket gets a report at once.
 void forget() {pushed.clear();}
 // One summary per minute of streaming for the application log (and its upload):
 // the average bitrate and the worst round trip, loss and jitter of that minute.
 QString logLine(qint64 now) {
  if(!wasStreaming || now-window.since<LOG_INTERVAL_MS) return {};
  QString line=QStringLiteral("Pixelview link: %1 s").arg((now-window.since)/1000);
  if(window.samples) line+=QStringLiteral(", %1 kb/s sent").arg(std::llround(window.kbps/window.samples));
  if(!window.reported) line+=QStringLiteral(", no receiver report from the media server");
  else {
   line+=window.rtt<0 ? QStringLiteral(", round trip unknown") : QStringLiteral(", round trip %1 ms (max %2)").arg(window.rtt).arg(window.maxRtt);
   line+=QStringLiteral(", loss max %1 %, jitter max %2 ms, %3 lost, %4 resent on request")
    .arg(window.maxLoss,0,'f',1).arg(window.maxJitter,0,'f',1).arg(window.lost).arg(window.nacked);
  }
  line+=window.control<0 ? QStringLiteral(", control round trip unknown") : QStringLiteral(", control round trip %1 ms").arg(window.control);
  window={};window.since=now;
  return line;
 }
private:
 static double tenth(double value) {return std::round(value*10.0)/10.0;}
 struct Window {
  qint64 since=0,lost=0,nacked=0;
  double kbps=0,maxLoss=0,maxJitter=0;
  int samples=0,rtt=-1,maxRtt=-1,control=-1;
  bool reported=false;
 };
 Window window;
 QByteArray pushed;
 bool wasStreaming=false;
 quint64 bytes=0;
 qint64 at=0,startedAt=0;
};
}
