#include "frontend/utility/PixelviewDesktopConnection.hpp"
#include "frontend/utility/PixelviewReceiver.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#import <Foundation/Foundation.h>
#include <cassert>
#include <iostream>
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv); assert(argc==3);
 bool sender=QString::fromUtf8(argv[2]).startsWith("sender");
 bool denied=QString::fromUtf8(argv[2]).endsWith("denied");
 bool delayed=QString::fromUtf8(argv[2]).endsWith("delay");
 pixelview::DesktopConnection desktop; pixelview::PixelviewReceiver receiver;
 int lost=0, endpoints=0, stopped=0;
 desktop.message=[](QByteArray){};
 desktop.disconnected=[&](int code){assert(denied ? code==4403 : (code==0 || code==1006));++lost;desktop.closeSocket(false);};
 receiver.onEndpoint=[&](const QString &){++endpoints;}; receiver.onStopped=[&]{++stopped;};
 if(sender) {QUrl ws(QString::fromUtf8(argv[1])+"/desktop/ws");ws.setScheme("ws");desktop.openSocket(ws,"fixture",17);}
 else {assert(receiver.setOrigin(QUrl(QString::fromUtf8(argv[1])),true));receiver.start("fixture-session","fixture-password","test");}
 QElapsedTimer clock; clock.start();
 while(clock.elapsed()<(delayed ? 19500 : 13500)) {
  QCoreApplication::processEvents(); CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);
  if(!sender && receiver.state()==pixelview::PixelviewReceiver::State::Reconnecting) {++lost;break;}
  if(denied && !sender && receiver.state()==pixelview::PixelviewReceiver::State::Error) {++lost;break;}
  if(sender && lost) break;
 }
 assert(lost==1);
 if(denied) {assert(endpoints==0);assert(sender || receiver.state()==pixelview::PixelviewReceiver::State::Error);std::cout<<"terminal upgrade rejection: PASS\n";return 0;}
 assert(clock.elapsed()>=(delayed ? 14900 : 9900) && clock.elapsed()<(delayed ? 19500 : 13500));
 if(!sender) {assert(endpoints==1 && stopped==0);receiver.stop();assert(stopped==1);}
 std::cout<<(sender ? "sender" : "receiver")<<" native unanswered RFC6455 ping detected without app heartbeat: PASS\n";
}
