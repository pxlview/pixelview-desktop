#include "frontend/utility/PixelviewDesktopConnection.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <cassert>
#import <CoreFoundation/CoreFoundation.h>
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);
 QTimer cocoa; QObject::connect(&cocoa,&QTimer::timeout,[]{CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.001,true);}); cocoa.start(5);
 QString account="test-"+QUuid::createUuid().toString();
 assert(pixelview::saveDevice(account,"test-only-secret"));
 assert(pixelview::loadDevice(account)=="test-only-secret");
 assert(pixelview::removeDevice(account)); assert(pixelview::loadDevice(account).isEmpty());
 QTcpServer server; assert(server.listen(QHostAddress::LocalHost));
 pixelview::DesktopConnection client;
 bool exchanged=false;
 client.paired=[&]{exchanged=true; app.quit();};
 QObject::connect(&server,&QTcpServer::newConnection,&app,[&]{
  auto s=server.nextPendingConnection();
  QObject::connect(s,&QTcpSocket::readyRead,s,[&,s]{
   auto data=s->readAll(); assert(data.contains("POST /desktop/exchange"));
   assert(data.contains("pairing_token"));
   QByteArray body="{\"desktop_id\":\"test\",\"node_id\":\"test\",\"device_token\":\"fixture-secret\"}";
   s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body); s->disconnectFromHost();
  });
 });
 QUrl origin("http://127.0.0.1:"+QString::number(server.serverPort()));
 client.pair(origin,true,"fixture-code");
 client.pair(QUrl("http://127.0.0.1:1"),true,"second-code");
 assert(client.origin==origin); // An inflight exchange cannot migrate its credential to another origin.
 QTimer::singleShot(5000,&app,&QCoreApplication::quit); app.exec();
 assert(exchanged); assert(client.identity.nodeId=="test" && client.identity.desktopId=="test"); assert(pixelview::loadDevice(origin.toString())=="fixture-secret");
 assert(pixelview::removeDevice(origin.toString()));
 bool gotReady=false;
 client.message=[&](QByteArray body){ gotReady=QJsonDocument::fromJson(body).object()["type"]=="ready"; app.quit(); };
 client.openSocket(QUrl(QString::fromUtf8(argv[1])),"fixture-secret");
 QTimer::singleShot(5000,&app,&QCoreApplication::quit); app.exec();
 assert(gotReady);
 int closeCode=0; client.disconnected=[&](int code){closeCode=code;app.quit();};
 client.sendSocket("{\"type\":\"test-close\"}");
 QTimer::singleShot(5000,&app,&QCoreApplication::quit);app.exec();
 assert(closeCode==4401);client.closeSocket();
}
