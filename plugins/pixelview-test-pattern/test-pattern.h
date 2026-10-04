// SPDX-License-Identifier: GPL-2.0-or-later
// Broadcast test patterns as 10-bit limited-range BT.709 Y'CbCr, packed v210
// like a DeckLink capture, so a test stream takes the same path as SDI input.
// No libobs dependency: the offline tests compile this file on its own.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum tp_pattern {
	TP_SMPTE_BARS, // SMPTE RP 219 HD bars with PLUGE
	TP_EBU_BARS,   // 100/0/75/0 full-field bars
	TP_FULL_BARS,  // 100% full-field bars
	TP_GRAY_RAMP,  // 10-bit luma ramp, black to white
	TP_GRAY_STEPS, // 11 steps, 0-100%
	TP_RGB_RAMPS,  // white, red, green and blue ramps
	TP_CROSSHATCH, // 16x9 grid, circle and centre cross
	TP_ZONE_PLATE, // circular zone plate reaching Nyquist at the side edges
	TP_BLACK,
	TP_PATTERN_COUNT
};

enum tp_audio {
	TP_AUDIO_SYNC_BEEP, // 1 kHz beep on the flash frame, once a second
	TP_AUDIO_TONE_20,   // continuous 1 kHz at -20 dBFS (SMPTE line-up)
	TP_AUDIO_TONE_18,   // continuous 1 kHz at -18 dBFS (EBU line-up)
	TP_AUDIO_OFF,
	TP_AUDIO_COUNT
};

const char *tp_pattern_name(enum tp_pattern pattern);
const char *tp_audio_name(enum tp_audio audio);

struct tp_ycc {
	uint16_t y, cb, cr;
};

// Non-linear R'G'B' in 0..1 to 10-bit limited BT.709.
struct tp_ycc tp_rgb(double r, double g, double b);
// Achromatic level in percent of the nominal range; may be below 0 (PLUGE).
struct tp_ycc tp_level(double percent);

// 4:4:4 working image; chroma is co-sited from even pixels when packed.
struct tp_image {
	uint32_t width, height;
	uint16_t *y, *cb, *cr;
};

bool tp_image_alloc(struct tp_image *img, uint32_t width, uint32_t height);
void tp_image_free(struct tp_image *img);
void tp_render(struct tp_image *img, enum tp_pattern pattern);

// Burnt-in timecode box: HH:MM:SS:FF in seven-segment digits, a bar that
// fills over each second and a square that flashes on the first frame of a
// second (the frame that carries the sync beep).
struct tp_overlay {
	unsigned hours, minutes, seconds, frames;
	unsigned frames_per_second; // nominal, for the progress bar
	bool flash;
};

struct tp_rect {
	uint32_t x, y, width, height;
};

struct tp_rect tp_overlay_rect(uint32_t width, uint32_t height);
// Draws the overlay over its whole rectangle; nothing outside it changes.
void tp_draw_overlay(struct tp_image *dst, const struct tp_overlay *overlay);

size_t tp_v210_linesize(uint32_t width);
// Packs rows [first, first + count) of img into a v210 buffer of img->height rows.
void tp_pack_v210(uint8_t *dst, const struct tp_image *img, uint32_t first, uint32_t count);

// Fills mono samples for the absolute sample positions
// [position, position + count). beep selects whether this span is a beep.
void tp_audio_fill(float *out, size_t count, uint64_t position, uint32_t rate, enum tp_audio audio, bool beep);

#ifdef __cplusplus
}
#endif
