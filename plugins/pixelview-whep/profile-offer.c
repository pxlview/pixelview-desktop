/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "profile-offer.h"
GstCaps *pixelview_profile_offer_caps(const GstCaps *input, unsigned profiles, unsigned level)
{
 if (!input || gst_caps_is_any(input)) return NULL;
 gboolean valid_level = FALSE;
 switch (level) {
 case 30: case 60: case 63: case 90: case 93: case 120: case 123:
 case 150: case 153: case 156: case 180: case 183: case 186: valid_level = TRUE;
 }
 char level_string[4]; g_snprintf(level_string, sizeof(level_string), "%u", level);
 gboolean used[128] = {0};
 for (guint i=0; i<gst_caps_get_size(input); i++) {
  int pt;
  if (!gst_structure_get_int(gst_caps_get_structure(input,i),"payload",&pt) ||
      pt < 0 || pt > 127 || used[pt]) return NULL;
  used[pt] = TRUE;
 }
 used[111] = TRUE; /* reserve separately offered Opus */
 GstCaps *out = gst_caps_new_empty();
 for (guint i=0; i<gst_caps_get_size(input); i++) {
  const GstStructure *source = gst_caps_get_structure(input,i);
  const char *name = gst_structure_get_string(source,"encoding-name");
  if (!name) continue;
  gboolean h264 = !g_ascii_strcasecmp(name,"H264");
  gboolean hevc = !g_ascii_strcasecmp(name,"H265");
  gboolean vp9 = !g_ascii_strcasecmp(name,"VP9");
  gboolean opus = !g_ascii_strcasecmp(name,"OPUS");
  unsigned choices[2] = {0,0};
  if (h264) choices[0] = profiles & PV_PROFILE_H264;
  if (hevc && valid_level) {
   choices[0] = profiles & PV_PROFILE_HEVC_MAIN;
   choices[1] = profiles & PV_PROFILE_HEVC_MAIN10;
  }
  if (vp9) { choices[0] = profiles & PV_PROFILE_VP9_0; choices[1] = profiles & PV_PROFILE_VP9_2; }
  if (opus) choices[0] = 1;
  gboolean first = TRUE;
  for (unsigned j=0; j<2; j++) {
   if (!choices[j]) continue;
   GstStructure *s = gst_structure_copy(source);
   if (!first) {
    int pt;
    /* Pinned rswebrtc creates audio AFTER this hook using low PTs. Keep
     * 96..119 for its H264/H265/VP9/Opus originals and automatic repair PTs. */
    for (pt=120; pt<128 && used[pt]; pt++) {}
    if (pt == 128) { gst_structure_free(s); gst_caps_unref(out); return NULL; }
    used[pt] = TRUE; gst_structure_set(s,"payload",G_TYPE_INT,pt,NULL);
   }
   first = FALSE;
   if (opus) gst_structure_set(s,"encoding-params",G_TYPE_STRING,"2",NULL);
   /* The embedded HD60 probe SPS is 67 42 c0 2a: constrained baseline,
    * constraint_set0/1, level4.2. Do not inherit unprobed factory profiles. */
   if (h264) gst_structure_set(s,"packetization-mode",G_TYPE_STRING,"1",
                               "profile-level-id",G_TYPE_STRING,"42c02a",NULL);
   if (hevc) {
    /* GstSDP serializes string fields in insertion order. Reinsert in engine order. */
    gst_structure_remove_fields(s,"level-id","profile-id","tier-flag","tx-mode",NULL);
    gst_structure_set(s,"level-id",G_TYPE_STRING,level_string,"profile-id",G_TYPE_STRING,j ? "2" : "1","tier-flag",G_TYPE_STRING,"0","tx-mode",G_TYPE_STRING,"SRST",NULL);
   }
   if (vp9) gst_structure_set(s,"profile-id",G_TYPE_STRING,j ? "2" : "0",NULL);
   gst_caps_append_structure(out,s);
  }
 }
 return out;
}

GstCaps *pixelview_profile_offer_caps_limited(const GstCaps *input,
 const struct pixelview_receive_limits *l)
{
 if (!l || !l->max_width || !l->max_height || !l->max_fps ||
     l->max_width > 1920 || l->max_height > 1080 || l->max_fps > 60) return NULL;
 if (l->profiles & (PV_PROFILE_HEVC_MAIN | PV_PROFILE_HEVC_MAIN10)) {
  /* H.265 Table A.6 MaxLumaPs / MaxLumaSr, limited to current HD policy. */
  guint64 ps=0, sr=0;
  switch (l->hevc_level_id) {
  case 30: ps=36864; sr=552960; break;
  case 60: ps=122880; sr=3686400; break;
  case 63: ps=245760; sr=7372800; break;
  case 90: ps=552960; sr=16588800; break;
  case 93: ps=983040; sr=33177600; break;
  case 120: ps=2228224; sr=66846720; break;
  case 123: ps=2228224; sr=133693440; break;
  default: return NULL;
  }
  guint64 pixels=(guint64)l->max_width*l->max_height;
  if (pixels>ps || pixels*l->max_fps>sr) return NULL;
 }
 return pixelview_profile_offer_caps(input,l->profiles,l->hevc_level_id);
}
