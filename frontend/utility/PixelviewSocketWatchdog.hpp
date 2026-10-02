#pragma once
#include "PixelviewControlPing.hpp"
#include <QtCore/QElapsedTimer>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <functional>
#include <memory>
#import <Foundation/Foundation.h>
namespace pixelview {
// Own on the Qt/Cocoa main thread; destroy on detach before canceling the task.
// Native ping completion means RFC6455 pong, not merely a successful write.
// answered, when given, gets each ping's round trip in milliseconds.
inline std::unique_ptr<QTimer> watchControlSocket(NSURLSessionWebSocketTask *task, std::function<void()> failed, std::function<void(int)> answered={})
{
 auto timer=std::make_unique<QTimer>();
 QPointer<QTimer> guard(timer.get());
 auto policy=std::make_shared<ControlPing>();
 QElapsedTimer clock; clock.start();
 QObject::connect(timer.get(),&QTimer::timeout,timer.get(),[guard,policy,clock,task,failed,answered]{
  const auto action=policy->poll(clock.elapsed());
  if(action==ControlPing::Timeout) {guard->stop();failed();return;}
  if(action!=ControlPing::Ping) return;
  const auto pinged=clock.elapsed();
  [task sendPingWithPongReceiveHandler:^(NSError *error){
   const auto took=clock.elapsed()-pinged; // Before the hop to the main queue.
   dispatch_async(dispatch_get_main_queue(), ^{
    if(!guard || !guard->isActive()) return;
    if(error) {guard->stop();failed();return;}
    policy->pong();
    if(answered) answered((int)took);
   });
  }];
 });
 timer->setTimerType(Qt::PreciseTimer);timer->start(250);
 return timer;
}
}
