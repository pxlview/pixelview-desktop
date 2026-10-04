// SPDX-License-Identifier: GPL-2.0-or-later
#include "test-pattern.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *pattern_names[TP_PATTERN_COUNT] = {
	"SMPTE color bars",  "EBU color bars (75%)", "Color bars (100%)", "Gray ramp (10-bit)", "Gray steps (11)",
	"RGB ramps",         "Crosshatch",           "Zone plate",        "Black",
};

static const char *audio_names[TP_AUDIO_COUNT] = {
	"Sync beep (1 kHz, once a second)",
	"1 kHz tone, -20 dBFS",
	"1 kHz tone, -18 dBFS",
	"No audio",
};

const char *tp_pattern_name(enum tp_pattern pattern)
{
	return (unsigned)pattern < TP_PATTERN_COUNT ? pattern_names[pattern] : "";
}

const char *tp_audio_name(enum tp_audio audio)
{
	return (unsigned)audio < TP_AUDIO_COUNT ? audio_names[audio] : "";
}

// v210 reserves 0-3 and 1020-1023 for timing references.
static uint16_t code(double value)
{
	const double rounded = floor(value + 0.5);
	return (uint16_t)(rounded < 4 ? 4 : rounded > 1019 ? 1019 : rounded);
}

struct tp_ycc tp_rgb(double r, double g, double b)
{
	const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
	return (struct tp_ycc){code(64 + 876 * y), code(512 + 896 * (b - y) / 1.8556),
			       code(512 + 896 * (r - y) / 1.5748)};
}

struct tp_ycc tp_level(double percent)
{
	return (struct tp_ycc){code(64 + 876 * percent / 100.0), 512, 512};
}

bool tp_image_alloc(struct tp_image *img, uint32_t width, uint32_t height)
{
	memset(img, 0, sizeof(*img));
	if (!width || !height)
		return false;
	const size_t count = (size_t)width * height;
	img->y = malloc(count * sizeof(uint16_t));
	img->cb = malloc(count * sizeof(uint16_t));
	img->cr = malloc(count * sizeof(uint16_t));
	if (!img->y || !img->cb || !img->cr) {
		tp_image_free(img);
		return false;
	}
	img->width = width;
	img->height = height;
	return true;
}

void tp_image_free(struct tp_image *img)
{
	free(img->y);
	free(img->cb);
	free(img->cr);
	memset(img, 0, sizeof(*img));
}

static inline void put(struct tp_image *img, uint32_t x, uint32_t y, struct tp_ycc c)
{
	const size_t i = (size_t)y * img->width + x;
	img->y[i] = c.y;
	img->cb[i] = c.cb;
	img->cr[i] = c.cr;
}

static void fill(struct tp_image *img, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, struct tp_ycc c)
{
	if (x1 > img->width)
		x1 = img->width;
	if (y1 > img->height)
		y1 = img->height;
	for (uint32_t y = y0; y < y1; y++)
		for (uint32_t x = x0; x < x1; x++)
			put(img, x, y, c);
}

static uint32_t at(double position)
{
	return (uint32_t)floor(position + 0.5);
}

// The eight bar colours in the usual order: white, yellow, cyan, green,
// magenta, red, blue, black.
static struct tp_ycc bar(unsigned index, double amplitude)
{
	const unsigned rgb[8] = {7, 6, 3, 2, 5, 4, 1, 0};
	const unsigned bits = rgb[index & 7];
	return tp_rgb(bits & 4 ? amplitude : 0, bits & 2 ? amplitude : 0, bits & 1 ? amplitude : 0);
}

