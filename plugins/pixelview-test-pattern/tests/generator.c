// SPDX-License-Identifier: GPL-2.0-or-later
// Offline checks for the test pattern generator: code values, layout, v210
// packing, overlay bounds and audio.
#include "../test-pattern.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...)                                     \
	do {                                                 \
		if (!(cond)) {                               \
			failures++;                          \
			fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
			fprintf(stderr, __VA_ARGS__);        \
			fputc('\n', stderr);                 \
		}                                            \
	} while (0)

static void expect(const char *what, struct tp_ycc c, unsigned y, unsigned cb, unsigned cr)
{
	CHECK(c.y == y && c.cb == cb && c.cr == cr, "%s: got %u/%u/%u want %u/%u/%u", what, c.y, c.cb, c.cr, y, cb, cr);
}

static struct tp_ycc pixel(const struct tp_image *img, uint32_t x, uint32_t y)
{
	const size_t i = (size_t)y * img->width + x;
	return (struct tp_ycc){img->y[i], img->cb[i], img->cr[i]};
}

// SMPTE RP 219 10-bit code values.
static void code_values(void)
{
	expect("75% white", tp_rgb(.75, .75, .75), 721, 512, 512);
	expect("75% yellow", tp_rgb(.75, .75, 0), 674, 176, 543);
	expect("75% cyan", tp_rgb(0, .75, .75), 581, 589, 176);
	expect("75% green", tp_rgb(0, .75, 0), 534, 253, 207);
	expect("75% magenta", tp_rgb(.75, 0, .75), 251, 771, 817);
	expect("75% red", tp_rgb(.75, 0, 0), 204, 435, 848);
	expect("75% blue", tp_rgb(0, 0, .75), 111, 848, 481);
	expect("100% cyan", tp_rgb(0, 1, 1), 754, 615, 64);
	expect("100% blue", tp_rgb(0, 0, 1), 127, 960, 471);
	expect("100% yellow", tp_rgb(1, 1, 0), 877, 64, 553);
	expect("100% red", tp_rgb(1, 0, 0), 250, 409, 960);
	expect("40% gray", tp_level(40), 414, 512, 512);
	expect("15% gray", tp_level(15), 195, 512, 512);
	expect("black", tp_level(0), 64, 512, 512);
	expect("white", tp_level(100), 940, 512, 512);
	expect("PLUGE -2%", tp_level(-2), 46, 512, 512);
	expect("PLUGE +2%", tp_level(2), 82, 512, 512);
	expect("PLUGE +4%", tp_level(4), 99, 512, 512);
	expect("clamp low", tp_level(-50), 4, 512, 512);
	expect("clamp high", tp_level(150), 1019, 512, 512);
}

