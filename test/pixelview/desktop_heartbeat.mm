// Actual NSURLSession transport + Desktop lease policy; synthetic loopback only.
#include "frontend/utility/PixelviewDesktopConnection.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#import <CoreFoundation/CoreFoundation.h>
#include <cassert>
#include <iostream>
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);
 QTimer cocoa; QObject::connect(&cocoa,&QTimer::timeout,[]{CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.001,true);}); cocoa.start(5);
 pixelview::DesktopConnection connection;
 pixelview::Desktop lease;
 pixelview::DesktopIdentity identity;
 QElapsedTimer clock; clock.start();
 int ready=0,acks=0,disconnects=0; bool retrying=false;
 QTimer heartbeat,watchdog;
 heartbeat.setInterval(15000); heartbeat.setTimerType(Qt::PreciseTimer);
 lease.monotonic=[&]{return clock.elapsed();};
 lease.send=[&](QJsonObject o){connection.sendSocket(QJsonDocument(o).toJson(QJsonDocument::Compact));};
 lease.error=[&](QString error){std::cerr<<error.toStdString()<<" at "<<clock.elapsed()<<"ms\n";};
 lease.halt=[&]{heartbeat.stop();};
 auto open=[&]{connection.openSocket(QUrl(QString::fromUtf8(argv[1])),"fixture-secret");};
 connection.message=[&](QByteArray bytes){
  auto o=QJsonDocument::fromJson(bytes).object();
  if(o["type"]=="ready") {assert(identity.accept(o));++ready;heartbeat.start();}
  if(o["type"]=="heartbeat") ++acks;
  lease.receive(o,clock.elapsed()); assert(lease.ready);
 };
 connection.disconnected=[&](int code){
  if(retrying) return;
  ++disconnects;
  if(code==4401) {assert(ready==2 && acks>=5 && disconnects==2); lease.fail("revoked"); connection.closeSocket();app.quit();return;}
  assert(pixelview::Desktop::transientClose(code));
  assert(clock.elapsed()>=65000 && ready==1 && acks>=4);
  retrying=true; lease.fail("loopback restart",true);connection.closeSocket();
  assert(!lease.intent && !lease.leased && !identity.nodeId.isEmpty());
  QTimer::singleShot(1000,&app,[&]{retrying=false;open();});
 };
 QObject::connect(&heartbeat,&QTimer::timeout,[&]{if(lease.heartbeatSent(clock.elapsed()))lease.send({{"type","heartbeat"},{"streaming",false}});});
 QObject::connect(&watchdog,&QTimer::timeout,[&]{if(lease.ready){lease.tick(clock.elapsed());assert(lease.ready);}});watchdog.start(100);
 QTimer::singleShot(95000,&app,[&]{std::cerr<<"loopback timed out\n";app.exit(2);});
 open();int result=app.exec();assert(result==0 && ready==2 && acks>=5);
 std::cout<<"healthy 65s, authenticated reconnect, heartbeat, terminal revocation PASS\n";
}
