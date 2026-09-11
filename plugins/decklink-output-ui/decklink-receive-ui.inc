// SPDX-License-Identifier: GPL-2.0-or-later
// UI-thread-only selection and watchdog. A weak exact source is pinned by the
// native output for an attempt. No names, scenes or saved settings identify it.
static obs_weak_source_t *receive_source = nullptr;
static bool receive_mode = false;
static QTimer *receive_watchdog = nullptr;
static bool receive_auto_pending = false;
static void bind_receive_source(void *, calldata_t *cd)
{
	const bool receiving = calldata_bool(cd, "receiving");
	auto *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (context.output) {
		output_stop(); // synchronous card drain before source switch
	}
	obs_weak_source_release(receive_source);
	receive_source = source ? obs_source_get_weak_source(source) : nullptr;
	receive_mode = receiving;
	OBSData settings = load_settings();
	receive_auto_pending = receiving && source && settings && obs_data_get_bool(settings, "auto_start");
	calldata_set_bool(cd, "bound", true);
}
static void receive_output_watchdog()
{
	if (shutting_down || !receive_mode) {
		return;
	}
	if (main_output_running && context.output) {
		calldata_t cd;
		calldata_init(&cd);
		bool ok = proc_handler_call(obs_output_get_proc_handler(context.output), "receive_status", &cd) &&
			  calldata_bool(&cd, "healthy");
		calldata_free(&cd);
		if (!ok) {
			output_stop();
		}
	} else if (receive_auto_pending) {
		OBSSourceAutoRelease source = obs_weak_source_get_source(receive_source);
		if (!source) {
			return;
		}
		calldata_t cd;
		calldata_init(&cd);
		bool ready = proc_handler_call(obs_source_get_proc_handler(source), "get_status", &cd) &&
			     calldata_bool(&cd, "ready");
		calldata_free(&cd);
		if (ready) {
			receive_auto_pending = false;
			output_start();
		}
	}
}
