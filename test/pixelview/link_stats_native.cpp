// RTCP link statistics behind the Desktop's connection report: pure parsing, no network.
#include "plugins/obs-webrtc/pixelview-link-stats.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using pixelview::LinkStats;
using Bytes=std::vector<uint8_t>;
static void put32(Bytes &b,uint32_t v) {for(int s=24;s>=0;s-=8) b.push_back((uint8_t)(v>>s));}
static void header(Bytes &b,uint8_t count,uint8_t type,size_t body) {
 b.push_back((uint8_t)(0x80|count));b.push_back(type);b.push_back((uint8_t)((body/4)>>8));b.push_back((uint8_t)(body/4));
}
// A sender report as this side emits it: no report blocks, followed by an SDES like libdatachannel's.
static Bytes senderReport(uint32_t ssrc,uint32_t seconds,uint32_t fraction) {
 Bytes b;header(b,0,200,24);put32(b,ssrc);put32(b,seconds);put32(b,fraction);put32(b,0);put32(b,0);put32(b,0);
 header(b,1,202,8);put32(b,ssrc);put32(b,0x01020000);
 return b;
}
static void reportBlock(Bytes &b,uint32_t ssrc,uint8_t fraction,uint32_t lost,uint32_t jitter,uint32_t lsr,uint32_t dlsr) {
 put32(b,ssrc);put32(b,((uint32_t)fraction<<24)|(lost&0xffffff));put32(b,0);put32(b,jitter);put32(b,lsr);put32(b,dlsr);
}
int main() {
 const uint32_t ssrc=0x11223344;
 LinkStats stats(ssrc,90000);
 auto idle=stats.snapshot(1000000);
 assert(!idle.reported && idle.rtt_ms==-1 && idle.age_ms==-1);

 // Sender report leaves at t=1 s; its NTP middle 32 bits come back as LSR.
 const uint32_t seconds=0xABCD1234, fraction=0x56789ABC, middle=0x12345678;
 auto sr=senderReport(ssrc,seconds,fraction);
 stats.sent(sr.data(),sr.size(),1000000);
 // The server held it 250 ms (DLSR 0x4000) and its answer arrives 290 ms after the report left.
 Bytes rr;header(rr,1,201,28);put32(rr,0x99);reportBlock(rr,ssrc,64,10,900,middle,0x4000);
 stats.received(rr.data(),rr.size(),1290000);
 auto first=stats.snapshot(1790000);
 assert(first.reported && first.rtt_ms==40 && first.age_ms==500 && first.lost==10);
 assert(std::fabs(first.loss_pct-25.0)<1e-9 && std::fabs(first.jitter_ms-10.0)<1e-9);

 // Smoothed: one 120 ms sample moves a 40 ms estimate a quarter of the way.
 auto sr2=senderReport(ssrc,seconds+1,fraction);
 stats.sent(sr2.data(),sr2.size(),2000000);
 Bytes rr2;header(rr2,1,201,28);put32(rr2,0x99);reportBlock(rr2,ssrc,0,10,90,middle+0x10000,0);
 stats.received(rr2.data(),rr2.size(),2120000);
 auto second=stats.snapshot(2120000);
 assert(second.rtt_ms==60 && second.loss_pct==0 && second.age_ms==0);

 // Another stream's block, an unknown LSR and LSR 0 never change the round trip.
 Bytes other;header(other,2,201,52);put32(other,0x99);reportBlock(other,ssrc+1,255,99,9000,middle,0);reportBlock(other,ssrc,3,12,180,0xDEADBEEF,0);
 stats.received(other.data(),other.size(),3000000);
 Bytes early;header(early,1,201,28);put32(early,0x99);reportBlock(early,ssrc,0,12,180,0,0);
 stats.received(early.data(),early.size(),3100000);
 auto third=stats.snapshot(3100000);
 assert(third.rtt_ms==60 && third.lost==12 && std::fabs(third.jitter_ms-2.0)<1e-9);

 // The server's own sender report can carry the block too, after its 24-byte sender info.
 Bytes peer;header(peer,1,200,48);put32(peer,0x99);for(int i=0;i<5;++i) put32(peer,0);reportBlock(peer,ssrc,128,20,0,0,0);
 stats.received(peer.data(),peer.size(),3200000);
 assert(std::fabs(stats.snapshot(3200000).loss_pct-50.0)<1e-9);

 // A negative cumulative count (duplicates) reads as no loss.
 Bytes negative;header(negative,1,201,28);put32(negative,0x99);reportBlock(negative,ssrc,0,0xFFFFFE,0,0,0);
 stats.received(negative.data(),negative.size(),3300000);
 assert(stats.snapshot(3300000).lost==0);

 // Generic NACK: one packet per entry plus one per set mask bit, for this stream only.
 Bytes nack;header(nack,1,205,16);put32(nack,0x99);put32(nack,ssrc);put32(nack,(100u<<16)|0x0005);put32(nack,(300u<<16));
 stats.received(nack.data(),nack.size(),3400000);
 Bytes foreign;header(foreign,1,205,12);put32(foreign,0x99);put32(foreign,ssrc+1);put32(foreign,(100u<<16)|0xFFFF);
 stats.received(foreign.data(),foreign.size(),3400000);
 Bytes pli;header(pli,1,206,8);put32(pli,0x99);put32(pli,ssrc);
 stats.received(pli.data(),pli.size(),3400000);
 assert(stats.snapshot(3400000).nacked==4);

 // Truncated, oversized-length and non-RTCP input is ignored without reading past the buffer.
 for(size_t cut=0;cut<rr.size();++cut) {LinkStats s(ssrc,90000);s.received(rr.data(),cut,1);assert(!s.snapshot(1).reported);}
 Bytes lying=rr;lying[3]=0xff;
 Bytes rtp(40,0);rtp[0]=0x80;rtp[1]=96;
 LinkStats untouched(ssrc,90000);
 untouched.received(lying.data(),lying.size(),1);untouched.received(rtp.data(),12,1);untouched.received(nullptr,0,1);
 Bytes v1=rr;v1[0]=0x41;untouched.received(v1.data(),v1.size(),1);
 assert(!untouched.snapshot(1).reported);
 // A block count larger than the packet holds stops at the last whole block.
 Bytes overcount=rr;overcount[0]=0x80|31;
 LinkStats counted(ssrc,90000);counted.received(overcount.data(),overcount.size(),5);
 assert(counted.snapshot(5).reported && counted.snapshot(5).lost==10);

 // Only the last eight sender reports are remembered.
 LinkStats ring(ssrc,48000);
 for(uint32_t i=0;i<9;++i) {auto s=senderReport(ssrc,i,0);ring.sent(s.data(),s.size(),i*1000000);}
 // Ten were sent (seconds 0..9), so second 1 has been pushed out and second 9 is still known.
 {auto s=senderReport(ssrc,9,0);ring.sent(s.data(),s.size(),9000000);}
 Bytes forgotten;header(forgotten,1,201,28);put32(forgotten,0x99);reportBlock(forgotten,ssrc,0,0,480,1u<<16,0);
 ring.received(forgotten.data(),forgotten.size(),9100000);
 assert(ring.snapshot(9100000).rtt_ms==-1 && std::fabs(ring.snapshot(9100000).jitter_ms-10.0)<1e-9);
 Bytes kept;header(kept,1,201,28);put32(kept,0x99);reportBlock(kept,ssrc,0,0,480,9u<<16,0);
 ring.received(kept.data(),kept.size(),9030000);
 assert(ring.snapshot(9100000).rtt_ms==30);
 std::cout<<"link stats ok\n";
}