// SMPTE RP 219: side panels d = W/8, seven bars c = 3W/28, rows 7/12, 1/12, 1/12, 3/12.
static void smpte_bars(struct tp_image *img)
{
	const uint32_t w = img->width, h = img->height;
	const double d = w / 8.0, c = 3.0 * w / 28.0;
	const uint32_t r1 = at(h * 7.0 / 12), r2 = at(h * 8.0 / 12), r3 = at(h * 9.0 / 12);
	const struct tp_ycc gray40 = tp_level(40), gray15 = tp_level(15), black = tp_level(0), white = tp_level(100);

	fill(img, 0, 0, at(d), r1, gray40);
	for (unsigned i = 0; i < 7; i++)
		fill(img, at(d + i * c), 0, at(d + (i + 1) * c), r1, bar(i, 0.75));
	fill(img, at(d + 7 * c), 0, w, r1, gray40);

	fill(img, 0, r1, at(d), r2, tp_rgb(0, 1, 1));
	fill(img, at(d), r1, at(d + c), r2, white);
	fill(img, at(d + c), r1, at(d + 7 * c), r2, tp_level(75));
	fill(img, at(d + 7 * c), r1, w, r2, tp_rgb(0, 0, 1));

	fill(img, 0, r2, at(d), r3, tp_rgb(1, 1, 0));
	fill(img, at(d), r2, at(d + c), r3, black);
	const uint32_t ramp0 = at(d + c), ramp1 = at(d + 6 * c);
	for (uint32_t x = ramp0; x < ramp1; x++)
		fill(img, x, r2, x + 1, r3, tp_level(100.0 * (x - ramp0) / (ramp1 - ramp0 - 1)));
	fill(img, ramp1, r2, at(d + 7 * c), r3, white);
	fill(img, at(d + 7 * c), r2, w, r3, tp_rgb(1, 0, 0));

	// Bottom row: black, white, black, PLUGE -2/0/+2/0/+4 %, black.
	struct {
		double width;
		struct tp_ycc colour;
	} const row[] = {{1.5, black},           {2, white},        {5.0 / 6, black},
			 {1.0 / 3, tp_level(-2)}, {1.0 / 3, black},  {1.0 / 3, tp_level(2)},
			 {1.0 / 3, black},        {1.0 / 3, tp_level(4)}, {1, black}};
	fill(img, 0, r3, at(d), h, gray15);
	double x = d;
	for (size_t i = 0; i < sizeof(row) / sizeof(row[0]); i++) {
		fill(img, at(x), r3, at(x + row[i].width * c), h, row[i].colour);
		x += row[i].width * c;
	}
	fill(img, at(d + 7 * c), r3, w, h, gray15);
}

static void full_bars(struct tp_image *img, double amplitude, bool white100)
{
	for (unsigned i = 0; i < 8; i++)
		fill(img, at(img->width * i / 8.0), 0, at(img->width * (i + 1) / 8.0), img->height,
		     i == 0 && white100 ? tp_level(100) : bar(i, amplitude));
}

static void gray_ramp(struct tp_image *img)
{
	for (uint32_t x = 0; x < img->width; x++)
		fill(img, x, 0, x + 1, img->height, tp_level(100.0 * x / (img->width > 1 ? img->width - 1 : 1)));
}

static void gray_steps(struct tp_image *img)
{
	for (unsigned i = 0; i < 11; i++)
		fill(img, at(img->width * i / 11.0), 0, at(img->width * (i + 1) / 11.0), img->height, tp_level(10.0 * i));
}

static void rgb_ramps(struct tp_image *img)
{
	for (unsigned band = 0; band < 4; band++) {
		const uint32_t y0 = at(img->height * band / 4.0), y1 = at(img->height * (band + 1) / 4.0);
		for (uint32_t x = 0; x < img->width; x++) {
			const double v = (double)x / (img->width > 1 ? img->width - 1 : 1);
			const struct tp_ycc c = band == 0   ? tp_rgb(v, v, v)
						: band == 1 ? tp_rgb(v, 0, 0)
						: band == 2 ? tp_rgb(0, v, 0)
							    : tp_rgb(0, 0, v);
			fill(img, x, y0, x + 1, y1, c);
		}
	}
}

static bool near_line(double position, double spacing, double extent, double half)
{
	if (position < 2 * half || position > extent - 2 * half)
		return true; // border lines stay full width
	const double k = floor(position / spacing + 0.5);
	return fabs(position - k * spacing) < half;
}

static void crosshatch(struct tp_image *img)
{
	const uint32_t w = img->width, h = img->height;
	const double scale = h / 1080.0, half = 1.0 * (scale < 1 ? 1 : scale);
	const double cx = w / 2.0, cy = h / 2.0, radius = h * 0.45, arm = 40 * scale;
	const struct tp_ycc black = tp_level(0), white = tp_level(100);
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			const double px = x + 0.5, py = y + 0.5;
			bool on = near_line(px, w / 16.0, w, half) || near_line(py, h / 9.0, h, half);
			on |= fabs(hypot(px - cx, py - cy) - radius) < half;
			on |= (fabs(px - cx) < half && fabs(py - cy) < arm) || (fabs(py - cy) < half && fabs(px - cx) < arm);
			put(img, x, y, on ? white : black);
		}
	}
}

