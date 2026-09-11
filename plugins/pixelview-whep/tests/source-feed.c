/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../source-feed-queue.h"
#include <assert.h>
#include <stdio.h>

static struct pv_feed_request request(unsigned command)
{
 return (struct pv_feed_request){.size=sizeof(struct pv_feed_request), .version=PV_FEED_VERSION,
  .command=command, .route=PV_FEED_NATIVE};
}
int main(void)
{
 struct pv_feed_queue q = {0};
 struct pv_feed_request r = request(PV_FEED_ATTACH);
 pv_feed_request(&q, 7, &r); assert(r.status == PV_FEED_OK && r.token && r.generation == 7);
 uint64_t token = r.token;
 struct pv_feed_request wrong=r; wrong.command=PV_FEED_DETACH; wrong.route=PV_FEED_RENDERED;
 pv_feed_request(&q, 7, &wrong); assert(wrong.status==PV_FEED_INVALID && q.token==token);
 wrong=r; wrong.command=PV_FEED_DETACH; wrong.generation++;
 pv_feed_request(&q, 7, &wrong); assert(wrong.status==PV_FEED_INVALID && q.token==token);
 wrong=r; wrong.command=PV_FEED_AUDIO; wrong.route=0;
 pv_feed_request(&q, 7, &wrong); assert(wrong.status==PV_FEED_INVALID);
 wrong=r; wrong.version=1;
 pv_feed_request(&q, 7, &wrong); assert(wrong.status==PV_FEED_INVALID);
 struct pv_feed_request other = request(PV_FEED_ATTACH);
 pv_feed_request(&q, 7, &other); assert(other.status == PV_FEED_BUSY);
 uint8_t data[128] = {0}, copy[128];
 struct pv_feed_request video = {.data=data, .bytes=sizeof(data), .width=2, .height=1, .stride=128,
  .fps_num=30000, .fps_den=1001, .duration_ns=33366666};
 for (unsigned i=0; i<4; i++) {
  data[0] = i; video.timestamp_ns = 1000000000 + i * 33366666ULL;
  pv_feed_push(&q, 7, &video, false);
 }
 memset(data, 255, sizeof(data));
 r = request(PV_FEED_VIDEO); r.token=token; r.generation=7; r.data=copy; r.capacity=1;
 pv_feed_request(&q, 7, &r); assert(r.status == PV_FEED_BUFFER_SMALL && r.bytes == 128);
 r.capacity=sizeof(copy);
 for (unsigned i=1; i<4; i++) {
  pv_feed_request(&q, 7, &r);
  assert(r.status == PV_FEED_OK && copy[0] == i && r.dropped_video == 1);
  assert(r.timestamp_ns == 1000000000 + i * 33366666ULL);
 }
 pv_feed_request(&q, 7, &r); assert(r.status == PV_FEED_EMPTY);
 struct pv_feed_request audio = {.data=data, .bytes=128, .audio_frames=32, .timestamp_ns=1000000000};
 pv_feed_push(&q, 7, &audio, true);
 r.command=PV_FEED_AUDIO; pv_feed_request(&q, 7, &r);
 assert(r.status == PV_FEED_OK && r.audio_frames == 32 && r.timestamp_ns == 1000000000);
 pv_feed_push(&q, 7, &video, false);
 pv_feed_reset(&q);
 r.command=PV_FEED_VIDEO; pv_feed_request(&q, 8, &r); assert(r.status == PV_FEED_RESET);
 r.command=PV_FEED_DETACH; pv_feed_request(&q, 8, &r); assert(r.status == PV_FEED_OK);
 r.command=PV_FEED_VIDEO; pv_feed_request(&q, 8, &r); assert(r.status == PV_FEED_DETACHED);
 r = request(PV_FEED_ATTACH); pv_feed_request(&q, 8, &r); assert(r.status == PV_FEED_OK && r.token != token);
 r.command=PV_FEED_VIDEO; r.data=copy; r.capacity=sizeof(copy);
 pv_feed_request(&q, 8, &r); assert(r.status == PV_FEED_EMPTY);
 r.version++; pv_feed_request(&q, 8, &r); assert(r.status == PV_FEED_INVALID);
 pv_feed_reset(&q);
 puts("PASS source feed: owned bounded A/V, overflow, buffer retry, epoch reset, exclusive token/detach");
}
