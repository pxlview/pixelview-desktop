/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Offline native sender audit: real libobs/Apple VT, no GUI/card/network.
 * Packet timestamps and payloads are recorded before transport quantization. */
#include <obs-module.h>
#include <media-io/video-frame.h>
#include <util/platform.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <thread>
#include <chrono>

struct Sink { obs_output_t *output; FILE *video, *csv; size_t packets = 0; };
static const char *name(void *) { return "Offline VT cadence audit"; }
static void *create(obs_data_t *settings, obs_output_t *output)
{
 auto *s = new Sink{output, nullptr, nullptr};
 std::string path = obs_data_get_string(settings, "path");
 s->video = fopen(path.c_str(), "wb"); s->csv = fopen((path + ".csv").c_str(), "w");
 assert(s->video && s->csv);
 fprintf(s->csv, "index,pts,dts,timebase_num,timebase_den,dts_usec,keyframe,bytes\n");
 return s;
}
static void destroy(void *data) { auto *s = (Sink *)data; fclose(s->video); fclose(s->csv); delete s; }
static bool start(void *data)
{
 auto *s = (Sink *)data;
 return obs_output_initialize_encoders(s->output, 0) && obs_output_begin_data_capture(s->output, 0);
}
static void stop(void *data, uint64_t)
{
 auto *s = (Sink *)data; obs_output_end_data_capture(s->output);
 obs_output_signal_stop(s->output, OBS_OUTPUT_SUCCESS);
}
static void packet(void *data, encoder_packet *p)
{
 assert(p); auto *s = (Sink *)data;
 if (!s->packets) {
  uint8_t *extra = nullptr; size_t size = 0;
  assert(obs_encoder_get_extra_data(obs_output_get_video_encoder(s->output), &extra, &size));
  assert(fwrite(extra, 1, size, s->video) == size);
 }
 assert(fwrite(p->data, 1, p->size, s->video) == p->size);
 fprintf(s->csv, "%zu,%lld,%lld,%d,%d,%lld,%d,%zu\n", s->packets++, (long long)p->pts,
         (long long)p->dts, p->timebase_num, p->timebase_den, (long long)p->dts_usec, p->keyframe, p->size);
}
int main(int argc, char **argv)
{
 assert(argc == 5); // plugin bundle, destination file, numerator, denominator
 const unsigned num = (unsigned)std::stoul(argv[3]), den = (unsigned)std::stoul(argv[4]);
 assert(num && den && num <= 30ULL * den);
 assert(obs_startup("en-US", nullptr, nullptr));
 std::string bundle = argv[1]; obs_module_t *module = nullptr;
 assert(obs_open_module(&module, (bundle + "/Contents/MacOS/mac-videotoolbox").c_str(),
                        (bundle + "/Contents/Resources").c_str()) == MODULE_SUCCESS);
 assert(obs_init_module(module)); obs_post_load_modules();
 const char *id = "com.apple.videotoolbox.videoencoder.ave.hevc";
 bool found = false; const char *candidate;
 for (size_t i = 0; obs_enum_encoder_types(i, &candidate); ++i) if (!strcmp(id, candidate)) found = true;
 assert(found);
 obs_output_info info{}; info.id = "offline_vt_cadence"; info.flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED;
 info.get_name = name; info.create = create; info.destroy = destroy; info.start = start; info.stop = stop;
 info.encoded_packet = packet; info.encoded_video_codecs = "hevc"; obs_register_output(&info);
 video_output_info vi{}; vi.name = "Synthetic HD limited709"; vi.format = VIDEO_FORMAT_P216;
 vi.fps_num = num; vi.fps_den = den; vi.width = 1920; vi.height = 1080; vi.cache_size = 8;
 vi.colorspace = VIDEO_CS_709; vi.range = VIDEO_RANGE_PARTIAL;
 video_t *video = nullptr; assert(video_output_open(&video, &vi) == 0);
 obs_data_t *settings = obs_encoder_defaults(id);
 obs_data_set_string(settings, "profile", "main42210"); obs_data_set_string(settings, "rate_control", "CBR");
 obs_data_set_int(settings, "bitrate", 6000); obs_data_set_int(settings, "keyint_sec", 1);
 obs_data_set_bool(settings, "bframes", false); obs_data_set_int(settings, "spatial_aq_mode", 1);
 obs_encoder_t *encoder = obs_video_encoder_create(id, "Offline HD Main422", settings, nullptr);
 obs_data_release(settings); assert(encoder); obs_encoder_set_video(encoder, video);
 obs_encoder_set_scaled_size(encoder, 0, 0); obs_encoder_set_gpu_scale_type(encoder, OBS_SCALE_DISABLE);
 settings = obs_data_create(); obs_data_set_string(settings, "path", argv[2]);
 obs_output_t *output = obs_output_create(info.id, "Offline packet capture", settings, nullptr);
 obs_data_release(settings); assert(output); obs_output_set_video_encoder(output, encoder);
 assert(obs_output_start(output));
 uint64_t epoch = os_gettime_ns();
 for (unsigned n = 0; n < 180; ++n) {
  video_frame frame{};
  assert(video_output_lock_frame(video, &frame, 1, epoch + uint64_t(n) * 1000000000 * den / num));
  for (unsigned y = 0; y < vi.height; ++y) {
   auto *luma = (uint16_t *)(frame.data[0] + y * frame.linesize[0]);
   auto *chroma = (uint16_t *)(frame.data[1] + y * frame.linesize[1]);
   for (unsigned x = 0; x < vi.width; ++x) {
    // Broad temporal patch survives lossy VT. Chroma detail is not lossless reference evidence.
    luma[x] = uint16_t((64 + (n * 4 + x / 16) % 877) << 6);
    chroma[x] = uint16_t((y % 2 ? 400 : 620) << 6);
   }
  }
  video_output_unlock_frame(video);
  os_sleepto_ns(epoch + uint64_t(n + 1) * 1000000000 * den / num);
 }
 std::this_thread::sleep_for(std::chrono::milliseconds(400));
 obs_output_stop(output);
 for (unsigned i = 0; obs_output_active(output) && i < 500; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
 assert(!obs_output_active(output));
 auto *s = (Sink *)obs_obj_get_data(output); fprintf(stderr, "VT_CAPTURE packets=%zu rate=%u/%u\n", s->packets, num, den);
 assert(s->packets >= 170); // Bounded stop need not flush all submitted asynchronous frames.
 obs_output_release(output); obs_encoder_release(encoder); video_output_close(video); obs_shutdown();
}
