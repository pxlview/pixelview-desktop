// Compiled against the production controller with its 23-hour budget scaled to 250 ms.
#include "frontend/utility/PixelviewReceiver.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QThread>
#include <cassert>
#include <iostream>
using namespace pixelview;
struct Wire : ReceiverTransport {
 Events events;
 void login(const QUrl &,const QByteArray &,Events e) override {events=std::move(e);}
 void open(const QUrl &) override {events.opened();}
 void send(const QByteArray &) override {}
 void cancel() override {}
};
namespace pixelview { std::unique_ptr<ReceiverTransport> makeReceiverTransport(){return std::make_unique<Wire>();} }
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);
 auto owned=std::make_unique<Wire>();auto *wire=owned.get();PixelviewReceiver receiver(nullptr,std::move(owned));
 int starts=0,stops=0;receiver.onEndpoint=[&](const QString &){++starts;};receiver.onStopped=[&]{++stops;};
 const QByteArray login=R"({"player":"WHEP","client_token":"token","stream_url":"https://engine.example/whep"})";
 const QByteArray registered=R"({"mutation":"SOCKET_ADD_VIEWER_WEB","data":{"status":"success"}})";
 receiver.start("session","password","name");wire->events.login(200,login);wire->events.message(registered);
 assert(starts==1);
 QThread::msleep(150);
 wire->events.closed(1006);
 auto *grace=receiver.findChild<QTimer *>("receiverControlGrace");
 assert(grace && grace->isActive() && grace->interval()<=100);
 // Dispatching timers must stop retained media at the old authorization boundary.
 QEventLoop loop;QTimer::singleShot(150,&loop,&QEventLoop::quit);loop.exec();
 assert(stops==1 && receiver.state()==PixelviewReceiver::State::Error);
 // Repeated HTTP successes/failed registrations cannot renew retained media.
 receiver.start("session","password","name");wire->events.login(200,login);wire->events.message(registered);
 wire->events.closed(1006);
 for(int attempt=0;attempt<3;++attempt) {
  QTimer::singleShot(15,&loop,&QEventLoop::quit);loop.exec();
  assert(receiver.state()==PixelviewReceiver::State::Authenticating);
  wire->events.login(200,login);
  assert(stops==1 && starts==2);
  if(attempt<2) wire->events.closed(1006);
 }
 QThread::msleep(220); // Old authority expired; latest HTTP token still within budget.
 wire->events.message(registered);
 assert(stops==2 && starts==2 && receiver.state()==PixelviewReceiver::State::Error);
 // Initial registration must also use request-start, not response/ACK time.
 receiver.start("session","password","name");wire->events.login(200,login);
 QThread::msleep(260);wire->events.message(registered);
 assert(starts==2 && receiver.state()==PixelviewReceiver::State::Error);
 // Proactive renewal reuses the control recovery path without tearing down media.
 receiver.start("session","password","name");wire->events.login(200,login);wire->events.message(registered);
 QTimer::singleShot(180,&loop,&QEventLoop::quit);loop.exec();
 assert(receiver.state()==PixelviewReceiver::State::Authenticating);
 assert(starts==3 && stops==2);
 wire->events.login(200,login);wire->events.message(registered);
 assert(receiver.state()==PixelviewReceiver::State::Ready && starts==3 && stops==2);
 // The old deadline no longer terminates a successfully registered renewal.
 QTimer::singleShot(90,&loop,&QEventLoop::quit);loop.exec();
 assert(receiver.state()==PixelviewReceiver::State::Ready && stops==2);
 receiver.stop();
 // Renewal login cannot move the independent hard watchdog. Disable only grace
 // to prove that the absolute watchdog itself remains armed across retries.
 receiver.start("session","password","name");wire->events.login(200,login);wire->events.message(registered);
 QTimer::singleShot(180,&loop,&QEventLoop::quit);loop.exec();
 wire->events.login(200,login);
 auto *expiry=receiver.findChild<QTimer *>("receiverAuthorizationExpiry");
 assert(expiry && expiry->isActive() && expiry->remainingTime()<=70);
 grace->stop();
 QTimer::singleShot(100,&loop,&QEventLoop::quit);loop.exec();
 assert(receiver.state()==PixelviewReceiver::State::Error && starts==4 && stops==4);
 // Delayed readiness must reject old authority even before timers dispatch.
 receiver.start("session","password","name");wire->events.login(200,login);wire->events.message(registered);
 QTimer::singleShot(180,&loop,&QEventLoop::quit);loop.exec();
 wire->events.login(200,login);
 QThread::msleep(90);wire->events.message(registered);
 assert(receiver.state()==PixelviewReceiver::State::Error && starts==5 && stops==5);
 std::cout<<"receiver authorization expiry and proactive renewal passed\n";
}
