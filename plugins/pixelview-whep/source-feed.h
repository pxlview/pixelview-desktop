/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PIXELVIEW_SOURCE_FEED_H
#define PIXELVIEW_SOURCE_FEED_H
#include <stddef.h>
#include <stdint.h>

/* In-process, versioned C ABI. No Gst/CV/OBS types or borrowed media pointers.
 * Call source proc "native422_feed(ptr request, out int version)" while holding
 * an OBS strong source reference (acquire it from the selected weak source).
 * Video is progressive limited BT709 SDR left-sited v210, with validated native
 * samples; no full/HDR/scaling contract is implied. Tokens are source-scoped.
 * NULL request queries version. Exactly one consumer may ATTACH. The consumer
 * owns its buffers; READ copies synchronously and never calls consumer code.
 * DETACH returns with no outstanding producer access to consumer memory.
 * No callback registration, retained request pointer or SDK call occurs here.
 * RESET means flush card A/V, detach, and require fresh route admission; never
 * play the previous generation or fall back to the sender canvas.
 */
#define PV_FEED_VERSION 2u
#define PV_FEED_VIDEO_CAPACITY 3u
#define PV_FEED_AUDIO_CAPACITY 10u
#define PV_FEED_MAX_AUDIO_FRAMES 5760u
#define PV_FEED_MAX_VIDEO_BYTES (5120u * 1080u)
/* ATTACH selects an immutable route for this token/generation. Native receives
 * early PCM for timestamped scheduling; rendered receives only clocked PCM.
 * Every request repeats the route. Switching requires DETACH + fresh ATTACH. */
enum pv_feed_route { PV_FEED_NATIVE = 1, PV_FEED_RENDERED };
enum pv_feed_command { PV_FEED_ATTACH = 1, PV_FEED_VIDEO, PV_FEED_AUDIO, PV_FEED_DETACH };
enum pv_feed_status { PV_FEED_INVALID = 0, PV_FEED_OK, PV_FEED_EMPTY, PV_FEED_BUSY,
 PV_FEED_RESET, PV_FEED_BUFFER_SMALL, PV_FEED_DETACHED };
struct pv_feed_request {
 uint32_t size, version, command, status;
 uint32_t route;
 uint64_t token, generation;
 void *data;
 size_t capacity, bytes;
 uint64_t timestamp_ns, duration_ns;
 uint32_t width, height, stride, fps_num, fps_den;
 uint32_t audio_frames; /* S16LE interleaved stereo, 48000 Hz; source mute/gain applied */
 uint64_t dropped_video, dropped_audio;
};
#endif
