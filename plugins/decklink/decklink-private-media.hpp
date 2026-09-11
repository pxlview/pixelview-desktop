// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <obs.h>

// Real, private libobs media endpoints for native output activity/accounting.
// Direct v210/PCM travels through the source ABI, never through these endpoints
// (and never masquerades as BGRA). These idle endpoints ensure normal raw-output
// begin/end hooks cannot attach the global canvas/audio mixer. The audio input
// deliberately reports no data; it does not synthesize or schedule silence.
// Close only after libobs has disconnected capture (output destruction joins it).
class DeckLinkPrivateMedia {
	video_t *video = nullptr;
	audio_t *audio = nullptr;
	video_t *externalVideo = nullptr;
	audio_t *externalAudio = nullptr;
	static bool Input(void *, uint64_t, uint64_t, uint64_t *, uint32_t, audio_output_data *) { return false; }

public:
	~DeckLinkPrivateMedia() { Close(); }
	bool Open(obs_output_t *output, uint32_t width, uint32_t height, uint32_t num, uint32_t den)
	{
		if (!output || obs_output_active(output) || !width || width > 1920 || !height || height > 1080 ||
		    !num || !den) {
			return false;
		}
		Restore(output);
		externalVideo = obs_output_video(output);
		externalAudio = obs_output_audio(output);
		Close();
		video_output_info vi = {};
		vi.name = "DeckLink private activity";
		vi.format = VIDEO_FORMAT_BGRA;
		vi.width = width;
		vi.height = height;
		vi.fps_num = num;
		vi.fps_den = den;
		vi.cache_size = 1;
		vi.colorspace = VIDEO_CS_709;
		vi.range = VIDEO_RANGE_FULL;
		audio_output_info ai = {};
		ai.name = "DeckLink private activity";
		ai.samples_per_sec = 48000;
		ai.format = AUDIO_FORMAT_FLOAT_PLANAR;
		ai.speakers = SPEAKERS_STEREO;
		ai.input_callback = Input;
		if (video_output_open(&video, &vi) != VIDEO_OUTPUT_SUCCESS ||
		    audio_output_open(&audio, &ai) != AUDIO_OUTPUT_SUCCESS) {
			Close();
			return false;
		}
		obs_output_set_media(output, video, audio);
		return obs_output_video(output) == video && obs_output_audio(output) == audio;
	}
	// External endpoints remain caller-owned and must outlive the retained output.
	// Never save one of our own endpoints as an external rendered queue.
	void Restore(obs_output_t *output)
	{
		if (!output || obs_output_active(output)) return;
		auto *v = obs_output_video(output);
		auto *a = obs_output_audio(output);
		obs_output_set_media(output, video && v == video ? externalVideo : v,
				     audio && a == audio ? externalAudio : a);
	}
	void Close()
	{
		video_output_close(video);
		audio_output_close(audio);
		video = nullptr;
		audio = nullptr;
	}
};
