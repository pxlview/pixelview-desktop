#pragma once
#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QString>
#include <QtCore/QUuid>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
// Pixelview modification, 2026-09-29: ship the application log to the backend
// (POST /desktop/logs, backend docs/desktop-log-ingestion.md), which forwards
// it to the central Loki. QtCore only, so tests compile it standalone.
namespace pixelview {

struct CapturedLine {
 qint64 seq=0, ts=0; // ts: wall clock, epoch ms
 int level=0;
 std::string text, session, viewer;
};

// Process-wide sink fed by the OBS log handler from any thread, from the first
// log line on. It runs inside blog(), so it must never log itself.
class LogCapture {
public:
 static constexpr size_t MaxBuffered=2000, MaxChars=4096;
 static LogCapture &instance() {static LogCapture capture; return capture;}
 const QString launchId=QUuid::createUuid().toString(QUuid::WithoutBraces);
 void capture(int level,const char *text) {
  if(!enabled.load(std::memory_order_relaxed) || !text) return;
  const qint64 now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  std::lock_guard<std::mutex> lock(mutex);
  if(lines.size()>=MaxBuffered) {lines.pop_front();++dropped;}
  lines.push_back({nextSeq++,now,level,std::string(text,strnlen(text,MaxChars)),session,viewer});
 }
 // What the Desktop is receiving; attached to receive lines captured afterwards.
 void setReceiveContext(const QString &sessionId,const QString &viewerId) {
  std::lock_guard<std::mutex> lock(mutex); session=sessionId.toStdString(); viewer=viewerId.toStdString();
 }
 void setEnabled(bool on) {
  enabled=on;
  if(!on) {std::lock_guard<std::mutex> lock(mutex); lines.clear(); dropped=0;}
 }
 bool isEnabled() const {return enabled;}
 std::deque<CapturedLine> take(qint64 &droppedOut) {
  std::lock_guard<std::mutex> lock(mutex);
  droppedOut=dropped; dropped=0;
  std::deque<CapturedLine> out; out.swap(lines); return out;
 }
private:
 LogCapture()=default;
 std::atomic<bool> enabled{true};
 std::mutex mutex;
 std::deque<CapturedLine> lines;
 qint64 nextSeq=0, dropped=0;
 std::string session, viewer;
};

// The backend's identifier rule (1-128 of A-Za-z0-9_-). It rejects a whole
// batch over one malformed value, so nothing else goes on the wire.
inline bool validId(const QString &id) {
 if(id.isEmpty() || id.size()>128) return false;
 return std::all_of(id.begin(),id.end(),[](QChar c){const auto u=c.unicode();
  return (u>='A' && u<='Z') || (u>='a' && u<='z') || (u>='0' && u<='9') || u=='_' || u=='-';});
}

struct ShippedLine {
 qint64 seq=0, ts=0;
 QString level, role, message, session, viewer, launch, version, build;
 qint64 uid=0; // Queue position, local to this process; never spooled or uploaded.
 QJsonObject spool() const {
  QJsonObject o{{"seq",seq},{"ts",ts},{"level",level},{"role",role},{"message",message},
   {"launch",launch},{"version",version},{"build",build}};
  if(!session.isEmpty()) o["session_id"]=session;
  if(!viewer.isEmpty()) o["viewer_id"]=viewer;
  return o;
 }
 QJsonObject upload() const {
  QJsonObject o{{"seq",seq},{"ts",ts},{"level",level},{"role",role},{"message",message}};
  if(role==QLatin1String("receive") && validId(session)) o["session_id"]=session;
  if(role==QLatin1String("receive") && validId(viewer)) o["viewer_id"]=viewer;
  return o;
 }
 static bool fromSpool(const QJsonObject &o,ShippedLine &line) {
  line={o["seq"].toInteger(-1),o["ts"].toInteger(-1),o["level"].toString(),o["role"].toString(),o["message"].toString(),
   o["session_id"].toString(),o["viewer_id"].toString(),o["launch"].toString(),o["version"].toString(),o["build"].toString()};
  return line.seq>=0 && line.ts>0 && !line.launch.isEmpty() && !line.level.isEmpty() && !line.role.isEmpty();
 }
};

// Main-thread shipping policy: redact, classify, spool, batch and back off.
// The caller owns the HTTP request and reports its outcome to finished().
class LogShipper {
public:
 static constexpr int MaxPending=5000, MaxBatchEntries=500, MaxBatchBytes=192*1024;
 // The backend drops entries older than 50 minutes (Loki's out-of-order window).
 static constexpr qint64 MaxAgeMs=50*60*1000, IntervalMs=10000, ErrorDelayMs=2000, SpoolIntervalMs=5000;
 // Floor between uploads for the early triggers (an error line, a full batch):
 // a steady error source must stay well inside the backend's request budget.
 static constexpr qint64 MinSpacingMs=5000;
 // cpu and hardwareModel: e.g. "Apple M1 Pro", "MacBookPro18,1"; at most 64 characters.
 QString launchId, version, build, osVersion, cpu, hardwareModel, home, spoolPath;
 std::function<qint64()> now=[]{return (qint64)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();};
 QList<ShippedLine> pending;
 bool inFlight=false;
 // Names the request in flight; an answer to an earlier one (sharing was
 // switched off and on meanwhile) must not be taken for the current one.
 qint64 flight=0;
 qint64 nextAttempt=0, lastSent=0, urgentAt=0, lastSpool=0;
 int failures=0;
 QString rejectedCredential; // Paused after 401/403 until the credential changes.
 bool dirty=false;

