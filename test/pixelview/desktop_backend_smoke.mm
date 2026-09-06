// Test-only control-plane smoke. Token arrives on stdin, never argv or logs.
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
 bool ready=false;
 client.status=[&](QString){std::cerr<<"exchange_failed\n";app.exit(1);};
 client.paired=[&]{
  std::cout<<"exchanged_keychain_verified\n";
  QUrl ws=origin;ws.setScheme(origin.scheme()=="https"?"wss":"ws");ws.setPath("/desktop/ws");
  client.openSocket(ws,pixelview::loadDevice(origin.toString()));
 };
 client.message=[&](QByteArray body){
  auto o=QJsonDocument::fromJson(body).object();
  if(o["type"]=="ready") {ready=true;std::cout<<"authenticated_ready\n";client.sendSocket("{\"type\":\"heartbeat\",\"streaming\":false}");}
  else if(o["type"]=="heartbeat" && ready) {std::cout<<"heartbeat_acknowledged\n";app.exit(0);}
  else {std::cerr<<"unexpected_protocol_event\n";app.exit(1);}
 };
 client.disconnected=[&](int){std::cerr<<"disconnected\n";app.exit(1);};
 QTimer cocoa;QObject::connect(&cocoa,&QTimer::timeout,[]{CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.001,true);});cocoa.start(5);
 client.pair(origin,true,QString::fromStdString(input));input.clear();
 QTimer::singleShot(15000,&app,[&]{app.exit(1);});int result=app.exec();client.closeSocket();
 // Disposable device is revoked separately in admin; don't leave its local bearer.
 if(!keep && !pixelview::removeDevice(origin.toString())) return 1;
 return result;
}
