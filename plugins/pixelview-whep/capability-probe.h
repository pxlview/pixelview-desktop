/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PIXELVIEW_CAPABILITY_PROBE_H
#define PIXELVIEW_CAPABILITY_PROBE_H
#include <glib.h>

#define PIXELVIEW_CAPABILITY_PROBE_BUDGET_MS 3000u
struct pixelview_receive_capabilities {
 unsigned profiles;       /* enum pixelview_receive_profile bits, profile-offer.h */
 unsigned hevc_level_id;  /* 123 (main-tier level 4.1) from 1080p60 fixtures, or zero */
};
typedef gboolean (*pixelview_capability_cancel_fn)(void *data);

/* Call on the receiver connection worker AFTER successful bundled gst_init_check,
 * BEFORE creating/configuring the WHEP transceivers. Never initializes GStreamer,
 * searches external runtimes, opens files/devices/network, or logs anything.
 * Returns FALSE with zero output if uninitialized or this waiter is cancelled.
 * TRUE returns an immutable process-cache snapshot (possibly zero on failure).
 * No fallback should be offered for absent bits. Only H264 constrained baseline
 * with RTP mode1, HEVC Main/Main10 4:2:0 and VP9 0/2 4:2:0 are exercised.
 * A pass means mapped native NV12/P010_10LE output from vtdec_hw, not chroma
 * fidelity, HDR accuracy, OBS/output integration, throughput or level-6 support.
 * HEVC level 123 is a conservative negotiation ceiling from the actual fixture,
 * NOT a claim to have stress-tested every legal main-tier level-4.1 bitstream.
 *
 * One process-owned background probe; concurrent waiters share a 3-second
 * monotonic deadline, checked every 20ms. Deadline seals completed profiles;
 * late results can never mutate that cache. Cancellation affects only the caller,
 * never retains its data and does not poison other callers. Callback must be fast.
 * Driver/plugin calls (including teardown) cannot be forcibly interrupted safely:
 * a stuck driver can leave ONE background thread, but cannot block a waiter or
 * spawn repeat probes. Keep this module and GStreamer loaded until process exit;
 * do not call gst_deinit or unload code while a probe could still be running.
 */
gboolean pixelview_capability_probe_get(struct pixelview_receive_capabilities *out,
                                       pixelview_capability_cancel_fn cancelled, void *data);
#endif
