// SPDX-License-Identifier: GPL-2.0-or-later
// UI-thread-only selection and watchdog. A weak exact source is pinned by the
// native output for an attempt. No names, scenes or saved settings identify it.
#include <util/platform.h>
static obs_weak_source_t *receive_source = nullptr;
static bool receive_mode = false;
static QTimer *receive_watchdog = nullptr;
static bool receive_auto_pending = false;
// Automatic resume after a watchdog stop is bounded: an attempt that ends
// within RECEIVE_RESUME_STABLE_NS counts as a quick failure, and after
// RECEIVE_RESUME_LIMIT consecutive quick failures the operator must Start.
static const uint64_t RECEIVE_RESUME_STABLE_NS = 10000000000ULL;
static const unsigned RECEIVE_RESUME_LIMIT = 3;
static bool receive_output_seen_running = false;
static uint64_t receive_output_started_ns = 0;
static unsigned receive_resume_failures = 0;
// Set by output_start: a native output owns the source's private media, a
// rendered one only plays the program canvas.
static bool receive_output_native = false;
// A running rendered output keeps playing across receive stops and reconnects
// and only changes its health source. Stopping the card while frames are still
// outstanding after a media stall can deadlock inside the DeckLink SDK.
static bool rebind_running_output(obs_source_t *source)
{
	if (!main_output_running || receive_output_native) {
		return false;
	}
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_ptr(&cd, "source", source);
	calldata_set_bool(&cd, "native", false);
	const bool bound = proc_handler_call(obs_output_get_proc_handler(context.output), "bind_receive", &cd) &&
			   calldata_bool(&cd, "bound");
	calldata_free(&cd);
	return bound;
}
static void bind_receive_source(void *, calldata_t *cd)
{
	const bool receiving = calldata_bool(cd, "receiving");
	auto *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	bool kept = false;
	if (context.output) {
		kept = receiving && rebind_running_output(source);
		if (!kept) {
			output_stop(); // synchronous card drain before source switch
		}
	}
	obs_weak_source_release(receive_source);
	receive_source = source ? obs_source_get_weak_source(source) : nullptr;
	receive_mode = receiving;
	receive_resume_failures = 0;
	receive_output_seen_running = false;
	OBSData settings = load_settings();
	receive_auto_pending = !kept && receiving && source && settings && obs_data_get_bool(settings, "auto_start");
	calldata_set_bool(cd, "bound", true);
}
static void receive_output_watchdog()
{
	if (shutting_down || !receive_mode) {
		return;
	}
	const bool running = main_output_running && context.output;
	if (running && !receive_output_seen_running) {
		receive_output_started_ns = os_gettime_ns();
	}
	receive_output_seen_running = running;
	if (running) {
		calldata_t cd;
		calldata_init(&cd);
		bool ok = proc_handler_call(obs_output_get_proc_handler(context.output), "receive_status", &cd) &&
			  calldata_bool(&cd, "healthy");
		if (!ok) {
			const char *reason = calldata_string(&cd, "reason");
			const uint64_t ran_ns = os_gettime_ns() - receive_output_started_ns;
			blog(LOG_WARNING, "[decklink-output-ui] receive output stopped after %llu ms: %s",
			     (unsigned long long)(ran_ns / 1000000ULL),
			     reason && *reason ? reason : "the output reported no receive status");
			output_stop();
			receive_output_seen_running = false;
			receive_resume_failures = ran_ns < RECEIVE_RESUME_STABLE_NS ? receive_resume_failures + 1 : 0;
			if (receive_resume_failures < RECEIVE_RESUME_LIMIT) {
				receive_auto_pending = true;
				blog(LOG_INFO, "[decklink-output-ui] receive output will resume once the stream is ready again");
			} else {
				blog(LOG_WARNING,
				     "[decklink-output-ui] receive output failed %u times in a row shortly after starting; "
				     "not resuming automatically, start it from DeckLink output settings",
				     receive_resume_failures);
			}
		}
		calldata_free(&cd);
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
