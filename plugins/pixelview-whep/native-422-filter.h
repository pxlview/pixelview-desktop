/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "native-422.h"
typedef gboolean (*pv_native422_delivery)(void *, GstSample *, const struct pv_native422_frame *);
/* Takes opaque/destroy ownership even on failure. Delivery borrows frame and
 * sample for the duration of the call. FALSE terminates this native branch. */
GstElement *pv_native422_filter_new(pv_native422_delivery delivery, void *opaque, GDestroyNotify destroy);
/* Before adding/starting the filter only. FALSE stores the eight-bit preview in
 * P010 containers to preserve the existing downstream precision-policy caps. */
/* Production WHEP contract: call before publishing the filter. Observes the
 * future public rtph265depay sink via deep-element-added; missing/ambiguous
 * topology refuses native media.
 * Observer has no source/filter pointer and lives through its pad's teardown. */
gboolean pv_native422_filter_require_rtp(GstElement *filter, GstElement *receiver);
void pv_native422_filter_preview_format(GstElement *filter, gboolean nv12);
