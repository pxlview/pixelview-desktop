#include "PixelviewReceiver.hpp"
#include <QtCore/QJsonDocument>
#include <QtCore/QPointer>
#include <QtCore/QUuid>
#include <QtCore/QUrlQuery>
#include <algorithm>
namespace pixelview {
static bool loopback(const QUrl &u)
{
 return u.host()=="localhost" || u.host()=="127.0.0.1" || u.host()=="::1";
}
static bool safeUrl(const QUrl &u, bool dev)
{
 return u.isValid() && !u.host().isEmpty() && u.userInfo().isEmpty() && !u.hasFragment() &&
        (u.scheme()=="https" || (dev && u.scheme()=="http" && loopback(u)));
}
PixelviewReceiver::PixelviewReceiver(QObject *parent, std::unique_ptr<ReceiverTransport> t)
 : QObject(parent), transport(t ? std::move(t) : makeReceiverTransport())
{
 deadline.setSingleShot(true); retry.setSingleShot(true); refresh.setSingleShot(true);
 deadline.setParent(this); deadline.setObjectName("receiverDeadline");
 refresh.setParent(this); refresh.setObjectName("receiverRefresh");
 connect(&deadline,&QTimer::timeout,this,[this]{if(intent) disconnected(0);});
 connect(&refresh,&QTimer::timeout,this,[this]{if(intent) disconnected(0);});
 connect(&retry,&QTimer::timeout,this,[this]{if(intent) authenticate();});
}
PixelviewReceiver::~PixelviewReceiver()
{
 onEndpoint={}; onStopped={}; onChanged={}; stop();
}
void PixelviewReceiver::change(State s,const QString &v)
{
 current=s; text=v; if(onChanged) onChanged();
}
bool PixelviewReceiver::setOrigin(const QUrl &u,bool dev)
{
 if(intent || !safeUrl(u,dev) || u.hasQuery() || (!u.path().isEmpty() && u.path()!="/")) return false;
 origin=u; origin.setPath(""); development=dev; return true;
}
void PixelviewReceiver::start(const QString &id,const QString &pw,const QString &n)
{
 if(intent) return;
 if(id.isEmpty() || id.size()>256 || pw.isEmpty() || pw.size()>4096 || n.trimmed().isEmpty() || n.size()>256) {
  fail("Enter a session ID, password and receiver name."); return;
 }
 sessionId=id; password=pw; name=n; viewerId=QUuid::createUuid().toString(QUuid::WithoutBraces);
 intent=true; authenticate();
}
void PixelviewReceiver::authenticate()
{
 const auto g=++generation; QPointer<PixelviewReceiver> self(this);
 change(State::Authenticating,"Authenticating receiver…");
 if(!intent || generation!=g) return;
 deadline.start(30000);
 ReceiverTransport::Events e;
 e.login=[self,g](int code,QByteArray body){if(self && self->intent && self->generation==g) self->loginFinished(code,body);};
 e.opened=[self,g]{if(self && self->intent && self->generation==g) self->send("ADD_VIEWER_WEB",{{"name",self->name},{"viewer_id",self->viewerId},{"initial_load",false}});};
 e.message=[self,g](QByteArray body){if(self && self->intent && self->generation==g) self->message(body);};
 e.closed=[self,g](int code){if(self && self->intent && self->generation==g) self->disconnected(code);};
 auto url=origin; url.setPath("/login/player");
 auto body=QJsonDocument(QJsonObject{{"session_id",sessionId},{"password",password},{"name",name},{"device_type","WEB"},{"platform","MAC"},{"browser","GSTREAMER"},{"mobile",false}}).toJson(QJsonDocument::Compact);
 transport->login(url,body,std::move(e));
}
void PixelviewReceiver::loginFinished(int code,const QByteArray &body)
{
 if(current!=State::Authenticating) return;
 if(code==0 || code==408 || code==429 || code==502 || code==503 || code==504) {disconnected(0);return;}
 if(code!=200) {fail("Receiver login rejected. Check session and password."); return;}
 if(body.size()>262144) {fail("Invalid receiver login response.");return;}
 auto o=QJsonDocument::fromJson(body).object();
 QUrl url(o["stream_url"].toString(),QUrl::StrictMode);
 const auto token=o["client_token"].toString();
 if(o["player"].toString()!="WHEP" || token.isEmpty() || token.size()>8192 || !safeUrl(url,development) ||
    o["stream_url"].toString().size()>16384 || QUrlQuery(url).hasQueryItem("viewer_id")) {fail("Invalid or unsupported receiver login response."); return;}
 // Preserve server-provided path, token and all existing query bytes.
 auto encoded=url.toEncoded(); if(!url.hasQuery()) encoded+='?'; else if(!url.query().isEmpty()) encoded+='&';
 encoded += "viewer_id="+QUrl::toPercentEncoding(viewerId); endpoint=QString::fromUtf8(encoded);
 auto ws=origin; ws.setScheme(origin.scheme()=="https" ? "wss" : "ws"); ws.setPath("/wsocket");
 ws.setQuery("token="+QString::fromLatin1(QUrl::toPercentEncoding(token)));
 deadline.start(15000);
 const auto g=generation;
 change(State::Registering,"Registering receiver…");
 if(intent && generation==g) transport->open(ws);
}
void PixelviewReceiver::send(const QString &kind,const QJsonObject &data)
{
 transport->send(QJsonDocument(QJsonObject{{"message",kind},{"data",data}}).toJson(QJsonDocument::Compact));
}
void PixelviewReceiver::message(const QByteArray &body)
{
 QJsonParseError error;
 const auto doc=QJsonDocument::fromJson(body,&error);
 if(body.size()>262144 || error.error!=QJsonParseError::NoError || !doc.isObject()) {fail("Invalid receiver control response.");return;}
 const auto o=doc.object(); const auto kind=o["mutation"].toString();
 if(kind.isEmpty()) {fail("Invalid receiver control response.");return;}
 if(kind=="SOCKET_TOKEN_EXPIRED" || kind=="SOCKET_TOKEN_INVALID") {disconnected(1008);return;}
 if(kind=="SOCKET_SEND_PING") {if(current==State::Ready) deadline.start(65000);send("PONG_RESPONSE"); return;}
 if(kind=="SOCKET_ADD_VIEWER_WEB" && current==State::Registering) {
  if(o["data"].toObject()["status"]!="success") {fail("Receiver registration rejected."); return;}
  retryCount=0; deadline.start(65000); refresh.start(23*60*60*1000);
  const auto g=generation;
  change(State::Ready,"Receiver authorized.");
  if(intent && generation==g && onEndpoint) {endpointDelivered=true; const auto value=endpoint;onEndpoint(value);}
 }
}
void PixelviewReceiver::clearAttempt()
{
 ++generation; deadline.stop(); retry.stop(); refresh.stop(); transport->cancel(); endpoint.clear();
 const bool delivered=endpointDelivered; endpointDelivered=false; if(delivered && onStopped) onStopped();
}
void PixelviewReceiver::stop()
{
 intent=false; clearAttempt(); sessionId.clear(); password.clear(); name.clear(); viewerId.clear(); retryCount=0;
 change(State::Idle,"Not receiving.");
}
void PixelviewReceiver::fail(const QString &reason)
{
 stop(); change(State::Error,reason);
}
void PixelviewReceiver::disconnected(int code)
{
 if(!intent) return;
 if(code==1008 || code==4401 || code==4403 || code==4001) {fail("Receiver authorization expired or rejected. Start again to sign in."); return;}
 clearAttempt();
 if(!intent) return;
 change(State::Reconnecting,"Receiver disconnected. Reconnecting…");
 if(intent) retry.start(std::min(30000,1000 << std::min(retryCount++,5)));
}
}
