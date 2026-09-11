/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../native-422-rate.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void gap_boundary(void)
{
 for (uint64_t gap=499999999;gap<=500000001;gap++) {
  struct pv422_rate s={0};
  assert(pv422_rate_packet(&s,7,10,0,true,false,1)==PV422_RATE_PENDING);
  enum pv422_rate_result r=pv422_rate_packet(&s,7,11,3750,true,false,1+gap);
  assert(r==(gap>500000000?PV422_RATE_REJECTED:PV422_RATE_PENDING));
  if(gap>500000000) {
   assert(s.reason && !strcmp(s.reason,"ordered-rtp-gap"));
   assert(s.failure_now-s.last==gap && s.failure_sequence==11 && s.sequence==10);
   assert(s.total_packets==2);
   assert(pv422_rate_packet(&s,8,90,0,true,true,9000000000ULL)==PV422_RATE_REJECTED);
   assert(!strcmp(s.reason,"ordered-rtp-gap") && s.failure_sequence==11 && s.total_packets==2 && s.failure_now==1+gap && s.failure_delta==3750);
  }
 }
}
static void additional_boundaries(void)
{
 for(uint64_t age=2999999999ULL;age<=3000000001ULL;age++) {
  struct pv422_rate s={0};
  assert(pv422_rate_packet(&s,1,0,0,false,false,1)==PV422_RATE_PENDING);
  for(unsigned i=1;i<=6;i++) assert(pv422_rate_packet(&s,1,i,0,false,false,1+i*450000000ULL)==PV422_RATE_PENDING);
  assert(pv422_rate_packet(&s,1,7,0,false,false,1+age)==(age>3000000000ULL?PV422_RATE_REJECTED:PV422_RATE_PENDING));
  if(s.failed) assert(!strcmp(s.reason,"ordered-rtp-acquisition-expired") && s.failure_now==1+age);
 }
 struct pv422_rate s={0};
 for(unsigned i=0;i<16384;i++) assert(pv422_rate_packet(&s,1,i,0,false,false,i+1)==PV422_RATE_PENDING);
 assert(s.packets==16384 && !s.failed);
 assert(pv422_rate_packet(&s,1,16384,37,false,false,16385)==PV422_RATE_REJECTED);
 assert(!strcmp(s.reason,"ordered-rtp-marker-absent") && s.failure_delta==37 && s.failure_now==16385);
 assert(pv422_rate_packet(&s,2,44,99,true,true,9000000000ULL)==PV422_RATE_REJECTED);
 assert(s.failure_now==16385 && s.failure_delta==37 && s.failure_sequence==16384 && s.total_packets==16385);
 for(unsigned alternating=0;alternating<2;alternating++) {
  s=(struct pv422_rate){0};uint32_t ts=0;
  for(unsigned i=0;i<=64;i++) {
   assert(pv422_rate_packet(&s,1,i,ts,true,false,1+i*42000000ULL)==(i<32?PV422_RATE_PENDING:PV422_RATE_READY));
   ts+=3753+(alternating && (i&1));
  }
  assert(s.num==24000 && s.den==1001);
 }
 puts("PASS packet acquisition 3s +/-1ns; marker 16384/16385; 3753-only/alternating; sticky time/delta");
}
int main(void)
{
 gap_boundary();additional_boundaries();
 const unsigned ticks[]={3754,3750,3600,3003};
 const unsigned nums[]={24000,24,25,30000}, dens[]={1001,1,1,1001};
 for(unsigned r=0;r<4;r++) {
  struct pv422_rate s={0}; unsigned stamp=0xfffffff0u; uint16_t seq=65520;
  for(unsigned i=0;i<=32;i++) {
   enum pv422_rate_result result=pv422_rate_packet(&s,7,seq++,stamp,true,i==0,1000000ULL+i*42000000ULL);
   assert(result==(i<32?PV422_RATE_PENDING:PV422_RATE_READY)); stamp+=ticks[r];
  }
  assert(s.num==nums[r] && s.den==dens[r]);
 }
 const char *reasons[]={"ordered-rtp-ssrc-change","ordered-rtp-sequence-gap","ordered-rtp-rate-change","ordered-rtp-discontinuity","ordered-rtp-gap","ordered-rtp-clock-backward"};
 for(unsigned bad=0;bad<7;bad++) {
  struct pv422_rate s={0}; uint32_t stamp=0; uint16_t seq=0;
  for(unsigned i=0;i<=32;i++,stamp+=3003) assert(pv422_rate_packet(&s,7,seq++,stamp,true,false,i*33000000ULL)!=PV422_RATE_REJECTED);
  assert(pv422_rate_packet(&s,bad==0?8:7,seq+(bad==1),bad==2?stamp+3:stamp,true,bad==3,
   bad==4?2000000000ULL:bad==5?1:1089000000ULL)==(bad<6?PV422_RATE_REJECTED:PV422_RATE_READY));
  if(bad<6) assert(!strcmp(s.reason,reasons[bad]));
 }
 for(unsigned tick=3000;tick<=3601;tick+=601) {
  struct pv422_rate s={0}; pv422_rate_packet(&s,1,0,0,true,false,1);
  assert(pv422_rate_packet(&s,1,1,tick,true,false,33000001)==PV422_RATE_REJECTED);
 }
 assert(!pv422_rate_supported(30,1) && !pv422_rate_supported(29999,1001));
 puts("finite rate: four approved rates, wrap, changing/unsupported rates, SSRC, loss, discontinuity, stale/backward clocks");
}