 static QString level(int obsLevel) {
  // libobs: LOG_ERROR 100, LOG_WARNING 200, LOG_INFO 300, LOG_DEBUG 400.
  return obsLevel<=100 ? QStringLiteral("error") : obsLevel<=200 ? QStringLiteral("warning") :
   obsLevel<=300 ? QStringLiteral("info") : QStringLiteral("debug");
 }
 // The allowlist: what may leave the Mac, and under which role. The log file
 // also names scenes, sources, devices and media paths, so only stream
 // diagnostics are uploaded: Pixelview's own lines, the encoders with their
 // settings, the stream output, video and audio settings, DeckLink, and
 // encode/send timing. Empty for every other line, which is not uploaded.
 static QString role(const QString &message) {
  // The unprefixed entries are the failure lines of the stream output, the
  // encoders and the DeckLink output, which libobs and the plugin log bare.
  static const char *receive[]={"[pixelview-whep]","[pixelview-receive]","Pixelview receive","[decklink-output-ui]",
   "[decklink] ","failed to create video frame","failed to schedule video frame","No active audio"};
  static const char *send[]={"[obs-webrtc]","==== Streaming","Pixelview remote control","[pixelview-send]",
   "[VideoToolbox ","[CoreAudio ","[obs-nvenc","[texture-amf-","[qsv encoder","Output '","obs-output '","Stream output type ","Error encoding with encoder '",
   "creating encoder '","Video stopped, number of skipped frames","video_thread("};
  static const char *app[]={"Pixelview","[pixelview-","video settings reset:","audio settings reset:","decklink:",
   "Decklink API","A DeckLink iterator could not be created","Max audio buffering reached","Crash or unclean shutdown detected"};
  // "[x264 encoder: 'name']", "[FFmpeg libopus encoder: 'name']", ...
  static const QRegularExpression encoder(QStringLiteral("^\\[[^\\]\\n]{0,64} encoder: '"));
  // Indented profiler rows under video_thread at shutdown, e.g.
  // "  ┗encode(advanced_video_stream): min=...".
  static const char *timing[]={"receive_video:","receive_audio:","do_encode:","encode(","send_packet:"};
  for(auto *prefix:receive) if(message.startsWith(QLatin1String(prefix))) return QStringLiteral("receive");
  for(auto *prefix:send) if(message.startsWith(QLatin1String(prefix))) return QStringLiteral("send");
  if(encoder.match(message).hasMatch()) return QStringLiteral("send");
  for(auto *prefix:app) if(message.startsWith(QLatin1String(prefix))) return QStringLiteral("app");
  if(message.startsWith(QLatin1String("adding ")) && message.contains(QLatin1String(" of audio buffering"))) return QStringLiteral("app");
  if(message.startsWith(QLatin1String("Source ")) && message.contains(QLatin1String(" audio is lagging (over by "))) return QStringLiteral("app");
  qsizetype at=0;
  while(at<message.size() && QStringLiteral(" \u2523\u2517\u2503").contains(message[at])) ++at;
  if(at) for(auto *prefix:timing) if(QStringView(message).mid(at).startsWith(QLatin1String(prefix))) return QStringLiteral("send");
  return {};
 }
 static QString redact(QString text,const QString &home) {
  static const QRegularExpression device(QStringLiteral("\\b[A-Za-z0-9_-]{1,64}\\.[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\.[A-Za-z0-9_-]{43}\\b"));
  static const QRegularExpression bearer(QStringLiteral("\\b([Bb]earer)\\s+[A-Za-z0-9._~+/=-]+"));
  static const QRegularExpression query(QStringLiteral("\\b((?:https?|wss?|srt|rtmps?)://[^\\s?#\"']+)\\?[^\\s\"']*"),QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression pair(QStringLiteral("(authorization|token|password|passphrase|secret|authkey|api[_-]?key)(\\s*[=:]\\s*)([^\\s,;&\"']+)"),QRegularExpression::CaseInsensitiveOption);
  // "adding N milliseconds of audio buffering ... (source: <the user's name for it>)"
  static const QRegularExpression source(QStringLiteral("\\(source: [^\\n]*\\)"));
  // "Source <name> audio is lagging (over by N ms) at max audio buffering..."
  static const QRegularExpression lagging(QStringLiteral("^Source .* audio is lagging \\(over by "));
  if(home.size()>1) {
   text.replace(home,QStringLiteral("~"));
   // Windows log lines use backslashes and may differ in case ("C:\\Users\\Name").
   QString native=home; native.replace('/','\\');
   if(native!=home) text.replace(native,QStringLiteral("~"),Qt::CaseInsensitive);
  }
  text.replace(source,QStringLiteral("(source)"));
  text.replace(lagging,QStringLiteral("Source audio is lagging (over by "));
  text.replace(device,QStringLiteral("[REDACTED]"));
  text.replace(bearer,QStringLiteral("\\1 [REDACTED]"));
  text.replace(query,QStringLiteral("\\1?[REDACTED]"));
  text.replace(pair,QStringLiteral("\\1\\2[REDACTED]"));
  return text;
 }

 void ingest(std::deque<CapturedLine> lines,qint64 captureDropped) {
  const qint64 t=now();
  if(captureDropped>0) append(t,QStringLiteral("warning"),QStringLiteral("app"),
   QStringLiteral("[pixelview-logs] %1 log lines were dropped before upload (capture buffer full)").arg(captureDropped),{},{},-1);
  for(auto &line:lines) {
   const auto raw=QString::fromStdString(line.text);
   const auto r=role(raw);
   if(r.isEmpty()) continue; // Not on the allowlist: stays in the local log file only.
   append(line.ts,level(line.level),r,redact(raw,home),QString::fromStdString(line.session),QString::fromStdString(line.viewer),line.seq);
  }
  prune(t);
 }
 // Previous launches' unsent lines go first; they are older.
 void loadSpool() {
  QFile file(spoolPath);
  if(!file.open(QIODevice::ReadOnly)) return;
  QList<ShippedLine> loaded;
  while(!file.atEnd()) {
   const auto doc=QJsonDocument::fromJson(file.readLine());
   ShippedLine line;
   if(doc.isObject() && ShippedLine::fromSpool(doc.object(),line)) loaded.append(line);
  }
  pending=loaded+pending; dirty=true;
  // Keep the queue ordered by uid; a batch built before this no longer matches.
  for(auto &line:pending) line.uid=nextUid++;
  prune(now());
 }
 bool saveSpool(bool force=false) {
  const qint64 t=now();
  if(!dirty || spoolPath.isEmpty() || (!force && t-lastSpool<SpoolIntervalMs && !urgentAt)) return true;
  if(pending.isEmpty()) {dirty=false; lastSpool=t; return !QFile::exists(spoolPath) || QFile::remove(spoolPath);}
  QSaveFile file(spoolPath);
  if(!file.open(QIODevice::WriteOnly)) return false;
  for(const auto &line:pending) file.write(QJsonDocument(line.spool()).toJson(QJsonDocument::Compact)+'\n');
  if(!file.commit()) return false;
  dirty=false; lastSpool=t; return true;
 }
 void disable() {pending.clear(); inFlight=false; ++flight; urgentAt=0; dirty=false; if(!spoolPath.isEmpty()) QFile::remove(spoolPath);}

 bool due(const QString &credential) const {
  if(pending.isEmpty() || inFlight || credential.isEmpty() || credential==rejectedCredential) return false;
  const qint64 t=now();
  if(t<nextAttempt) return false;
  if(t-lastSent>=IntervalMs) return true;
  return t-lastSent>=MinSpacingMs && (pending.size()>=MaxBatchEntries || (urgentAt && t>=urgentAt));
 }
 // lastUid names the lines in the batch: the queue can lose lines from the
 // front (age, overflow) while the request is in flight.
 struct Batch {QByteArray body; int count=0; qint64 lastUid=0;};
 // One launch per batch: the backend takes launch/version from the batch.
 Batch nextBatch() const {
  Batch batch;
  if(pending.isEmpty()) return batch;
  const auto &first=pending.front();
  QJsonObject o{{"launch_id",first.launch},{"app_version",first.version},{"build",first.build},{"sent_at",now()}};
  if(!osVersion.isEmpty()) o["os_version"]=osVersion;
  if(!cpu.isEmpty()) o["cpu"]=cpu.left(64);
  if(!hardwareModel.isEmpty()) o["hardware_model"]=hardwareModel.left(64);
  QJsonArray entries; qsizetype bytes=512;
  for(const auto &line:pending) {
   if(line.launch!=first.launch || batch.count>=MaxBatchEntries) break;
   const auto entry=line.upload();
   const auto size=QJsonDocument(entry).toJson(QJsonDocument::Compact).size()+1;
   if(batch.count && bytes+size>MaxBatchBytes) break;
   entries.append(entry); bytes+=size; ++batch.count; batch.lastUid=line.uid;
  }
  o["entries"]=entries;
  batch.body=QJsonDocument(o).toJson(QJsonDocument::Compact);
  return batch;
 }
 enum class Result {Sent, Discarded, Paused, Retry};
 // http 0 is a transport failure; retryAfter in seconds, 0 when absent.
 Result finished(int http,int retryAfter,qint64 lastUid,const QString &credential) {
  const qint64 t=now();
  inFlight=false;
  if((http>=200 && http<300) || http==400 || http==413 || http==422) {
   // Accepted, or a batch the backend will never accept: do not resend it.
   pending.erase(pending.begin(),std::find_if(pending.begin(),pending.end(),[lastUid](const ShippedLine &l){return l.uid>lastUid;}));
   dirty=true; failures=0; lastSent=t; nextAttempt=0;
   if(urgentAt && std::none_of(pending.begin(),pending.end(),[](const ShippedLine &l){return l.level==QLatin1String("error");})) urgentAt=0;
   return http<300 ? Result::Sent : Result::Discarded;
  }
  if(http==401 || http==403) {rejectedCredential=credential; return Result::Paused;}
  if(http==404 || http==405) {nextAttempt=t+3600*1000; return Result::Paused;} // Backend without log ingestion.
  if(http==429 || http==503) {nextAttempt=t+std::clamp(retryAfter>0 ? retryAfter : 60,1,3600)*1000LL; return Result::Retry;}
  ++failures;
  nextAttempt=t+std::min<qint64>(300000,IntervalMs<<std::min(failures-1,5));
  return Result::Retry;
 }
private:
 qint64 nextUid=1;
 void append(qint64 ts,const QString &lvl,const QString &r,const QString &message,const QString &session,const QString &viewer,qint64 seq) {
  // Synthetic lines (seq -1) continue after the newest captured sequence.
  if(seq<0) seq=pending.isEmpty() || pending.back().launch!=launchId ? 0 : pending.back().seq;
  pending.append({seq,ts,lvl,r,message,session,viewer,launchId,version,build,nextUid++});
  dirty=true;
  if(lvl==QLatin1String("error") && !urgentAt) urgentAt=now()+ErrorDelayMs;
 }
 void prune(qint64 t) {
  const qint64 oldest=t-MaxAgeMs;
  pending.erase(std::remove_if(pending.begin(),pending.end(),[oldest](const ShippedLine &l){return l.ts<oldest;}),pending.end());
  if(pending.size()>MaxPending) pending.erase(pending.begin(),pending.begin()+(pending.size()-MaxPending));
 }
};
}
