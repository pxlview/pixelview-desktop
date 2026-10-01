// SPDX-License-Identifier: GPL-2.0-or-later
// Hardware SDI loop (see run-sdi-loop.py): needs a DeckLink output cabled to a
// DeckLink input. Default mode: a v210 pattern through the real libobs canvas
// and the DrawV210 render to the built decklink_output; the observer compares
// what the decklink-input captures. With CANVAS=P216|P010|NV12 the card is
// fed the pattern directly, the capture fills the canvas, and the canvas
// output (the encoder input for that profile) is compared instead.
#import <Foundation/Foundation.h>
#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>
#include <media-io/video-io.h>
#include <media-io/video-frame.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "decklink-v210-render.hpp"
extern "C" {
#include "sdi-loop-pattern.h"
}
static bool capture_mode; static const uint8_t *direct; static unsigned direct_stride; static uint16_t Yi[PW * PH], Cbi[PW / 2 * PH], Cri[PW / 2 * PH];
static std::vector<uint8_t> p0, p1; static long raws; static unsigned out_bytes = 2, chroma_rows = PH; static enum video_format canvas_format;
static gs_texrender_t *tr; static gs_stagesurf_t *st; static video_t *queue; static pixelview_v210::Mode vmode; static long pushed;
static void rendered(void *) {
	if (capture_mode) { struct video_frame o; if (queue && video_output_lock_frame(queue, &o, 1, os_gettime_ns())) { for (unsigned y = 0; y < PH; y++) memcpy(o.data[0] + (size_t)y * o.linesize[0], direct + (size_t)y * direct_stride, o.linesize[0]); video_output_unlock_frame(queue); pushed++; } return; }
	gs_texture_t *tex = obs_get_main_texture(); if (!tex || !queue) return;
	const uint32_t words = pixelview_v210::row_words(PW);
	if (!tr) { tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE); st = gs_stagesurface_create(words, PH, GS_RGBA); }
	gs_texrender_reset(tr);
	if (!gs_texrender_begin(tr, words, PH)) return;
	bool ok = pixelview_v210::draw(tex, PW, PH, vmode);
	gs_texrender_end(tr); if (!ok) return;
	gs_stage_texture(st, gs_texrender_get_texture(tr));
	uint8_t *data; uint32_t stride; struct video_frame out;
	if (gs_stagesurface_map(st, &data, &stride)) {
		if (video_output_lock_frame(queue, &out, 1, os_gettime_ns())) {
			for (unsigned y = 0; y < PH; y++) memcpy(out.data[0] + (size_t)y * out.linesize[0], data + (size_t)y * stride, out.linesize[0]);
			video_output_unlock_frame(queue); pushed++;
		}
		gs_stagesurface_unmap(st);
	}
}
static void raw(void *, struct video_data *f) { const size_t row = (size_t)PW * out_bytes; p0.resize(row * PH); p1.resize(row * PH);
	for (unsigned y = 0; y < PH; y++) memcpy(&p0[y * row], f->data[0] + (size_t)y * f->linesize[0], row);
	for (unsigned y = 0; y < chroma_rows; y++) memcpy(&p1[y * row], f->data[1] + (size_t)y * f->linesize[1], row); raws++; }
