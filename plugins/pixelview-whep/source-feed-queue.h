/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PIXELVIEW_SOURCE_FEED_QUEUE_H
#define PIXELVIEW_SOURCE_FEED_QUEUE_H
#include "source-feed.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Private source implementation. All operations require the receiver lock.
 * Media is copied at both boundaries; no arbitrary callback runs under it. */
struct pv_feed_item { struct pv_feed_request media; };
struct pv_feed_queue {
 struct pv_feed_item video[PV_FEED_VIDEO_CAPACITY], audio[PV_FEED_AUDIO_CAPACITY];
 unsigned videos, audios;
 uint32_t route;
 uint64_t token, serial, generation, dropped_video, dropped_audio;
 bool invalidated;
};
static inline void pv_feed_clear_items(struct pv_feed_queue *q)
{
 for (unsigned i=0; i<q->videos; i++) free(q->video[i].media.data);
 for (unsigned i=0; i<q->audios; i++) free(q->audio[i].media.data);
 q->videos=q->audios=0;
}
static inline void pv_feed_reset(struct pv_feed_queue *q)
{
 pv_feed_clear_items(q); q->invalidated=true;
}
static inline bool pv_feed_push(struct pv_feed_queue *q, uint64_t generation,
 const struct pv_feed_request *media, bool audio)
{
 if (!q->token || q->invalidated || q->generation != generation) return false;
 size_t max = audio ? PV_FEED_MAX_AUDIO_FRAMES * 4u : PV_FEED_MAX_VIDEO_BYTES;
 if (!media->data || !media->bytes || media->bytes > max) { pv_feed_reset(q); return false; }
 void *copy = malloc(media->bytes);
 if (!copy) { pv_feed_reset(q); return false; }
 memcpy(copy, media->data, media->bytes);
 struct pv_feed_item *items = audio ? q->audio : q->video;
 unsigned *count = audio ? &q->audios : &q->videos;
 unsigned capacity = audio ? PV_FEED_AUDIO_CAPACITY : PV_FEED_VIDEO_CAPACITY;
 if (*count == capacity) {
  free(items[0].media.data);
  memmove(items, items+1, (--*count) * sizeof(*items));
  if (audio) q->dropped_audio++; else q->dropped_video++;
 }
 items[*count].media = *media; items[(*count)++].media.data=copy;
 return true;
}
static inline void pv_feed_request(struct pv_feed_queue *q, uint64_t generation,
 struct pv_feed_request *r)
{
 if (!r || r->size < sizeof(*r)) return;
 r->status=PV_FEED_INVALID; r->bytes=0;
 if (r->version != PV_FEED_VERSION || (r->route != PV_FEED_NATIVE && r->route != PV_FEED_RENDERED)) return;
 if (r->command == PV_FEED_ATTACH) {
  if (q->token) { r->status=PV_FEED_BUSY; return; }
  /* Exhaustion fails closed rather than reusing a stale consumer token. */
  if (q->serial == UINT64_MAX) return;
  pv_feed_clear_items(q); q->invalidated=false;
  q->token=++q->serial; q->generation=generation; q->route=r->route;
  q->dropped_video=q->dropped_audio=0;
  r->token=q->token; r->generation=generation; r->status=PV_FEED_OK; return;
 }
 if (!r->token || r->token != q->token) { r->status=PV_FEED_DETACHED; return; }
 if (r->route != q->route || r->generation != q->generation) return;
 if (r->command == PV_FEED_DETACH) {
  pv_feed_reset(q); q->token=0; r->status=PV_FEED_OK; return;
 }
 if (q->invalidated || generation != q->generation || r->generation != generation) {
  pv_feed_reset(q); r->status=PV_FEED_RESET; return;
 }
 if (r->command != PV_FEED_VIDEO && r->command != PV_FEED_AUDIO) return;
 bool audio = r->command == PV_FEED_AUDIO;
 unsigned *count = audio ? &q->audios : &q->videos;
 struct pv_feed_item *items = audio ? q->audio : q->video;
 r->dropped_video=q->dropped_video; r->dropped_audio=q->dropped_audio;
 if (!*count) { r->status=PV_FEED_EMPTY; return; }
 struct pv_feed_request *m=&items[0].media;
 r->bytes=m->bytes;
 if (!r->data || r->capacity < m->bytes) { r->status=PV_FEED_BUFFER_SMALL; return; }
 memcpy(r->data, m->data, m->bytes);
 r->timestamp_ns=m->timestamp_ns; r->duration_ns=m->duration_ns;
 r->width=m->width; r->height=m->height; r->stride=m->stride;
 r->fps_num=m->fps_num; r->fps_den=m->fps_den; r->audio_frames=m->audio_frames;
 free(m->data); memmove(items, items+1, (--*count) * sizeof(*items));
 r->status=PV_FEED_OK;
}
#endif
