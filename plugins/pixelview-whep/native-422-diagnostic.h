/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <gst/gst.h>
/* Terminal failure only. Never parse error/debug strings or serialize arbitrary
 * details. Bounded typed allowlist; canonical reason pointers own no input text. */
static const char *const pv422_diagnostic_fields[]={
 "active",
 "au",
 "decoded",
 "now",
 "started",
 "last_rtp",
 "failure_now",
 "packets",
 "seq",
 "failure_seq",
 "marker_delta",
 "intervals",
 "candidates",
 "num",
 "den",
 "pts",
 "last_pts",
 "duration",
 "previous_decode_ns",
 "previous_delivery_ns",
 "previous_push_ns",
 "native_begin",
 "configured",
 "session_begin",
 "session_end",
 "submitted",
 "returned",
 "waited",
 "callback",
 "first_callback",
 "packed",
 "previewed",
};
struct pv422_diagnostic { const char *reason; guint64 values[G_N_ELEMENTS(pv422_diagnostic_fields)]; };
static inline const char *pv422_diagnostic_reason(const char *input)
{
 static const char *const known[]={
  "au-acquisition-clock-backward",
  "au-acquisition-expired",
  "au-caps-rate-conflict",
  "au-clock-backward",
  "au-duration-conflict",
  "au-ordered-rtp-stale",
  "au-pts-gap",
  "au-pts-missing",
  "au-pts-nonmonotonic",
  "au-rtp-uninitialized",
  "au-segment-not-time",
  "ordered-rtp-acquisition-expired",
  "ordered-rtp-clock-backward",
  "ordered-rtp-discontinuity",
  "ordered-rtp-duplicate-depay",
  "ordered-rtp-gap",
  "ordered-rtp-map-or-size",
  "ordered-rtp-marker-absent",
  "ordered-rtp-observer-failed",
  "ordered-rtp-probe-attach",
  "ordered-rtp-rate-ambiguous",
  "ordered-rtp-rate-change",
  "ordered-rtp-rate-unsupported",
  "ordered-rtp-sequence-gap",
  "ordered-rtp-ssrc-change",
  "raw-caps-refused",
  "raw-segment-refused",
 };
 for(guint i=0;input && i<G_N_ELEMENTS(known);i++) if(!strcmp(input,known[i])) return known[i];
 return NULL;
}
static inline gboolean pv422_diagnostic_read(GstMessage *msg,struct pv422_diagnostic *out)
{
 if(!msg || GST_MESSAGE_TYPE(msg)!=GST_MESSAGE_ERROR) return FALSE;
 const GstStructure *s=NULL;gst_message_parse_error_details(msg,&s);
 if(!s || !gst_structure_has_name(s,"pixelview-native422-failure")) return FALSE;
 struct pv422_diagnostic d={0};
 d.reason=pv422_diagnostic_reason(gst_structure_get_string(s,"reason"));
 if(!d.reason) return FALSE;
 for(guint i=0;i<G_N_ELEMENTS(d.values);i++) {
  if(gst_structure_has_field(s,pv422_diagnostic_fields[i]) &&
     !gst_structure_get_uint64(s,pv422_diagnostic_fields[i],&d.values[i])) return FALSE;
 }
 *out=d;return TRUE;
}
static inline GstStructure *pv422_diagnostic_details(const struct pv422_diagnostic *d)
{
 GstStructure *s=gst_structure_new("pixelview-native422-failure","reason",G_TYPE_STRING,d->reason,NULL);
 for(guint i=0;i<G_N_ELEMENTS(d->values);i++)
  gst_structure_set(s,pv422_diagnostic_fields[i],G_TYPE_UINT64,d->values[i],NULL);
 return s;
}
static inline char *pv422_diagnostic_text(const struct pv422_diagnostic *d,guint64 generation)
{
 if(!d->reason) return g_strdup("");
 GString *s=g_string_new(NULL);
 g_string_append_printf(s,"generation=%" G_GUINT64_FORMAT " reason=%s",generation,d->reason);
 for(guint i=0;i<G_N_ELEMENTS(d->values);i++)
  g_string_append_printf(s," %s=%" G_GUINT64_FORMAT,pv422_diagnostic_fields[i],d->values[i]);
 return g_string_free(s,FALSE);
}