static void smpte_layout(void)
{
	struct tp_image img;
	assert(tp_image_alloc(&img, 1920, 1080));
	tp_render(&img, TP_SMPTE_BARS);
	const double d = 240, c = 1440 / 7.0;
	const struct tp_ycc bars[7] = {tp_rgb(.75, .75, .75), tp_rgb(.75, .75, 0), tp_rgb(0, .75, .75), tp_rgb(0, .75, 0),
				       tp_rgb(.75, 0, .75),   tp_rgb(.75, 0, 0),   tp_rgb(0, 0, .75)};
	expect("left 40%", pixel(&img, 120, 300), 414, 512, 512);
	expect("right 40%", pixel(&img, 1800, 300), 414, 512, 512);
	for (int i = 0; i < 7; i++) {
		const struct tp_ycc got = pixel(&img, (uint32_t)(d + (i + 0.5) * c), 300);
		char name[32];
		snprintf(name, sizeof name, "bar %d", i);
		expect(name, got, bars[i].y, bars[i].cb, bars[i].cr);
	}
	// Rows: 630 / 90 / 90 / 270 lines.
	expect("row 2 cyan", pixel(&img, 120, 675), 754, 615, 64);
	expect("row 2 white", pixel(&img, 340, 675), 940, 512, 512);
	expect("row 2 75%", pixel(&img, 1000, 675), 721, 512, 512);
	expect("row 2 blue", pixel(&img, 1800, 675), 127, 960, 471);
	expect("row 3 yellow", pixel(&img, 120, 765), 877, 64, 553);
	expect("row 3 black", pixel(&img, 340, 765), 64, 512, 512);
	expect("row 3 red", pixel(&img, 1800, 765), 250, 409, 960);
	expect("row 3 ramp start", pixel(&img, (uint32_t)(d + c + 0.5), 765), 64, 512, 512);
	expect("row 3 ramp end", pixel(&img, (uint32_t)(d + 6 * c + 0.5) - 1, 765), 940, 512, 512);
	uint16_t last = 0;
	for (uint32_t x = (uint32_t)(d + c + 0.5); x < (uint32_t)(d + 6 * c + 0.5); x++) {
		CHECK(img.y[765 * 1920 + x] >= last, "ramp not monotonic at %u", x);
		last = img.y[765 * 1920 + x];
	}
	expect("row 4 15%", pixel(&img, 120, 950), 195, 512, 512);
	expect("row 4 white", pixel(&img, (uint32_t)(d + 2.5 * c), 950), 940, 512, 512);
	const double pluge = d + (1.5 + 2 + 5.0 / 6) * c, third = c / 3;
	const unsigned levels[5] = {46, 64, 82, 64, 99};
	for (int i = 0; i < 5; i++)
		expect("PLUGE", pixel(&img, (uint32_t)(pluge + (i + 0.5) * third), 950), levels[i], 512, 512);
	expect("row 4 right 15%", pixel(&img, 1800, 950), 195, 512, 512);
	tp_image_free(&img);
}

static void all_patterns(void)
{
	const uint32_t sizes[][2] = {{1920, 1080}, {1280, 720}, {3840, 2160}, {722, 480}};
	for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
		struct tp_image img;
		assert(tp_image_alloc(&img, sizes[s][0], sizes[s][1]));
		for (int p = 0; p < TP_PATTERN_COUNT; p++) {
			memset(img.y, 0, (size_t)img.width * img.height * 2);
			memset(img.cb, 0, (size_t)img.width * img.height * 2);
			memset(img.cr, 0, (size_t)img.width * img.height * 2);
			tp_render(&img, (enum tp_pattern)p);
			size_t bad = 0;
			for (size_t i = 0; i < (size_t)img.width * img.height; i++)
				bad += img.y[i] < 4 || img.y[i] > 1019 || img.cb[i] < 4 || img.cb[i] > 1019 || img.cr[i] < 4 ||
				       img.cr[i] > 1019;
			CHECK(!bad, "%s at %ux%u left %zu samples unset or illegal", tp_pattern_name((enum tp_pattern)p),
			      img.width, img.height, bad);
			CHECK(*tp_pattern_name((enum tp_pattern)p), "pattern %d has no name", p);
		}
		tp_image_free(&img);
	}
	struct tp_image img;
	assert(tp_image_alloc(&img, 1920, 1080));
	tp_render(&img, TP_EBU_BARS);
	expect("EBU white", pixel(&img, 100, 500), 940, 512, 512);
	expect("EBU yellow", pixel(&img, 340, 500), 674, 176, 543);
	expect("EBU black", pixel(&img, 1850, 500), 64, 512, 512);
	tp_render(&img, TP_GRAY_RAMP);
	expect("ramp left", pixel(&img, 0, 10), 64, 512, 512);
	expect("ramp right", pixel(&img, 1919, 10), 940, 512, 512);
	// 877 codes over 1920 pixels: every code appears, so 8-bit paths show steps.
	int seen[1024] = {0};
	for (uint32_t x = 0; x < 1920; x++)
		seen[img.y[x]] = 1;
	int distinct = 0;
	for (int i = 0; i < 1024; i++)
		distinct += seen[i];
	CHECK(distinct == 877, "gray ramp has %d codes, want 877", distinct);
	tp_render(&img, TP_GRAY_STEPS);
	expect("step 0", pixel(&img, 50, 10), 64, 512, 512);
	expect("step 5", pixel(&img, 960, 10), 502, 512, 512);
	expect("step 10", pixel(&img, 1900, 10), 940, 512, 512);
	tp_render(&img, TP_RGB_RAMPS);
	expect("red ramp end", pixel(&img, 1919, 400), 250, 409, 960);
	expect("blue ramp end", pixel(&img, 1919, 1000), 127, 960, 471);
	tp_render(&img, TP_CROSSHATCH);
	expect("grid line", pixel(&img, 120, 500), 940, 512, 512);
	expect("grid cell", pixel(&img, 60, 60), 64, 512, 512);
	expect("centre", pixel(&img, 960, 540), 940, 512, 512);
	tp_render(&img, TP_ZONE_PLATE);
	expect("zone centre", pixel(&img, 960, 540), 940, 512, 512);
	tp_image_free(&img);
}

