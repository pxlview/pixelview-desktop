// Offline real-libobs fidelity probe for the 4:4:4 paths. An async source fills
// a 1920x1080 scene 1:1 with either
//   r10l: 10-bit R'G'B' at video levels, what the DeckLink capture delivers for
//         an RGB 4:4:4 SDI signal (the sender), or
//   p416: 10-bit 4:4:4 Y'CbCr, what the receiver delivers for a Main 4:4:4 10
//         stream,
// and is compared, sample by sample, with
//   - the P416 canvas output the encoder is given (rounded to ten bits as the
//     encoder does), and
//   - the DrawR10L render the DeckLink output sends to the card as RGB 4:4:4.
// The references are computed in double precision with the BT.709 matrix.
// No app, capture hardware, network or persisted configuration.
#import <Foundation/Foundation.h>
#include <obs.h>
#include <util/platform.h>
#include <media-io/video-io.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "decklink-v210-render.hpp"

static constexpr unsigned W = 1920, H = 1080, REGIONS = 5;
static std::vector<uint16_t> out_y(W * H), out_c(W * H * 2); // canvas output, tightly packed rows
static std::vector<uint32_t> out_rgb(W * H);
static std::atomic<int> canvas_frames{0}, card_frames{0};
static gs_texrender_t *texrender;
static gs_stagesurf_t *stage;
static bool full_output;

static const char *name(void *)
{
	return "4:4:4 fidelity pattern";
}
static void *create(obs_data_t *, obs_source_t *s)
{
	return s;
}
static void destroy(void *) {}

static void raw(void *, struct video_data *f)
{
	for (unsigned y = 0; y < H; y++) {
		memcpy(&out_y[y * W], f->data[0] + (size_t)y * f->linesize[0], W * 2);
		memcpy(&out_c[y * W * 2], f->data[1] + (size_t)y * f->linesize[1], W * 4);
	}
	canvas_frames++;
}

static void rendered(void *)
{
	gs_texture_t *tex = obs_get_main_texture();
	if (!tex)
		return;
	if (!texrender) {
		texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		stage = gs_stagesurface_create(W, H, GS_RGBA);
	}
	gs_texrender_reset(texrender);
	if (!gs_texrender_begin(texrender, W, H))
		return;
	const bool drawn = pixelview_v210::draw_r10l(tex, W, H, pixelview_v210::Mode::SDR, full_output);
	gs_texrender_end(texrender);
	if (!drawn)
		return;
	gs_stage_texture(stage, gs_texrender_get_texture(texrender));
	uint8_t *data;
	uint32_t stride;
	if (gs_stagesurface_map(stage, &data, &stride)) {
		for (unsigned y = 0; y < H; y++)
			memcpy(&out_rgb[(size_t)y * W], data + (size_t)y * stride, W * 4);
		gs_stagesurface_unmap(stage);
		card_frames++;
	}
}

struct Stat {
	long n = 0, exact = 0;
	int worst = 0;
	void add(int d)
	{
		d = abs(d);
		n++;
		exact += d == 0;
		if (d > worst)
			worst = d;
	}
};

static int code(double v)
{
	return (int)(v < 0 ? 0 : v > 1023 ? 1023 : floor(v + 0.5));
}
// BT.709, limited range, ten bits.
static void rgb_to_ycc(const int rgb[3], double ycc[3])
{
	const double r = (rgb[0] - 64) / 876.0, g = (rgb[1] - 64) / 876.0, b = (rgb[2] - 64) / 876.0;
	const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
	ycc[0] = 64 + 876 * y;
	ycc[1] = 512 + 896 * (b - y) / 1.8556;
	ycc[2] = 512 + 896 * (r - y) / 1.5748;
}
static void ycc_to_rgb(const int ycc[3], double rgb[3])
{
	const double y = (ycc[0] - 64) / 876.0, cb = (ycc[1] - 512) / 896.0, cr = (ycc[2] - 512) / 896.0;
	const double r = y + 1.5748 * cr, b = y + 1.8556 * cb, g = (y - 0.2126 * r - 0.0722 * b) / 0.7152;
	rgb[0] = 64 + 876 * r;
	rgb[1] = 64 + 876 * g;
	rgb[2] = 64 + 876 * b;
}

