// SPDX-License-Identifier: GPL-2.0-or-later
// Pixelview test pattern source: a generated stand-in for a DeckLink input.
// Frames are v210 limited-range BT.709 at the canvas size and frame rate, so
// the pattern takes the same conversion path as a 10-bit SDI capture.
#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>
#include <util/util_uint64.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "test-pattern.h"

OBS_DECLARE_MODULE()

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Pixelview test patterns";
}

#define AUDIO_RATE 48000u

struct tp_source {
	obs_source_t *source;
	os_event_t *stop;
	pthread_t thread;
	bool thread_started;

	pthread_mutex_t mutex;
	enum tp_pattern pattern;
	enum tp_audio audio;
	bool timecode;
	uint64_t generation;
};

struct tp_settings {
	enum tp_pattern pattern;
	enum tp_audio audio;
	bool timecode;
	uint64_t generation;
};

static struct tp_settings snapshot(struct tp_source *tp)
{
	pthread_mutex_lock(&tp->mutex);
	const struct tp_settings s = {tp->pattern, tp->audio, tp->timecode, tp->generation};
	pthread_mutex_unlock(&tp->mutex);
	return s;
}

static int64_t wall_offset_ns(void)
{
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	return (int64_t)now.tv_sec * 1000000000 + now.tv_nsec - (int64_t)os_gettime_ns();
}

struct tp_render_state {
	struct tp_image base, work;
	uint8_t *packed;
	size_t linesize;
	uint64_t generation;
	bool overlay_drawn;
};

static bool prepare(struct tp_render_state *r, const struct tp_settings *s, uint32_t width, uint32_t height)
{
	if (r->packed && r->generation == s->generation && r->base.width == width && r->base.height == height)
		return true;
	tp_image_free(&r->base);
	tp_image_free(&r->work);
	bfree(r->packed);
	r->packed = NULL;
	if (!tp_image_alloc(&r->base, width, height) || !tp_image_alloc(&r->work, width, height))
		return false;
	tp_render(&r->base, s->pattern);
	const size_t count = (size_t)width * height * sizeof(uint16_t);
	memcpy(r->work.y, r->base.y, count);
	memcpy(r->work.cb, r->base.cb, count);
	memcpy(r->work.cr, r->base.cr, count);
	r->linesize = tp_v210_linesize(width);
	r->packed = bmalloc(r->linesize * height);
	tp_pack_v210(r->packed, &r->work, 0, height);
	r->generation = s->generation;
	r->overlay_drawn = false;
	return true;
}

static void restore_overlay_area(struct tp_render_state *r)
{
	const struct tp_rect rect = tp_overlay_rect(r->base.width, r->base.height);
	for (uint32_t y = rect.y; y < rect.y + rect.height; y++) {
		const size_t i = (size_t)y * r->base.width + rect.x, n = rect.width * sizeof(uint16_t);
		memcpy(r->work.y + i, r->base.y + i, n);
		memcpy(r->work.cb + i, r->base.cb + i, n);
		memcpy(r->work.cr + i, r->base.cr + i, n);
	}
	tp_pack_v210(r->packed, &r->work, rect.y, rect.height);
	r->overlay_drawn = false;
}

