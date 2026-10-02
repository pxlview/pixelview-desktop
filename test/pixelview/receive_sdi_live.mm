// Live receive to SDI without the application window (see run_receive_sdi_live.py).
// The real PixelviewReceiver (backend login) and the packaged pixelview-whep
// module receive a session; the source fills a 1920x1080 libobs canvas 1:1 and
// the canvas is rendered to the built decklink_output with the same shared
// renderer the DeckLink output UI uses: v210 (4:2:2 Y'CbCr) or R10l (4:4:4 RGB).
// An analyser on another DeckLink input (run-e2e-sdi-tool.py --capture) then
// measures what left the card. Credentials arrive on stdin as one JSON line.
#include "frontend/utility/PixelviewReceiver.hpp"
#import <Foundation/Foundation.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>
#include <media-io/video-io.h>
#include <media-io/video-frame.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include "decklink-v210-render.hpp"

static constexpr unsigned W = 1920, H = 1080;
static gs_texrender_t *texrender;
static gs_stagesurf_t *stage;
static video_t *queue;
static bool rgb444, full_output;
static std::atomic<long> pushed{0};

static void rendered(void *)
{
	gs_texture_t *tex = obs_get_main_texture();
	if (!tex || !queue)
		return;
	const uint32_t words = rgb444 ? W : pixelview_v210::row_words(W);
	if (!texrender) {
		texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		stage = gs_stagesurface_create(words, H, GS_RGBA);
	}
	gs_texrender_reset(texrender);
	if (!gs_texrender_begin(texrender, words, H))
		return;
	const bool drawn = rgb444 ? pixelview_v210::draw_r10l(tex, W, H, pixelview_v210::Mode::SDR, full_output)
				  : pixelview_v210::draw(tex, W, H, pixelview_v210::Mode::SDR, full_output);
	gs_texrender_end(texrender);
	if (!drawn)
		return;
	gs_stage_texture(stage, gs_texrender_get_texture(texrender));
	uint8_t *data;
	uint32_t stride;
	struct video_frame out;
	if (gs_stagesurface_map(stage, &data, &stride)) {
		if (video_output_lock_frame(queue, &out, 1, os_gettime_ns())) {
			for (unsigned y = 0; y < H; y++)
				memcpy(out.data[0] + (size_t)y * out.linesize[0], data + (size_t)y * stride, out.linesize[0]);
			video_output_unlock_frame(queue);
			pushed++;
		}
		gs_stagesurface_unmap(stage);
	}
}

// What the receive plugin hands to OBS.
static std::atomic<long> frames_seen{0};
static std::atomic<int> last_format{-1}, last_width{0}, last_height{0};
static const char *audit_name(void *)
{
	return "audit";
}
static void *audit_create(obs_data_t *, obs_source_t *)
{
	return (void *)1;
}
static void audit_destroy(void *) {}
static struct obs_source_frame *audit_video(void *, struct obs_source_frame *f)
{
	frames_seen++;
	last_format = (int)f->format;
	last_width = (int)f->width;
	last_height = (int)f->height;
	return f;
}

static void pump()
{
	QCoreApplication::processEvents();
	CFRunLoopRunInMode(kCFRunLoopDefaultMode, .01, true);
}
static bool choose(obs_properties_t *props, obs_data_t *settings, const char *key, const char *want, bool is_int)
{
	obs_property_t *p = obs_properties_get(props, key);
	if (!p)
		return false;
	for (size_t i = 0; i < obs_property_list_item_count(p); i++) {
		const char *n = obs_property_list_item_name(p, i);
		if (!n || !strstr(n, want) || obs_property_list_item_disabled(p, i))
			continue;
		if (is_int) {
			obs_data_set_int(settings, key, obs_property_list_item_int(p, i));
		} else {
			obs_data_set_string(settings, key, obs_property_list_item_string(p, i));
			obs_data_set_string(settings, "device_name", n);
		}
		obs_property_modified(p, settings);
		return true;
	}
	return false;
}

