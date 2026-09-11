#pragma once

#include "DecklinkBase.h"
#include "decklink-private-media.hpp"

#include <media-io/video-scaler.h>

class DeckLinkOutput : public DecklinkBase {
protected:
	obs_output_t *output;
	int width;
	int height;
	obs_source_t *receiveSource = nullptr;
	bool nativeReceive = false, receiveFailed = false;
	uint64_t receiveFrames = 0, receiveLastFrame = 0;
	DeckLinkPrivateMedia privateMedia;
	struct pv_feed_request feedIdentity = {};
	std::vector<uint8_t> receiveAudio;
	void DetachReceive();
	void PumpReceiveAudio();

	static void DevicesChanged(void *param, DeckLinkDevice *device, bool added);

public:
	std::string deviceHash;
	bool BindReceive(obs_source_t *source, bool native);
	bool PrepareReceive(DeckLinkDeviceMode *mode);
	bool ReceiveHealthy();
	void ReceiveStats(calldata_t *cd)
	{
		std::lock_guard<std::recursive_mutex> lock(deviceMutex);
		if (instance) {
			instance->NativeStats(cd);
		}
	}
	bool IsNativeReceive() const { return nativeReceive; }
	bool IsReceive() const { return receiveSource != nullptr; }
	long long modeID;
	uint64_t start_timestamp;
	uint32_t audio_samplerate;
	size_t audio_planes;
	size_t audio_size;
	int keyerMode;
	bool force_sdr;

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
