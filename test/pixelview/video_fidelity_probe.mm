// Offline real-libobs fidelity probe for the 10-bit 4:2:2 chain. A v210 async
// source (what the DeckLink capture and the WHEP receiver deliver) fills a
// 1920x1080 scene 1:1 and is compared, sample by sample, with
//   - the canvas's P216 output rounded to ten bits (what the HEVC 4:2:2 encoder
//     is given), when the canvas is P216, and
//   - the DrawV210 render the DeckLink output sends to the card.
// No app, capture hardware, network or persisted configuration.
#import <Foundation/Foundation.h>
#include <obs.h>
#include <util/platform.h>
#include <media-io/video-io.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "decklink-v210-render.hpp"

static constexpr unsigned W = 1920, H = 1080, REGIONS = 5;
static std::vector<uint16_t> Yi(W * H), Cbi(W / 2 * H), Cri(W / 2 * H);
static std::vector<uint16_t> Yo(W * H), Co(W * H);
static std::vector<uint32_t> Vo;
static std::atomic<int> p216_frames{0}, v210_frames{0};
static gs_texrender_t *texrender;
static gs_stagesurf_t *stage;
static pixelview_v210::Mode mode;

static const char *name(void *)
{
	return "v210 fidelity pattern";
}
static void *create(obs_data_t *, obs_source_t *s)
{
	return s;
}
static void destroy(void *) {}

static void raw(void *, struct video_data *f)
{
	for (unsigned y = 0; y < H; y++) {
		memcpy(&Yo[y * W], f->data[0] + (size_t)y * f->linesize[0], W * 2);
		memcpy(&Co[y * W], f->data[1] + (size_t)y * f->linesize[1], W * 2);
	}
	p216_frames++;
}

