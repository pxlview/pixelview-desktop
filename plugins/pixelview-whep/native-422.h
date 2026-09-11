/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <gst/gst.h>

/* Worker-confined decoder. Returned storage is borrowed until next decode or
 * destroy; consumers must copy before returning. No card, signaling or preview
 * ownership lives here. Rejection is sticky until a new receive generation. */
struct pv_native422;
/* Worker-confined diagnostic snapshot. Absolute GLib monotonic nanoseconds;
 * zero denotes an event not reached. Never used to alter admission/media time. */
struct pv_native422_timing {
 guint64 begin, configured, session_begin, session_end, submitted, returned, waited;
 guint64 callback, first_callback, packed, previewed;
};
struct pv_native422_timing pv_native422_get_timing(const struct pv_native422 *decoder);
enum pv_native422_result { PV_NATIVE422_REJECTED, PV_NATIVE422_FRAME };
struct pv_native422_frame {
 const guint8 *v210;
 guint width, height, stride;
 GstClockTime pts, duration;
 gboolean hardware;
};
struct pv_native422 *pv_native422_create(void);
/* Before decode: apply the approved HD progressive level120 finite-rate envelope. */
void pv_native422_require_initial_format(struct pv_native422 *decoder,guint numerator,guint denominator);
void pv_native422_destroy(struct pv_native422 *decoder);
enum pv_native422_result pv_native422_decode(struct pv_native422 *decoder,
 GstSample *sample, struct pv_native422_frame *frame);
/* Optional owned NV12 limited709 copy. Caller unrefs; never aliases packed or CV
 * storage. Preview allocation failure does not reject an otherwise native frame. */
enum pv_native422_result pv_native422_decode_preview(struct pv_native422 *decoder,
 GstSample *sample, struct pv_native422_frame *frame, GstBuffer **preview);
