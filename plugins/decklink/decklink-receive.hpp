// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "platform.hpp"
#include "../pixelview-whep/source-feed.h"
#include <obs.h>
#include <util/platform.h>
#include <util/util_uint64.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>

// Scheduler only: the existing DeviceInstance acquires/owns the device. This
// object never discovers or opens a second card. SDK callbacks hold references
// to this self-contained gate, NOT a borrowed DeviceInstance/Output pointer.
class DeckLinkReceive final : public IDeckLinkVideoOutputCallback {
	std::atomic<ULONG> refs{1};
	std::recursive_mutex gate;
	ComPtr<IDeckLinkOutput> card;
	obs_source_t *source = nullptr;
	struct pv_feed_request identity = {}, pending = {}, audio = {};
	std::vector<uint8_t> videoBytes, activeBytes, audioBytes;
	uint64_t epoch = 0, lastVideo = 0, lastCallback = 0, audioEnd = 0;
	uint32_t audioOffset = 0, width = 0, height = 0, row = 0;
	int64_t duration = 0, scale = 0, slot = 0, preroll = 0;
	bool attached = false, videoEnabled = false, audioEnabled = false;
	std::atomic<bool> running{false};
	bool callbackSet = false, playbackAttempted = false;
	std::atomic<bool> failed{false};
	uint64_t completed = 0, repeated = 0, dropped = 0, late = 0, audioEmpty = 0, partialWrites = 0;

