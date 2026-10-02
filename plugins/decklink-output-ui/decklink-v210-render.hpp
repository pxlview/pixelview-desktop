// SPDX-License-Identifier: GPL-2.0-or-later
// Pixelview: draw a linear RGB program texture as v210 (10-bit 4:2:2 Y'CbCr)
// with the DrawV210* techniques in libobs/data/default.effect. Shared by the
// DeckLink output UI and its offline fidelity probe; graphics thread only.
#pragma once
#include <obs.h>
#include <graphics/matrix4.h>

namespace pixelview_v210 {

enum class Mode { SDR, Tonemap, PQ, HLG };

// Texels (32-bit words) per row of the RGBA8 render target: v210 rows are
// padded to 48 pixels, the same layout as libobs's VIDEO_FORMAT_V210.
inline uint32_t row_words(uint32_t width)
{
	return ((width + 47) / 48) * 32;
}

inline Mode mode_for(enum video_colorspace canvas, bool hdr_output)
{
	const bool hdr_canvas = canvas == VIDEO_CS_2100_PQ || canvas == VIDEO_CS_2100_HLG;
	if (!hdr_canvas)
		return Mode::SDR;
	if (!hdr_output)
		return Mode::Tonemap;
	return canvas == VIDEO_CS_2100_HLG ? Mode::HLG : Mode::PQ;
}

// Colour space of the Y'CbCr the mode produces (limited range).
inline enum video_colorspace colorspace_for(Mode mode)
{
	return mode == Mode::PQ ? VIDEO_CS_2100_PQ : mode == Mode::HLG ? VIDEO_CS_2100_HLG : VIDEO_CS_709;
}

// Call between gs_texrender_begin(target, row_words(width), height) and
// gs_texrender_end. `source` is width x height, read 1:1 (no scaling).
// full_range chooses the levels of the Y'CbCr written (the canvas itself has
// no range): limited is the exact inverse of the matrix the v210 source
// conversion uses, full rescales black/white from 64/940 to 0/1023 (SDI keeps
// codes 0-3 and 1020-1023 reserved, so the shader clips to 4-1019).
inline bool draw(gs_texture_t *source, uint32_t width, uint32_t height, Mode mode, bool full_range = false)
{
	struct matrix4 matrix;
	if (!video_format_get_parameters_for_format(colorspace_for(mode),
						    full_range ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL, VIDEO_FORMAT_V210,
						    (float *)&matrix, nullptr, nullptr) ||
	    !matrix4_inv(&matrix, &matrix))
		return false;

	gs_effect_t *const effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	const char *const technique = mode == Mode::PQ        ? "DrawV210PQ"
				      : mode == Mode::HLG     ? "DrawV210HLG"
				      : mode == Mode::Tonemap ? "DrawV210Tonemap"
							      : "DrawV210";
	const bool previous = gs_framebuffer_srgb_enabled();
	// The shader writes packed bytes, never colour values.
	gs_enable_framebuffer_srgb(false);
	gs_enable_blending(false);
	const uint32_t words = row_words(width);
	gs_ortho(0.0f, (float)words, 0.0f, (float)height, -100.0f, 100.0f);
	// An eight-bit sRGB canvas texture is linearised on load; float canvases already are linear.
	gs_effect_set_texture_srgb(gs_effect_get_param_by_name(effect, "image"), source);
	gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "v210_vec_y"), &matrix.x);
	gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "v210_vec_cb"), &matrix.y);
	gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "v210_vec_cr"), &matrix.z);
	gs_effect_set_float(gs_effect_get_param_by_name(effect, "v210_width"), (float)width);
	gs_effect_set_float(gs_effect_get_param_by_name(effect, "multiplier"),
			    obs_get_video_sdr_white_level() / 10000.f);
	gs_effect_set_float(gs_effect_get_param_by_name(effect, "hdr_lw"), obs_get_video_hdr_nominal_peak_level());
	while (gs_effect_loop(effect, technique))
		gs_draw_sprite(source, 0, words, height);
	gs_enable_blending(true);
	gs_enable_framebuffer_srgb(previous);
	return true;
}

} // namespace pixelview_v210
