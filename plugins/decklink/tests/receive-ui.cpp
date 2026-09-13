// SPDX-License-Identifier: GPL-2.0-or-later
// Compile the production selection/watchdog include with Qt and real OBS weak
// references/procs. Output start/stop are controlled boundaries; actual owner
// Start/Stop and card callbacks are covered by receive.cpp, not mocked here.
#include <QCoreApplication>
#include <QTimer>
#include <obs.hpp>
#include <cassert>
#include <cstdio>
static bool shutting_down = false, main_output_running = false, healthy = true;
static unsigned starts = 0, stops = 0;
static obs_output_t *fixtureOutput = nullptr;
static struct {
	obs_output_t *output = nullptr;
} context;
static obs_data_t *settings = nullptr;
static OBSData load_settings()
{
	return OBSData(settings);
}
static void output_start()
{
	++starts;
	main_output_running = true;
	context.output = fixtureOutput;
}
static void output_stop()
{
	++stops;
	main_output_running = false;
	context.output = nullptr;
}
#include "../../decklink-output-ui/decklink-receive-ui.inc"
#include "manual-ready.inc"
static int frames = 0;
static bool sourceReady = false;
static const char *name(void *)
{
	return "offline native output UI";
}
static void *create_source(obs_data_t *, obs_source_t *s)
{
	proc_handler_add(
		obs_source_get_proc_handler(s), "void get_status(out int frames, out bool ready)",
		[](void *, calldata_t *cd) { calldata_set_int(cd, "frames", frames); calldata_set_bool(cd, "ready", sourceReady); }, nullptr);
	return s;
}
static void *create_output(obs_data_t *, obs_output_t *o)
{
	proc_handler_add(
		obs_output_get_proc_handler(o), "void receive_status(out bool healthy)",
		[](void *, calldata_t *cd) { calldata_set_bool(cd, "healthy", healthy); }, nullptr);
	return o;
}
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	assert(obs_startup("en-US", nullptr, nullptr));
	obs_source_info si = {};
	si.id = "ui_source";
	si.type = OBS_SOURCE_TYPE_INPUT;
	si.get_name = name;
	si.create = create_source;
	si.destroy = [](void *) {
	};
	obs_register_source(&si);
	obs_output_info oi = {};
	oi.id = "ui_output";
	oi.flags = OBS_OUTPUT_ENCODED;
	oi.get_name = name;
	oi.create = create_output;
	oi.destroy = [](void *) {
	};
	oi.start = [](void *) {
		return false;
	};
	oi.stop = [](void *, uint64_t) {
	};
	oi.encoded_packet = [](void *, encoder_packet *) {
	};
	obs_register_output(&oi);
	fixtureOutput = obs_output_create(oi.id, "ui-output", nullptr, nullptr);
	assert(fixtureOutput);
	auto *source = obs_source_create_private(si.id, "exact source", nullptr);
	assert(source);
	settings = obs_data_create();
	obs_data_set_bool(settings, "auto_start", true);
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_ptr(&cd, "source", source);
	calldata_set_bool(&cd, "receiving", true);
	bind_receive_source(nullptr, &cd);
	assert(receive_mode && receive_auto_pending);
	auto *pinned = obs_weak_source_get_source(receive_source);
	assert(pinned == source);
	obs_source_release(pinned);
	receive_output_watchdog();
	assert(starts == 0);
	frames = 1;
	assert(!manual_ready(source));
	receive_output_watchdog();
	assert(starts == 0); // stale cumulative frames cannot authorize AutoStart
	sourceReady = true;
	assert(manual_ready(source));
	receive_output_watchdog();
	assert(starts == 1 && !receive_auto_pending && main_output_running);
	receive_output_watchdog();
	assert(stops == 0);
	healthy = false;
	receive_output_watchdog();
	assert(stops == 1 && !main_output_running && receive_auto_pending);
	// A watchdog stop resumes once the source is ready again, but an attempt
	// that fails again right away counts against a bounded budget: three
	// consecutive quick failures end automatic resume until the next bind.
	receive_output_watchdog();
	assert(starts == 2 && main_output_running && !receive_auto_pending);
	receive_output_watchdog();
	assert(stops == 2 && receive_auto_pending);
	receive_output_watchdog();
	assert(starts == 3 && main_output_running);
	receive_output_watchdog();
	assert(stops == 3 && !receive_auto_pending); // budget exhausted
	receive_output_watchdog();
	receive_output_watchdog();
	assert(starts == 3); // no restart loop after repeated failure
	// A fresh bind resets the resume budget.
	healthy = true;
	bind_receive_source(nullptr, &cd);
	assert(receive_auto_pending && stops == 3);
	receive_output_watchdog();
	assert(starts == 4 && main_output_running);
	healthy = false;
	receive_output_watchdog();
	assert(stops == 4 && receive_auto_pending);
	healthy = true;
	receive_output_watchdog();
	assert(starts == 5 && main_output_running);
	receive_output_watchdog();
	assert(stops == 4);
	context.output = fixtureOutput;
	calldata_set_ptr(&cd, "source", nullptr);
	bind_receive_source(nullptr, &cd);
	assert(stops == 5 && receive_mode && !receive_source && !receive_auto_pending);
	calldata_set_bool(&cd, "receiving", false);
	bind_receive_source(nullptr, &cd);
	assert(!receive_mode);
	receive_output_watchdog();
	assert(starts == 5);
	obs_output_release(fixtureOutput);
	obs_source_release(source);
	obs_data_release(settings);
	calldata_free(&cd);
	obs_shutdown();
	puts("compiled production receive UI: exact weak source, ready-only AutoStart, source-change drain, loss watchdog and bounded resume PASS");
}
