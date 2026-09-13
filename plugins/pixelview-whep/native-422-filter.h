/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "native-422.h"
typedef gboolean (*pv_native422_delivery)(void *, GstSample *, const struct pv_native422_frame *);
/* Takes opaque/destroy ownership even on failure. Selects on parsed CAPS:
 * ordinary media uses a stock capsfilter without a custom AU chain/queue;
 * explicit main-422-10 creates the existing native decoder + raw-preview queue.
 * Delivery borrows frame/sample for the call. FALSE terminates native decode. */
GstElement *pv_native422_filter_new(pv_native422_delivery delivery, void *opaque, GDestroyNotify destroy);
/* Native main-422-10 admission is closed by default. Without it, a parsed HEVC
 * stream whose profile is not main/main-10 is refused before its first AU with
 * a GST_STREAM_ERROR_WRONG_TYPE whose details structure is
 * PV_UNSUPPORTED_PROFILE_DETAILS carrying "reason" = one of the canonical
 * PV_UNSUPPORTED_* strings (never sender-controlled text). Call before the
 * first parsed CAPS event, i.e. before the filter is published. */
#define PV_UNSUPPORTED_PROFILE_DETAILS "pixelview-unsupported-profile"
#define PV_UNSUPPORTED_HEVC_MAIN_422_10 "unsupported-hevc-main-422-10"
#define PV_UNSUPPORTED_HEVC_PROFILE "unsupported-hevc-profile"
void pv_native422_filter_admit_native(GstElement *filter, gboolean admit);
/* Before adding/starting the filter only. FALSE stores the eight-bit preview in
 * P010 containers to preserve the existing downstream precision-policy caps. */
/* Production WHEP contract: call before publishing the filter. Observes the
 * future public rtph265depay sink via deep-element-added; missing/ambiguous
 * topology refuses native media.
 * Observer has no source/filter pointer and lives through its pad's teardown. */
gboolean pv_native422_filter_require_rtp(GstElement *filter, GstElement *receiver);
void pv_native422_filter_preview_format(GstElement *filter, gboolean nv12);