static void rendered(void *)
{
	gs_texture_t *tex = obs_get_main_texture();
	if (!tex)
		return;
	const uint32_t words = pixelview_v210::row_words(W);
	if (!texrender) {
		texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		stage = gs_stagesurface_create(words, H, GS_RGBA);
		Vo.resize((size_t)words * H);
	}
	gs_texrender_reset(texrender);
	if (!gs_texrender_begin(texrender, words, H))
		return;
	const bool drawn = pixelview_v210::draw(tex, W, H, mode);
	gs_texrender_end(texrender);
	if (!drawn)
		return;
	gs_stage_texture(stage, gs_texrender_get_texture(texrender));
	uint8_t *data;
	uint32_t stride;
	if (gs_stagesurface_map(stage, &data, &stride)) {
		for (unsigned y = 0; y < H; y++)
			memcpy(&Vo[(size_t)y * words], data + (size_t)y * stride, words * 4);
		gs_stagesurface_unmap(stage);
		v210_frames++;
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

// argv: libobs data dir, graphics module, sdr|pq|hlg, canvas P216|P010|NV12, chroma saturation
int main(int argc, char **argv)
{
	@autoreleasepool {
		if (argc != 6)
			return 2;
		const bool pq = !strcmp(argv[3], "pq"), hlg = !strcmp(argv[3], "hlg"), hdr = pq || hlg;
		const double saturation = atof(argv[5]);
		if (!obs_startup("en-US", nullptr, nullptr))
			return 3;
		obs_add_data_path(argv[1]);
		obs_video_info vi = {};
		vi.graphics_module = argv[2];
		vi.fps_num = 25;
		vi.fps_den = 1;
		vi.base_width = vi.output_width = W;
		vi.base_height = vi.output_height = H;
		vi.output_format = !strcmp(argv[4], "P010")   ? VIDEO_FORMAT_P010
				   : !strcmp(argv[4], "NV12") ? VIDEO_FORMAT_NV12
							      : VIDEO_FORMAT_P216;
		const bool p216 = vi.output_format == VIDEO_FORMAT_P216;
		const bool eight_bit = vi.output_format == VIDEO_FORMAT_NV12;
		vi.colorspace = pq ? VIDEO_CS_2100_PQ : hlg ? VIDEO_CS_2100_HLG : VIDEO_CS_709;
		vi.range = VIDEO_RANGE_PARTIAL;
		vi.gpu_conversion = true;
		vi.scale_type = OBS_SCALE_DISABLE;
		if (obs_reset_video(&vi) != OBS_VIDEO_SUCCESS)
			return 4;
		if (hdr)
			obs_set_video_levels(300.f, 1000.f);
		obs_source_info si = {};
		si.id = "v210-fidelity";
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

		// Rows: 0 luma ramp on neutral chroma, 1 smooth chroma gradients with luma
		// steps, 2 colour bars (hard edges), 3 random legal noise, 4 every code from
		// 4 to 1019 including sub-black and super-white.
		srand(1);
		for (unsigned y = 0; y < H; y++) {
			const unsigned region = y * REGIONS / H;
			for (unsigned x = 0; x < W; x++) {
				unsigned Y, C0 = 512, C1 = 512;
				const unsigned k = x / 2;
				static const unsigned bars[8][3] = {{721, 512, 512}, {674, 176, 543}, {581, 589, 176},
								    {534, 253, 207}, {251, 771, 817}, {204, 435, 848},
								    {111, 848, 481}, {64, 512, 512}};
				switch (region) {
				case 0:
					Y = 64 + (x * 876 / (W - 1));
					break;
				case 1:
					Y = 300 + (x % 400);
					C0 = 200 + (k * 600 / (W / 2 - 1));
					C1 = 800 - (k * 500 / (W / 2 - 1));
					break;
				case 2:
					Y = bars[x * 8 / W][0];
					C0 = bars[k * 2 * 8 / W][1];
					C1 = bars[k * 2 * 8 / W][2];
					break;
				case 3:
					Y = 64 + rand() % 877;
					C0 = 64 + rand() % 897;
					C1 = 64 + rand() % 897;
					break;
				default:
					Y = 4 + (x * 1015 / (W - 1));
					C0 = 4 + ((k * 7) % 1016);
					C1 = 1019 - ((k * 13) % 1016);
					break;
				}
				C0 = (unsigned)(512 + ((int)C0 - 512) * saturation + 0.5);
				C1 = (unsigned)(512 + ((int)C1 - 512) * saturation + 0.5);
				Yi[y * W + x] = Y;
				if (!(x & 1)) {
					Cbi[y * (W / 2) + k] = C0;
					Cri[y * (W / 2) + k] = C1;
				}
			}
		}
		const unsigned stride = ((W + 47) / 48) * 128;
		std::vector<uint8_t> v210(stride * H);
		for (unsigned y = 0; y < H; y++)
			for (unsigned g = 0; g < W / 6; g++) {
				const uint16_t *Y = &Yi[y * W + g * 6], *cb = &Cbi[y * (W / 2) + g * 3],
					       *cr = &Cri[y * (W / 2) + g * 3];
				const uint32_t w[4] = {(uint32_t)cb[0] | Y[0] << 10 | cr[0] << 20,
						       (uint32_t)Y[1] | cb[1] << 10 | Y[2] << 20,
						       (uint32_t)cr[1] | Y[3] << 10 | cb[2] << 20,
						       (uint32_t)Y[4] | cr[2] << 10 | Y[5] << 20};
				memcpy(&v210[(size_t)y * stride + g * 16], w, 16);
			}
		obs_source_frame2 f = {};
		f.width = W;
		f.height = H;
		f.format = VIDEO_FORMAT_V210;
		f.range = VIDEO_RANGE_PARTIAL;
		f.trc = pq ? VIDEO_TRC_PQ : hlg ? VIDEO_TRC_HLG : VIDEO_TRC_DEFAULT;
		f.data[0] = v210.data();
		f.linesize[0] = stride;
		if (!video_format_get_parameters_for_format(vi.colorspace, f.range, f.format, f.color_matrix,
							    f.color_range_min, f.color_range_max))
			return 6;
		// As the capture and receive plugins do for SDR: no 64-940 clamp on the way in.
		if (!hdr)
			for (int i = 0; i < 3; i++) {
				f.color_range_min[i] = 0.f;
				f.color_range_max[i] = 1.f;
			}
		mode = pixelview_v210::mode_for(vi.colorspace, true);
		if (p216)
			obs_add_raw_video_callback(nullptr, raw, nullptr);
		obs_add_main_rendered_callback(rendered, nullptr);
		for (unsigned i = 0; i < 50; i++) {
			f.timestamp = os_gettime_ns();
			obs_source_output_video2(source, &f);
			[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1. / 25.]];
		}
		if (p216)
			obs_remove_raw_video_callback(raw, nullptr);
		obs_remove_main_rendered_callback(rendered, nullptr);

		static const char *names[REGIONS] = {"luma ramp, neutral chroma", "chroma gradients, luma steps",
						     "colour bars", "random legal noise", "codes 4-1019"};
		const uint32_t words = pixelview_v210::row_words(W);
		bool passed = v210_frames.load() > 10 && (!p216 || p216_frames.load() > 10);
		for (int pass = p216 ? 0 : 1; pass < 2; pass++) {
			for (unsigned region = 0; region < REGIONS; region++) {
				Stat luma, chroma;
				for (unsigned y = region * H / REGIONS + 2; y < (region + 1) * H / REGIONS - 2; y++)
					for (unsigned x = 6; x < W - 6; x++) {
						int yo, cbo, cro;
						const unsigned k = x / 2;
						if (pass == 0) {
							// The encoder rounds the 16-bit words to ten-bit codes.
							yo = (Yo[y * W + x] + 32) >> 6;
							cbo = (Co[y * W + (x & ~1u)] + 32) >> 6;
							cro = (Co[y * W + (x & ~1u) + 1] + 32) >> 6;
						} else {
							const uint32_t *w = &Vo[(size_t)y * words + (x / 6) * 4];
							static const unsigned word[6] = {0, 1, 1, 2, 3, 3},
									      shift[6] = {10, 0, 20, 10, 0, 20};
							const unsigned j = x % 6, pair = j / 2;
							yo = (w[word[j]] >> shift[j]) & 1023;
							cbo = pair == 0   ? (w[0] & 1023)
							      : pair == 1 ? ((w[1] >> 10) & 1023)
									  : ((w[2] >> 20) & 1023);
							cro = pair == 0   ? ((w[0] >> 20) & 1023)
							      : pair == 1 ? (w[2] & 1023)
									  : ((w[3] >> 10) & 1023);
						}
						luma.add(yo - (int)Yi[y * W + x]);
						if (!(x & 1)) {
							chroma.add(cbo - (int)Cbi[y * (W / 2) + k]);
							chroma.add(cro - (int)Cri[y * (W / 2) + k]);
						}
					}
				printf("RESULT colour=%s canvas=%s path=%s region=\"%s\" luma_exact=%.4f luma_worst=%d chroma_exact=%.4f chroma_worst=%d\n",
				       argv[3], argv[4], pass == 0 ? "encoder-input" : "decklink-v210", names[region],
				       100.0 * luma.exact / luma.n, luma.worst, 100.0 * chroma.exact / chroma.n, chroma.worst);
				// SDR on a ten-bit canvas is bit-exact for every pattern. An eight-bit
				// canvas cannot be. HDR is exact for ordinary colours (regions 0-2 at
				// the reduced saturation the runner passes); the linear float canvas
				// loses precision in near-zero channels of extreme ones.
				const bool must_be_exact = !eight_bit && (!hdr || region <= 2);
				const int tolerance = hlg && region == 0 ? 2 : 0; // HLG OOTF rounding near black
				if (must_be_exact && (luma.worst > tolerance || chroma.worst > 0))
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
		printf("%s colour=%s canvas=%s\n", passed ? "PASS" : "FAIL", argv[3], argv[4]);
		return passed ? 0 : 7;
	}
}
