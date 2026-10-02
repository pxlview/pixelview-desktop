// SPDX-License-Identifier: GPL-2.0-or-later
// SDI pattern player and/or capture host for the end-to-end test.
// env PLAY=<output device substring> PLAY_RANGE=limited|full   -> plays the e2e pattern as v210
//     E2E_FORMAT=rgb                                            -> the 4:4:4 picture as 10-bit RGB (R10l) instead,
//                                                                  played and captured as RGB 4:4:4 on SDI
//     CAPTURE=<input device substring>                          -> captures (the interposed observer analyses)
//     SECONDS=<run time>  E2E_FPS_NUM/E2E_FPS_DEN=<frame rate of the mode, default 25/1>
#import <Foundation/Foundation.h>
#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>
#include <media-io/video-io.h>
#include <media-io/video-frame.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
extern "C" {
#include "e2e-pattern.h"
}
static void pump(double s) { [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:s]]; }
static bool choose(obs_properties_t *props, obs_data_t *settings, const char *key, const char *want, bool is_int) {
	obs_property_t *p = obs_properties_get(props, key); if (!p) return false; bool found = false;
	for (size_t i = 0; i < obs_property_list_item_count(p); i++) {
		const char *n = obs_property_list_item_name(p, i);
		const bool match = !found && n && strstr(n, want) && !obs_property_list_item_disabled(p, i);
		printf("  %s[%zu] %s%s value=%s\n", key, i, n ? n : "", match ? "   <- selected" : "", is_int ? std::to_string(obs_property_list_item_int(p, i)).c_str() : obs_property_list_item_string(p, i));
		if (match) { found = true; if (is_int) obs_data_set_int(settings, key, obs_property_list_item_int(p, i)); else { obs_data_set_string(settings, key, obs_property_list_item_string(p, i)); if (!strcmp(key, "device_hash")) obs_data_set_string(settings, "device_name", n); } obs_property_modified(p, settings); }
	}
	return found;
}
int main(int argc, char **argv) { @autoreleasepool {
	if (argc < 6) return 2; // libobs data, graphics module, decklink binary, decklink data, mode name
	setbuf(stdout, NULL);
	const char *play = getenv("PLAY"), *capture = getenv("CAPTURE");
	const bool full = getenv("PLAY_RANGE") && !strcmp(getenv("PLAY_RANGE"), "full");
	const bool rgb = getenv("E2E_FORMAT") && !strcmp(getenv("E2E_FORMAT"), "rgb");
	const int seconds = getenv("SECONDS") ? atoi(getenv("SECONDS")) : 20;
	const uint32_t fps_num = getenv("E2E_FPS_NUM") ? (uint32_t)atoi(getenv("E2E_FPS_NUM")) : 25, fps_den = getenv("E2E_FPS_DEN") ? (uint32_t)atoi(getenv("E2E_FPS_DEN")) : 1;
	if (!obs_startup("en-US", nullptr, nullptr)) return 3;
	obs_add_data_path(argv[1]);
	obs_video_info vi = {}; vi.graphics_module = argv[2]; vi.fps_num = fps_num; vi.fps_den = fps_den;
	vi.base_width = vi.output_width = 320; vi.base_height = vi.output_height = 180; vi.output_format = VIDEO_FORMAT_NV12;
	vi.colorspace = VIDEO_CS_709; vi.range = VIDEO_RANGE_PARTIAL; vi.gpu_conversion = true; vi.scale_type = OBS_SCALE_DISABLE;
	if (obs_reset_video(&vi) != OBS_VIDEO_SUCCESS) return 4;
	obs_audio_info ai = {48000, SPEAKERS_STEREO}; if (!obs_reset_audio(&ai)) return 5;
	obs_module_t *module = nullptr;
	if (obs_open_module(&module, argv[3], argv[4]) != MODULE_SUCCESS || !obs_init_module(module)) { puts("decklink module failed to load"); return 6; }
	pump(2.5);
	obs_output_t *output = nullptr; video_t *queue = nullptr; std::vector<uint8_t> v210;
	const unsigned stride = rgb ? EW * 4 : ((EW + 47) / 48) * 128;
	if (play) {
		v210.resize(stride * EH);
		// R10l: one little-endian word per pixel, R in bits 31-22, G in 21-12, B in 11-2.
		if (rgb) for (unsigned y = 0; y < EH; y++) for (unsigned x = 0; x < EW; x++) {
			const uint32_t w = e2e_rgb_sample(x, y, 0, full) << 22 | e2e_rgb_sample(x, y, 1, full) << 12 | e2e_rgb_sample(x, y, 2, full) << 2;
			memcpy(&v210[(size_t)y * stride + x * 4], &w, 4); }
		else for (unsigned y = 0; y < EH; y++) for (unsigned g = 0; g < EW / 6; g++) { unsigned Y[6], cb[3], cr[3];
			for (unsigned k = 0; k < 6; k++) Y[k] = e2e_sample(g * 6 + k, y, 0, full);
			for (unsigned k = 0; k < 3; k++) { cb[k] = e2e_sample(g * 6 + 2 * k, y, 1, full); cr[k] = e2e_sample(g * 6 + 2 * k, y, 2, full); }
			const uint32_t w[4] = {cb[0] | Y[0] << 10 | cr[0] << 20, Y[1] | cb[1] << 10 | Y[2] << 20, cr[1] | Y[3] << 10 | cb[2] << 20, Y[4] | cr[2] << 10 | Y[5] << 20};
			memcpy(&v210[(size_t)y * stride + g * 16], w, 16); }
		obs_data_t *os = obs_data_create(); obs_properties_t *op = obs_get_output_properties("decklink_output");
		puts("output devices/modes:");
		if (!op || !choose(op, os, "device_hash", play, false) || !choose(op, os, "mode_id", argv[5], true)) { puts("no output device/mode"); return 7; }
		obs_data_set_int(os, "output_format", rgb ? 1 : 0);
		output = obs_output_create("decklink_output", "pattern out", os, nullptr);
		const struct video_scale_info *conv = output ? obs_output_get_video_conversion(output) : nullptr;
		if (!conv) { puts("output unavailable"); return 8; }
		video_output_info qi = {}; qi.name = "pattern queue"; qi.format = rgb ? VIDEO_FORMAT_R10L : VIDEO_FORMAT_V210; qi.width = EW; qi.height = EH; qi.fps_num = fps_num; qi.fps_den = fps_den; qi.cache_size = 16; qi.colorspace = VIDEO_CS_DEFAULT; qi.range = conv->range;
		if (video_output_open(&queue, &qi) != VIDEO_OUTPUT_SUCCESS) return 9;
		obs_output_set_media(output, queue, obs_get_audio());
		const bool started = obs_output_start(output);
		printf("pattern output (%s, %s range codes) start: %d %s\n", rgb ? "10-bit RGB 4:4:4" : "10-bit 4:2:2 YUV", full ? "full" : "limited", started, started ? "" : (obs_output_get_last_error(output) ? obs_output_get_last_error(output) : ""));
		if (!started) return 10;
	}
	obs_source_t *source = nullptr;
	if (capture) {
		obs_data_t *is = obs_data_create(); obs_properties_t *ip = obs_get_source_properties("decklink-input");
		puts("input devices/modes:");
		if (!ip || !choose(ip, is, "device_hash", capture, false)) { puts("no capture device"); return 11; }
		choose(ip, is, "mode_id", argv[5], true);
		if (rgb) choose(ip, is, "pixel_format", "10-bit RGB", true);
		source = obs_source_create_private("decklink-input", "analyser in", is);
		printf("capture source: %s\n", source ? "created" : "FAILED");
	}
	puts("RUNNING");
	const double frame_seconds = (double)fps_den / fps_num;
	for (int i = 0; i < (int)(seconds / frame_seconds); i++) {
		if (queue) { struct video_frame o; if (video_output_lock_frame(queue, &o, 1, os_gettime_ns())) { for (unsigned y = 0; y < EH; y++) memcpy(o.data[0] + (size_t)y * o.linesize[0], &v210[(size_t)y * stride], o.linesize[0]); video_output_unlock_frame(queue); } }
		pump(frame_seconds);
	}
	if (source) obs_source_release(source);
	if (output) { obs_output_stop(output); pump(1.0); obs_output_release(output); video_output_close(queue); }
	puts("done"); _exit(0);
}}
