// SPDX-License-Identifier: GPL-2.0-or-later
// Included in the existing output owner translation unit.
#include <util/platform.h>

bool DeckLinkOutput::BindReceive(obs_source_t *source)
{
	std::lock_guard<std::recursive_mutex> lock(deviceMutex);
	// Health/watchdog metadata only: the output plays the program canvas and
	// the OBS mix, not the source, so a running output may change its health
	// source when the receive reconnects or stops.
	obs_source_release(receiveSource);
	receiveSource = obs_source_get_ref(source);
	receiveFrames = 0;
	receiveLastFrame = os_gettime_ns();
	return true;
}

bool DeckLinkOutput::ReceiveHealthy(const char **reason)
{
	const char *why = nullptr;
	std::lock_guard<std::recursive_mutex> lock(deviceMutex);
	if (!instance) {
		why = "the card output is no longer active";
	} else if (GetDevice()->Removed()) {
		why = "the DeckLink device was removed";
	} else if (receiveSource && !obs_source_removed(receiveSource)) {
		// The output plays the program canvas, not the source: a stalled, ended
		// or reconnecting receive only repeats the last canvas frame, so it
		// never drains the card (stopping the card with frames still outstanding
		// can deadlock inside the DeckLink SDK).
		calldata_t cd;
		calldata_init(&cd);
		proc_handler_call(obs_source_get_proc_handler(receiveSource), "get_status", &cd);
		const uint64_t frames = uint64_t(calldata_int(&cd, "frames"));
		if (frames > receiveFrames) {
			receiveFrames = frames;
			receiveLastFrame = os_gettime_ns();
		}
		calldata_free(&cd);
	}
	if (reason) {
		*reason = why;
	}
	return why == nullptr;
}
