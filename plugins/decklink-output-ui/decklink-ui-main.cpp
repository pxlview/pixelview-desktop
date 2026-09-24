#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include <QAction>
#include <QTimer>
#include <QMessageBox>
#include <QPointer>
#include <util/util.hpp>
#include <util/platform.h>
#include <media-io/video-io.h>
#include <media-io/video-frame.h>
#include "DecklinkOutputUI.h"
#include "../../../plugins/decklink/const.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("decklink-output-ui", "en-US")

DecklinkOutputUI *doUI;

bool shutting_down = false;

bool main_output_running = false;

constexpr size_t STAGE_BUFFER_COUNT = 3;

struct decklink_ui_output {
	bool enabled;
	obs_output_t *output;

	video_t *video_queue;
	gs_texrender_t *texrender;
	gs_stagesurf_t *stagesurfaces[STAGE_BUFFER_COUNT];
	bool surf_written[STAGE_BUFFER_COUNT];
	size_t stage_index;
	uint8_t *video_data;
	uint32_t video_linesize;

	obs_video_info ovi;
};

static struct decklink_ui_output context = {0};

OBSData load_settings()
{
	BPtr<char> path = obs_module_get_config_path(obs_current_module(), "decklinkOutputProps.json");
	BPtr<char> jsonData = os_quick_read_utf8_file(path);
	if (!!jsonData) {
		obs_data_t *data = obs_data_create_from_json(jsonData);
		OBSData dataRet(data);
		obs_data_release(data);

		return dataRet;
	}

	return nullptr;
}

void output_start();
void output_stop();
#include "decklink-receive-ui.inc"

static void decklink_ui_tick(void *param, float sec);
static void decklink_ui_render(void *param);

// Pixelview: a start that does not happen is an operator-visible error, not
// only a log line. Non-blocking so the watchdog timer never nests a modal loop.
// The launch-time auto start may simply be early (nothing received yet): that
// stays a log line, the watchdog starts the output once video arrives.
static bool launch_auto_start = false;
static void start_failed(const QString &reason, bool waiting = false)
{
	blog(LOG_WARNING, "[decklink-output-ui] Start failed: %s", reason.toUtf8().constData());
	if (shutting_down || (waiting && launch_auto_start)) {
		return;
	}
	static QPointer<QMessageBox> box;
	if (box) {
		box->close();
	}
	QWidget *parent = doUI && doUI->isVisible() ? static_cast<QWidget *>(doUI)
						    : static_cast<QWidget *>(obs_frontend_get_main_window());
	box = new QMessageBox(QMessageBox::Warning, QStringLiteral("DeckLink output"),
			      QStringLiteral("The DeckLink output did not start.\n\n") + reason, QMessageBox::Ok, parent);
	box->setAttribute(Qt::WA_DeleteOnClose);
	box->open();
}

static QString output_error(obs_output_t *output)
{
	const char *error = obs_output_get_last_error(output);
	return error && *error ? QString::fromUtf8(error)
			       : QStringLiteral("The DeckLink device refused to start. Check the device, the output "
						"mode and that no other application is using the card.");
}

// In receive mode the selected output mode is the frame-rate setting: the
// frontend moves its receive canvas to the mode's exact rate before the start.
static bool follow_mode_frame_rate(obs_output_t *output, QString &error)
{
	calldata_t cd;
	calldata_init(&cd);
	bool ok = true;
	if (proc_handler_call(obs_output_get_proc_handler(output), "mode_frame_rate", &cd) &&
	    calldata_int(&cd, "num") > 0 && calldata_int(&cd, "den") > 0 &&
	    proc_handler_call(obs_get_proc_handler(), "pixelview_receive_frame_rate", &cd)) {
		ok = calldata_bool(&cd, "ok");
		if (!ok) {
			const char *text = calldata_string(&cd, "error");
			error = text && *text ? QString::fromUtf8(text)
					      : QStringLiteral("The canvas could not follow the output mode's frame rate.");
		}
	}
	calldata_free(&cd);
	return ok;
}

void output_stop()
{
	receive_auto_pending = false;
	if (!context.output) {
		return;
	}
	obs_remove_main_rendered_callback(decklink_ui_render, &context);

	obs_output_stop(context.output);
	obs_output_release(context.output);
	context.output = nullptr;

	obs_enter_graphics();
	for (gs_stagesurf_t *&surf : context.stagesurfaces) {
		gs_stagesurface_destroy(surf);
		surf = nullptr;
	}
	gs_texrender_destroy(context.texrender);
	context.texrender = nullptr;
	obs_leave_graphics();

	video_output_close(context.video_queue);
	context.video_queue = nullptr;
	obs_remove_tick_callback(decklink_ui_tick, &context);

	main_output_running = false;

	if (!shutting_down) {
		doUI->OutputStateChanged(false);
	}
}

