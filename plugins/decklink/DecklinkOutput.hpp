#pragma once

#include "DecklinkBase.h"

#include <media-io/video-scaler.h>

class DeckLinkOutput : public DecklinkBase {
protected:
	obs_output_t *output;
	int width;
	int height;
	obs_source_t *receiveSource = nullptr;
	uint64_t receiveFrames = 0, receiveLastFrame = 0;

	static void DevicesChanged(void *param, DeckLinkDevice *device, bool added);

public:
	std::string deviceHash;
	// Selects (or clears) the receive source whose status the watchdog follows.
	bool BindReceive(obs_source_t *source);
	// A false result names the cause in *reason (static storage, UI-thread use).
	bool ReceiveHealthy(const char **reason = nullptr);
	long long modeID = 0;
	uint64_t start_timestamp;
	uint32_t audio_samplerate;
	size_t audio_planes;
	size_t audio_size;
	int keyerMode = 0;
	bool force_sdr = false;
	// Pixelview: the stream and canvas are always limited range; this only chooses
	// the levels of the Y'CbCr put on SDI, to match what the monitor expects.
	bool full_range = false;

	DeckLinkOutput(obs_output_t *output, DeckLinkDeviceDiscovery *discovery);
	virtual ~DeckLinkOutput(void);
	obs_output_t *GetOutput(void) const;
	bool Activate(DeckLinkDevice *device, long long modeId) override;
	void Deactivate() override;
	void UpdateVideoFrame(video_data *pData);
	void WriteAudio(audio_data *frames);
	void SetSize(int width, int height);
	int GetWidth();
	int GetHeight();
};
