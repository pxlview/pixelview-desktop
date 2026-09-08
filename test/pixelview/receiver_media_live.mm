// Live native controller -> packaged module worker. No graphics or monitoring.
#include "frontend/utility/PixelviewReceiver.hpp"
#import <Foundation/Foundation.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonDocument>
#include <obs-module.h>
#include <iostream>
#include <cassert>
static void pump(){ QCoreApplication::processEvents();CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true); }
int main(int argc,char **argv){
 QCoreApplication app(argc,argv); assert(argc==2);
 std::string input;std::getline(std::cin,input);
 auto credentials=QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();input.clear();
 assert(obs_startup("en-US",nullptr,nullptr));
 obs_audio_info ai={48000,SPEAKERS_STEREO};assert(obs_reset_audio(&ai));
 obs_module_t *module=nullptr;assert(obs_open_module(&module,argv[1],".")==MODULE_SUCCESS);assert(obs_init_module(module));
 auto *s=obs_source_create_private("pixelview_whep_source","native-media-test",nullptr);assert(s);
 auto *ph=obs_source_get_proc_handler(s);
 pixelview::PixelviewReceiver r;assert(r.setOrigin(QUrl("http://127.0.0.1:8000"),true));
 const int soakMs=qEnvironmentVariableIntValue("PIXELVIEW_RECEIVER_SOAK_SECONDS")*1000;
 int endpoints=0,stops=0;
 bool delivered=false;
 r.onEndpoint=[&](const QString &endpoint){ ++endpoints; QString mediaEndpoint=endpoint; if(qEnvironmentVariableIsSet("PIXELVIEW_TEST_CANONICAL_LOOPBACK")){QUrl u(endpoint);assert(u.scheme()=="http" && u.host()=="localhost" && u.port()==8000);u.setHost("127.0.0.1");mediaEndpoint=QString::fromUtf8(u.toEncoded());} calldata_t cd;calldata_init(&cd);calldata_set_string(&cd,"endpoint",mediaEndpoint.toUtf8().constData());calldata_set_int(&cd,"latency",50);assert(proc_handler_call(ph,"connect",&cd));calldata_free(&cd);delivered=true;};
 r.onStopped=[&]{++stops;calldata_t cd;calldata_init(&cd);assert(proc_handler_call(ph,"disconnect",&cd));calldata_free(&cd);};
 bool passed=true;
 for(int cycle=0;cycle<(soakMs>0?1:2);cycle++){
  delivered=false;r.start(credentials["session_id"].toString(),credentials["password"].toString(),"Native media worker verification");
  QElapsedTimer t;t.start();calldata_t cd;calldata_init(&cd);
  bool enough=false;
  qint64 lastFrames=0,lastAudio=0,nextSample=10000;bool monotonic=true;
  while(t.elapsed()<(soakMs>0?soakMs:40000)){pump();assert(proc_handler_call(ph,"get_status",&cd));
   auto frames=calldata_int(&cd,"frames"),audio=calldata_int(&cd,"audio_frames");
   monotonic &= frames>=lastFrames && audio>=lastAudio;lastFrames=frames;lastAudio=audio;
   if(t.elapsed()>=nextSample){std::cout<<"SOAK_SAMPLE elapsed_ms="<<t.elapsed()<<" endpoints="<<endpoints<<" stops="<<stops<<" video="<<frames<<" audio="<<audio<<" monotonic="<<monotonic<<std::endl;nextSample+=10000;}
   std::string state=calldata_string(&cd,"state");
   if(delivered && (state=="error" || state=="ended"))break;
   if(r.state()==pixelview::PixelviewReceiver::State::Error)break;
   enough=state=="playing" && calldata_int(&cd,"frames")>=60 && calldata_int(&cd,"audio_frames")>=48000 && calldata_int(&cd,"jitter_latency")==50;
   if(enough && soakMs<=0)break;
   if(soakMs>0 && (endpoints>1 || stops>0 || !monotonic))break;
  }
  std::cout<<"MEDIA_RESULT cycle="<<cycle<<" authorized="<<delivered<<" state="<<calldata_string(&cd,"state")<<" video="<<calldata_int(&cd,"frames")<<" audio="<<calldata_int(&cd,"audio_frames")<<" jitter="<<calldata_int(&cd,"jitter_latency")<<std::endl;
  if(soakMs>0)enough &= t.elapsed()>=soakMs && endpoints==1 && stops==0 && monotonic && r.state()==pixelview::PixelviewReceiver::State::Ready;
  passed &= enough;r.stop();assert(proc_handler_call(ph,"get_status",&cd));assert(!strcmp(calldata_string(&cd,"state"),"idle"));
  auto frames=calldata_int(&cd,"frames"),audio=calldata_int(&cd,"audio_frames");t.restart();while(t.elapsed()<1500)pump();
  assert(proc_handler_call(ph,"get_status",&cd));assert(calldata_int(&cd,"frames")==frames && calldata_int(&cd,"audio_frames")==audio);calldata_free(&cd);
  if(!passed)break;
 }
 credentials={};obs_source_release(s);obs_shutdown();return passed?0:1;
}
