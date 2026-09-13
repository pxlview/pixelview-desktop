// Actual NSURLSession transport + Desktop control policy against a scripted loopback
// server. Modes: "lifecycle" (start, control drop while streaming, reconnect
// without a new start, revocation) and "replaced" (no automatic reconnect).
#include "frontend/utility/PixelviewDesktopConnection.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#import <CoreFoundation/CoreFoundation.h>
#include <cassert>
#include <iostream>
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv); assert(argc==3);
 const QString mode=QString::fromUtf8(argv[2]);
 QTimer cocoa; QObject::connect(&cocoa,&QTimer::timeout,[]{CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.001,true);}); cocoa.start(5);
 pixelview::DesktopConnection connection;
 pixelview::Desktop lease;
 pixelview::DesktopIdentity identity;
 QElapsedTimer clock; clock.start();
 int ready=0,pings=0,starts=0,halts=0,disconnects=0,reconnects=0; bool streaming=false;
 lease.development=true;
 lease.monotonic=[&]{return clock.elapsed();};
 lease.send=[&](QJsonObject o){connection.sendSocket(QJsonDocument(o).toJson(QJsonDocument::Compact));};
 lease.report=[&]{return QJsonObject{{"streaming",streaming},{"settings",QJsonValue::Null}};};
 lease.publish=[&](QString endpoint,QString bearer){assert(bearer=="fixture-bearer" && endpoint.startsWith("http://127.0.0.1:"));++starts;streaming=true;};
 lease.halt=[&]{++halts;streaming=false;};
 lease.error=[&](QString error){std::cerr<<error.toStdString()<<" at "<<clock.elapsed()<<"ms\n";};
 auto open=[&]{lease.ready=false;lease.pending=false;lease.pingDeadline=0;lease.revoked=false;lease.replaced=false;
  connection.openSocket(QUrl(QString::fromUtf8(argv[1])),"fixture-secret");};
 connection.message=[&](QByteArray bytes){
  auto o=QJsonDocument::fromJson(bytes).object();
  if(o["mutation"]=="DESKTOP_READY") {assert(identity.accept(o["data"].toObject()));++ready;}
  if(o["mutation"]=="SOCKET_SEND_PING") ++pings;
  lease.receive(o,clock.elapsed());
  if(lease.revoked) {assert(mode=="lifecycle" && halts==1 && !streaming && ready==2);connection.closeSocket();app.quit();return;}
  if(lease.replaced) {assert(mode=="replaced" && streaming && halts==0 && lease.authorized(0));connection.closeSocket();
   QTimer::singleShot(3000,&app,[&]{assert(reconnects==0);app.quit();});return;}
  if(ready==1 && o["mutation"]=="DESKTOP_READY") assert(lease.requestStart(clock.elapsed()));
 };
 connection.disconnected=[&](int code){
  ++disconnects;
  if(lease.revoked || lease.replaced) return; // The mutation already decided.
  assert(pixelview::Desktop::transientClose(code));
  assert(streaming && lease.controlLost() && lease.authorized(0) && halts==0);
  connection.closeSocket();
  QTimer::singleShot(1000,&app,[&]{++reconnects;open();});
 };
 QTimer watchdog;QObject::connect(&watchdog,&QTimer::timeout,[&]{assert(!lease.pingExpired(clock.elapsed()));});watchdog.start(100);
 QTimer::singleShot(60000,&app,[&]{std::cerr<<"loopback timed out\n";app.exit(2);});
 open();int result=app.exec();
 assert(result==0 && starts==1 && pings>=3);
 if(mode=="lifecycle") assert(ready==2 && reconnects==1 && halts==1);
 else assert(ready==1 && reconnects==0 && streaming);
 std::cout<<mode.toStdString()<<": token upgrade, ready, ping/pong, start, control loss keeps media, terminal mutation PASS\n";
}
