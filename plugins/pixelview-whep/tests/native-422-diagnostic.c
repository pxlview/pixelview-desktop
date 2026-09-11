/* SPDX-License-Identifier: GPL-2.0-or-later */
#define PIXELVIEW_WHEP_TEST
#include "../pixelview-whep.c"
#include <assert.h>
#include <stdio.h>
static GstMessage *message(const char *reason, GType type, gboolean details)
{
 GstStructure *s=details?gst_structure_new("pixelview-native422-failure","reason",G_TYPE_STRING,reason,NULL):NULL;
 if(s) {
  if(type==G_TYPE_UINT64) gst_structure_set(s,"now",type,(guint64)123,NULL);
  else gst_structure_set(s,"now",G_TYPE_STRING,"https://secret.invalid/payload",NULL);
  gst_structure_set(s,"unknown",G_TYPE_STRING,"secret-token",NULL);
 }
 GError *e=g_error_new_literal(GST_STREAM_ERROR,GST_STREAM_ERROR_FORMAT,"https://secret.invalid/error");
 GstMessage *m=gst_message_new_error_with_details(NULL,e,"secret-debug",s);g_error_free(e);return m;
}
int main(void)
{
 gst_init(NULL,NULL);
 struct pv422_diagnostic expected={.reason="ordered-rtp-gap"},actual={0};
 for(guint i=0;i<G_N_ELEMENTS(expected.values);i++)expected.values[i]=G_MAXUINT64-i;
 GError *safe=g_error_new_literal(GST_STREAM_ERROR,GST_STREAM_ERROR_FORMAT,"safe");
 GstMessage *roundtrip=gst_message_new_error_with_details(NULL,safe,NULL,pv422_diagnostic_details(&expected));g_error_free(safe);
 assert(pv422_diagnostic_read(roundtrip,&actual));
 assert(!memcmp(expected.values,actual.values,sizeof(expected.values)));gst_message_unref(roundtrip);
 char *bounded=pv422_diagnostic_text(&actual,G_MAXUINT64);assert(strlen(bounded)<2048);g_free(bounded);
 assert(sizeof(actual)<=512);
 struct receiver r={.generation=7,.active_generation=7};g_mutex_init(&r.lock);g_cond_init(&r.wake);
 calldata_t cd;calldata_init(&cd);
 GstMessage *m=message("ordered-rtp-gap",G_TYPE_UINT64,TRUE);
 native422_failure_locked(&r,m);
 status_proc(&r,&cd);
 const char *text=calldata_string(&cd,"native422_diagnostic");
 assert(strstr(text,"ordered-rtp-gap") && strstr(text,"123"));
 assert(!strstr(text,"secret") && !strstr(text,"https") && !strstr(text,"unknown"));
 char *first=g_strdup(text);gst_message_unref(m);
 m=message("au-pts-gap",G_TYPE_UINT64,TRUE);native422_failure_locked(&r,m);gst_message_unref(m);
 status_proc(&r,&cd);assert(!strcmp(first,calldata_string(&cd,"native422_diagnostic")));g_free(first);
 disconnect_proc(&r,&cd);status_proc(&r,&cd);assert(!*calldata_string(&cd,"native422_diagnostic"));
 /* A late old-pipeline failure cannot repopulate a cancelled generation. */
 m=message("ordered-rtp-gap",G_TYPE_UINT64,TRUE);native422_failure_locked(&r,m);gst_message_unref(m);
 status_proc(&r,&cd);assert(!*calldata_string(&cd,"native422_diagnostic"));
 calldata_set_string(&cd,"endpoint","test://synthetic");connect_proc(&r,&cd);
 r.active_generation=r.generation;r.changed=false;
 for(unsigned bad=0;bad<3;bad++) {
  m=message(bad==0?"https://secret.invalid/reason":"ordered-rtp-gap",bad==1?G_TYPE_STRING:G_TYPE_UINT64,bad!=2);
  native422_failure_locked(&r,m);gst_message_unref(m);status_proc(&r,&cd);
  assert(!*calldata_string(&cd,"native422_diagnostic"));
 }
 m=message("au-pts-gap",G_TYPE_UINT64,TRUE);native422_failure_locked(&r,m);gst_message_unref(m);
 status_proc(&r,&cd);assert(strstr(calldata_string(&cd,"native422_diagnostic"),"au-pts-gap"));
 connect_proc(&r,&cd);status_proc(&r,&cd);assert(!*calldata_string(&cd,"native422_diagnostic"));
 disconnect_proc(&r,&cd);calldata_free(&cd);g_cond_clear(&r.wake);g_mutex_clear(&r.lock);
 puts("PASS production diagnostic typed redaction, sticky first failure, connect/disconnect/late generation lifecycle");
}
