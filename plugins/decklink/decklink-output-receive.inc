// SPDX-License-Identifier: GPL-2.0-or-later
// Included in the existing native output owner translation unit.
bool DeckLinkOutput::BindReceive(obs_source_t *source, bool native)
{
	std::lock_guard<std::recursive_mutex> lock(deviceMutex);
	if (instance || (output && obs_output_active(output)) || (native && !source)) {
		return false;
	}
	if (source) {
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
	privateMedia.Restore(output);
	video_t *renderedVideo = obs_output_video(output);
	if (!nativeReceive && !renderedVideo) return false;
	if (!privateMedia.Open(output, mode->GetWidth(), mode->GetHeight(), uint32_t(s), uint32_t(d))) {
		return false;
	}
	if (!nativeReceive) {
		obs_output_set_media(output, renderedVideo, obs_output_audio(output));
	}
	if (!nativeReceive) {
		feedIdentity = {};
		feedIdentity.size = sizeof(feedIdentity);
		feedIdentity.version = PV_FEED_VERSION;
		feedIdentity.command = PV_FEED_ATTACH;
		feedIdentity.route = PV_FEED_RENDERED;
		calldata_t cd;
		calldata_init(&cd);
		calldata_set_ptr(&cd, "request", &feedIdentity);
		proc_handler_call(obs_source_get_proc_handler(receiveSource), "native422_feed", &cd);
		calldata_free(&cd);
		if (feedIdentity.status != PV_FEED_OK) {
			return false;
		}
		receiveAudio.resize(PV_FEED_MAX_AUDIO_FRAMES * 4);
	}
	receiveFailed = false;
	receiveFrames = 0;
	receiveLastFrame = os_gettime_ns();
	return true;
}

void DeckLinkOutput::DetachReceive()
{
	if (receiveSource && feedIdentity.token) {
		feedIdentity.command = PV_FEED_DETACH;
		calldata_t cd;
		calldata_init(&cd);
		calldata_set_ptr(&cd, "request", &feedIdentity);
		proc_handler_call(obs_source_get_proc_handler(receiveSource), "native422_feed", &cd);
		calldata_free(&cd);
		feedIdentity.token = 0;
	}
}

bool DeckLinkOutput::ReceiveHealthy()
{
	std::lock_guard<std::recursive_mutex> lock(deviceMutex);
	if (!receiveSource || obs_source_removed(receiveSource) || !instance || receiveFailed ||
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

void DeckLinkOutput::PumpReceiveAudio()
{
	if (!receiveSource || nativeReceive || !instance) {
		return;
	}
	for (unsigned i = 0; i < PV_FEED_AUDIO_CAPACITY; i++) {
		struct pv_feed_request r = feedIdentity;
		r.command = PV_FEED_AUDIO;
		r.data = receiveAudio.data();
		r.capacity = receiveAudio.size();
		calldata_t cd;
		calldata_init(&cd);
		calldata_set_ptr(&cd, "request", &r);
		proc_handler_call(obs_source_get_proc_handler(receiveSource), "native422_feed", &cd);
		calldata_free(&cd);
		if (r.status == PV_FEED_EMPTY) {
			return;
		}
		if (r.status != PV_FEED_OK || !r.audio_frames || r.audio_frames > PV_FEED_MAX_AUDIO_FRAMES ||
		    r.bytes != size_t(r.audio_frames) * 4) {
			receiveFailed = true;
			return;
		}
		audio_data pcm = {};
		pcm.data[0] = receiveAudio.data();
		pcm.frames = r.audio_frames;
		pcm.timestamp = r.timestamp_ns;
		// Preserve the ordinary rendered route's first-video trim, but with the
		// feed's explicit 48k/S16/stereo layout, not global audio format guesses.
		if (pcm.timestamp < start_timestamp) {
			uint64_t skip = util_mul_div64(start_timestamp - pcm.timestamp, 48000, 1000000000ULL);
			if (skip >= pcm.frames) {
				continue;
			}
			pcm.data[0] += skip * 4;
			pcm.frames -= uint32_t(skip);
		}
		instance->WriteAudio(&pcm);
	}
}
