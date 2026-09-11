/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Finite publisher contract, NOT exact arbitrary-rational discovery. Observe
 * post-jitterbuffer RTP (90kHz), never arrival time or skew-corrected Gst PTS.
 * OBS WHIP rounds adjacent microsecond intervals independently: 24000/1001
 * produces 3754, whereas an absolute RTP clock may alternate 3753/3754.
 * Neither sequence distinguishes unsupported nearby aliases. */
enum pv422_rate_result { PV422_RATE_PENDING, PV422_RATE_READY, PV422_RATE_REJECTED };
struct pv422_rate {
 uint32_t ssrc, timestamp, marker_timestamp, num, den;
 uint16_t sequence;
 uint64_t started, last;
 /* Immutable first-failure snapshot, no packet payload or endpoint. */
 const char *reason;
 uint64_t total_packets, failure_now;
 uint16_t failure_sequence;
 uint32_t failure_delta;
 unsigned intervals, packets, candidates;
 bool initialized, marker_seen, failed;
};
static inline bool pv422_rate_supported(uint32_t n, uint32_t d)
{
 return d && ((uint64_t)n*1001==24000ULL*d || n==24ULL*d ||
              n==25ULL*d || (uint64_t)n*1001==30000ULL*d);
}
static inline enum pv422_rate_result pv422_rate_packet(struct pv422_rate *s,
 uint32_t ssrc, uint16_t seq, uint32_t timestamp, bool marker, bool discontinuity,
 uint64_t now)
{
 if(s->failed) return PV422_RATE_REJECTED;
 s->total_packets++;
 const char *reason=0;
 uint32_t delta=timestamp-s->marker_timestamp;
#define PV422_REJECT(why) do { reason=(why); goto fail; } while(0)
 if(!s->initialized) {
  s->initialized=true; s->ssrc=ssrc; s->sequence=seq; s->timestamp=timestamp;
  s->started=now; s->last=now; s->candidates=15;
 } else {
  if(discontinuity) PV422_REJECT("ordered-rtp-discontinuity");
  if(ssrc!=s->ssrc) PV422_REJECT("ordered-rtp-ssrc-change");
  if((uint16_t)(seq-s->sequence)!=1) PV422_REJECT("ordered-rtp-sequence-gap");
  if(now<s->last) PV422_REJECT("ordered-rtp-clock-backward");
  if(now-s->last>500000000ULL) PV422_REJECT("ordered-rtp-gap");
  if(!s->num && now-s->started>3000000000ULL) PV422_REJECT("ordered-rtp-acquisition-expired");
  s->sequence=seq; s->last=now;
 }
 if(++s->packets>16384) PV422_REJECT("ordered-rtp-marker-absent");
 if(!marker) return s->num ? PV422_RATE_READY : PV422_RATE_PENDING;
 s->packets=0;
 if(s->marker_seen) {
  unsigned mask=(delta==3753 || delta==3754 ? 1u:0u) |
   (delta==3750 ? 2u:0u) | (delta==3600 ? 4u:0u) | (delta==3003 ? 8u:0u);
  s->candidates &= mask;
  if(!s->candidates) PV422_REJECT(s->num ? "ordered-rtp-rate-change" : "ordered-rtp-rate-unsupported");
  if(s->intervals<32) s->intervals++;
  if(s->intervals==32) {
   static const uint32_t nums[]={24000,24,25,30000}, dens[]={1001,1,1,1001};
   unsigned index=0; while(index<4 && s->candidates!=(1u<<index)) index++;
   if(index==4) PV422_REJECT("ordered-rtp-rate-ambiguous");
   s->num=nums[index]; s->den=dens[index];
  }
 }
 s->marker_seen=true; s->marker_timestamp=timestamp;
 return s->num ? PV422_RATE_READY : PV422_RATE_PENDING;
fail:
 s->reason=reason; s->failure_now=now; s->failure_sequence=seq; s->failure_delta=delta;
 s->failed=true; return PV422_RATE_REJECTED;
#undef PV422_REJECT
}
