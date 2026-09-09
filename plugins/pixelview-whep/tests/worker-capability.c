/* Real receiver worker/probe: no OBS device, pipeline or network allowed. */
#define PIXELVIEW_WHEP_TEST 1
#include <gst/gst.h>
static gint pipelines;
static GstElement *forbidden_parse(const char *spec,GError **error)
{ (void)spec;(void)error;g_atomic_int_inc(&pipelines);return NULL; }
#define gst_parse_launch forbidden_parse
#include "../pixelview-whep.c"
#undef gst_parse_launch
#include <stdio.h>
void pixelview_capability_probe_test_configure(const char *,unsigned,unsigned);
unsigned pixelview_capability_probe_test_worker_count(void);
int main(int argc,char **argv)
{
 g_assert_cmpint(argc,==,2);gst_init(NULL,NULL);
 bool cancel=!strcmp(argv[1],"cancel"),timeout=!strcmp(argv[1],"timeout");
 pixelview_capability_probe_test_configure(cancel?"vtdec_hw":"pixelview_absent_decoder",0,timeout?3200:cancel?250:0);
 struct receiver *r=g_new0(struct receiver,1);
 r->state="connecting";r->changed=true;r->generation=1;r->latency=50;r->jitter_latency=-1;
 r->endpoint=g_strdup("http://127.0.0.1:9/never-contacted");
 g_mutex_init(&r->lock);g_rec_mutex_init(&r->delivery);g_cond_init(&r->wake);
 gint64 start=g_get_monotonic_time();r->thread=g_thread_new("receiver-probe-test",worker,r);
 if(cancel) {
  while(!pixelview_capability_probe_test_worker_count())g_usleep(1000);
  g_mutex_lock(&r->lock);r->active_caps.profiles=77;g_mutex_unlock(&r->lock);
  disconnect_proc(r,NULL);
 } else {
  bool failed=false;
  for(unsigned i=0;i<400&&!failed;i++) {
   g_usleep(10000);g_mutex_lock(&r->lock);failed=!strcmp(r->state,"error");g_mutex_unlock(&r->lock);
  }
  g_assert_true(failed);
 }
 g_mutex_lock(&r->lock);
 g_assert_cmpuint(r->active_caps.profiles,==,cancel?77:0);
 g_assert_cmpint(r->jitter_latency,==,-1);g_assert_cmpuint(r->frames,==,0);
 r->quit=true;r->generation++;g_cond_signal(&r->wake);g_mutex_unlock(&r->lock);
 g_thread_join(r->thread);
 g_assert_cmpint(g_get_monotonic_time()-start,<,timeout?3500000:200000);
 g_cond_clear(&r->wake);g_rec_mutex_clear(&r->delivery);g_mutex_clear(&r->lock);g_free(r);
 /* A background decoder may still be running; it must not retain receiver data. */
 struct pixelview_receive_capabilities cached;
 g_assert_true(pixelview_capability_probe_get(&cached,NULL,NULL));
 if(timeout) {g_usleep(400000);g_assert_true(pixelview_capability_probe_get(&cached,NULL,NULL));g_assert_cmpuint(cached.profiles,==,0);}
 g_assert_cmpint(g_atomic_int_get(&pipelines),==,0);
 g_assert_cmpuint(pixelview_capability_probe_test_worker_count(),==,1);
 printf("PASS worker %s: bounded, zero pipeline/network/SDP/media, no stale caps or retained receiver\n",argv[1]);
}
