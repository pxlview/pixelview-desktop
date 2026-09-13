// Test-only control-plane smoke: pair, connect, answer the first server ping,
// request and release a start. Token arrives on stdin, never argv or logs.
#include "frontend/utility/PixelviewDesktopConnection.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#import <CoreFoundation/CoreFoundation.h>
#include <iostream>
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv); if(argc<2 || argc>3) return 2;
 const bool keep=argc==3 && std::string(argv[2])=="--keep-device"; if(argc==3 && !keep) return 2;
 QUrl origin(QString::fromUtf8(argv[1])); if(!pixelview::Desktop::validOrigin(origin,true)) return 2;
 origin.setPath(""); // Match the account saved by pair() for authentication and cleanup.
 std::string input;std::getline(std::cin,input); if(input.empty())return 2;
 pixelview::DesktopConnection client;
 bool ready=false,started=false;
 client.status=[&](QString s){if(s.contains("Keychain") && !s.contains("failed")) return; std::cerr<<"exchange_failed\n";app.exit(1);};
 client.paired=[&]{
  std::cout<<"exchanged_keychain_verified\n";
  QUrl ws=origin;ws.setScheme(origin.scheme()=="https"?"wss":"ws");ws.setPath("/desktop/ws");
  client.openSocket(ws,pixelview::loadDevice(origin.toString()));
 };
 client.message=[&](QByteArray body){
  auto o=QJsonDocument::fromJson(body).object();
  const QString mutation=o["mutation"].toString();
  if(mutation=="DESKTOP_READY") {ready=true;std::cout<<"authenticated_ready\n";}
  else if(mutation=="SOCKET_SEND_PING") {
   // The server's ping loop can run ahead of DESKTOP_READY.
   client.sendSocket("{\"message\":\"PONG_RESPONSE\",\"data\":{\"streaming\":false,\"settings\":null}}");
   std::cout<<"ping_answered\n";
   if(ready && !started) {started=true;client.sendSocket("{\"message\":\"DESKTOP_START\",\"data\":{}}");}
  }
  else if(mutation=="DESKTOP_STARTED" && started) {
   const auto whip=o["data"].toObject()["config"].toObject()["whip"];
   std::cout<<(whip.isNull() ? "started_without_whip\n" : "started_whip_granted\n");
   client.sendSocket("{\"message\":\"DESKTOP_STOP\",\"data\":{}}");
  }
  else if(mutation=="DESKTOP_ERROR" && started) {std::cout<<"start_denied_"<<o["data"].toObject()["code"].toString().toStdString()<<"\n";client.sendSocket("{\"message\":\"DESKTOP_STOP\",\"data\":{}}");}
  else if(mutation=="DESKTOP_STOPPED" && started) {std::cout<<"stopped\n";app.exit(0);}
  else {std::cerr<<"unexpected_protocol_event "<<mutation.toStdString()<<"\n";app.exit(1);}
 };
 client.disconnected=[&](int){std::cerr<<"disconnected\n";app.exit(1);};
 QTimer cocoa;QObject::connect(&cocoa,&QTimer::timeout,[]{CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.001,true);});cocoa.start(5);
 client.pair(origin,true,QString::fromStdString(input));input.clear();
 QTimer::singleShot(40000,&app,[&]{app.exit(1);});int result=app.exec();client.closeSocket();
 // Disposable device is revoked separately in admin; don't leave its local bearer.
 if(!keep && !pixelview::removeDevice(origin.toString())) return 1;
 return result;
}
