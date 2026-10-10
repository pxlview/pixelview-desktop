#pragma once
// Pixelview: P3-D65 to Rec.2020 for a node whose colour profile is
// "P3-D65 PQ, sending with Pixelview Desktop". The capture is decoded as
// Rec.2100 PQ (BT.2020 primaries), but its code values are P3-D65; this filter
// corrects the primaries in the canvas's linear light, so the stream leaves as
// true Rec.2100 PQ (P3 colours in the BT.2020 container) and the engine needs no
// LUT. White (D65) is unchanged.
#include <obs-module.h>
#include <graphics/matrix4.h>

namespace pixelview {

inline constexpr const char *P3_FILTER_ID = "pixelview_p3d65_to_rec2020";
inline constexpr const char *P3_FILTER_NAME = "Pixelview P3-D65 to Rec.2020";

// Linear canvas (BT.709 primaries) correction: P3->709 after 709->2020, i.e. the
// colour libobs decoded as BT.2020 re-read as P3-D65. Rows of C, exact from the
// ITU-R BT.709, BT.2020 and SMPTE EG 432-1 primaries; each row sums to 1.
inline constexpr float P3_CANVAS_CORRECTION[3][3] = {
	{0.7529894824f, 0.1965104445f, 0.0505000731f},
	{0.0456166137f, 0.9443648220f, 0.0100185643f},
	{0.0002480687f, 0.0178873585f, 0.9818645728f},
};

namespace p3filter {
inline const char *effectSource()
{
	return "uniform float4x4 ViewProj;\n"
	       "uniform texture2d image;\n"
	       "uniform float4x4 correction;\n"
	       "sampler_state pointSampler { Filter = Point; AddressU = Clamp; AddressV = Clamp; };\n"
	       "struct VertData { float4 pos : POSITION; float2 uv : TEXCOORD0; };\n"
	       "VertData VSDefault(VertData v_in) { VertData v_out; v_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj); v_out.uv = v_in.uv; return v_out; }\n"
	       "float4 PSCorrect(VertData v_in) : TARGET {\n"
	       " float4 rgba = image.Sample(pointSampler, v_in.uv);\n"
	       " rgba.rgb = mul(float4(rgba.rgb, 0.0), correction).rgb;\n"
	       " return rgba;\n"
	       "}\n"
	       "technique Draw { pass { vertex_shader = VSDefault(v_in); pixel_shader = PSCorrect(v_in); } }\n";
}
struct Filter {
	obs_source_t *context = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *correction = nullptr;
};
inline const char *name(void *) { return P3_FILTER_NAME; }
inline void *create(obs_data_t *, obs_source_t *context)
{
	auto *filter = new Filter{context};
	obs_enter_graphics();
	char *errors = nullptr;
	filter->effect = gs_effect_create(effectSource(), "pixelview-p3-filter", &errors);
	if (errors) {
		blog(LOG_ERROR, "[pixelview] P3 filter effect: %s", errors);
		bfree(errors);
	}
	if (filter->effect) filter->correction = gs_effect_get_param_by_name(filter->effect, "correction");
	obs_leave_graphics();
	return filter;
}
inline void destroy(void *data)
{
	auto *filter = static_cast<Filter *>(data);
	obs_enter_graphics();
	gs_effect_destroy(filter->effect);
	obs_leave_graphics();
	delete filter;
}
inline enum gs_color_space colorSpace(void *data, size_t, const enum gs_color_space *)
{
	auto *filter = static_cast<Filter *>(data);
	const enum gs_color_space spaces[] = {GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED, GS_CS_709_SCRGB};
	return obs_source_get_color_space(obs_filter_get_target(filter->context), OBS_COUNTOF(spaces), spaces);
}
inline void render(void *data, gs_effect_t *)
{
	auto *filter = static_cast<Filter *>(data);
	const enum gs_color_space spaces[] = {GS_CS_709_EXTENDED, GS_CS_709_SCRGB};
	const enum gs_color_space space =
		obs_source_get_color_space(obs_filter_get_target(filter->context), OBS_COUNTOF(spaces), spaces);
	// Only linear HDR light can be corrected by a matrix; anything else passes unchanged.
	if (!filter->effect || !filter->correction || (space != GS_CS_709_EXTENDED && space != GS_CS_709_SCRGB)) {
		obs_source_skip_video_filter(filter->context);
		return;
	}
	if (!obs_source_process_filter_begin_with_color_space(filter->context, GS_RGBA16F, space, OBS_ALLOW_DIRECT_RENDERING))
		return;
	// The rows of C go into the rows of the uploaded matrix (measured on the SDI loop:
	// the transposed layout tinted white, since C's columns do not sum to 1).
	struct matrix4 m;
	matrix4_identity(&m);
	vec4_set(&m.x, P3_CANVAS_CORRECTION[0][0], P3_CANVAS_CORRECTION[0][1], P3_CANVAS_CORRECTION[0][2], 0.f);
	vec4_set(&m.y, P3_CANVAS_CORRECTION[1][0], P3_CANVAS_CORRECTION[1][1], P3_CANVAS_CORRECTION[1][2], 0.f);
	vec4_set(&m.z, P3_CANVAS_CORRECTION[2][0], P3_CANVAS_CORRECTION[2][1], P3_CANVAS_CORRECTION[2][2], 0.f);
	vec4_set(&m.t, 0.f, 0.f, 0.f, 1.f);
	gs_effect_set_matrix4(filter->correction, &m);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	obs_source_process_filter_end(filter->context, filter->effect, 0, 0);
	gs_blend_state_pop();
}
} // namespace p3filter

// Registered by the application before the scene collection loads.
inline void registerP3Filter()
{
	static bool registered = false;
	if (registered) return;
	registered = true;
	struct obs_source_info info = {};
	info.id = P3_FILTER_ID;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;
	info.get_name = p3filter::name;
	info.create = p3filter::create;
	info.destroy = p3filter::destroy;
	info.video_render = p3filter::render;
	info.video_get_color_space = p3filter::colorSpace;
	obs_register_source(&info);
}

} // namespace pixelview
