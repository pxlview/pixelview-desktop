// SPDX-License-Identifier: GPL-2.0-or-later
// Interposed into the SDI tool: measures the captured v210 picture against the end-to-end pattern,
// or the captured R10l picture against the 4:4:4 R'G'B' pattern.
// Env: E2E_EXPECT=limited|full (levels expected on the wire), E2E_CAPTURE_ID=<source name substring, optional>
#include <obs.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "e2e-pattern.h"
#define ACC_FRAMES 50
static double accY[EH][EW / 2], accCb[EH][EW / 2], accCr[EH][EW / 2]; // per pixel pair: mean of the two lumas, and chroma
static uint16_t lastY[EH][EW];
static void unpack(const struct obs_source_frame2 *f)
{
	for (unsigned y = 0; y < EH; y++) {
		const uint8_t *row = f->data[0] + (size_t)y * f->linesize[0];
		for (unsigned g = 0; g < EW / 6; g++) {
			uint32_t w[4]; memcpy(w, row + g * 16, 16);
			unsigned Y[6] = {(w[0] >> 10) & 1023, w[1] & 1023, (w[1] >> 20) & 1023, (w[2] >> 10) & 1023, w[3] & 1023, (w[3] >> 20) & 1023};
			unsigned cb[3] = {w[0] & 1023, (w[1] >> 10) & 1023, (w[2] >> 20) & 1023}, cr[3] = {(w[0] >> 20) & 1023, w[2] & 1023, (w[3] >> 10) & 1023};
			for (unsigned k = 0; k < 3; k++) { accY[y][g * 3 + k] += 0.5 * (Y[2 * k] + Y[2 * k + 1]); accCb[y][g * 3 + k] += cb[k]; accCr[y][g * 3 + k] += cr[k]; }
			for (unsigned k = 0; k < 6; k++) lastY[y][g * 6 + k] = (uint16_t)Y[k];
		}
	}
}
static int src_full;
// 4:4:4 R'G'B': per-pixel sums of each component.
static double accRGB[3][EH][EW];
static uint16_t lastG[EH][EW];
static void unpack_rgb(const struct obs_source_frame2 *f)
{
	for (unsigned y = 0; y < EH; y++) {
		const uint8_t *row = f->data[0] + (size_t)y * f->linesize[0];
		for (unsigned x = 0; x < EW; x++) {
			uint32_t w; memcpy(&w, row + x * 4, 4);
			accRGB[0][y][x] += (w >> 22) & 1023; accRGB[1][y][x] += (w >> 12) & 1023; accRGB[2][y][x] += (w >> 2) & 1023;
			lastG[y][x] = (uint16_t)((w >> 12) & 1023);
		}
	}
}
#define RGBEXP(x, y, c) e2e_rgb_expected(x, y, c, src_full, full)
static void report_rgb(int n, int full)
{
	src_full = getenv("E2E_SOURCE") && !strcmp(getenv("E2E_SOURCE"), "full");
	double worst[3] = {0, 0, 0};
	printf("E2E capture (10-bit RGB 4:4:4): %d frames averaged, source %s range, output %s range\n", n, src_full ? "full" : "limited", full ? "full" : "limited");
	printf("  %-12s %-18s %-18s %s\n", "patch", "expected R/G/B", "measured", "error");
	for (unsigned p = 0; p < E_PATCHES; p++) {
		double s[3] = {0, 0, 0}; long cnt = 0;
		for (unsigned y = 30; y < 270; y++) for (unsigned x = p * 120 + 20; x < p * 120 + 100; x++) { for (int c = 0; c < 3; c++) s[c] += accRGB[c][y][x]; cnt++; }
		double m[3], e[3]; unsigned x = p * 120 + 60; int clipped = p >= 14 && (full || src_full);
		for (int c = 0; c < 3; c++) { m[c] = s[c] / cnt / n; e[c] = m[c] - RGBEXP(x, 100, c); if (!clipped && fabs(e[c]) > worst[c]) worst[c] = fabs(e[c]); }
		printf("  %-12s %6.1f %6.1f %6.1f  %7.2f %7.2f %7.2f   %+6.2f %+6.2f %+6.2f\n", e2e_rgb_patch_name[p], RGBEXP(x, 100, 0), RGBEXP(x, 100, 1), RGBEXP(x, 100, 2), m[0], m[1], m[2], e[0], e[1], e[2]);
	}
	unsigned char seen[1024] = {0}; unsigned distinct = 0; double rampErr = 0; long rc = 0;
	for (unsigned x = 12; x < EW - 12; x++) { unsigned v = lastG[390][x]; if (!seen[v]) { seen[v] = 1; distinct++; } }
	for (unsigned x = 12; x < EW - 12; x++) { double mean = 0; for (unsigned y = 320; y < 460; y++) mean += accRGB[1][y][x]; rampErr += fabs(mean / 140 / n - RGBEXP(x, 390, 1)); rc++; }
	// 4:4:4 horizontal chroma: mean |R(x) - R(x+1)| and |B(x) - B(x+1)| in the alternating field, and the level of G there
	double alt[2] = {0, 0}, g = 0; long ac = 0;
	for (unsigned y = 500; y < 640; y++) for (unsigned x = 40; x < EW / 2 - 40; x++) { alt[0] += fabs(accRGB[0][y][x] - accRGB[0][y][x + 1]) / n; alt[1] += fabs(accRGB[2][y][x] - accRGB[2][y][x + 1]) / n; g += accRGB[1][y][x] / n; ac++; }
	double flat[3] = {0, 0, 0}; long fc = 0;
	for (unsigned y = 500; y < 640; y++) for (unsigned x = EW / 2 + 40; x < EW - 40; x++) { for (int c = 0; c < 3; c++) flat[c] += accRGB[c][y][x] / n; fc++; }
	// bar edge yellow|cyan at x = 241 (odd): B on the last pixel before and the first after, against the plateaus
	double left = 0, right = 0, before = 0, after = 0; for (unsigned y = 700; y < 1040; y++) { left += accRGB[2][y][150] / n; right += accRGB[2][y][350] / n; before += accRGB[2][y][240] / n; after += accRGB[2][y][241] / n; }
	left /= 340; right /= 340; before /= 340; after /= 340;
	double altExp = fabs(RGBEXP(100, 500, 0) - RGBEXP(101, 500, 0));
	printf("  ramp: %u distinct green codes on one row (source has %u), mean abs error %.2f codes\n", distinct, 632u, rampErr / rc);
	printf("  pixel-alternating colour: mean pixel-to-pixel difference R %.1f B %.1f (source %.0f; 4:2:2 or 4:2:0 gives about 0), G %.2f (source %.0f)\n", alt[0] / ac, alt[1] / ac, altExp, g / ac, RGBEXP(100, 500, 1));
	printf("  flat grey: R %.2f G %.2f B %.2f (source %.0f)\n", flat[0] / fc, flat[1] / fc, flat[2] / fc, RGBEXP(EW - 100, 500, 1));
	printf("  bar edge yellow|cyan B: plateau %.1f -> %.1f, last pixel before %.1f, first pixel after %.1f\n", left, right, before, after);
	printf("  worst patch error (excluding clipped): R %.2f G %.2f B %.2f\n", worst[0], worst[1], worst[2]);
	printf("E2E_SUMMARY rgb worstR=%.2f worstG=%.2f worstB=%.2f ramp_codes=%u ramp_err=%.2f altR=%.1f altB=%.1f alt_source=%.0f\n", worst[0], worst[1], worst[2], distinct, rampErr / rc, alt[0] / ac, alt[1] / ac, altExp);
	fflush(stdout);
}
#define EXP(x, y, c) e2e_expected(x, y, c, src_full, full)
static void report(int n, int full, const struct obs_source_frame2 *f)
{
	src_full = getenv("E2E_SOURCE") && !strcmp(getenv("E2E_SOURCE"), "full");
	double worst[3] = {0, 0, 0};
	printf("E2E capture: %d frames averaged, source %s range, output %s range\n", n, src_full ? "full" : "limited", full ? "full" : "limited");
	printf("  %-12s %-18s %-18s %s\n", "patch", "expected Y/Cb/Cr", "measured", "error");
	for (unsigned p = 0; p < E_PATCHES; p++) {
		double s[3] = {0, 0, 0}; long cnt = 0;
		for (unsigned y = 30; y < 270; y++) for (unsigned k = p * 60 + 10; k < p * 60 + 50; k++) { s[0] += accY[y][k]; s[1] += accCb[y][k]; s[2] += accCr[y][k]; cnt++; }
		double m[3], e[3]; unsigned x = p * 120 + 60; int clipped = p >= 14 && (full || src_full);
		for (int c = 0; c < 3; c++) { m[c] = s[c] / cnt / n; e[c] = m[c] - EXP(x, 100, c); if (!clipped && fabs(e[c]) > worst[c]) worst[c] = fabs(e[c]); }
		printf("  %-12s %6.1f %6.1f %6.1f  %7.2f %7.2f %7.2f   %+6.2f %+6.2f %+6.2f\n", e2e_patch_name[p], EXP(x, 100, 0), EXP(x, 100, 1), EXP(x, 100, 2), m[0], m[1], m[2], e[0], e[1], e[2]);
	}
	// ramp: distinct luma codes actually present on one row of the last frame, and mean error of the averaged ramp
	unsigned char seen[1024] = {0}; unsigned distinct = 0; double rampErr = 0; long rc = 0;
	for (unsigned x = 12; x < EW - 12; x++) { unsigned v = lastY[390][x]; if (!seen[v]) { seen[v] = 1; distinct++; } }
	for (unsigned k = 6; k < EW / 2 - 6; k++) { double exp = 0.5 * (EXP(2 * k, 390, 0) + EXP(2 * k + 1, 390, 0)); double mean = 0; for (unsigned y = 320; y < 460; y++) mean += accY[y][k]; rampErr += fabs(mean / 140 / n - exp); rc++; }
	// 4:2:2 vertical chroma: mean |Cb(row) - Cb(row+1)| in the alternating field
	double alt = 0; long ac = 0; for (unsigned y = 500; y < 640; y++) for (unsigned k = 40; k < EW / 4 - 40; k++) { alt += fabs(accCb[y][k] - accCb[y + 1][k]) / n; ac++; }
	double g[2] = {0, 0}; long gc = 0; for (unsigned y = 500; y < 640; y++) for (unsigned k = EW / 4 + 40; k < EW / 2 - 40; k++) { g[0] += accCb[y][k] / n; g[1] += accCr[y][k] / n; gc++; }
	// bar edge: chroma one pair before and after the yellow/cyan boundary (pair 240) relative to the plateaus
	double left = 0, right = 0, before = 0, after = 0; for (unsigned y = 700; y < 1040; y++) { left += accCb[y][200] / n; right += accCb[y][280] / n; before += accCb[y][239] / n; after += accCb[y][240] / n; }
	left /= 340; right /= 340; before /= 340; after /= 340;
	double altExp = fabs(EXP(100, 500, 1) - EXP(100, 501, 1));
	printf("  ramp: %u distinct luma codes on one row (source has %u), mean abs error %.2f codes\n", distinct, 632u, rampErr / rc);
	printf("  row-alternating chroma: mean row-to-row Cb difference %.1f (source %.0f; 4:2:0 gives about 0)\n", alt / ac, altExp);
	printf("  flat grey: Cb %.2f Cr %.2f (neutral is 512)\n", g[0] / gc, g[1] / gc);
	printf("  bar edge yellow|cyan Cb: plateau %.1f -> %.1f, last pair before %.1f, first pair after %.1f\n", left, right, before, after);
	printf("  worst patch error (excluding clipped): Y %.2f Cb %.2f Cr %.2f\n", worst[0], worst[1], worst[2]);
	printf("E2E_SUMMARY worstY=%.2f worstCb=%.2f worstCr=%.2f ramp_codes=%u ramp_err=%.2f alt=%.1f grey_cb=%.2f grey_cr=%.2f\n", worst[0], worst[1], worst[2], distinct, rampErr / rc, alt / ac, g[0] / gc, g[1] / gc);
	fflush(stdout);
}
static void observed(obs_source_t *s, const struct obs_source_frame2 *f)
{
	static long n; static int acc, done, full = -1; static long start = -1;
	if (full < 0) { full = getenv("E2E_EXPECT") && !strcmp(getenv("E2E_EXPECT"), "full"); start = getenv("E2E_SKIP") ? atol(getenv("E2E_SKIP")) : 75; }
	if (f && s && !strcmp(obs_source_get_id(s), "decklink-input") && !done) {
		n++;
		if (n == 5 || n == start) fprintf(stderr, "E2E capture frame %ld: format=%d %ux%u\n", n, (int)f->format, f->width, f->height);
		if (n >= start && f->format == VIDEO_FORMAT_V210 && f->width == EW && f->height == EH) {
			unpack(f); if (++acc == ACC_FRAMES) { report(acc, full, f); done = 1; }
		}
		if (n >= start && f->format == VIDEO_FORMAT_R10L && f->width == EW && f->height == EH) {
			unpack_rgb(f); if (++acc == ACC_FRAMES) { report_rgb(acc, full); done = 1; }
		}
	}
	obs_source_output_video2(s, f);
}
__attribute__((used)) static struct { const void *replacement, *replacee; } interpose
	__attribute__((section("__DATA,__interpose"))) = {(const void *)observed, (const void *)obs_source_output_video2};