// argv: whep module, libobs data, graphics module, decklink module, decklink data, output device substring,
//       mode name, yuv422|rgb444, limited|full, seconds, backend origin, fps numerator, fps denominator
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	if (argc != 14)
		return 2;
	const uint32_t fps_num = (uint32_t)atoi(argv[12]), fps_den = (uint32_t)atoi(argv[13]);
	setbuf(stdout, nullptr);
	rgb444 = !strcmp(argv[8], "rgb444");
	const bool full = !strcmp(argv[9], "full");
	const int seconds = atoi(argv[10]);
	std::string input;
	std::getline(std::cin, input);
	auto credentials = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
	input.clear();
	if (!obs_startup("en-US", nullptr, nullptr))
		return 3;
	obs_add_data_path(argv[2]);
	obs_video_info vi = {};
	vi.graphics_module = argv[3];
	vi.fps_num = fps_num;
	vi.fps_den = fps_den;
	vi.base_width = vi.output_width = W;
	vi.base_height = vi.output_height = H;
	vi.output_format = VIDEO_FORMAT_P010;
	vi.colorspace = VIDEO_CS_709;
	vi.range = VIDEO_RANGE_PARTIAL;
	vi.gpu_conversion = true;
	vi.scale_type = OBS_SCALE_DISABLE;
	if (obs_reset_video(&vi) != OBS_VIDEO_SUCCESS)
		return 4;
	obs_audio_info ai = {48000, SPEAKERS_STEREO};
	if (!obs_reset_audio(&ai))
		return 5;
	obs_module_t *whep = nullptr, *decklink = nullptr;
	if (obs_open_module(&whep, argv[1], ".") != MODULE_SUCCESS || !obs_init_module(whep))
		return 6;
	if (obs_open_module(&decklink, argv[4], argv[5]) != MODULE_SUCCESS || !obs_init_module(decklink))
		return 7;
	for (int i = 0; i < 250; i++)
		pump(); // device arrival callbacks

	obs_source_t *source = obs_source_create_private("pixelview_whep_source", "receive-sdi-live", nullptr);
	if (!source)
		return 8;
	struct obs_source_info audit = {};
	audit.id = "pv_receive_sdi_audit";
	audit.type = OBS_SOURCE_TYPE_FILTER;
	audit.output_flags = OBS_SOURCE_ASYNC_VIDEO;
	audit.get_name = audit_name;
	audit.create = audit_create;
	audit.destroy = audit_destroy;
	audit.filter_video = audit_video;
	obs_register_source(&audit);
	obs_source_t *filter = obs_source_create_private(audit.id, "audit", nullptr);
	obs_source_filter_add(source, filter);
	obs_scene_t *scene = obs_scene_create_private("receive");
	obs_scene_add(scene, source);
	obs_set_output_source(0, obs_scene_get_source(scene));

	obs_data_t *settings = obs_data_create();
	obs_properties_t *props = obs_get_output_properties("decklink_output");
	if (!props || !choose(props, settings, "device_hash", argv[6], false) || !choose(props, settings, "mode_id", argv[7], true)) {
		puts("no output device/mode");
		return 9;
	}
	obs_data_set_int(settings, "output_format", rgb444 ? 1 : 0);
	obs_data_set_int(settings, "output_range", full ? 1 : 0);
	obs_output_t *output = obs_output_create("decklink_output", "receive out", settings, nullptr);
	const struct video_scale_info *conv = output ? obs_output_get_video_conversion(output) : nullptr;
	if (!conv)
		return 10;
	full_output = conv->range == VIDEO_RANGE_FULL;
	video_output_info qi = {};
	qi.name = "receive queue";
	qi.format = conv->format;
	qi.width = W;
	qi.height = H;
	qi.fps_num = fps_num;
	qi.fps_den = fps_den;
	qi.cache_size = 16;
	qi.colorspace = VIDEO_CS_DEFAULT;
	qi.range = conv->range;
	if (video_output_open(&queue, &qi) != VIDEO_OUTPUT_SUCCESS)
		return 11;
	obs_add_main_rendered_callback(rendered, nullptr);
	obs_output_set_media(output, queue, obs_get_audio());
	const bool started = obs_output_start(output);
	printf("output %s %s range start: %d %s\n", rgb444 ? "10-bit RGB 4:4:4" : "10-bit 4:2:2 YUV", full ? "full" : "limited", started,
	       started ? "" : (obs_output_get_last_error(output) ? obs_output_get_last_error(output) : ""));
	if (!started)
		return 12;

	auto *ph = obs_source_get_proc_handler(source);
	pixelview::PixelviewReceiver receiver;
	if (!receiver.setOrigin(QUrl(argv[11]), true))
		return 13;
	bool delivered = false;
	receiver.onEndpoint = [&](const QString &endpoint) {
		calldata_t cd;
		calldata_init(&cd);
		calldata_set_string(&cd, "endpoint", endpoint.toUtf8().constData());
		calldata_set_int(&cd, "latency", 100);
		calldata_set_string(&cd, "color", "sdr");
		proc_handler_call(ph, "connect", &cd);
		calldata_free(&cd);
		delivered = true;
		puts("EVT endpoint");
	};
	receiver.onStopped = [&] {
		calldata_t cd;
		calldata_init(&cd);
		proc_handler_call(ph, "disconnect", &cd);
		calldata_free(&cd);
	};
	const QString id = credentials["session_id"].toString(), password = credentials["password"].toString();
	QElapsedTimer clock;
	clock.start();
	receiver.start(id, password, "receive to SDI test");
	calldata_t cd;
	calldata_init(&cd);
	qint64 next = 5000, retry_at = -1;
	while (clock.elapsed() < seconds * 1000LL) {
		pump();
		if (retry_at >= 0) {
			if (clock.elapsed() >= retry_at) {
				retry_at = -1;
				delivered = false;
				receiver.start(id, password, "receive to SDI test");
				puts("EVT restart");
			}
			continue;
		}
		if (receiver.state() == pixelview::PixelviewReceiver::State::Error) {
			printf("EVT receiver_error %s\n", receiver.status().toUtf8().constData());
			break;
		}
		if (!delivered)
			continue;
		proc_handler_call(ph, "get_status", &cd);
		const std::string state = calldata_string(&cd, "state");
		const char *failure = calldata_string(&cd, "failure");
		if (state == "error" || state == "ended") {
			if (failure && *failure) {
				printf("EVT typed_failure %s\n", failure);
				break;
			}
			// As the Desktop does: an untyped stall or end starts a fresh session.
			puts("EVT media ended, reconnecting in 2 s");
			receiver.stop();
			retry_at = clock.elapsed() + 2000;
			continue;
		}
		if (clock.elapsed() >= next) {
			printf("STAT t=%lld state=%s video=%lld audio=%lld source_format=%d %dx%d frames_to_card=%ld\n", clock.elapsed() / 1000,
			       state.c_str(), calldata_int(&cd, "frames"), calldata_int(&cd, "audio_frames"), last_format.load(),
			       last_width.load(), last_height.load(), pushed.load());
			next += 5000;
		}
	}
	calldata_free(&cd);
	receiver.stop();
	credentials = {};
	obs_remove_main_rendered_callback(rendered, nullptr);
	obs_output_stop(output);
	for (int i = 0; i < 100; i++)
		pump();
	obs_output_release(output);
	video_output_close(queue);
	obs_set_output_source(0, nullptr);
	puts("done");
	_exit(0);
}
