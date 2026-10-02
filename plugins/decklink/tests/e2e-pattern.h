// SPDX-License-Identifier: GPL-2.0-or-later
// End-to-end test picture, 1920x1080 10-bit 4:2:2, defined in limited-range codes.
// Rows   0-299: sixteen flat patches (120 px each)
// Rows 300-479: fine luma ramp, one code per three pixels (200..839), neutral chroma
// Rows 480-659: left half Cb alternates 412/612 on every row (needs 4:2:2); right half flat grey
// Rows 660-1079: 75% colour bars (hard edges on even pixels)
#include <stdint.h>
#define EW 1920u
#define EH 1080u
#define E_PATCHES 16
static const char *const e2e_patch_name[E_PATCHES] = {"black", "white", "grey", "dark", "bright", "yellow", "cyan", "green",
	"magenta", "red", "blue", "pastel1", "pastel2", "skin", "sub-black", "super-white"};
static const unsigned e2e_patch[E_PATCHES][3] = {{64, 512, 512}, {940, 512, 512}, {502, 512, 512}, {100, 512, 512}, {850, 512, 512},
	{674, 176, 543}, {581, 589, 176}, {534, 253, 207}, {251, 771, 817}, {204, 435, 848}, {111, 848, 481},
	{600, 460, 560}, {400, 560, 470}, {560, 470, 580}, {40, 512, 512}, {980, 512, 512}};
static const unsigned e2e_bars[8][3] = {{721, 512, 512}, {674, 176, 543}, {581, 589, 176}, {534, 253, 207},
	{251, 771, 817}, {204, 435, 848}, {111, 848, 481}, {64, 512, 512}};
// limited-range sample at (x, y): c = 0 Y, 1 Cb, 2 Cr (chroma of the pixel pair)
static inline unsigned e2e_limited(unsigned x, unsigned y, unsigned c)
{
	if (y < 300) return e2e_patch[x / 120][c];
	if (y < 480) return c ? 512 : 200 + x / 3;
	if (y < 660) return c == 0 ? 500 : (c == 1 && x < EW / 2) ? ((y & 1) ? 412 : 612) : 512;
	return e2e_bars[(x & ~1u) / 240][c];
}
static inline unsigned e2e_clip(double v) { return v < 4 ? 4 : v > 1019 ? 1019 : (unsigned)(v + 0.5); }
// BT.2100 full range from a limited code
static inline double e2e_full_y(double v) { return (v - 64) * 1023.0 / 876.0; }
static inline double e2e_full_c(double v) { return 512 + (v - 512) * 1023.0 / 896.0; }
static inline unsigned e2e_sample(unsigned x, unsigned y, unsigned c, int full)
{
	unsigned v = e2e_limited(x, y, c);
	return full ? e2e_clip(c ? e2e_full_c(v) : e2e_full_y(v)) : v;
}

// What the output should carry: the codes the generator actually put on SDI (source range,
// already clipped to 4-1019), converted to the output range. Returned as a real number.
static inline double e2e_expected(unsigned x, unsigned y, unsigned c, int source_full, int out_full)
{
	double v = e2e_sample(x, y, c, source_full);
	if (source_full) v = c ? 512 + (v - 512) * 896.0 / 1023.0 : 64 + v * 876.0 / 1023.0; // to limited
	if (out_full) v = c ? e2e_full_c(v) : e2e_full_y(v);
	return v < 4 ? 4 : v > 1019 ? 1019 : v;
}
