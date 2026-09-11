/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../native-422.h"
#include <gst/app/gstappsink.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

int main(int argc, char **argv)
{
 assert(argc == 4 || argc == 6);
 unsigned width = argc == 6 ? (unsigned)atoi(argv[4]) : 1024;
 unsigned height = argc == 6 ? (unsigned)atoi(argv[5]) : 64;
 gst_init(NULL, NULL);
 GstElement *p = gst_parse_launch("filesrc name=input ! h265parse ! video/x-h265,stream-format=hvc1,alignment=au ! appsink name=sink sync=false", NULL);
 assert(p);
 GstElement *input = gst_bin_get_by_name(GST_BIN(p), "input");
 g_object_set(input, "location", argv[1], NULL); gst_object_unref(input);
 GstAppSink *sink = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(p), "sink"));
 assert(gst_element_set_state(p, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
 struct pv_native422 *decoder = pv_native422_create();
 assert(decoder);
 unsigned frames = 0, rejected = 0;
 FILE *out = fopen(argv[2], "wb"); assert(out);
 for (;;) {
  GstSample *sample = gst_app_sink_try_pull_sample(sink, 5 * GST_SECOND);
  if (!sample) break;
  if (!frames && !rejected) {
   char *text = gst_caps_to_string(gst_sample_get_caps(sample));
   fprintf(stderr,"NATIVE_FIXTURE_CAPS %s\n", text); g_free(text);
  }
  /* Elementary files do not supply RTP timestamps. This offline transport
   * fixture assigns exact rational PTS; the WHEP test must use real RTP PTS. */
  GstBuffer *timed = gst_buffer_copy(gst_sample_get_buffer(sample));
  GST_BUFFER_PTS(timed) = gst_util_uint64_scale(frames + rejected, 1001 * GST_SECOND, 30000);
  GST_BUFFER_DURATION(timed) = gst_util_uint64_scale(1, 1001 * GST_SECOND, 30000);
  sample = gst_sample_make_writable(sample);
  gst_sample_set_buffer(sample, timed); gst_buffer_unref(timed);
  if (frames + rejected == 1) {
   if (!strcmp(argv[3], "malformed")) {
    GstBuffer *bad = gst_buffer_new_allocate(NULL, 4, NULL);
    gst_buffer_memset(bad, 0, 0xff, 4);
    GST_BUFFER_PTS(bad) = GST_SECOND; GST_BUFFER_DURATION(bad) = GST_SECOND / 30;
    gst_sample_set_buffer(sample, bad); gst_buffer_unref(bad);
   } else if (!strcmp(argv[3], "missing-pts")) {
    GST_BUFFER_PTS(gst_sample_get_buffer(sample)) = GST_CLOCK_TIME_NONE;
   } else if (!strcmp(argv[3], "resolution") || !strcmp(argv[3], "profile")) {
    GstCaps *changed = gst_caps_copy(gst_sample_get_caps(sample));
    if (!strcmp(argv[3], "resolution")) gst_caps_set_simple(changed, "width", G_TYPE_INT, 1022, NULL);
    else gst_caps_set_simple(changed, "profile", G_TYPE_STRING, "main-10", NULL);
    gst_sample_set_caps(sample, changed); gst_caps_unref(changed);
   }
  }
  struct pv_native422_frame frame = {0};
  enum pv_native422_result result = pv_native422_decode(decoder, sample, &frame);
  if (result == PV_NATIVE422_FRAME) {
   struct pv_native422_timing t=pv_native422_get_timing(decoder);
   assert(t.begin && t.configured>=t.begin && t.session_begin && t.session_end>=t.session_begin);
   assert(t.submitted>=t.configured && t.returned>=t.submitted && t.waited>=t.returned);
   assert(t.callback>=t.submitted && t.callback<=t.waited && t.first_callback<=t.callback);
   assert(t.packed>=t.waited && t.previewed>=t.packed);
   assert(frame.hardware && frame.width == width && frame.height == height);
   assert(frame.stride == ((width + 47) / 48) * 128 && GST_CLOCK_TIME_IS_VALID(frame.pts));
   assert(fwrite(frame.v210, frame.stride * frame.height, 1, out) == 1);
   frames++;
  } else {
   assert(result == PV_NATIVE422_REJECTED); rejected++;
  }
  gst_sample_unref(sample);
 }
 assert(gst_app_sink_is_eos(sink));
 fclose(out);
 pv_native422_destroy(decoder);
 gst_element_set_state(p, GST_STATE_NULL);
 gst_object_unref(sink); gst_object_unref(p);
 if (!strcmp(argv[3], "accept")) assert(frames == 3 && rejected == 0);
 else if (!strcmp(argv[3], "reject")) assert(frames == 0 && rejected == 3);
 else assert(frames == 1 && rejected == 2); /* refusal stays latched */
 printf("native422 frames=%u rejected=%u\n", frames, rejected);
 return 0;
}