static void zone_plate(struct tp_image *img)
{
	const double cx = img->width / 2.0, cy = img->height / 2.0, edge = img->width / 2.0;
	for (uint32_t y = 0; y < img->height; y++) {
		for (uint32_t x = 0; x < img->width; x++) {
			const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
			// Local frequency r / (2 * edge) cycles per pixel: Nyquist at the side edges.
			const double phase = M_PI * (dx * dx + dy * dy) / (2 * edge);
			put(img, x, y, tp_level(50 + 50 * cos(phase)));
		}
	}
}

void tp_render(struct tp_image *img, enum tp_pattern pattern)
{
	switch (pattern) {
	case TP_SMPTE_BARS: smpte_bars(img); break;
	case TP_EBU_BARS: full_bars(img, 0.75, true); break;
	case TP_FULL_BARS: full_bars(img, 1.0, false); break;
	case TP_GRAY_RAMP: gray_ramp(img); break;
	case TP_GRAY_STEPS: gray_steps(img); break;
	case TP_RGB_RAMPS: rgb_ramps(img); break;
	case TP_CROSSHATCH: crosshatch(img); break;
	case TP_ZONE_PLATE: zone_plate(img); break;
	default: fill(img, 0, 0, img->width, img->height, tp_level(0)); break;
	}
}

// Overlay layout in 1080-line units, scaled with the image height.
struct layout {
	double unit;
	uint32_t pad, digit_w, digit_h, stroke, gap, colon_w, flash, bar_h;
};

static struct layout overlay_layout(uint32_t height)
{
	const double u = height / 1080.0;
	struct layout l = {u, at(16 * u), at(40 * u), at(72 * u), at(9 * u), at(12 * u), at(10 * u), at(72 * u), at(8 * u)};
	if (!l.stroke)
		l.stroke = 1;
	return l;
}

static uint32_t digits_width(const struct layout *l)
{
	// Four pairs of digits separated by three colons.
	return 8 * l->digit_w + 4 * l->gap + 3 * (l->colon_w + 2 * l->gap);
}

struct tp_rect tp_overlay_rect(uint32_t width, uint32_t height)
{
	const struct layout l = overlay_layout(height);
	uint32_t w = 2 * l.pad + digits_width(&l) + 2 * l.gap + l.flash;
	uint32_t h = 2 * l.pad + l.digit_h + l.gap + l.bar_h;
	w = (w + 1) & ~1u;
	if (w > width)
		w = width & ~1u;
	if (h > height)
		h = height;
	uint32_t x = ((width - w) / 2) & ~1u;
	uint32_t y = at(height * 0.40 - h / 2.0);
	if (y + h > height)
		y = height - h;
	return (struct tp_rect){x, y, w, h};
}

static void box(struct tp_image *img, const struct tp_rect *clip, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
		struct tp_ycc c)
{
	const uint32_t x1 = x0 + w, y1 = y0 + h;
	fill(img, x0 < clip->x ? clip->x : x0, y0 < clip->y ? clip->y : y0,
	     x1 > clip->x + clip->width ? clip->x + clip->width : x1,
	     y1 > clip->y + clip->height ? clip->y + clip->height : y1, c);
}