void output_start()
{
	if (context.output || shutting_down) {
		return;
	}
	OBSSourceAutoRelease selected = receive_mode ? obs_weak_source_get_source(receive_source) : nullptr;
	if (receive_mode && !selected) {
		start_failed(QStringLiteral("Nothing is being received yet. Click Start receiving first; the output "
					    "can start once video arrives."),
			     true);
		return;
	}
	OBSData settings = load_settings();
	if (settings == nullptr) {
		start_failed(QStringLiteral("No DeckLink output settings are saved yet. Choose a device and mode first."));
	}

	if (settings != nullptr) {
		obs_output_t *const output = obs_output_create("decklink_output", "decklink_output", settings, NULL);
		if (!output) {
			start_failed(QStringLiteral("The DeckLink output could not be created. Check the Desktop Video "
						    "installation."));
			return;
		}
		if (receive_mode) {
			calldata_t cd;
			calldata_init(&cd);
			bool ready = proc_handler_call(obs_source_get_proc_handler(selected), "get_status", &cd) &&
				     calldata_bool(&cd, "ready");
			const bool native = calldata_int(&cd, "native422_frames") > 0;
			calldata_set_ptr(&cd, "source", selected);
			calldata_set_bool(&cd, "native", native);
			bool bound = ready &&
				     proc_handler_call(obs_output_get_proc_handler(output), "bind_receive", &cd) &&
				     calldata_bool(&cd, "bound");
			calldata_free(&cd);
			if (!bound) {
				start_failed(ready ? QStringLiteral("The output refused to bind the received stream.")
						   : QStringLiteral("No fresh video is being received yet. Start receiving "
								    "and wait for the picture, then start the output."),
					     !ready);
				obs_output_release(output);
				return;
			}
			QString rateError;
			if (!follow_mode_frame_rate(output, rateError)) {
				start_failed(rateError);
				obs_output_release(output);
				return;
			}
			if (native) {
				context.output = output;
				main_output_running = obs_output_start(output);
				if (!shutting_down) {
					doUI->OutputStateChanged(main_output_running);
				}
				if (!main_output_running) {
					start_failed(output_error(output));
					output_stop();
				}
				return;
			}
		}
		const struct video_scale_info *const conversion = obs_output_get_video_conversion(output);
		if (conversion != nullptr) {
			context.output = output;
			obs_add_tick_callback(decklink_ui_tick, &context);

			obs_get_video_info(&context.ovi);

			const uint32_t width = conversion->width;
			const uint32_t height = conversion->height;

			obs_enter_graphics();
			context.texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
			for (gs_stagesurf_t *&surf : context.stagesurfaces) {
				surf = gs_stagesurface_create(width, height, GS_BGRA);
			}
			obs_leave_graphics();

			for (bool &written : context.surf_written) {
				written = false;
			}

			context.stage_index = 0;

			video_output_info vi = {0};
			vi.format = VIDEO_FORMAT_BGRA;
			vi.width = width;
			vi.height = height;
			vi.fps_den = context.ovi.fps_den;
			vi.fps_num = context.ovi.fps_num;
			vi.cache_size = 16;
			vi.colorspace = VIDEO_CS_DEFAULT;
			vi.range = VIDEO_RANGE_FULL;
			vi.name = "decklink_output";

			video_output_open(&context.video_queue, &vi);

			obs_add_main_rendered_callback(decklink_ui_render, &context);

			// Ordinary reception uses the same rendered program and OBS mix as
			// sending. Only native422 owns private v210/source-PCM endpoints.
			obs_output_set_media(context.output, context.video_queue, obs_get_audio());
			bool started = obs_output_start(context.output);

			main_output_running = started;

			if (!shutting_down) {
				doUI->OutputStateChanged(started);
			}

			if (!started) {
				start_failed(output_error(context.output));
				output_stop();
			}
		} else {
			start_failed(QStringLiteral("The selected DeckLink device or output mode is unavailable. Check the "
						    "card and choose a device and mode in DeckLink output settings."));
			obs_output_release(output);
		}
	}
}

void output_toggle()
{
	if (main_output_running) {
		output_stop();
	} else {
		output_start();
	}
}

static void decklink_ui_tick(void *param, float /* sec */)
{
	auto ctx = (struct decklink_ui_output *)param;

	if (ctx->texrender) {
		gs_texrender_reset(ctx->texrender);
	}
}

