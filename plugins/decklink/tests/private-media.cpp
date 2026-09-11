// SPDX-License-Identifier: GPL-2.0-or-later
#include <obs.h>
#include <util/platform.h>
#include "DecklinkOutput.hpp"
#include "decklink-private-media.hpp"
#include <cassert>
#include <cstdio>
static int videoCalls = 0, audioCalls = 0;
static const char *name(void *)
{
	return "private accounting test";
}
static void *create(obs_data_t *, obs_output_t *o)
{
	return o;
}
static bool start(void *p)
{
	return obs_output_begin_data_capture((obs_output_t *)p, 0);
}
static void stop(void *p, uint64_t)
{
	obs_output_end_data_capture((obs_output_t *)p);
}
int main(int argc, char **argv)
{
	assert(argc == 3);
	assert(obs_startup("en-US", nullptr, nullptr));
	obs_audio_info ai = {48000, SPEAKERS_STEREO};
	assert(obs_reset_audio(&ai));
	obs_add_data_path(argv[2]);
	obs_video_info vi = {};
	vi.graphics_module = argv[1];
	vi.fps_num = 30;
	vi.fps_den = 1;
	vi.base_width = vi.output_width = 32;
	vi.base_height = vi.output_height = 32;
	vi.output_format = VIDEO_FORMAT_BGRA;
	vi.colorspace = VIDEO_CS_709;
	vi.range = VIDEO_RANGE_PARTIAL;
	vi.gpu_conversion = false;
	assert(obs_reset_video(&vi) == OBS_VIDEO_SUCCESS);
	obs_output_info info = {};
	info.id = "private_accounting";
	info.flags = OBS_OUTPUT_AV;
	info.get_name = name;
	info.create = create;
	info.destroy = [](void *) {
	};
	info.start = start;
	info.stop = stop;
	info.raw_video = [](void *, video_data *) {
		++videoCalls;
	};
	info.raw_audio = [](void *, audio_data *) {
		++audioCalls;
	};
	obs_register_output(&info);
	auto *o = obs_output_create(info.id, "private", nullptr, nullptr);
	assert(o);
	DeckLinkPrivateMedia media;
	assert(media.Open(o, 48, 2, 30000, 1001));
	assert(obs_output_audio(o) && obs_output_audio(o) != obs_get_audio());
	assert(obs_output_video(o) && obs_output_video(o) != obs_get_video());
	assert(obs_output_start(o));
	assert(obs_output_active(o));
	os_sleep_ms(100);
	assert(videoCalls == 0 && audioCalls == 0);
	obs_output_stop(o);
	for (int i = 0; i < 100 && obs_output_active(o); i++) {
		os_sleep_ms(2);
	}
	assert(!obs_output_active(o));
	// Caller replaces both endpoints while the retained output is inactive.
	// Another real private libobs pair supplies externally owned test endpoints.
	auto *external = obs_output_create(info.id, "external endpoints", nullptr, nullptr);
	assert(external);
	DeckLinkPrivateMedia replacement;
	assert(replacement.Open(external, 64, 4, 30, 1));
	auto *externalVideo = obs_output_video(external);
	auto *externalAudio = obs_output_audio(external);
	obs_output_set_media(o, externalVideo, externalAudio);
	assert(media.Open(o, 48, 2, 30000, 1001));
	media.Restore(o);
	assert(obs_output_video(o) == externalVideo && obs_output_audio(o) == externalAudio);
	assert(video_output_get_info(externalVideo)->width == 64);
	media.Close();
	obs_output_release(o);
	obs_output_release(external);
	replacement.Close();
	obs_shutdown();
	puts("real libobs private A/V activity/no global mix PASS");
}
