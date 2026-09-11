// SPDX-License-Identifier: GPL-2.0-or-later
// Reuse compiled SDK fakes AND all whole production owner translation units.
#define main legacy_owner_main
#include "receive.cpp"
#undef main
extern "C" obs_source_t *pv_whep_create(const char *);
extern "C" void pv_whep_status(obs_source_t *, uint64_t *, uint64_t *, bool *);
extern "C" void pv_whep_disconnect(obs_source_t *);
extern "C" void pv_whep_preview_release();
extern "C" void pv_whep_preview_still_blocked();
extern "C" void pv_whep_audit();
extern "C" void pv_whep_reconnect(obs_source_t *, const char *);
int main(int argc,char **argv)
{
 assert(argc==7);setbuf(stdout,nullptr);
 assert(obs_startup("en-US",nullptr,nullptr));
 obs_audio_info ai={48000,SPEAKERS_STEREO};assert(obs_reset_audio(&ai));
 auto *source=pv_whep_create(argv[1]);
 Device sdk;sdk.card.mode.width=std::stoi(argv[3]);sdk.card.mode.height=std::stoi(argv[4]);
 DeckLinkDevice device(&sdk);DeckLinkDeviceMode mode(&sdk.card.mode,1);
 DeckLinkDeviceInstance owner(nullptr,&device);
 const size_t size=((sdk.card.mode.width+47)/48)*128*sdk.card.mode.height;
 std::vector<uint8_t> reference(size*300);FILE *f=fopen(argv[2],"rb");assert(f);
 assert(fread(reference.data(),1,reference.size(),f)==reference.size());fclose(f);
 for (unsigned attempt=0; attempt<2; ++attempt) {
 if (attempt) pv_whep_reconnect(source,argv[5]);
 sdk.card.audio.clear();sdk.card.playedSamples=0;
 // Wait for HTTP/ICE separately: StartNativeOutput has a deliberately short
 // fresh-source attachment wait, not a signaling timeout.
 uint64_t video=0,audio=0;bool error=false;
 for(unsigned i=0;i<150 && (video<30 || audio<48000) && !error;++i){os_sleep_ms(100);pv_whep_status(source,&video,&audio,&error);}
 fprintf(stderr,"TRANSPORT native=%llu opus=%llu error=%d\n",video,audio,error);
 assert(video && !error);
 assert(owner.StartNativeOutput(&mode,source));
 unsigned compared=0,distinct=0;int previous=-1;
 // Absolute rational deadlines keep fake playback on the same monotonic clock
 // as the live RTP producer. Relative sleeps add callback/comparison/logging
 // work to every frame and artificially leave the card behind the RAW queue.
 const uint64_t playback_wall=os_gettime_ns();
 for(unsigned i=0;i<45 && owner.NativeOutputHealthy();++i){
  const uint64_t deadline=playback_wall+uint64_t(i+1)*1001*1000000000ULL/30000;
  os_sleepto_ns(deadline);sdk.card.playedSamples=uint64_t(i)*1001*48000/30000;
  sdk.card.complete();auto &v=sdk.card.queued.back();
  fprintf(stderr,"SCHEDULE slot=%u actual=%lld/%lld/%lld healthy=%d audio_writes=%zu clock=%lld\n",i,(long long)v.t,(long long)v.d,(long long)v.s,owner.NativeOutputHealthy(),sdk.card.audio.size(),(long long)sdk.card.playedSamples);
  assert(v.d==1001 && v.s==30000 && v.t==int64_t(i+3)*1001);
  void *p=nullptr;assert(v.f->GetBytes(&p)==S_OK);
  uint32_t first;memcpy(&first,p,4);int identity=int((first>>10)&1023)-64;
  assert(identity>=0 && identity<300 && identity>=previous);
  assert(!memcmp(p,reference.data()+size*identity,size));
  fprintf(stderr,"PACING slot=%u identity=%d wall_late_ns=%lld\n",i,identity,(long long)(os_gettime_ns()-deadline));
  if(identity!=previous)++distinct;previous=identity;++compared;
 }
 pv_whep_status(source,&video,&audio,&error);
 double energy=0;size_t samples=0;
 for(size_t i=1;i<sdk.card.audio.size();++i)for(size_t j=0;j<sdk.card.audio[i].data.size();j+=2){int16_t s;memcpy(&s,sdk.card.audio[i].data.data()+j,2);energy+=double(s)*s;++samples;}
 bool healthy=owner.NativeOutputHealthy();
 pv_whep_preview_still_blocked();
 alarm(15); // RED watchdog: do not release the blocked OBS callback to stop.
 uint64_t stop_begin=os_gettime_ns();
 pv_whep_disconnect(source);
 assert(os_gettime_ns()-stop_begin < 500000000ULL);
 fprintf(stderr,"STOP_REQUEST_NS %llu\n",(unsigned long long)(os_gettime_ns()-stop_begin));
 alarm(0);os_sleep_ms(150);assert(!owner.NativeOutputHealthy());
 owner.StopOutput();assert(!sdk.card.cb && !sdk.card.started);
 printf("MAIN422_OWNER compared=%u native=%llu opus=%llu pcm_samples=%zu energy=%g healthy=%d distinct=%u error=%d\n",compared,video,audio,samples,energy,healthy,distinct,error);
 if (distinct<20 || error) os_sleep_ms(1000);
 assert(compared>=30 && distinct>=20 && healthy && !error && audio>=24000 && samples>1000 && energy>1);
 }
 pv_whep_reconnect(source,argv[6]);
 uint64_t video=0,audio=0;bool error=false;
 for(unsigned i=0;i<80 && !error;++i){os_sleep_ms(100);pv_whep_status(source,&video,&audio,&error);}
 assert(error && !video && !audio);
 assert(!owner.StartNativeOutput(&mode,source));
 pv_whep_preview_still_blocked();pv_whep_disconnect(source);
 pv_whep_audit();
 obs_weak_source_t *weak=obs_source_get_weak_source(source);
 obs_source_release(source);
 // Audio enumeration can temporarily pin a source: wait for its final strong
 // reference to disappear before asserting destruction-queue quiescence.
 for(unsigned i=0;i<100 && !obs_weak_source_expired(weak);++i)os_sleep_ms(10);
 assert(obs_weak_source_expired(weak));obs_weak_source_release(weak);
 std::atomic<bool> destroyed{false};
 std::thread drain([&]{
  // obs_wait_for_destroy_queue returns false without a graphics thread. Use
  // the actual FIFO destruction queue barrier in this CPU-only harness.
  obs_queue_task(OBS_TASK_DESTROY, [](void*){}, nullptr, true);destroyed=true;
 });
 os_sleep_ms(100);
 if(const char *preview=getenv("PV_WHEP_PREVIEW"); preview && !strcmp(preview,"blocked")) assert(!destroyed.load());
 // Quiescence is intentionally NOT claimed bounded: destruction joins the
 // in-flight source-owned preview worker and returns only after OBS returns.
 pv_whep_preview_release();drain.join();assert(destroyed.load());obs_shutdown();
 return 0;
}