static void digit(struct tp_image *img, const struct tp_rect *clip, const struct layout *l, uint32_t x, uint32_t y,
		  unsigned value, struct tp_ycc c)
{
	// Segments a-g as bits 0-6.
	static const uint8_t masks[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
	const uint8_t m = masks[value % 10];
	const uint32_t w = l->digit_w, h = l->digit_h, t = l->stroke, mid = (h - t) / 2;
	if (m & 0x01) box(img, clip, x, y, w, t, c);
	if (m & 0x02) box(img, clip, x + w - t, y, t, h / 2 + t / 2, c);
	if (m & 0x04) box(img, clip, x + w - t, y + mid, t, h - mid, c);
	if (m & 0x08) box(img, clip, x, y + h - t, w, t, c);
	if (m & 0x10) box(img, clip, x, y + mid, t, h - mid, c);
	if (m & 0x20) box(img, clip, x, y, t, h / 2 + t / 2, c);
	if (m & 0x40) box(img, clip, x, y + mid, w, t, c);
}

void tp_draw_overlay(struct tp_image *dst, const struct tp_overlay *ov)
{
	const struct tp_rect r = tp_overlay_rect(dst->width, dst->height);
	const struct layout l = overlay_layout(dst->height);
	const struct tp_ycc background = tp_level(0), white = tp_level(100), dim = tp_level(15), fill75 = tp_level(75);
	box(dst, &r, r.x, r.y, r.width, r.height, background);

	const unsigned values[4] = {ov->hours, ov->minutes, ov->seconds, ov->frames};
	uint32_t x = r.x + l.pad;
	const uint32_t y = r.y + l.pad;
	for (unsigned pair = 0; pair < 4; pair++) {
		if (pair) {
			x += l.gap;
			box(dst, &r, x, y + l.digit_h / 3 - l.colon_w / 2, l.colon_w, l.colon_w, white);
			box(dst, &r, x, y + 2 * l.digit_h / 3 - l.colon_w / 2, l.colon_w, l.colon_w, white);
			x += l.colon_w + l.gap;
		}
		digit(dst, &r, &l, x, y, (values[pair] / 10) % 10, white);
		x += l.digit_w + l.gap;
		digit(dst, &r, &l, x, y, values[pair] % 10, white);
		x += l.digit_w;
	}
	box(dst, &r, x + 2 * l.gap, y, l.flash, l.digit_h, ov->flash ? white : dim);

	const uint32_t bar_y = y + l.digit_h + l.gap, bar_w = r.width - 2 * l.pad;
	const unsigned fps = ov->frames_per_second ? ov->frames_per_second : 1;
	const unsigned done = ov->frames + 1 > fps ? fps : ov->frames + 1;
	box(dst, &r, r.x + l.pad, bar_y, bar_w, l.bar_h, dim);
	box(dst, &r, r.x + l.pad, bar_y, (uint32_t)((uint64_t)bar_w * done / fps), l.bar_h, fill75);
}

size_t tp_v210_linesize(uint32_t width)
{
	return (size_t)((width + 47) / 48) * 128;
}

void tp_pack_v210(uint8_t *dst, const struct tp_image *img, uint32_t first, uint32_t count)
{
	const size_t linesize = tp_v210_linesize(img->width);
	const uint32_t groups = (img->width + 5) / 6;
	for (uint32_t row = first; row < first + count && row < img->height; row++) {
		const size_t base = (size_t)row * img->width;
		uint8_t *out = dst + row * linesize;
		memset(out, 0, linesize);
		for (uint32_t g = 0; g < groups; g++) {
			uint32_t Y[6], Cb[3], Cr[3];
			for (unsigned i = 0; i < 6; i++) {
				const uint32_t x = g * 6 + i;
				Y[i] = x < img->width ? img->y[base + x] : 64;
				if (!(i & 1)) {
					Cb[i / 2] = x < img->width ? img->cb[base + x] : 512;
					Cr[i / 2] = x < img->width ? img->cr[base + x] : 512;
				}
			}
			const uint32_t words[4] = {Cb[0] | Y[0] << 10 | Cr[0] << 20, Y[1] | Cb[1] << 10 | Y[2] << 20,
						   Cr[1] | Y[3] << 10 | Cb[2] << 20, Y[4] | Cr[2] << 10 | Y[5] << 20};
			for (unsigned i = 0; i < 4; i++) {
				out[g * 16 + i * 4 + 0] = (uint8_t)words[i];
				out[g * 16 + i * 4 + 1] = (uint8_t)(words[i] >> 8);
				out[g * 16 + i * 4 + 2] = (uint8_t)(words[i] >> 16);
				out[g * 16 + i * 4 + 3] = (uint8_t)(words[i] >> 24);
			}
		}
	}
}

void tp_audio_fill(float *out, size_t count, uint64_t position, uint32_t rate, enum tp_audio audio, bool beep)
{
	double amplitude = 0;
	switch (audio) {
	case TP_AUDIO_SYNC_BEEP: amplitude = beep ? 0.1 : 0; break;
	case TP_AUDIO_TONE_20: amplitude = 0.1; break;
	case TP_AUDIO_TONE_18: amplitude = pow(10.0, -18.0 / 20); break;
	default: break;
	}
	for (size_t i = 0; i < count; i++) {
		// Phase from the absolute position keeps the tone continuous across chunks.
		const uint64_t cycle = ((position + i) * 1000) % rate;
		out[i] = amplitude ? (float)(amplitude * sin(2 * M_PI * (double)cycle / rate)) : 0.0f;
	}
}
