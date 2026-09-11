/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../native-422.m"
#include <assert.h>
#include <stdio.h>

static GstBuffer *fragmented(gsize size)
{
 GstBuffer *buffer = gst_buffer_new();
 gst_buffer_append_memory(buffer, gst_allocator_alloc(NULL, size / 2, NULL));
 gst_buffer_append_memory(buffer, gst_allocator_alloc(NULL, size - size / 2, NULL));
 assert(gst_buffer_n_memory(buffer) == 2);
 return buffer;
}
int main(int argc, char **argv)
{
 assert(argc == 2); gst_init(NULL, NULL);
 struct pv_native422 *d = pv_native422_create();
 GstCaps *caps = gst_caps_from_string("video/x-h265,stream-format=hvc1,alignment=au,width=1024,height=64,framerate=30/1");
 gboolean codec = !strcmp(argv[1], "codec");
 GstBuffer *buffer = fragmented(codec ? 65537 : 16 * 1024 * 1024 + 1);
 GstMemory *first = gst_buffer_peek_memory(buffer, 0);
 GstMemory *second = gst_buffer_peek_memory(buffer, 1);
 GstSample *sample = NULL;
 if (codec) {
  gst_caps_set_simple(caps, "codec_data", GST_TYPE_BUFFER, buffer, NULL);
  gst_buffer_unref(buffer); /* Keep only the caps owner: mapping can merge. */
  buffer = gst_value_get_buffer(gst_structure_get_value(gst_caps_get_structure(caps, 0), "codec_data"));
  assert(!configure(d, caps));
 } else {
  /* Isolate the AU boundary, before any native session can be used. */
  d->caps = gst_caps_copy(caps);
  GST_BUFFER_PTS(buffer) = 0; GST_BUFFER_DURATION(buffer) = GST_SECOND / 30;
  sample = gst_sample_new(buffer, caps, NULL, NULL);
  gst_buffer_unref(buffer); /* Sample is the sole owner, as in appsink. */
  buffer = gst_sample_get_buffer(sample);
  struct pv_native422_frame frame;
  assert(pv_native422_decode(d, sample, &frame) == PV_NATIVE422_REJECTED);
  assert(d->failed && !frame.v210);
 }
 printf("%s rejection: memory blocks=%u (expected 2, no merge)\n", argv[1], gst_buffer_n_memory(buffer));
 fflush(stdout);
 assert(gst_buffer_n_memory(buffer) == 2);
 assert(gst_buffer_peek_memory(buffer, 0) == first);
 assert(gst_buffer_peek_memory(buffer, 1) == second);
 if (sample) gst_sample_unref(sample);
 gst_caps_unref(caps); pv_native422_destroy(d);
 return 0;
}
