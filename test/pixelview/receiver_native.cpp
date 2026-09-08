#include "frontend/utility/PixelviewReceiver.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QJsonDocument>
#include <QtCore/QUrlQuery>
#include <cassert>
#include <iostream>
using namespace pixelview;
struct Fake : ReceiverTransport {
 QUrl loginUrl, socketUrl; QByteArray loginBody, sent; Events events; int cancels=0;
 void login(const QUrl &u,const QByteArray &b,Events e) override {loginUrl=u;loginBody=b;events=std::move(e);}
 void open(const QUrl &u) override {socketUrl=u;events.opened();}
 void send(const QByteArray &b) override {sent=b;}
 void cancel() override {++cancels;}
};
namespace pixelview { std::unique_ptr<ReceiverTransport> makeReceiverTransport(){return std::make_unique<Fake>();} }
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);
 auto owned=std::make_unique<Fake>(); auto *f=owned.get(); PixelviewReceiver r(nullptr,std::move(owned));
 assert(r.state()==PixelviewReceiver::State::Idle);
 assert(r.setOrigin(QUrl("http://127.0.0.1:8000"),true));
 QString endpoint; int stopped=0;
 r.onEndpoint=[&](const QString &u){endpoint=u;}; r.onStopped=[&]{++stopped;};
 r.start("session","password","host");
 assert(r.state()==PixelviewReceiver::State::Authenticating);
 assert(f->loginUrl.path()=="/login/player");
 assert(QJsonDocument::fromJson(f->loginBody).object()["password"]=="password");
 f->events.login(200,R"({"player":"WHEP","stream_url":"http://localhost:8080/custom/whep?token=a%2Bb%3D&extra=x%2Fy","client_token":"private-token"})");
 assert(r.state()==PixelviewReceiver::State::Registering);
 auto registration=QJsonDocument::fromJson(f->sent).object();
 assert(registration["message"]=="ADD_VIEWER_WEB");
 QString id=registration["data"].toObject()["viewer_id"].toString(); assert(!id.isEmpty());
 assert(endpoint.isEmpty());
 f->events.message(R"({"mutation":"SOCKET_ADD_VIEWER_WEB","data":{"status":"success","con_id":"fixture"}})");
 assert(r.state()==PixelviewReceiver::State::Ready);
 assert(endpoint.startsWith("http://localhost:8080/custom/whep?token=a%2Bb%3D&extra=x%2Fy&viewer_id="));
 f->events.message(R"({"mutation":"SOCKET_SEND_PING","data":{}})");
 assert(QJsonDocument::fromJson(f->sent).object()["message"]=="PONG_RESPONSE");
 r.stop(); assert(r.state()==PixelviewReceiver::State::Idle); assert(stopped==1);
 // Cancellation generations, authenticated reconnect with the same viewer ID.
 r.start("session","password","host"); auto late=f->events;
 r.stop(); late.login(200,R"({"player":"WHEP","stream_url":"https://engine.pixelview.io/whep?token=x","client_token":"t"})");
 assert(r.state()==PixelviewReceiver::State::Idle);
 r.start("session","password","host");
 f->events.login(200,R"({"player":"WHEP","stream_url":"https://engine.pixelview.io/whep?token=x","client_token":"t"})");
 auto before=QJsonDocument::fromJson(f->sent).object()["data"].toObject()["viewer_id"].toString();
 f->events.message(R"({"mutation":"SOCKET_ADD_VIEWER_WEB","data":{"status":"success"}})");
 auto old=f->events; f->events.closed(1006);
 assert(r.state()==PixelviewReceiver::State::Reconnecting); assert(stopped==2);
 old.message(R"({"mutation":"SOCKET_ADD_VIEWER_WEB","data":{"status":"success"}})");
 QEventLoop loop; QTimer::singleShot(1200,&loop,&QEventLoop::quit); loop.exec();
 assert(r.state()==PixelviewReceiver::State::Authenticating);
 f->events.login(200,R"({"player":"WHEP","stream_url":"https://engine.pixelview.io/fresh?token=y","client_token":"new"})");
 assert(QJsonDocument::fromJson(f->sent).object()["data"].toObject()["viewer_id"].toString()==before);
 f->events.closed(1006); r.stop();
 QTimer::singleShot(1200,&loop,&QEventLoop::quit); loop.exec(); assert(r.state()==PixelviewReceiver::State::Idle);
 r.start("session","password","host"); f->events.login(401,"secret error body");
 assert(r.state()==PixelviewReceiver::State::Error); assert(!r.status().contains("secret"));
 r.start("session","password","host"); f->events.login(200,"[]"); assert(r.state()==PixelviewReceiver::State::Error);
 r.start("session","password","host"); f->events.login(200,R"({"player":"WHEP","stream_url":"https://engine.pixelview.io/whep?token=x","client_token":"t"})");
 f->events.closed(1008); assert(r.state()==PixelviewReceiver::State::Error);
 // Malformed/hostile authority and cancellation from UI callbacks.
 r.stop(); assert(!r.setOrigin(QUrl("http://api4.pixelview.io"),true));
 assert(!r.setOrigin(QUrl("https://user:pw@api4.pixelview.io")));
 assert(!r.setOrigin(QUrl("https://api4.pixelview.io/path")));
 assert(r.setOrigin(QUrl("https://api4.pixelview.io")));
 for(const auto &response : {QByteArray(R"({"player":"WHEP","client_token":"t","stream_url":"https://engine.pixelview.io/a?viewer_id=evil"})"), QByteArray(R"({"player":"WHEP","client_token":"t","stream_url":"http://127.0.0.1/a"})"), QByteArray(R"({"player":"WHEP","client_token":42,"stream_url":"https://engine.pixelview.io/a"})")}) {
  r.start("s","p","n");f->events.login(200,response);assert(r.state()==PixelviewReceiver::State::Error);
 }
 r.start("","p","n");assert(r.state()==PixelviewReceiver::State::Error);
 r.start("s","p","n"); auto pending=f->events;pending.login(503,"private upstream failure");assert(r.state()==PixelviewReceiver::State::Reconnecting);r.stop();
 r.onChanged=[&]{if(r.state()==PixelviewReceiver::State::Authenticating)r.stop();};
 const auto previous=f->loginBody; r.start("different","p","n");assert(r.state()==PixelviewReceiver::State::Idle);assert(f->loginBody==previous);r.onChanged={};
 r.start("s","p","n"); f->events.login(200,R"({"player":"WHEP","client_token":"t","stream_url":"https://different-engine.example/a?x=1"})");
 f->events.message("not-json");assert(r.state()==PixelviewReceiver::State::Error);
 r.start("s","p","n");
 auto *deadline=r.findChild<QTimer *>("receiverDeadline");assert(deadline && deadline->isActive());
 QMetaObject::invokeMethod(deadline,"timeout",Qt::DirectConnection);
 assert(r.state()==PixelviewReceiver::State::Reconnecting); r.stop();
 r.start("s","p","n"); f->events.login(200,R"({"player":"WHEP","client_token":"t","stream_url":"https://different-engine.example/a?x=1"})");
 f->events.message(R"({"mutation":"SOCKET_ADD_VIEWER_WEB","data":{"status":"success"}})");
 assert(deadline->isActive());QMetaObject::invokeMethod(deadline,"timeout",Qt::DirectConnection);assert(r.state()==PixelviewReceiver::State::Reconnecting);r.stop();
 r.start("s","p","n"); f->events.login(200,R"({"player":"WHEP","client_token":"t","stream_url":"https://engine.example/a?"})");
 r.onStopped=[&]{r.stop();}; f->events.message(R"({"mutation":"SOCKET_ADD_VIEWER_WEB","data":{"status":"success"}})");
 assert(!endpoint.contains("??"));f->events.closed(1006);assert(r.state()==PixelviewReceiver::State::Idle);
 r.onStopped={};
 std::cout<<"receiver native basic, recovery and security passed\n";
}
