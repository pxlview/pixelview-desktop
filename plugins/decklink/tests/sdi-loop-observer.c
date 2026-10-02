// SPDX-License-Identifier: GPL-2.0-or-later
// Interposed into the loop harness: compares every v210 frame the DeckLink capture delivers with the pattern.
#include <obs.h>
#include <stdio.h>
#include <string.h>
#include "sdi-loop-pattern.h"
static uint16_t Yi[PW * PH], Cbi[PW / 2 * PH], Cri[PW / 2 * PH];
static void observed(obs_source_t *s, const struct obs_source_frame2 *f)
{
	static long n; static int filled, full = -1;
	if (full < 0) full = getenv("RANGE") && !strcmp(getenv("RANGE"), "full");
	if (f && s && !strcmp(obs_source_get_id(s), "decklink-input")) {
		if (!filled) { pattern_fill(Yi, Cbi, Cri); filled = 1; }
		n++;
		if (n % 25 == 5) {
			if (f->format != VIDEO_FORMAT_V210 || f->width != PW || f->height != PH) {
				fprintf(stderr, "CAPTURED frame=%ld format=%d size=%ux%u (not 1080 v210)\n", n, (int)f->format, f->width, f->height);
			} else {
				static const char *names[5] = {"luma ramp", "chroma gradients", "colour bars", "random noise", "codes 4-1019"};
				static const unsigned word[6] = {0, 1, 1, 2, 3, 3}, shift[6] = {10, 0, 20, 10, 0, 20};
				for (unsigned region = 0; region < 5; region++) {
					long ny = 0, ey = 0, nc = 0, ec = 0; int wy = 0, wc = 0;
					for (unsigned y = region * PH / 5 + 2; y < (region + 1) * PH / 5 - 2; y++) {
						const uint8_t *row = f->data[0] + (size_t)y * f->linesize[0];
						for (unsigned x = 6; x < PW - 6; x++) {
							uint32_t w[4]; memcpy(w, row + (x / 6) * 16, 16);
							const unsigned j = x % 6, pair = j / 2, k = x / 2;
							int yr = Yi[y * PW + x], cbr = Cbi[y * (PW / 2) + k], crr = Cri[y * (PW / 2) + k];
							if (full) { /* BT.2100 full range, clipped to the SDI codes 4-1019 */
								double v = (yr - 64) * 1023.0 / 876.0; yr = (int)(v < 4 ? 4 : v > 1019 ? 1019 : v + 0.5);
								v = 512 + (cbr - 512) * 1023.0 / 896.0; cbr = (int)(v < 4 ? 4 : v > 1019 ? 1019 : v + 0.5);
								v = 512 + (crr - 512) * 1023.0 / 896.0; crr = (int)(v < 4 ? 4 : v > 1019 ? 1019 : v + 0.5);
							}
							int d = (int)((w[word[j]] >> shift[j]) & 1023) - yr;
							ny++; ey += d == 0; if (abs(d) > wy) wy = abs(d);
							if (!(x & 1)) {
								int cb = pair == 0 ? (w[0] & 1023) : pair == 1 ? ((w[1] >> 10) & 1023) : ((w[2] >> 20) & 1023);
								int cr = pair == 0 ? ((w[0] >> 20) & 1023) : pair == 1 ? (w[2] & 1023) : ((w[3] >> 10) & 1023);
								int a = cb - cbr, b = cr - crr;
								nc += 2; ec += (a == 0) + (b == 0); if (abs(a) > wc) wc = abs(a); if (abs(b) > wc) wc = abs(b);
							}
						}
					}
					fprintf(stderr, "CAPTURED frame=%ld region=\"%s\" luma_exact=%.4f%% worst=%d chroma_exact=%.4f%% worst=%d trc=%d range=%d\n",
						n, names[region], 100.0 * ey / ny, wy, 100.0 * ec / nc, wc, (int)f->trc, (int)f->range);
				}
			}
		}
	}
	obs_source_output_video2(s, f);
}
__attribute__((used)) static struct { const void *replacement, *replacee; } interpose
	__attribute__((section("__DATA,__interpose"))) = {(const void *)observed, (const void *)obs_source_output_video2};
