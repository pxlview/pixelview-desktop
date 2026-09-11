// SPDX-License-Identifier: GPL-2.0-or-later
#define main legacy_owner_main
#include "receive.cpp"
#undef main
extern "C" void pv_campaign_record(uint64_t,uint64_t,const uint64_t *,unsigned);
extern "C" void pv_campaign_flush(void);
extern "C" obs_source_t *pv_whep_create(const char *);
extern "C" void pv_whep_status(obs_source_t *, uint64_t *, uint64_t *, bool *);
extern "C" void pv_whep_disconnect(obs_source_t *);
extern "C" void pv_whep_reconnect(obs_source_t *, const char *);
int main(int argc,char **argv)
{
 assert(argc==6); setbuf(stdout,nullptr);
 assert(obs_startup("en-US",nullptr,nullptr));obs_audio_info ai={48000,SPEAKERS_STEREO};assert(obs_reset_audio(&ai));
 unsigned num=std::stoul(argv[3]),den=std::stoul(argv[4]);
 auto *source=pv_whep_create(argv[1]); Device sdk;
 sdk.card.mode.width=1920;sdk.card.mode.height=1080;sdk.card.mode.duration=den;sdk.card.mode.scale=num;
 sdk.card.mode.identifier=num==24000?bmdModeHD1080p2398:num==24?bmdModeHD1080p24:num==25?bmdModeHD1080p25:bmdModeHD1080p2997;
 DeckLinkDevice device(&sdk);DeckLinkDeviceMode mode(&sdk.card.mode,1);DeckLinkDeviceInstance owner(nullptr,&device);
 uint64_t reference_begin=os_gettime_ns();
 const size_t size=5120*1080;FILE *f=fopen(argv[2],"rb");assert(f);fseek(f,0,SEEK_END);size_t bytes=ftell(f);rewind(f);
 assert(bytes && bytes%size==0);std::vector<uint8_t> reference(bytes);assert(fread(reference.data(),1,bytes,f)==bytes);fclose(f);
 uint64_t load_times[]={reference_begin,os_gettime_ns(),bytes};pv_campaign_record(5,0,load_times,3);
 for(unsigned attempt=0;attempt<2;attempt++) {
  if(attempt) pv_whep_reconnect(source,argv[5]);
  sdk.card.audio.clear();sdk.card.playedSamples=0;
  uint64_t video=0,audio=0;bool error=false;
  for(unsigned i=0;i<120 && (video<10 || audio<48000) && !error;i++){os_sleep_ms(50);pv_whep_status(source,&video,&audio,&error);}
  fprintf(stderr,"APPROVED_TRANSPORT native=%llu opus=%llu error=%d\n",video,audio,error);assert(video>=10 && !error);
  // Exact selected native mode, not a nearest-mode fallback.
  sdk.card.mode.scale=num==25?24:25;DeckLinkDeviceMode wrong(&sdk.card.mode,2);
  assert(!owner.StartNativeOutput(&wrong,source));sdk.card.mode.scale=num;
  assert(owner.StartNativeOutput(&mode,source));
  unsigned compared=0,distinct=0;int previous=-1;const uint64_t wall=os_gettime_ns();
  for(unsigned i=0;i<45 && owner.NativeOutputHealthy();i++) {
   uint64_t deadline=wall+uint64_t(i+1)*den*1000000000ULL/num;os_sleepto_ns(deadline);
   sdk.card.playedSamples=uint64_t(i)*den*48000/num;sdk.card.complete();auto &v=sdk.card.queued.back();
   fprintf(stderr,"APPROVED_SCHEDULE i=%u t=%lld d=%lld s=%lld queued=%zu\n",i,(long long)v.t,(long long)v.d,(long long)v.s,sdk.card.queued.size());
   if(getenv("PV_MATRIX_TRACE") && !(v.d==den && v.s==num && v.t==int64_t(i+3)*den)) { fprintf(stderr,"MATRIX_FIRST_FAILURE preserved; holding for stack capture\n"); os_sleep_ms(3000); }
   assert(v.d==den && v.s==num && v.t==int64_t(i+3)*den);void *p=nullptr;assert(v.f->GetBytes(&p)==S_OK);
   uint64_t compare_begin=os_gettime_ns();
   int identity=-1;for(size_t j=0;j<bytes/size;j++)if(!memcmp(p,reference.data()+j*size,size)){identity=j;break;}
   uint64_t comparison[]={compare_begin,os_gettime_ns(),i};pv_campaign_record(6,attempt+1,comparison,3);
   assert(identity>=0 && identity>=previous);if(identity!=previous)distinct++;previous=identity;compared++;
   fprintf(stderr,"APPROVED_SLOT index=%u identity=%d late_ns=%lld t=%lld d=%lld s=%lld\n",i,identity,(long long)(os_gettime_ns()-deadline),(long long)v.t,(long long)v.d,(long long)v.s);
  }
  pv_whep_status(source,&video,&audio,&error);double energy=0;size_t samples=0;
  for(auto &a:sdk.card.audio)for(size_t j=0;j<a.data.size();j+=2){int16_t s;memcpy(&s,a.data.data()+j,2);energy+=double(s)*s;samples++;}
  bool healthy=owner.NativeOutputHealthy();pv_whep_disconnect(source);os_sleep_ms(150);assert(!owner.NativeOutputHealthy());owner.StopOutput();assert(!sdk.card.cb && !sdk.card.started);
  printf("APPROVED_OWNER attempt=%u num=%u den=%u compared=%u distinct=%u native=%llu opus=%llu pcm=%zu energy=%g healthy=%d error=%d\n",attempt,num,den,compared,distinct,video,audio,samples,energy,healthy,error);
  assert(compared==45 && distinct>=35 && healthy && !error && samples>1000 && energy>1);
 }
 obs_source_release(source);obs_queue_task(OBS_TASK_DESTROY,[](void*){},nullptr,true);obs_shutdown();pv_campaign_flush();
}