static void *tp_thread(void *data)
{
	struct tp_source *tp = data;
	os_set_thread_name("pixelview-test-pattern");

	struct tp_render_state render = {.generation = UINT64_MAX};
	struct obs_source_frame frame = {.format = VIDEO_FORMAT_V210, .full_range = false};
	video_format_get_parameters_for_format(VIDEO_CS_709, VIDEO_RANGE_PARTIAL, VIDEO_FORMAT_V210, frame.color_matrix,
					       frame.color_range_min, frame.color_range_max);
	// As for a DeckLink v210 capture: keep sub-black PLUGE and super-white.
	for (int i = 0; i < 3; i++) {
		frame.color_range_min[i] = 0.0f;
		frame.color_range_max[i] = 1.0f;
	}

	float *samples = NULL;
	size_t sample_capacity = 0;
	uint32_t fps_num = 0, fps_den = 0;
	uint64_t start = 0, index = 0, position = 0;
	int64_t last_second = -1;
	unsigned frame_in_second = 0;

	while (os_event_try(tp->stop) == EAGAIN) {
		struct obs_video_info ovi;
		if (!obs_get_video_info(&ovi) || !ovi.fps_num || !ovi.fps_den || !ovi.base_width || !ovi.base_height) {
			os_event_timedwait(tp->stop, 100);
			continue;
		}
		const uint64_t now = os_gettime_ns();
		const uint64_t frame_ns = util_mul_div64(1000000000ULL, ovi.fps_den, ovi.fps_num);
		uint64_t ts = start + util_mul_div64(index, 1000000000ULL * ovi.fps_den, ovi.fps_num);
		// Restart the clock on a frame-rate change or after falling behind
		// (pattern rendering, a stalled thread): never burst out late frames.
		if (ovi.fps_num != fps_num || ovi.fps_den != fps_den || now > ts + 4 * frame_ns) {
			fps_num = ovi.fps_num;
			fps_den = ovi.fps_den;
			start = ts = now;
			index = position = 0;
		}
		const uint64_t next = start + util_mul_div64(index + 1, 1000000000ULL * fps_den, fps_num);

		const struct tp_settings s = snapshot(tp);
		const int64_t wall = (int64_t)ts + wall_offset_ns();
		const int64_t second = wall / 1000000000;
		const bool flash = second != last_second;
		frame_in_second = flash ? 0 : frame_in_second + 1;
		last_second = second;

		const bool showing = obs_source_showing(tp->source);
		if (showing && prepare(&render, &s, ovi.base_width, ovi.base_height)) {
			if (s.timecode) {
				const time_t t = (time_t)second;
				struct tm local;
				localtime_r(&t, &local);
				const unsigned nominal = (fps_num + fps_den - 1) / fps_den;
				const struct tp_overlay overlay = {(unsigned)local.tm_hour, (unsigned)local.tm_min,
								   (unsigned)local.tm_sec, frame_in_second, nominal, flash};
				const struct tp_rect rect = tp_overlay_rect(render.work.width, render.work.height);
				tp_draw_overlay(&render.work, &overlay);
				tp_pack_v210(render.packed, &render.work, rect.y, rect.height);
				render.overlay_drawn = true;
			} else if (render.overlay_drawn) {
				restore_overlay_area(&render);
			}
			frame.data[0] = render.packed;
			frame.linesize[0] = (uint32_t)render.linesize;
			frame.width = render.base.width;
			frame.height = render.base.height;
			frame.timestamp = ts;
			obs_source_output_video(tp->source, &frame);
		}

		// Audio spans exactly this frame's interval, so the beep starts on the
		// flash frame's timestamp and A/V offset is zero by construction.
		const uint64_t end = util_mul_div64(next - start, AUDIO_RATE, 1000000000ULL);
		const size_t count = (size_t)(end - position);
		if (showing && s.audio != TP_AUDIO_OFF && count) {
			if (count > sample_capacity) {
				samples = brealloc(samples, count * sizeof(float));
				sample_capacity = count;
			}
			tp_audio_fill(samples, count, position, AUDIO_RATE, s.audio, flash);
			struct obs_source_audio audio = {
				.data = {(uint8_t *)samples, (uint8_t *)samples},
				.frames = (uint32_t)count,
				.speakers = SPEAKERS_STEREO,
				.format = AUDIO_FORMAT_FLOAT_PLANAR,
				.samples_per_sec = AUDIO_RATE,
				.timestamp = start + util_mul_div64(position, 1000000000ULL, AUDIO_RATE),
			};
			obs_source_output_audio(tp->source, &audio);
		}
		position = end;
		index++;
		os_sleepto_ns(next);
	}

	tp_image_free(&render.base);
	tp_image_free(&render.work);
	bfree(render.packed);
	bfree(samples);
	return NULL;
}

