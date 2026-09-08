#include "frontend/utility/PixelviewReceiver.hpp"
#import <Foundation/Foundation.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QThread>
#include <cassert>
#include <iostream>
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv); pixelview::PixelviewReceiver r;
 assert(r.setOrigin(QUrl(QString::fromLocal8Bit(argv[1])),true));
 bool ready=false,stopped=false;
 r.onEndpoint=[&](const QString &u){assert(u.contains("viewer_id="));ready=true;};
 r.onStopped=[&]{stopped=true;};
 r.start("fixture-session","fixture-password","Native receiver test");
 QElapsedTimer timer; timer.start();
 const bool soak=argc>2 && std::string(argv[2])=="soak";
 const bool reject=argc>2 && !soak;
 while(timer.elapsed()<8000 && !(reject ? r.state()==pixelview::PixelviewReceiver::State::Error : ready)) {
  QCoreApplication::processEvents(); CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);
 }
 if(reject) {assert(r.state()==pixelview::PixelviewReceiver::State::Error);assert(!ready);}
 else {assert(ready); for(int i=0;i<60;++i){QCoreApplication::processEvents(); CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);}}
 if(soak){timer.restart();while(timer.elapsed()<35000){QCoreApplication::processEvents();CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);assert(!stopped);assert(r.state()==pixelview::PixelviewReceiver::State::Ready);}}
 r.stop(); assert(!ready || stopped);
 for(int i=0;i<30;++i){QCoreApplication::processEvents(); CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);}
 std::cout<<"receiver NSURLSession transport passed\n";
}