static const char *name(void *) { return "pattern"; }
static void *create(obs_data_t *, obs_source_t *s) { return s; }
static void destroy(void *) {}
static void pump(double s) { [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:s]]; }
// Pick list entries by substring; prints the list.
static bool choose(obs_properties_t *props, obs_data_t *settings, const char *key, const char *want, bool is_int) {
	obs_property_t *p = obs_properties_get(props, key); if (!p) return false; bool found = false;
	for (size_t i = 0; i < obs_property_list_item_count(p); i++) {
		const char *n = obs_property_list_item_name(p, i);
		const bool match = !found && n && strstr(n, want) && !obs_property_list_item_disabled(p, i);
		printf("  %s[%zu] %s%s\n", key, i, n ? n : "", match ? "   <- selected" : "");
		if (match) { found = true; if (is_int) obs_data_set_int(settings, key, obs_property_list_item_int(p, i)); else { obs_data_set_string(settings, key, obs_property_list_item_string(p, i)); if (!strcmp(key, "device_hash")) obs_data_set_string(settings, "device_name", n); } obs_property_modified(p, settings); }
	}
	return found;
}
int main(int argc, char **argv) { @autoreleasepool {
	if (argc < 6) return 2; // data, graphics module, decklink plugin binary, decklink data dir, mode name, [sdr|pq|hlg]
	const char *cs = argc > 6 ? argv[6] : "sdr"; const bool pq = !strcmp(cs, "pq"), hlg = !strcmp(cs, "hlg"), hdr = pq || hlg;
	setbuf(stdout, NULL); const char *canvas = getenv("CANVAS"); capture_mode = canvas != nullptr;
	if (!obs_startup("en-US", nullptr, nullptr)) return 3;
	obs_add_data_path(argv[1]);
	obs_video_info vi = {}; vi.graphics_module = argv[2]; vi.fps_num = 25; vi.fps_den = 1;
	vi.base_width = vi.output_width = PW; vi.base_height = vi.output_height = PH; vi.output_format = !canvas ? VIDEO_FORMAT_P010 : !strcmp(canvas, "P216") ? VIDEO_FORMAT_P216 : !strcmp(canvas, "NV12") ? VIDEO_FORMAT_NV12 : VIDEO_FORMAT_P010; canvas_format = vi.output_format;
	out_bytes = canvas_format == VIDEO_FORMAT_NV12 ? 1 : 2; chroma_rows = canvas_format == VIDEO_FORMAT_P216 ? PH : PH / 2;
	vi.colorspace = pq ? VIDEO_CS_2100_PQ : hlg ? VIDEO_CS_2100_HLG : VIDEO_CS_709; vi.range = VIDEO_RANGE_PARTIAL; vi.gpu_conversion = true; vi.scale_type = OBS_SCALE_DISABLE;
	if (obs_reset_video(&vi) != OBS_VIDEO_SUCCESS) return 4;
	if (hdr) obs_set_video_levels(300.f, 1000.f);
	obs_audio_info ai = {48000, SPEAKERS_STEREO}; if (!obs_reset_audio(&ai)) return 5;
	obs_module_t *module = nullptr;
	if (obs_open_module(&module, argv[3], argv[4]) != MODULE_SUCCESS || !obs_init_module(module)) { puts("decklink module failed to load"); return 6; }
	pump(2.5); // device arrival callbacks
	// pattern source
	pattern_fill(Yi, Cbi, Cri);
	const unsigned stride = ((PW + 47) / 48) * 128; std::vector<uint8_t> v210(stride * PH);
	for (unsigned y = 0; y < PH; y++) for (unsigned g = 0; g < PW / 6; g++) { const uint16_t *Y = &Yi[y * PW + g * 6], *cb = &Cbi[y * (PW / 2) + g * 3], *cr = &Cri[y * (PW / 2) + g * 3];
		const uint32_t w[4] = {(uint32_t)cb[0] | Y[0] << 10 | cr[0] << 20, (uint32_t)Y[1] | cb[1] << 10 | Y[2] << 20, (uint32_t)cr[1] | Y[3] << 10 | cb[2] << 20, (uint32_t)Y[4] | cr[2] << 10 | Y[5] << 20};
		memcpy(&v210[(size_t)y * stride + g * 16], w, 16); }
	obs_source_info si = {}; si.id = "loop-pattern"; si.type = OBS_SOURCE_TYPE_INPUT; si.output_flags = OBS_SOURCE_ASYNC_VIDEO; si.get_name = name; si.create = create; si.destroy = destroy; obs_register_source(&si);
	obs_source_t *pattern = obs_source_create_private(si.id, "pattern", nullptr); obs_scene_t *scene = obs_scene_create_private("loop");
	if (!capture_mode) obs_scene_add(scene, pattern);
	direct = v210.data(); direct_stride = stride; obs_source_set_async_unbuffered(pattern, true); obs_set_output_source(0, obs_scene_get_source(scene));
	obs_source_frame2 f = {}; f.width = PW; f.height = PH; f.format = VIDEO_FORMAT_V210; f.range = VIDEO_RANGE_PARTIAL; f.trc = pq ? VIDEO_TRC_PQ : hlg ? VIDEO_TRC_HLG : VIDEO_TRC_DEFAULT;
	f.data[0] = v210.data(); f.linesize[0] = stride;
	video_format_get_parameters_for_format(vi.colorspace, f.range, f.format, f.color_matrix, f.color_range_min, f.color_range_max);
	if (!hdr) for (int i = 0; i < 3; i++) { f.color_range_min[i] = 0.f; f.color_range_max[i] = 1.f; }
	// output
	obs_data_t *os = obs_data_create(); obs_properties_t *op = obs_get_output_properties("decklink_output");
	puts("output devices/modes:");
	if (!op || !choose(op, os, "device_hash", "Monitor", false) || !choose(op, os, "mode_id", argv[5], true)) { puts("no output device/mode"); return 7; }
	obs_output_t *output = obs_output_create("decklink_output", "loop out", os, nullptr);
	const struct video_scale_info *conv = output ? obs_output_get_video_conversion(output) : nullptr;
	if (!conv) { puts("output has no conversion (device/mode unavailable)"); return 8; }
	printf("output conversion: format=%d (V210=%d) %ux%u range=%d colorspace=%d\n", (int)conv->format, (int)VIDEO_FORMAT_V210, conv->width, conv->height, (int)conv->range, (int)conv->colorspace);
	vmode = pixelview_v210::mode_for(vi.colorspace, conv->colorspace == VIDEO_CS_2100_PQ);
	video_output_info qi = {}; qi.name = "loop queue"; qi.format = VIDEO_FORMAT_V210; qi.width = PW; qi.height = PH; qi.fps_num = 25; qi.fps_den = 1; qi.cache_size = 16; qi.colorspace = VIDEO_CS_DEFAULT; qi.range = VIDEO_RANGE_PARTIAL;
	if (video_output_open(&queue, &qi) != VIDEO_OUTPUT_SUCCESS) return 9;
	obs_add_main_rendered_callback(rendered, nullptr);
	obs_output_set_media(output, queue, obs_get_audio());
	const bool started = obs_output_start(output);
	printf("output start: %d %s\n", started, started ? "" : (obs_output_get_last_error(output) ? obs_output_get_last_error(output) : ""));
	// input
	obs_data_t *is = obs_data_create(); obs_properties_t *ip = obs_get_source_properties("decklink-input");
	puts("input devices/modes:");
	obs_source_t *capture = nullptr;
	if (ip && choose(ip, is, "device_hash", "Recorder", false)) {
		choose(ip, is, "mode_id", argc > 7 ? argv[7] : argv[5], true);
		capture = obs_source_create_private("decklink-input", "loop in", is);
		if (capture_mode && capture) { obs_scene_add(scene, capture); obs_add_raw_video_callback(nullptr, raw, nullptr); }
		printf("capture source: %s pixel_format=%lld allow_10_bit=%d\n", capture ? "created" : "FAILED", (long long)obs_data_get_int(obs_source_get_settings(capture), "pixel_format"), (int)obs_data_get_bool(obs_source_get_settings(capture), "allow_10_bit"));
	} else puts("no capture device");
	for (int i = 0; i < 25 * 14; i++) { f.timestamp = os_gettime_ns(); obs_source_output_video2(pattern, &f); pump(1. / 25.); }
	if (capture_mode) { obs_remove_raw_video_callback(raw, nullptr);
		static const char *names[5] = {"luma ramp", "chroma gradients", "colour bars", "random noise", "codes 4-1019"};
		for (unsigned region = 0; region < 5 && raws; region++) { long ny = 0, ey = 0, nc = 0, ec = 0; int wy = 0, wc = 0;
			for (unsigned y = region * PH / 5 + 4; y < (region + 1) * PH / 5 - 4; y++) for (unsigned x = 6; x < PW - 6; x++) { const unsigned k = x / 2; int yo, cbo, cro, yr = Yi[y * PW + x], cbr = Cbi[y * (PW / 2) + k], crr = Cri[y * (PW / 2) + k];
				if (canvas_format == VIDEO_FORMAT_P216) { const uint16_t *Y = (const uint16_t *)p0.data(), *C = (const uint16_t *)p1.data(); yo = (Y[y * PW + x] + 32) >> 6; cbo = (C[y * PW + (x & ~1u)] + 32) >> 6; cro = (C[y * PW + (x & ~1u) + 1] + 32) >> 6; }
				else { const unsigned j2 = y / 2; cbr = (Cbi[2 * j2 * (PW / 2) + k] + Cbi[(2 * j2 + 1) * (PW / 2) + k] + 1) >> 1; crr = (Cri[2 * j2 * (PW / 2) + k] + Cri[(2 * j2 + 1) * (PW / 2) + k] + 1) >> 1;
					if (canvas_format == VIDEO_FORMAT_P010) { const uint16_t *Y = (const uint16_t *)p0.data(), *C = (const uint16_t *)p1.data(); yo = Y[y * PW + x] >> 6; cbo = C[j2 * PW + (x & ~1u)] >> 6; cro = C[j2 * PW + (x & ~1u) + 1] >> 6; }
					else { yo = p0[y * PW + x]; cbo = p1[j2 * PW + (x & ~1u)]; cro = p1[j2 * PW + (x & ~1u) + 1]; yr = (yr + 2) >> 2; cbr = (cbr + 2) >> 2; crr = (crr + 2) >> 2; } }
				int d = abs(yo - yr); ny++; ey += d == 0; if (d > wy) wy = d;
				if (!(x & 1)) { int a = abs(cbo - cbr), b = abs(cro - crr); nc += 2; ec += (a == 0) + (b == 0); if (a > wc) wc = a; if (b > wc) wc = b; } }
			printf("ENCODER-INPUT canvas=%s region=\"%s\" luma_exact=%.4f%% worst=%d chroma_exact=%.4f%% worst=%d (raw frames %ld)\n", canvas, names[region], 100.0 * ey / ny, wy, 100.0 * ec / nc, wc, raws); } }
	printf("frames pushed to the card queue: %ld, output active=%d total_frames=%d dropped=%d\n", pushed, obs_output_active(output), obs_output_get_total_frames(output), obs_output_get_frames_dropped(output));
	obs_remove_main_rendered_callback(rendered, nullptr);
	if (capture) obs_source_release(capture);
	obs_output_stop(output); pump(1.0); obs_output_release(output); video_output_close(queue);
	obs_set_output_source(0, nullptr); obs_scene_release(scene); obs_source_release(pattern);
	puts("done"); _exit(0);
}}
