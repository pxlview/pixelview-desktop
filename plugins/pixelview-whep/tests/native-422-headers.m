/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../native-422.m"
#include <gst/app/gstappsink.h>
#include <assert.h>
#include <stdio.h>
static gboolean mutate(GstCaps *caps,const char *mode)
{
 GstStructure *s=gst_caps_get_structure(caps,0);
 const char *colon=strchr(mode,':');
 if(!colon)return FALSE;
 if(g_str_has_prefix(mode,"caps:") || g_str_has_prefix(mode,"ints:")) {
  char **parts=g_strsplit(mode+5,"=",2);assert(parts[0] && parts[1]);
  if(g_str_has_prefix(mode,"ints:")) gst_structure_set(s,parts[0],G_TYPE_INT,atoi(parts[1]),NULL);
  else gst_structure_set(s,parts[0],G_TYPE_STRING,parts[1],NULL);
  g_strfreev(parts);return TRUE;
 }
 guint index,mask;assert(sscanf(colon+1,"%u:%x",&index,&mask)==2 && mask<=255);
 GstBuffer *old=gst_value_get_buffer(gst_structure_get_value(s,"codec_data"));
 GstMapInfo m;assert(gst_buffer_map(old,&m,GST_MAP_READ));
 GByteArray *out=g_byte_array_new();
 if(g_str_has_prefix(mode,"hvcc:")) {
  assert(index<m.size);g_byte_array_append(out,m.data,m.size);out->data[index]^=mask;
 } else {
  guint target=g_str_has_prefix(mode,"vps:")?32:33;
  g_byte_array_append(out,m.data,23);gsize off=23;
  for(guint a=0;a<m.data[22];a++) {
   guint type=m.data[off]&63,count=GST_READ_UINT16_BE(m.data+off+1);
   g_byte_array_append(out,m.data+off,3);off+=3;
   for(guint n=0;n<count;n++) {
    guint size=GST_READ_UINT16_BE(m.data+off);off+=2;
    GByteArray *nal=g_byte_array_new();g_byte_array_append(nal,m.data+off,2);
    if(type==target) {
     GByteArray *rbsp=g_byte_array_new();guint zeros=0;
     for(guint i=2;i<size;i++) {
      guint8 b=m.data[off+i];if(zeros==2 && b==3){zeros=0;continue;}
      g_byte_array_append(rbsp,&b,1);zeros=b==0?zeros+1:0;
     }
     guint prefix=type==32?4:1;assert(prefix+index<rbsp->len);
     rbsp->data[prefix+index]^=mask;zeros=0;
     for(guint i=0;i<rbsp->len;i++) {
      guint8 b=rbsp->data[i];if(zeros==2 && b<=3){guint8 ep=3;g_byte_array_append(nal,&ep,1);zeros=0;}
      g_byte_array_append(nal,&b,1);zeros=b==0?zeros+1:0;
     }
     g_byte_array_unref(rbsp);
    } else g_byte_array_append(nal,m.data+off+2,size-2);
    guint8 length[2];GST_WRITE_UINT16_BE(length,nal->len);
    g_byte_array_append(out,length,2);g_byte_array_append(out,nal->data,nal->len);g_byte_array_unref(nal);off+=size;
   }
  }
  assert(off==m.size);
 }
 gst_buffer_unmap(old,&m);
 GstBuffer *b=gst_buffer_new_allocate(NULL,out->len,NULL);assert(gst_buffer_fill(b,0,out->data,out->len)==out->len);
 gst_structure_set(s,"codec_data",GST_TYPE_BUFFER,b,NULL);gst_buffer_unref(b);g_byte_array_unref(out);return TRUE;
}
int main(int argc,char **argv) {
 assert(argc==3);gst_init(NULL,NULL);
 char *description=g_strdup_printf("filesrc location=\"%s\" ! h265parse ! video/x-h265,stream-format=hvc1,alignment=au ! appsink name=s sync=false",argv[1]);
 GstElement *pipe=gst_parse_launch(description,NULL),*sink=gst_bin_get_by_name(GST_BIN(pipe),"s");assert(pipe && sink);g_free(description);
 gst_element_set_state(pipe,GST_STATE_PLAYING);
 GstSample *sample=gst_app_sink_try_pull_sample(GST_APP_SINK(sink),5*GST_SECOND);assert(sample);
 GstCaps *caps=gst_caps_copy(gst_sample_get_caps(sample));
 char *caps_text=gst_caps_to_string(caps);fprintf(stderr,"HEADER_INPUT %s\n",caps_text);g_free(caps_text);
 gst_caps_set_simple(caps,"framerate",GST_TYPE_FRACTION,24,1,NULL);
 if(!strcmp(argv[2],"hvcc")) {
  GstStructure *s=gst_caps_get_structure(caps,0);GstBuffer *b=gst_buffer_copy_deep(gst_value_get_buffer(gst_structure_get_value(s,"codec_data")));
  GstMapInfo map;assert(gst_buffer_map(b,&map,GST_MAP_WRITE));memset(map.data+6,0,6);gst_buffer_unmap(b,&map);
  gst_structure_set(s,"codec_data",GST_TYPE_BUFFER,b,NULL);gst_buffer_unref(b);
 } else if(!strcmp(argv[2],"caps")) gst_caps_set_simple(caps,"tier",G_TYPE_STRING,"high","level",G_TYPE_STRING,"5",NULL);
 else if(g_str_has_prefix(argv[2],"allow-")) assert(mutate(caps,argv[2]+6));
 else if(strcmp(argv[2],"valid")) assert(mutate(caps,argv[2]));
 struct pv_native422 *d=pv_native422_create();pv_native422_require_initial_format(d,24,1);
 gboolean accepted=configure(d,caps);fprintf(stderr,"headers %s accepted=%d hardware=%d\n",argv[2],accepted,d->hardware);
 assert(accepted==(!strcmp(argv[2],"valid") || g_str_has_prefix(argv[2],"allow-")));
 pv_native422_destroy(d);gst_caps_unref(caps);gst_sample_unref(sample);gst_element_set_state(pipe,GST_STATE_NULL);gst_object_unref(sink);gst_object_unref(pipe);
 return 0;
}
