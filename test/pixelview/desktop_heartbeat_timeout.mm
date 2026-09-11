// RFC6455 PONGs must not renew Desktop application acknowledgement authority.
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
 lease.halt=[&]{assert(readyAt>=0 && clock.elapsed()-readyAt>=30000 && clock.elapsed()-readyAt<32000);expired=true;connection.closeSocket();app.quit();};
 connection.message=[&](QByteArray bytes){auto o=QJsonDocument::fromJson(bytes).object();assert(o["type"]=="ready");assert(identity.accept(o));readyAt=clock.elapsed();lease.receive(o,readyAt);};
 connection.disconnected=[&](int){assert(false && "TCP should remain healthy until app watchdog expires");};
 QTimer heartbeat;heartbeat.setInterval(15000);QObject::connect(&heartbeat,&QTimer::timeout,[&]{if(lease.ready && lease.heartbeatSent(clock.elapsed()))connection.sendSocket("{\"type\":\"heartbeat\",\"streaming\":false}");});heartbeat.start();
 QTimer watchdog;QObject::connect(&watchdog,&QTimer::timeout,[&]{lease.tick(clock.elapsed());});watchdog.start(100);
 QTimer::singleShot(35000,&app,[&]{app.exit(2);});
 connection.openSocket(QUrl(QString::fromUtf8(argv[1])),"fixture-secret");
 int result=app.exec();assert(result==0 && expired && !identity.nodeId.isEmpty() && !lease.ready && !lease.leased && !lease.intent);
 std::cout<<"control PONG does not renew Desktop ACK; expires at 30s, pairing retained PASS\n";
}
