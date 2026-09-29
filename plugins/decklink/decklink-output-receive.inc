// SPDX-License-Identifier: GPL-2.0-or-later
// Included in the existing native output owner translation unit.
bool DeckLinkOutput::BindReceive(obs_source_t *source, bool native)
{
	std::lock_guard<std::recursive_mutex> lock(deviceMutex);
	// A running rendered output may change its health source when the receive
	// reconnects or stops; it plays the program canvas, not the source. Native
	// ownership is fixed at start.
	const bool running = instance || (output && obs_output_active(output));
	if ((running && (native || nativeReceive)) || (native && !source)) {
		return false;
	}
	// Ordinary selection is health/watchdog metadata only. No native ABI or
	// source PCM queue is needed for the stock rendered program/mixer path.
	if (source && native) {
		calldata_t cd;
		calldata_init(&cd);
		bool valid = proc_handler_call(obs_source_get_proc_handler(source), "native422_feed", &cd) &&
			     calldata_int(&cd, "version") == PV_FEED_VERSION;
		calldata_free(&cd);
		if (!valid) {
			return false;
		}
	}
	if (!running) {
		privateMedia.Restore(output);
	}
	obs_source_release(receiveSource);
	receiveSource = obs_source_get_ref(source);
	nativeReceive = native;
	receiveFrames = 0;
	receiveLastFrame = os_gettime_ns();
	return true;
}

bool DeckLinkOutput::PrepareReceive(DeckLinkDeviceMode *mode)
{
	if (!receiveSource || !mode) {
		return false;
	}
	BMDTimeValue d = 0;
	BMDTimeScale s = 0;
	if (!mode->GetFrameRate(&d, &s) || d <= 0 || s <= 0 || d > 10000 || s > 120000) {
		return false;
	}
	return nativeReceive && privateMedia.Open(output, mode->GetWidth(), mode->GetHeight(), uint32_t(s), uint32_t(d));
}

bool DeckLinkOutput::ReceiveHealthy(const char **reason)
{
	const char *why = nullptr;
	std::lock_guard<std::recursive_mutex> lock(deviceMutex);
	if (!instance) {
		why = "the card output is no longer active";
	} else if (GetDevice()->Removed()) {
		why = "the DeckLink device was removed";
	} else if (nativeReceive) {
		if (!receiveSource) {
			why = "no receive source is bound";
		} else if (obs_source_removed(receiveSource)) {
			why = "the receive source was removed";
		} else if (!instance->NativeOutputHealthy()) {
			why = "native 4:2:2 scheduling failed or stalled";
		}
	} else if (receiveSource && !obs_source_removed(receiveSource)) {
		// The rendered output plays the program canvas, not the source: a
		// stalled, ended or reconnecting receive only repeats the last canvas
		// frame, so it never drains the card (stopping the card with frames
		// still outstanding can deadlock inside the DeckLink SDK). Only a newly
		// detected Main422 stream cannot continue through a rendered path:
		// drain and require a new Start/admission, never silently downconvert.
		calldata_t cd;
		calldata_init(&cd);
		bool ok = proc_handler_call(obs_source_get_proc_handler(receiveSource), "get_status", &cd);
		const uint64_t frames = uint64_t(calldata_int(&cd, "frames"));
		if (frames > receiveFrames) {
			receiveFrames = frames;
			receiveLastFrame = os_gettime_ns();
		}
		if (ok && calldata_int(&cd, "native422_frames") != 0) {
			why = "the stream changed to native 4:2:2 and needs a new Start";
		}
		calldata_free(&cd);
	}
	if (reason) {
		*reason = why;
	}
	return why == nullptr;
}