static unsigned word(const uint8_t *p)
{
	return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static void v210(void)
{
	CHECK(tp_v210_linesize(1920) == 5120, "1920 linesize %zu", tp_v210_linesize(1920));
	CHECK(tp_v210_linesize(1280) == 3456, "1280 linesize %zu", tp_v210_linesize(1280));
	CHECK(tp_v210_linesize(722) == 2048, "722 linesize %zu", tp_v210_linesize(722));
	const uint32_t widths[] = {1920, 1280, 722};
	for (size_t w = 0; w < 3; w++) {
		struct tp_image img;
		assert(tp_image_alloc(&img, widths[w], 4));
		srand(7);
		for (size_t i = 0; i < (size_t)img.width * img.height; i++) {
			img.y[i] = (uint16_t)(4 + rand() % 1016);
			img.cb[i] = (uint16_t)(4 + rand() % 1016);
			img.cr[i] = (uint16_t)(4 + rand() % 1016);
		}
		const size_t linesize = tp_v210_linesize(img.width);
		uint8_t *packed = malloc(linesize * img.height);
		memset(packed, 0xAA, linesize * img.height);
		tp_pack_v210(packed, &img, 0, img.height);
		size_t bad = 0;
		for (uint32_t row = 0; row < img.height; row++) {
			const uint8_t *line = packed + row * linesize;
			for (uint32_t x = 0; x < img.width; x++) {
				const uint8_t *g = line + (x / 6) * 16;
				unsigned s[12];
				for (int k = 0; k < 4; k++) {
					const unsigned v = word(g + 4 * k);
					s[3 * k] = v & 1023;
					s[3 * k + 1] = (v >> 10) & 1023;
					s[3 * k + 2] = (v >> 20) & 1023;
				}
				// Cb0 Y0 Cr0 Y1 Cb1 Y2 Cr1 Y3 Cb2 Y4 Cr2 Y5
				static const int ypos[6] = {1, 3, 5, 7, 9, 11};
				const unsigned i = x % 6, pair = i / 2;
				const size_t even = (size_t)row * img.width + (x & ~1u);
				bad += s[ypos[i]] != img.y[(size_t)row * img.width + x];
				bad += s[pair * 4] != img.cb[even];
				bad += s[pair * 4 + 2] != img.cr[even];
			}
		}
		CHECK(!bad, "v210 round trip at width %u: %zu mismatches", img.width, bad);
		free(packed);
		tp_image_free(&img);
	}
}

static void overlay(void)
{
	const uint32_t sizes[][2] = {{1920, 1080}, {1280, 720}, {3840, 2160}};
	for (size_t s = 0; s < 3; s++) {
		const uint32_t w = sizes[s][0], h = sizes[s][1];
		const struct tp_rect r = tp_overlay_rect(w, h);
		CHECK(r.x % 2 == 0 && r.width % 2 == 0, "overlay not chroma aligned at %ux%u", w, h);
		CHECK(r.x + r.width <= w && r.y + r.height <= h && r.width && r.height, "overlay outside image");
		// Inside the SMPTE 75% bars, clear of the lower rows and PLUGE.
		CHECK(r.y + r.height < h * 7 / 12, "overlay covers lower SMPTE rows at %ux%u", w, h);
		struct tp_image img, ref;
		assert(tp_image_alloc(&img, w, h) && tp_image_alloc(&ref, w, h));
		tp_render(&img, TP_SMPTE_BARS);
		tp_render(&ref, TP_SMPTE_BARS);
		const struct tp_overlay a = {12, 34, 56, 7, 25, false}, b = {12, 34, 56, 8, 25, true};
		tp_draw_overlay(&img, &a);
		size_t outside = 0, inside = 0;
		for (uint32_t y = 0; y < h; y++)
			for (uint32_t x = 0; x < w; x++) {
				const size_t i = (size_t)y * w + x;
				const bool in = x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
				const bool diff = img.y[i] != ref.y[i] || img.cb[i] != ref.cb[i] || img.cr[i] != ref.cr[i];
				outside += !in && diff;
				inside += in && img.y[i] == 940;
			}
		CHECK(!outside, "overlay touched %zu pixels outside its rectangle", outside);
		CHECK(inside > 1000, "overlay drew only %zu white pixels", inside);
		uint16_t *before = malloc((size_t)w * h * 2);
		memcpy(before, img.y, (size_t)w * h * 2);
		tp_draw_overlay(&img, &b);
		size_t changed = 0, white_more = 0;
		for (size_t i = 0; i < (size_t)w * h; i++) {
			changed += before[i] != img.y[i];
			white_more += img.y[i] == 940 && before[i] != 940;
		}
		CHECK(changed > 0 && white_more > (size_t)(60 * h / 1080) * (60 * h / 1080),
		      "frame digit and flash did not change the overlay (%zu changed)", changed);
		free(before);
		tp_image_free(&img);
		tp_image_free(&ref);
	}
}

static void audio(void)
{
	float a[480], b[96];
	tp_audio_fill(a, 480, 0, 48000, TP_AUDIO_TONE_20, false);
	float peak = 0;
	for (int i = 0; i < 480; i++)
		peak = fmaxf(peak, fabsf(a[i]));
	CHECK(fabsf(peak - 0.1f) < 1e-4f, "-20 dBFS peak %f", peak);
	CHECK(fabsf(a[0]) < 1e-6f && fabsf(a[48] - a[0]) < 1e-6f && fabsf(a[12] - 0.1f) < 1e-6f, "1 kHz period");
	// Continuity: a chunk at position 480 continues the same waveform.
	tp_audio_fill(b, 96, 480, 48000, TP_AUDIO_TONE_20, false);
	CHECK(fabsf(b[12] - a[12]) < 1e-6f, "tone phase not continuous");
	tp_audio_fill(a, 480, 0, 48000, TP_AUDIO_TONE_18, false);
	peak = 0;
	for (int i = 0; i < 480; i++)
		peak = fmaxf(peak, fabsf(a[i]));
	CHECK(fabsf(peak - 0.12589f) < 1e-4f, "-18 dBFS peak %f", peak);
	tp_audio_fill(a, 480, 1000, 48000, TP_AUDIO_SYNC_BEEP, false);
	for (int i = 0; i < 480; i++)
		CHECK(a[i] == 0, "beep silent span has signal");
	tp_audio_fill(a, 480, 1000, 48000, TP_AUDIO_SYNC_BEEP, true);
	peak = 0;
	for (int i = 0; i < 480; i++)
		peak = fmaxf(peak, fabsf(a[i]));
	CHECK(fabsf(peak - 0.1f) < 1e-4f, "beep peak %f", peak);
	tp_audio_fill(a, 480, 0, 48000, TP_AUDIO_OFF, true);
	for (int i = 0; i < 480; i++)
		CHECK(a[i] == 0, "audio off has signal");
	for (int i = 0; i < TP_AUDIO_COUNT; i++)
		CHECK(*tp_audio_name((enum tp_audio)i), "audio %d has no name", i);
}

int main(void)
{
	code_values();
	smpte_layout();
	all_patterns();
	v210();
	overlay();
	audio();
	if (failures) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	puts("test pattern generator: ok");
	return 0;
}