static const char *tp_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "Pixelview Test Pattern";
}

static void tp_update(void *data, obs_data_t *settings)
{
	struct tp_source *tp = data;
	long long pattern = obs_data_get_int(settings, "pattern");
	long long audio = obs_data_get_int(settings, "audio");
	pthread_mutex_lock(&tp->mutex);
	const enum tp_pattern next = pattern >= 0 && pattern < TP_PATTERN_COUNT ? (enum tp_pattern)pattern : TP_SMPTE_BARS;
	if (next != tp->pattern || !tp->generation) {
		tp->pattern = next;
		tp->generation++;
	}
	tp->audio = audio >= 0 && audio < TP_AUDIO_COUNT ? (enum tp_audio)audio : TP_AUDIO_SYNC_BEEP;
	tp->timecode = obs_data_get_bool(settings, "timecode");
	pthread_mutex_unlock(&tp->mutex);
}

static void tp_destroy(void *data)
{
	struct tp_source *tp = data;
	if (tp->thread_started) {
		os_event_signal(tp->stop);
		pthread_join(tp->thread, NULL);
	}
	os_event_destroy(tp->stop);
	pthread_mutex_destroy(&tp->mutex);
	bfree(tp);
}

static void *tp_create(obs_data_t *settings, obs_source_t *source)
{
	struct tp_source *tp = bzalloc(sizeof(*tp));
	tp->source = source;
	if (pthread_mutex_init(&tp->mutex, NULL) != 0) {
		bfree(tp);
		return NULL;
	}
	if (os_event_init(&tp->stop, OS_EVENT_TYPE_MANUAL) != 0) {
		tp_destroy(tp);
		return NULL;
	}
	tp_update(tp, settings);
	// Like the DeckLink input's default (buffering off): show the newest frame.
	obs_source_set_async_unbuffered(source, true);
	if (pthread_create(&tp->thread, NULL, tp_thread, tp) != 0) {
		tp_destroy(tp);
		return NULL;
	}
	tp->thread_started = true;
	return tp;
}

static void tp_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "pattern", TP_SMPTE_BARS);
	obs_data_set_default_int(settings, "audio", TP_AUDIO_SYNC_BEEP);
	obs_data_set_default_bool(settings, "timecode", true);
}

static obs_properties_t *tp_properties(void *unused)
{
	UNUSED_PARAMETER(unused);
	obs_properties_t *props = obs_properties_create();
	obs_property_t *pattern =
		obs_properties_add_list(props, "pattern", "Pattern", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (int i = 0; i < TP_PATTERN_COUNT; i++)
		obs_property_list_add_int(pattern, tp_pattern_name((enum tp_pattern)i), i);
	obs_property_t *audio = obs_properties_add_list(props, "audio", "Audio", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (int i = 0; i < TP_AUDIO_COUNT; i++)
		obs_property_list_add_int(audio, tp_audio_name((enum tp_audio)i), i);
	obs_property_t *timecode = obs_properties_add_bool(props, "timecode", "Timecode and sync flash");
	obs_property_set_long_description(
		timecode, "Burns in the Mac's time of day with a frame count, and flashes a square on the frame that carries "
			  "the sync beep. Compare with the receiver's clock to read glass-to-glass latency.");
	return props;
}

static struct obs_source_info pixelview_test_pattern = {
	.id = "pixelview_test_pattern",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE,
	.get_name = tp_get_name,
	.create = tp_create,
	.destroy = tp_destroy,
	.update = tp_update,
	.get_defaults = tp_defaults,
	.get_properties = tp_properties,
	.icon_type = OBS_ICON_TYPE_COLOR,
};

bool obs_module_load(void)
{
	obs_register_source(&pixelview_test_pattern);
	return true;
}