static void decklink_ui_render(void *param)
{
	auto *const ctx = (struct decklink_ui_output *)param;

	gs_texture_t *tex = nullptr;

	if (ctx == &context) {
		if (!main_output_running) {
			return;
		}

		tex = obs_get_main_texture();
		if (!tex) {
			return;
		}
	} else {
		return;
	}

	const struct video_scale_info *const conversion = obs_output_get_video_conversion(ctx->output);
	const uint32_t scaled_width = conversion->width;
	const uint32_t scaled_height = conversion->height;

	if (!gs_texrender_begin(ctx->texrender, scaled_width, scaled_height)) {
		return;
	}

	const bool previous = gs_framebuffer_srgb_enabled();
	const bool source_hdr = (ctx->ovi.colorspace == VIDEO_CS_2100_PQ) || (ctx->ovi.colorspace == VIDEO_CS_2100_HLG);
	const bool target_hdr = source_hdr && (conversion->colorspace == VIDEO_CS_2100_PQ);
	gs_enable_framebuffer_srgb(!target_hdr);
	gs_enable_blending(false);

	gs_effect_t *const effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_effect_set_texture_srgb(gs_effect_get_param_by_name(effect, "image"), tex);
	// Pixelview: an HLG canvas leaves as HLG; the HDR metadata follows in the output.
	const bool target_hlg = target_hdr && (ctx->ovi.colorspace == VIDEO_CS_2100_HLG);
	const char *const tech_name = target_hlg   ? "DrawAlphaDivideR10LHLG"
				      : target_hdr ? "DrawAlphaDivideR10L"
						   : (source_hdr ? "DrawAlphaDivideTonemap" : "DrawAlphaDivide");
	while (gs_effect_loop(effect, tech_name)) {
		gs_effect_set_float(gs_effect_get_param_by_name(effect, "multiplier"),
				    obs_get_video_sdr_white_level() / 10000.f);
		if (target_hlg)
			gs_effect_set_float(gs_effect_get_param_by_name(effect, "hdr_lw"),
					    obs_get_video_hdr_nominal_peak_level());
		gs_draw_sprite(tex, 0, 0, 0);
	}

	gs_enable_blending(true);
	gs_enable_framebuffer_srgb(previous);

	gs_texrender_end(ctx->texrender);

	const size_t write_stage_index = ctx->stage_index;
	gs_stage_texture(ctx->stagesurfaces[write_stage_index], gs_texrender_get_texture(ctx->texrender));
	ctx->surf_written[write_stage_index] = true;

	const size_t read_stage_index = (write_stage_index + 1) % STAGE_BUFFER_COUNT;
	if (ctx->surf_written[read_stage_index]) {
		struct video_frame output_frame;
		if (video_output_lock_frame(ctx->video_queue, &output_frame, 1, os_gettime_ns())) {
			gs_stagesurf_t *const read_surf = ctx->stagesurfaces[read_stage_index];
			if (gs_stagesurface_map(read_surf, &ctx->video_data, &ctx->video_linesize)) {
				uint32_t linesize = output_frame.linesize[0];
				for (uint32_t i = 0; i < scaled_height; i++) {
					uint32_t dst_offset = linesize * i;
					uint32_t src_offset = ctx->video_linesize * i;
					memcpy(output_frame.data[0] + dst_offset, ctx->video_data + src_offset,
					       linesize);
				}

				gs_stagesurface_unmap(read_surf);
				ctx->video_data = nullptr;
			}

			video_output_unlock_frame(ctx->video_queue);
		}
	}

	ctx->stage_index = read_stage_index;
}

void addOutputUI(void)
{
	QAction *action = (QAction *)obs_frontend_add_tools_menu_qaction(obs_module_text("Decklink Output"));

	QMainWindow *window = (QMainWindow *)obs_frontend_get_main_window();

	obs_frontend_push_ui_translation(obs_module_get_string);
	doUI = new DecklinkOutputUI(window);
	obs_frontend_pop_ui_translation();

	auto cb = []() {
		doUI->ShowHideDialog();
	};

	QObject::connect(action, &QAction::triggered, action, cb);
}

static void OBSEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		OBSData settings = load_settings();

		if (settings && obs_data_get_bool(settings, "auto_start")) {
			launch_auto_start = true;
			output_start();
			launch_auto_start = false;
		}

		// Pixelview exposes program output only. Never load legacy Preview
		// settings here: a saved auto_start must not open an invisible output.
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		shutting_down = true;

		if (main_output_running) {
			output_stop();
		}
	}
}

bool obs_module_load(void)
{
	return true;
}

void obs_module_unload(void)
{
	shutting_down = true;
	if (receive_watchdog) {
		receive_watchdog->stop();
		delete receive_watchdog;
		receive_watchdog = nullptr;
	}
	obs_weak_source_release(receive_source);
	receive_source = nullptr;

	if (main_output_running) {
		output_stop();
	}
}

void obs_module_post_load(void)
{
	if (!obs_get_module("decklink")) {
		return;
	}

	addOutputUI();
	proc_handler_add(obs_get_proc_handler(),
			 "void pixelview_decklink_receive(ptr source, bool receiving, out bool bound)",
			 bind_receive_source, nullptr);
	proc_handler_add(
		obs_get_proc_handler(), "void pixelview_decklink_receive_state(out bool receiving)",
		[](void *, calldata_t *cd) { calldata_set_bool(cd, "receiving", receive_mode); }, nullptr);
	receive_watchdog = new QTimer;
	receive_watchdog->setInterval(100);
	QObject::connect(receive_watchdog, &QTimer::timeout, receive_output_watchdog);
	receive_watchdog->start();

	obs_frontend_add_event_callback(OBSEvent, nullptr);
}
