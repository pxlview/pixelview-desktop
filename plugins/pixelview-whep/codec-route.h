/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <gst/gst.h>
/* Encoded-filter bin for the receive pipeline: a stock capsfilter named
 * "codec-route" plus a CAPS probe. The first parsed CAPS event pins the codec
 * (and, for HEVC, the exact profile) for the rest of the attempt; no AU
 * callback, queue or decoder is added and compressed buffers pass unchanged.
 * A parsed HEVC stream whose profile is not admitted is refused before its
 * first AU with a GST_STREAM_ERROR_WRONG_TYPE whose details structure is
 * PV_UNSUPPORTED_PROFILE_DETAILS carrying "reason" = one of the canonical
 * PV_UNSUPPORTED_* strings (never sender-controlled text). */
#define PV_UNSUPPORTED_PROFILE_DETAILS "pixelview-unsupported-profile"
#define PV_UNSUPPORTED_HEVC_MAIN_422_10 "unsupported-hevc-main-422-10"
#define PV_UNSUPPORTED_HEVC_PROFILE "unsupported-hevc-profile"
/* Floating reference; NULL when the capsfilter factory is unavailable. */
GstElement *pv_codec_route_new(void);
/* Admits a parsed main-422-10 stream: the caller's raw policy and the patched
 * vtdec deliver it as v210. Set only when the capability probe decoded Main
 * 4:2:2 10. Closed by default; main and main-10 are always admitted. Call
 * before the first parsed CAPS event, i.e. before the filter is published. */
void pv_codec_route_admit_main422(GstElement *filter, gboolean admit);
