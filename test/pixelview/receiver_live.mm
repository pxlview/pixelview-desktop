// Opt-in live loopback authorization smoke. Reads credentials only from stdin.
#include "frontend/utility/PixelviewReceiver.hpp"
#import <Foundation/Foundation.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonDocument>
#include <QtCore/QUrlQuery>
#include <iostream>
static void pump() {QCoreApplication::processEvents();CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);}
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);std::string input;std::getline(std::cin,input);
 auto credentials=QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();input.clear();
 pixelview::PixelviewReceiver r;if(!r.setOrigin(QUrl("http://127.0.0.1:8000"),true)) return 1;
 bool ready=false;
 r.onEndpoint=[&](const QString &endpoint){
  ready=true;
  // Viewer ID is a random correlation ID, not the session token or URL.
  std::cout<<QUrlQuery(QUrl(endpoint)).queryItemValue("viewer_id").toStdString()<<std::endl;
 };
 r.start(credentials["session_id"].toString(),credentials["password"].toString(),"Desktop native controller verification");credentials={};
 QElapsedTimer t;t.start();while(t.elapsed()<35000 && !ready && r.state()!=pixelview::PixelviewReceiver::State::Error)pump();
 if(!ready) {std::cout<<"AUTH_FAILED"<<std::endl;r.stop();return 2;}
 const int soakMs=qEnvironmentVariableIntValue("PIXELVIEW_RECEIVER_SOAK_SECONDS")*1000;
 t.restart();while(t.elapsed()<(soakMs>0?soakMs:25000) && r.state()==pixelview::PixelviewReceiver::State::Ready)pump();
 bool healthy=r.state()==pixelview::PixelviewReceiver::State::Ready;r.stop();
 t.restart();while(t.elapsed()<2500)pump();
 std::cout<<(healthy ? "STOPPED" : "CONTROL_LOST")<<std::endl;return healthy?0:3;
}
