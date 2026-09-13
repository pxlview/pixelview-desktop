// RFC6455 PONGs are not application liveness: 60 s without SOCKET_SEND_PING ends the socket.
#include "frontend/utility/PixelviewDesktopConnection.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#import <CoreFoundation/CoreFoundation.h>
#include <cassert>
#include <iostream>
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);QElapsedTimer clock;clock.start();
 QTimer cocoa;QObject::connect(&cocoa,&QTimer::timeout,[]{CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.001,true);});cocoa.start(5);
 pixelview::DesktopConnection connection;pixelview::Desktop lease;pixelview::DesktopIdentity identity;
 qint64 readyAt=-1;bool expired=false;
 lease.monotonic=[&]{return clock.elapsed();};
 connection.message=[&](QByteArray bytes){auto o=QJsonDocument::fromJson(bytes).object();assert(o["mutation"]=="DESKTOP_READY");assert(identity.accept(o["data"].toObject()));readyAt=clock.elapsed();lease.receive(o,readyAt);};
 connection.disconnected=[&](int){assert(false && "TCP should remain healthy until the ping watchdog expires");};
 QTimer watchdog;QObject::connect(&watchdog,&QTimer::timeout,[&]{
  if(lease.pingExpired(clock.elapsed())) {
   assert(readyAt>=0 && clock.elapsed()-readyAt>=pixelview::Desktop::PING_SILENCE_MS && clock.elapsed()-readyAt<pixelview::Desktop::PING_SILENCE_MS+2000);
   expired=true;connection.closeSocket();app.quit();
  }
 });watchdog.start(100);
 QTimer::singleShot(70000,&app,[&]{app.exit(2);});
 connection.openSocket(QUrl(QString::fromUtf8(argv[1])),"fixture-secret");
 int result=app.exec();assert(result==0 && expired && !identity.nodeId.isEmpty() && lease.ready && !lease.started && !lease.intent);
 std::cout<<"control PONG does not renew application liveness; ping silence expires at 60s, pairing retained PASS\n";
}
