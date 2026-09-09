/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PIXELVIEW_PROFILE_OFFER_H
#define PIXELVIEW_PROFILE_OFFER_H
#include <gst/gst.h>
/* Verified end-to-end receive profiles, NOT merely installed decoder factories.
 * No default mask: caller must supply hardware/decoder/output-path probe results.
 * HEVC 4:2:2 is deliberately absent: its receive precision path is unverified.
 * VP9 profile 3 is deliberately absent until a 4:2:2/4:4:4 decode path is proven. */
enum pixelview_receive_profile {
 PV_PROFILE_H264 = 1u,
 PV_PROFILE_HEVC_MAIN = 2u,
 PV_PROFILE_HEVC_MAIN10 = 4u,
 PV_PROFILE_VP9_0 = 8u,
 PV_PROFILE_VP9_2 = 16u,
};
/* Transfer-full result; input unchanged. HEVC advertises the exact supplied valid
 * level-id. Zero or an invalid level removes HEVC, never upgrades capability.
 * Do not claim level 6 from a smaller roundtrip or copy Safari capabilities.
 * Empty caps means no supported video: caller must abort, not use ANY/fallback.
 * Input payloads must be unique RTP values; NULL result means invalid/exhausted.
 * Intended for pinned rswebrtc raw-output <H265,H264,VP9> + <OPUS>,
 * one video and one audio transceiver. Extra profile PTs use 120..127;
 * low upstream PTs must not overlap that range. Other graphs need a shared
 * session-wide allocator, not independent calls for multiple video tracks.
 * Opus receives encoding-params=2; no other primary audio codec is retained. */
GstCaps *pixelview_profile_offer_caps(const GstCaps *input, unsigned verified_profiles,
                                    unsigned verified_hevc_level_id);
/* Validates the current HD policy envelope; caller MUST enforce these same
 * dimensions/fps on decoded raw caps. SDP level alone is not a resolution cap.
 * Explicit caller verification remains required. No automatic hardware probe. */
struct pixelview_receive_limits {
 unsigned profiles, hevc_level_id, max_width, max_height, max_fps;
};
GstCaps *pixelview_profile_offer_caps_limited(const GstCaps *input,
 const struct pixelview_receive_limits *limits);
#endif
