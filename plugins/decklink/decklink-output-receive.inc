// SPDX-License-Identifier: GPL-2.0-or-later
// Included in the existing native output owner translation unit.
bool DeckLinkOutput::BindReceive(obs_source_t *source, bool native)
{
	std::lock_guard<std::recursive_mutex> lock(deviceMutex);
	if (instance || (output && obs_output_active(output)) || (native && !source)) {
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
	privateMedia.Restore(output);
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

bool DeckLinkOutput::ReceiveHealthy()
{
	std::lock_guard<std::recursive_mutex> lock(deviceMutex);
	if (!receiveSource || obs_source_removed(receiveSource) || !instance ||
	    GetDevice()->Removed()) {
		return false;
	}
	if (nativeReceive) {
		return instance->NativeOutputHealthy();
	}
	// A newly detected Main422 stream cannot continue through a rendered path:
	// drain and require a new Start/admission, never silently downconvert it.
	calldata_t cd;
	calldata_init(&cd);
	bool ok = proc_handler_call(obs_source_get_proc_handler(receiveSource), "get_status", &cd);
	const char *state = calldata_string(&cd, "state");
	const uint64_t frames = uint64_t(calldata_int(&cd, "frames"));
	if (frames > receiveFrames) {
		receiveFrames = frames;
		receiveLastFrame = os_gettime_ns();
	}
	ok = ok && state && strcmp(state, "playing") == 0 && calldata_int(&cd, "native422_frames") == 0 &&
	     os_gettime_ns() - receiveLastFrame < 500000000ULL;
	calldata_free(&cd);
	return ok;
}