	bool Request(struct pv_feed_request &r, uint32_t command, void *data = nullptr, size_t capacity = 0)
	{
		r = identity;
		r.size = sizeof(r);
		r.version = PV_FEED_VERSION;
		r.command = command;
		r.data = data;
		r.capacity = capacity;
		r.status = PV_FEED_INVALID;
		calldata_t cd;
		calldata_init(&cd);
		calldata_set_ptr(&cd, "request", &r);
		bool ok = source && !obs_source_removed(source) &&
			  proc_handler_call(obs_source_get_proc_handler(source), "native422_feed", &cd);
		ok = ok && calldata_int(&cd, "version") == PV_FEED_VERSION;
		calldata_free(&cd);
		if (!ok) {
			r.status = PV_FEED_INVALID;
		}
		return r.status == PV_FEED_OK || r.status == PV_FEED_EMPTY;
	}
	bool ReadVideo()
	{
		if (!Request(pending, PV_FEED_VIDEO, videoBytes.data(), videoBytes.size())) {
			return false;
		}
		if (pending.status == PV_FEED_EMPTY) {
			return true;
		}
		return pending.width == width && pending.height == height && pending.stride == row &&
		       pending.bytes == size_t(row) * height && pending.fps_num && pending.fps_den &&
		       uint64_t(pending.fps_num) * duration == uint64_t(pending.fps_den) * scale &&
		       (!epoch || (pending.timestamp_ns >= epoch && pending.timestamp_ns - epoch <= 86400000000000ULL));
	}
	uint64_t SlotNS(int64_t n) const
	{
		return util_mul_div64(uint64_t(n), uint64_t(duration) * 1000000000ULL, scale);
	}
	uint64_t AudioTime(uint64_t timestamp) const
	{
		return util_mul_div64(timestamp - epoch, 48000, 1000000000ULL) +
		       util_mul_div64(preroll * duration, 48000, scale);
	}
	bool ScheduleAudio()
	{
		uint32_t buffered = 0;
		if (card->GetBufferedAudioSampleFrameCount(&buffered) != S_OK) {
			return false;
		}
		if (!buffered) ++audioEmpty;
		if (buffered > 24000) {
			// Hardware lookahead must not suspend expiry of our pending packet.
			BMDTimeValue played = 0;
			double speed = 0;
			return card->GetScheduledStreamTime(48000, &played, &speed) == S_OK && played >= 0 &&
			       speed == 1.0 && (!audio.audio_frames ||
			       AudioTime(audio.timestamp_ns) + audioOffset >= uint64_t(played));
		}
		for (unsigned packets = 0; packets < PV_FEED_AUDIO_CAPACITY; ++packets) {
			if (!audio.audio_frames) {
				if (!Request(audio, PV_FEED_AUDIO, audioBytes.data(), audioBytes.size())) {
					return false;
				}
				if (audio.status == PV_FEED_EMPTY) {
					return true;
				}
				if (!audio.audio_frames || audio.audio_frames > PV_FEED_MAX_AUDIO_FRAMES ||
				    audio.bytes != size_t(audio.audio_frames) * 4) {
					return false;
				}
				audioOffset = 0;
				if (audio.timestamp_ns < epoch) {
					audioOffset = uint32_t(std::min<uint64_t>(
						audio.audio_frames,
						util_mul_div64(epoch - audio.timestamp_ns, 48000, 1000000000ULL)));
					if (audioOffset == audio.audio_frames) {
						audio.audio_frames = 0;
						continue;
					}
					audio.audio_frames -= audioOffset;
					memmove(audioBytes.data(), audioBytes.data() + size_t(audioOffset) * 4,
						size_t(audio.audio_frames) * 4);
					audioOffset = 0;
					audio.timestamp_ns = epoch;
					// Normalize storage and timestamp together so partial-write offsets do
					// not count the removed prefix a second time.
				}
				if (audio.timestamp_ns - epoch > 86400000000000ULL) {
					return false;
				}
				uint64_t start = AudioTime(audio.timestamp_ns);
				if (start < audioEnd) {
					audioOffset += uint32_t(
						std::min<uint64_t>(audio.audio_frames - audioOffset, audioEnd - start));
				}
			}
			if (audioOffset >= audio.audio_frames) {
				audio.audio_frames = 0;
				continue;
			}
			const uint64_t t = AudioTime(audio.timestamp_ns) + audioOffset;
			// Fail-stop rather than replay expired PCM after SDK backpressure.
			// Query the running card in the audio timescale on EVERY retry; the
			// last accepted audio end is not a playback clock (zero writes stall it).
			BMDTimeValue played = 0;
			double speed = 0;
			if (card->GetScheduledStreamTime(48000, &played, &speed) != S_OK || played < 0 ||
			    speed != 1.0 || t < uint64_t(played)) {
				return false;
			}
			// Reject gross forward discontinuities instead of buffering seconds of media.
			if (t > util_mul_div64(slot * duration, 48000, scale) + 24000) {
				return false;
			}
			uint32_t written = 0;
			if (card->ScheduleAudioSamples(audioBytes.data() + size_t(audioOffset) * 4,
						       audio.audio_frames - audioOffset, t, 48000, &written) != S_OK ||
			    written > audio.audio_frames - audioOffset) {
				return false;
			}
			audioOffset += written;
			audioEnd = t + written;
			if (audioOffset < audio.audio_frames) {
				++partialWrites;
				return true;
			} // retry including zero writes
			audio.audio_frames = 0;
		}
		return true;
	}
	bool Fill(IDeckLinkVideoFrame *frame)
	{
		if (frame->GetWidth() != width || frame->GetHeight() != height || frame->GetRowBytes() != row ||
		    frame->GetPixelFormat() != bmdFormat10BitYUV) {
			return false;
		}
		const uint64_t target = epoch + SlotNS(slot - preroll);
		bool changed = false;
		for (unsigned n = 0; n <= PV_FEED_VIDEO_CAPACITY; n++) {
			if (pending.status != PV_FEED_OK && !ReadVideo()) {
				return false;
			}
			if (pending.status == PV_FEED_EMPTY || pending.timestamp_ns > target) {
				break;
			}
			if (changed) {
				++dropped;
			}
			activeBytes = videoBytes;
			changed = true;
			lastVideo = os_gettime_ns();
			pending.status = PV_FEED_EMPTY;
		}
		if (!changed) {
			++repeated;
		}
		if (os_gettime_ns() - lastVideo > 500000000ULL) {
			return false;
		}
		void *bytes = nullptr;
		if (frame->GetBytes(&bytes) != S_OK || !bytes || activeBytes.size() != size_t(row) * height) {
			return false;
		}
		memcpy(bytes, activeBytes.data(), activeBytes.size());
		return true;
	}
	static bool Black(IDeckLinkVideoFrame *frame)
	{
		void *p = nullptr;
		if (frame->GetBytes(&p) != S_OK || !p) {
			return false;
		}
		const uint32_t words[4] = {512u | (64u << 10) | (512u << 20), 64u | (512u << 10) | (64u << 20),
					   512u | (64u << 10) | (512u << 20), 64u | (512u << 10) | (64u << 20)};
		for (long y = 0; y < frame->GetHeight(); y++) {
			for (long x = 0; x < frame->GetRowBytes(); x += 16) {
				memcpy(static_cast<uint8_t *>(p) + y * frame->GetRowBytes() + x, words, 16);
			}
		}
		return true;
	}

public:
	~DeckLinkReceive() { Stop(); }
	HRESULT QueryInterface(REFIID iid, void **p) override
	{
		if (!p) return E_INVALIDARG;
		*p = nullptr;
		const CFUUIDBytes unknown = CFUUIDGetUUIDBytes(IUnknownUUID);
		if (!memcmp(&iid, &unknown, sizeof(iid)) ||
		    !memcmp(&iid, &IID_IDeckLinkVideoOutputCallback, sizeof(iid))) {
			*p = static_cast<IDeckLinkVideoOutputCallback *>(this);
			AddRef();
			return S_OK;
		}
		return E_NOINTERFACE;
	}
	ULONG AddRef() override { return ++refs; }
	ULONG Release() override
	{
		auto n = --refs;
		if (!n) {
			delete this;
		}
		return n;
	}
	bool Start(IDeckLinkOutput *output, DeckLinkDeviceMode *mode, obs_source_t *selected, int64_t minimum,
		   IDeckLinkKeyer *keyer)
	{
		if (!output || !mode || !selected || minimum < 0 || minimum > 30) {
			return false;
		}
		source = obs_source_get_ref(selected);
		card = output;
		width = mode->GetWidth();
		height = mode->GetHeight();
		row = ((width + 47) / 48) * 128;
		if (!width || width > 1920 || (width & 1) || !height || height > 1080 ||
		    !mode->GetFrameRate(&duration, &scale) || duration <= 0 || scale <= 0 || scale > 120000 ||
		    duration > 10000 || scale > duration * 60) {
			return false;
		}
		preroll = std::max<int64_t>(3, minimum);
		slot = preroll;
		identity.route = PV_FEED_NATIVE;
		if (!Request(identity, PV_FEED_ATTACH) || identity.status != PV_FEED_OK) {
			return false;
		}
		attached = true;
		videoBytes.resize(size_t(row) * height);
		activeBytes.resize(size_t(row) * height);
		audioBytes.resize(PV_FEED_MAX_AUDIO_FRAMES * 4);
		const uint64_t deadline = os_gettime_ns() + 250000000ULL;
		do {
			if (!ReadVideo()) {
				return false;
			}
			if (pending.status == PV_FEED_OK) {
				break;
			}
			os_sleep_ms(2);
		} while (os_gettime_ns() < deadline);
		if (pending.status != PV_FEED_OK) {
			return false;
		}
		epoch = pending.timestamp_ns;
		lastVideo = lastCallback = os_gettime_ns();
		ComPtr<IDeckLinkConfiguration> config;
		int64_t connector = 0;
		BMDDisplayMode actual = bmdModeUnknown;
		decklink_bool_t supported = false;
		if (card->QueryInterface(IID_IDeckLinkConfiguration, (void **)&config) != S_OK ||
		    config->GetInt(bmdDeckLinkConfigVideoOutputConnection, &connector) != S_OK ||
		    connector != bmdVideoConnectionSDI) {
			return false;
		}
		if (card->DoesSupportVideoMode(BMDVideoConnection(connector), mode->GetDisplayMode(), bmdFormat10BitYUV,
					       bmdNoVideoOutputConversion, bmdSupportedVideoModeDefault, &actual,
					       &supported) != S_OK ||
		    !supported || actual != mode->GetDisplayMode()) {
			return false;
		}
		ComPtr<IDeckLinkDisplayMode> actualMode;
		if (card->GetDisplayMode(actual, &actualMode) != S_OK || !actualMode ||
		    actualMode->GetFieldDominance() != bmdProgressiveFrame) {
			return false;
		}
		int64_t conversion = 0;
		if (config->SetInt(bmdDeckLinkConfigVideoOutputConversionMode, bmdNoVideoOutputConversion) != S_OK ||
		    config->GetInt(bmdDeckLinkConfigVideoOutputConversionMode, &conversion) != S_OK ||
		    conversion != bmdNoVideoOutputConversion) {
			return false;
		}
		if (keyer && keyer->Disable() != S_OK) {
			return false;
		}
		if (card->EnableVideoOutput(actual, bmdVideoOutputFlagDefault) != S_OK) {
			return false;
		}
		videoEnabled = true;
		if (card->EnableAudioOutput(bmdAudioSampleRate48kHz, bmdAudioSampleType16bitInteger, 2,
					    bmdAudioOutputStreamTimestamped) != S_OK) {
			return false;
		}
		audioEnabled = true;
		if (card->SetScheduledFrameCompletionCallback(this) != S_OK) {
			return false;
		}
		callbackSet = true;
		if (card->BeginAudioPreroll() != S_OK) {
			return false;
		}
		for (int64_t i = 0; i < preroll; i++) {
			ComPtr<IDeckLinkMutableVideoFrame> f;
			if (card->CreateVideoFrame(width, height, row, bmdFormat10BitYUV, bmdFrameFlagDefault, &f) !=
				    S_OK ||
			    !f || f->GetWidth() != width || f->GetHeight() != height || f->GetRowBytes() != row ||
			    f->GetPixelFormat() != bmdFormat10BitYUV || !Black(f)) {
				return false;
			}
			if (card->ScheduleVideoFrame(f, i * duration, duration, scale) != S_OK) {
				return false;
			}
		}
		const uint64_t silenceFrames = util_mul_div64(preroll * duration, 48000, scale);
		if (silenceFrames > 48000) {
			return false;
		}
		std::vector<int16_t> silence(size_t(silenceFrames) * 2, 0);
		uint32_t offset = 0;
		for (unsigned i = 0; offset < silenceFrames && i < 64; i++) {
			uint32_t written = 0;
			if (card->ScheduleAudioSamples(silence.data() + offset * 2, uint32_t(silenceFrames) - offset,
						       offset, 48000, &written) != S_OK ||
			    !written || written > silenceFrames - offset) {
				return false;
			}
			offset += written;
		}
		if (offset != silenceFrames || card->EndAudioPreroll() != S_OK) {
			return false;
		}
		audioEnd = silenceFrames;
		running = true;
		playbackAttempted = true;
		if (card->StartScheduledPlayback(0, scale, 1.0) != S_OK) {
			running = false;
			return false;
		}
		return true;
	}
	bool Stop()
	{
		bool ok = true;
		// Waiting for the gate drains every callback that can touch source/media.
		// Never hold it across SDK unregister/stop: a driver may join its callback.
		{
			std::lock_guard<std::recursive_mutex> lock(gate);
			running = false;
		}
		if (card) {
			if (callbackSet) {
				ok = (card->SetScheduledFrameCompletionCallback(nullptr) == S_OK) && ok;
			}
			if (playbackAttempted || videoEnabled) {
				ok = (card->StopScheduledPlayback(0, nullptr, scale ? scale : 1) == S_OK) && ok;
			}
			if (audioEnabled) {
				ok = (card->FlushBufferedAudioSamples() == S_OK) && ok;
				ok = (card->DisableAudioOutput() == S_OK) && ok;
			}
			if (videoEnabled) {
				ok = (card->DisableVideoOutput() == S_OK) && ok;
			}
		}
		callbackSet = playbackAttempted = videoEnabled = audioEnabled = false;
		obs_source_t *oldSource = nullptr;
		{
			std::lock_guard<std::recursive_mutex> lock(gate);
			if (attached) {
				struct pv_feed_request r;
				Request(r, PV_FEED_DETACH);
				attached = false;
			}
			oldSource = source; source = nullptr;
		}
		// Releasing external objects may join threads or dispatch lifecycle code.
		// Late callbacks see running=false without waiting on a driver-held gate.
		obs_source_release(oldSource);
		card.Clear();
		return ok;
	}
	bool Healthy()
	{
		std::lock_guard<std::recursive_mutex> lock(gate);
		if (!running || failed || os_gettime_ns() - lastCallback >= 500000000ULL) {
			return false;
		}
		struct pv_feed_request probe;
		Request(probe, PV_FEED_VIDEO); // non-consuming generation/loss check
		return probe.status == PV_FEED_EMPTY || probe.status == PV_FEED_BUFFER_SMALL;
	}
	void Stats(calldata_t *cd)
	{
		std::lock_guard<std::recursive_mutex> lock(gate);
		calldata_set_int(cd, "completed", completed);
		calldata_set_int(cd, "repeats", repeated);
		calldata_set_int(cd, "dropped", dropped);
		calldata_set_int(cd, "late", late);
		calldata_set_int(cd, "audio_empty_polls", audioEmpty);
		calldata_set_int(cd, "partial_writes", partialWrites);
	}
	HRESULT ScheduledFrameCompleted(IDeckLinkVideoFrame *frame, BMDOutputFrameCompletionResult result) override
	{
		std::lock_guard<std::recursive_mutex> lock(gate);
		if (!running || failed) {
			return S_OK;
		}
		if (!Healthy()) {
			failed = true;
			return S_OK;
		}
		lastCallback = os_gettime_ns();
		++completed;
		if (result == bmdOutputFrameDisplayedLate) {
			++late;
		}
		if (result == bmdOutputFrameDropped || result == bmdOutputFrameFlushed || !Fill(frame) ||
		    !ScheduleAudio() || card->ScheduleVideoFrame(frame, slot * duration, duration, scale) != S_OK) {
			failed = true;
			return S_OK;
		}
		++slot;
		return S_OK;
	}
	HRESULT ScheduledPlaybackHasStopped() override
	{
		std::lock_guard<std::recursive_mutex> lock(gate);
		if (running) {
			failed = true;
		}
		return S_OK;
	}
};