// argv: libobs data dir, graphics module, source r10l|p416, [full]
int main(int argc, char **argv)
{
	@autoreleasepool {
		if (argc != 4 && argc != 5)
			return 2;
		const bool source_rgb = !strcmp(argv[3], "r10l");
		full_output = argc == 5 && !strcmp(argv[4], "full");
		if (!obs_startup("en-US", nullptr, nullptr))
			return 3;
		obs_add_data_path(argv[1]);
		obs_video_info vi = {};
		vi.graphics_module = argv[2];
		vi.fps_num = 25;
		vi.fps_den = 1;
		vi.base_width = vi.output_width = W;
		vi.base_height = vi.output_height = H;
		vi.output_format = VIDEO_FORMAT_P416;
		vi.colorspace = VIDEO_CS_709;
		vi.range = VIDEO_RANGE_PARTIAL;
		vi.gpu_conversion = true;
		vi.scale_type = OBS_SCALE_DISABLE;
		if (obs_reset_video(&vi) != OBS_VIDEO_SUCCESS)
			return 4;
		obs_source_info si = {};
		si.id = "fidelity-444";
		si.type = OBS_SOURCE_TYPE_INPUT;
		si.output_flags = OBS_SOURCE_ASYNC_VIDEO;
		si.get_name = name;
		si.create = create;
		si.destroy = destroy;
		obs_register_source(&si);
		obs_source_t *source = obs_source_create_private(si.id, "pattern", nullptr);
		obs_scene_t *scene = obs_scene_create_private("fidelity scene");
		if (!source || !scene || !obs_scene_add(scene, source))
			return 5;
		obs_source_set_async_unbuffered(source, true);
		obs_set_output_source(0, obs_scene_get_source(scene));

		// Three ten-bit components per pixel: R'G'B' for r10l, Y'CbCr for p416.
		// Rows: 0 grey ramp, 1 colour gradients, 2 a colour that changes on every
		// pixel (what chroma subsampling cannot carry), 3 random noise within the
		// nominal range, 4 every code from 4 to 1019 in the first component with
		// the others near the middle (sub-black and super-white included).
		std::vector<int> in(W * H * 3);
		srand(1);
		for (unsigned y = 0; y < H; y++) {
			const unsigned region = y * REGIONS / H;
			for (unsigned x = 0; x < W; x++) {
				int a, b, c;
				const int ramp = 64 + (int)(x * 876 / (W - 1));
				switch (region) {
				case 0:
					a = ramp;
					b = source_rgb ? ramp : 512;
					c = source_rgb ? ramp : 512;
					break;
				case 1:
					a = 300 + (int)(x % 400);
					b = source_rgb ? 200 + (int)(x * 600 / (W - 1)) : 400 + (int)(x * 220 / (W - 1));
					c = source_rgb ? 800 - (int)(x * 500 / (W - 1)) : 600 - (int)(x * 180 / (W - 1));
					break;
				case 2:
					a = source_rgb ? ((x & 1) ? 650 : 350) : 500;
					b = source_rgb ? 500 : ((x & 1) ? 580 : 440);
					c = source_rgb ? ((x & 1) ? 350 : 650) : ((x & 1) ? 450 : 570);
					break;
				case 3:
					if (source_rgb) {
						a = 64 + rand() % 877;
						b = 64 + rand() % 877;
						c = 64 + rand() % 877;
					} else {
						// Y'CbCr noise that stays inside the R'G'B' cube.
						a = 300 + rand() % 400;
						b = 462 + rand() % 101;
						c = 462 + rand() % 101;
					}
					break;
				default:
					a = 4 + (int)(x * 1015 / (W - 1));
					b = source_rgb ? a : 512;
					c = source_rgb ? a : 512;
					break;
				}
				int *p = &in[((size_t)y * W + x) * 3];
				p[0] = a;
				p[1] = b;
				p[2] = c;
			}
		}
		std::vector<uint32_t> r10l(W * H);
		std::vector<uint16_t> p416_y(W * H), p416_c(W * H * 2);
		for (size_t i = 0; i < (size_t)W * H; i++) {
			const int *p = &in[i * 3];
			r10l[i] = (uint32_t)p[0] << 22 | (uint32_t)p[1] << 12 | (uint32_t)p[2] << 2;
			p416_y[i] = (uint16_t)(p[0] << 6);
			p416_c[i * 2] = (uint16_t)(p[1] << 6);
			p416_c[i * 2 + 1] = (uint16_t)(p[2] << 6);
		}

		obs_source_frame2 f = {};
		f.width = W;
		f.height = H;
		f.format = source_rgb ? VIDEO_FORMAT_R10L : VIDEO_FORMAT_P416;
		f.range = VIDEO_RANGE_PARTIAL;
		f.trc = VIDEO_TRC_DEFAULT;
		if (source_rgb) {
			f.data[0] = (uint8_t *)r10l.data();
			f.linesize[0] = W * 4;
		} else {
			f.data[0] = (uint8_t *)p416_y.data();
			f.data[1] = (uint8_t *)p416_c.data();
			f.linesize[0] = W * 2;
			f.linesize[1] = W * 4;
		}
		if (!video_format_get_parameters_for_format(vi.colorspace, f.range, f.format, f.color_matrix,
							    f.color_range_min, f.color_range_max))
			return 6;
		// As the capture and receive plugins do for SDR: no 64-940 clamp on the way in.
		for (int i = 0; i < 3; i++) {
			f.color_range_min[i] = 0.f;
			f.color_range_max[i] = 1.f;
		}
		obs_add_raw_video_callback(nullptr, raw, nullptr);
		obs_add_main_rendered_callback(rendered, nullptr);
		for (unsigned i = 0; i < 50; i++) {
			f.timestamp = os_gettime_ns();
			obs_source_output_video2(source, &f);
			[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1. / 25.]];
		}
		obs_remove_raw_video_callback(raw, nullptr);
		obs_remove_main_rendered_callback(rendered, nullptr);

		static const char *names[REGIONS] = {"grey ramp", "colour gradients", "per-pixel colour changes",
						     "random noise", "codes 4-1019"};
		bool passed = canvas_frames.load() > 10 && card_frames.load() > 10;
		for (int pass = 0; pass < 2; pass++) {
			for (unsigned region = 0; region < REGIONS; region++) {
				Stat stat[3];
				for (unsigned y = region * H / REGIONS + 4; y < (region + 1) * H / REGIONS - 4; y++)
					for (unsigned x = 6; x < W - 6; x++) {
						const size_t i = (size_t)y * W + x;
						const int *p = &in[i * 3];
						int got[3], want[3];
						double conv[3];
						if (pass == 0) {
							// The encoder rounds the 16-bit words to ten-bit codes.
							got[0] = (out_y[i] + 32) >> 6;
							got[1] = (out_c[i * 2] + 32) >> 6;
							got[2] = (out_c[i * 2 + 1] + 32) >> 6;
							if (source_rgb) {
								rgb_to_ycc(p, conv);
								for (int c = 0; c < 3; c++)
									want[c] = code(conv[c]);
							} else {
								for (int c = 0; c < 3; c++)
									want[c] = p[c];
							}
						} else {
							const uint32_t w = out_rgb[i];
							got[0] = (w >> 22) & 1023;
							got[1] = (w >> 12) & 1023;
							got[2] = (w >> 2) & 1023;
							if (source_rgb) {
								for (int c = 0; c < 3; c++)
									conv[c] = p[c];
							} else {
								ycc_to_rgb(p, conv);
							}
							for (int c = 0; c < 3; c++) {
								double v = conv[c];
								if (full_output)
									v = (v - 64) * 1023.0 / 876.0;
								want[c] = (int)(v < 4 ? 4 : v > 1019 ? 1019 : floor(v + 0.5));
							}
						}
						for (int c = 0; c < 3; c++)
							stat[c].add(got[c] - want[c]);
					}
				printf("RESULT source=%s path=%s region=\"%s\" exact=%.4f/%.4f/%.4f worst=%d/%d/%d\n", argv[3],
				       pass == 0 ? "encoder-input-p416" : "decklink-r10l", names[region],
				       100.0 * stat[0].exact / stat[0].n, 100.0 * stat[1].exact / stat[1].n,
				       100.0 * stat[2].exact / stat[2].n, stat[0].worst, stat[1].worst, stat[2].worst);
				// Same-family paths (R'G'B' to R'G'B', Y'CbCr to Y'CbCr) must be exact.
				// A matrix conversion rounds once, so a tie may land one code either way;
				// the full-range rescale likewise.
				const bool converted = (pass == 0) == source_rgb;
				const int limit = converted || (pass == 1 && full_output) ? 1 : 0;
				for (int c = 0; c < 3; c++)
					if (stat[c].worst > limit)
						passed = false;
			}
		}
		obs_set_output_source(0, nullptr);
		obs_scene_release(scene);
		obs_source_release(source);
		obs_enter_graphics();
		gs_stagesurface_destroy(stage);
		gs_texrender_destroy(texrender);
		obs_leave_graphics();
		obs_shutdown();
		printf("%s source=%s output=%s\n", passed ? "PASS" : "FAIL", argv[3], full_output ? "full" : "limited");
		return passed ? 0 : 7;
	}
}
