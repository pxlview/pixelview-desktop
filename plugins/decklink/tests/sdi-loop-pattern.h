// SPDX-License-Identifier: GPL-2.0-or-later
// Shared 1920x1080 10-bit 4:2:2 reference pattern (five horizontal bands).
#include <stdint.h>
#include <stdlib.h>
#define PW 1920u
#define PH 1080u
static inline void pattern_fill(uint16_t *Yi, uint16_t *Cbi, uint16_t *Cri)
{
	static const unsigned bars[8][3] = {{721, 512, 512}, {674, 176, 543}, {581, 589, 176}, {534, 253, 207},
					    {251, 771, 817}, {204, 435, 848}, {111, 848, 481}, {64, 512, 512}};
	srand(1);
	for (unsigned y = 0; y < PH; y++) {
		const unsigned region = y * 5 / PH;
		for (unsigned x = 0; x < PW; x++) {
			unsigned Y, C0 = 512, C1 = 512;
			const unsigned k = x / 2;
			switch (region) {
			case 0: Y = 64 + (x * 876 / (PW - 1)); break;
			case 1: Y = 300 + (x % 400); C0 = 200 + (k * 600 / (PW / 2 - 1)); C1 = 800 - (k * 500 / (PW / 2 - 1)); break;
			case 2: Y = bars[x * 8 / PW][0]; C0 = bars[k * 2 * 8 / PW][1]; C1 = bars[k * 2 * 8 / PW][2]; break;
			case 3: Y = 64 + rand() % 877; C0 = 64 + rand() % 897; C1 = 64 + rand() % 897; break;
			default: Y = 4 + (x * 1015 / (PW - 1)); C0 = 4 + ((k * 7) % 1016); C1 = 1019 - ((k * 13) % 1016); break;
			}
			{ static double sat = -1; if (sat < 0) sat = getenv("SAT") ? atof(getenv("SAT")) : 1.0;
			  C0 = (unsigned)(512 + ((int)C0 - 512) * sat + 0.5); C1 = (unsigned)(512 + ((int)C1 - 512) * sat + 0.5); }
			Yi[y * PW + x] = (uint16_t)Y;
			if (!(x & 1)) { Cbi[y * (PW / 2) + k] = (uint16_t)C0; Cri[y * (PW / 2) + k] = (uint16_t)C1; }
		}
	}
}
