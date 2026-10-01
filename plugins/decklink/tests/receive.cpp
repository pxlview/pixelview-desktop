// SPDX-License-Identifier: GPL-2.0-or-later
#include "decklink-device-instance.hpp"
#include "DecklinkOutput.hpp"
#include "decklink-devices.hpp"
#include "sdk-stubs.hpp"
#include "rendered-media.inc"
#include <util/platform.h>
#include <media-io/video-frame.h>
#include <cassert>
#include <thread>
#include <mutex>
#include <cstdio>
#include <deque>
#include <cstring>
extern "C" const char *obs_module_text(const char *text)
{
	return text;
}
// No installed SDK dispatcher or physical discovery is linked.
extern "C" IDeckLinkDiscovery *CreateDeckLinkDiscoveryInstance()
{
	return nullptr;
}
extern "C" IDeckLinkVideoConversion *CreateVideoConversionInstance()
{
	return nullptr;
}
extern "C" IDeckLinkIterator *CreateDeckLinkIteratorInstance()
{
	return nullptr;
}
struct Frame : StubIDeckLinkMutableVideoFrame {
	long w, h, row;
	BMDPixelFormat format = bmdFormat10BitYUV;
	std::vector<uint8_t> bytes;
	unsigned refs = 1;
	Frame(long w, long h, long row) : w(w), h(h), row(row), bytes(row * h, 0xcc) {}
	ULONG AddRef() override { return ++refs; }
	ULONG Release() override
	{
		auto r = --refs;
		if (!r) {
			delete this;
		}
		return r;
	}
	long GetWidth() override { return w; }
	long GetHeight() override { return h; }
	long GetRowBytes() override { return row; }
	BMDPixelFormat GetPixelFormat() override { return format; }
	HRESULT GetBytes(void **p) override
	{
		*p = bytes.data();
		return S_OK;
	}
};
struct Mode : StubIDeckLinkDisplayMode {
	long width = 48, height = 2;
 BMDTimeValue duration=1001; BMDTimeScale scale=30000;
 BMDDisplayMode identifier=bmdModeHD1080p2997;
	long GetWidth() override { return width; }
	long GetHeight() override { return height; }
	BMDDisplayMode GetDisplayMode() override { return identifier; }
	HRESULT GetFrameRate(BMDTimeValue *d, BMDTimeScale *s) override
	{
		*d = duration;
		*s = scale;
		return S_OK;
	}
	BMDFieldDominance GetFieldDominance() override { return bmdProgressiveFrame; }
	HRESULT GetName(CFStringRef *s) override
	{
		*s = CFSTR("synthetic");
		CFRetain(*s);
		return S_OK;
	}
};
struct ModeIterator : StubIDeckLinkDisplayModeIterator {
	IDeckLinkDisplayMode *mode;
	bool done = false;
	explicit ModeIterator(IDeckLinkDisplayMode *m) : mode(m) {}
	HRESULT Next(IDeckLinkDisplayMode **p) override
	{
		*p = done ? nullptr : mode;
		done = true;
		return *p ? S_OK : S_FALSE;
	}
};
struct Attributes : StubIDeckLinkProfileAttributes {
	HRESULT GetFlag(BMDDeckLinkAttributeID, bool *p) override
	{
		*p = false;
		return S_OK;
	}
};
struct Keyer : StubIDeckLinkKeyer {
	bool enabled = true;
	HRESULT Disable() override
	{
		enabled = false;
		return S_OK;
	}
};
struct Config : StubIDeckLinkConfiguration {
	int64_t conversion = 1234;
	HRESULT SetInt(BMDDeckLinkConfigurationID id, int64_t v) override
	{
		if (id != bmdDeckLinkConfigVideoOutputConversionMode) {
			return E_FAIL;
		}
		conversion = v;
		return S_OK;
	}
	HRESULT GetInt(BMDDeckLinkConfigurationID id, int64_t *v) override
	{
		if (id == bmdDeckLinkConfigVideoOutputConversionMode) {
			*v = conversion;
			return S_OK;
		}
		if (id != bmdDeckLinkConfigVideoOutputConnection) {
			return E_FAIL;
		}
		*v = bmdVideoConnectionSDI;
		return S_OK;
	}
};
struct Card : StubIDeckLinkOutput {
	Config config;
	Mode mode;
	ModeIterator iterator{&mode};
	IDeckLinkVideoOutputCallback *cb = nullptr;
	HRESULT GetDisplayModeIterator(IDeckLinkDisplayModeIterator **p) override
	{
		iterator.done = false;
		*p = &iterator;
		return S_OK;
	}
	HRESULT GetDisplayMode(BMDDisplayMode, IDeckLinkDisplayMode **p) override
	{
		*p = &mode;
		return S_OK;
	}
	struct V {
		IDeckLinkVideoFrame *f;
		int64_t t, d, s;
	};
	std::deque<V> queued;
	std::atomic<unsigned> syncWrites{0};
	std::mutex syncMutex;
	std::vector<int16_t> syncPCM;
	HRESULT WriteAudioSamplesSync(void *p, uint32_t n, uint32_t *written) override
	{
		std::lock_guard<std::mutex> lock(syncMutex);
		++syncWrites; *written=n;
		syncPCM.assign(static_cast<int16_t *>(p), static_cast<int16_t *>(p)+n*2);
		return S_OK;
	}
	bool video = false, sound = false, started = false;
	int calls = 0, failAt = 0;
	bool support = true;
	bool ok() { return ++calls != failAt; }
	HRESULT QueryInterface(REFIID id, void **p) override
	{
		if (!memcmp(&id, &IID_IDeckLinkConfiguration, sizeof(id))) {
			*p = &config;
			return S_OK;
		}
		*p = nullptr;
		return E_NOINTERFACE;
	}
	HRESULT DoesSupportVideoMode(BMDVideoConnection c, BMDDisplayMode m, BMDPixelFormat f,
				     BMDVideoOutputConversionMode conv, BMDSupportedVideoModeFlags,
				     BMDDisplayMode *actual, bool *supported) override
	{
		assert(c == bmdVideoConnectionSDI && f == bmdFormat10BitYUV && conv == bmdNoVideoOutputConversion);
		*actual = m;
		*supported = support;
		return ok() ? S_OK : E_FAIL;
	}
	HRESULT EnableVideoOutput(BMDDisplayMode, BMDVideoOutputFlags) override
	{
		if (!ok()) {
			return E_FAIL;
		}
		video = true;
		return S_OK;
	}
	HRESULT EnableAudioOutput(BMDAudioSampleRate r, BMDAudioSampleType t, uint32_t ch,
				  BMDAudioOutputStreamType s) override
	{
		assert(r == bmdAudioSampleRate48kHz && t == bmdAudioSampleType16bitInteger && ch == 2 &&
		       s == bmdAudioOutputStreamTimestamped);
		if (!ok()) {
			return E_FAIL;
		}
		sound = true;
		return S_OK;
	}
	HRESULT DisableVideoOutput() override
	{
		video = false;
		while (!queued.empty()) {
			queued.front().f->Release();
			queued.pop_front();
		}
		return S_OK;
	}
	HRESULT DisableAudioOutput() override
	{
		sound = false;
		return S_OK;
	}
	HRESULT CreateVideoFrame(int32_t w, int32_t h, int32_t row, BMDPixelFormat fmt, BMDFrameFlags,
				 IDeckLinkMutableVideoFrame **f) override
	{
		assert(fmt == bmdFormat10BitYUV || fmt == bmdFormat8BitBGRA);
		if (!ok()) {
			return E_FAIL;
		}
		auto *frame = new Frame(w, h, row);
		frame->format = fmt;
		*f = frame;
		return S_OK;
	}
	HRESULT SetScheduledFrameCompletionCallback(IDeckLinkVideoOutputCallback *p) override
	{
		if (p && !ok()) {
			return E_FAIL;
		}
		if (p) {
			p->AddRef();
		}
		if (cb) {
			cb->Release();
		}
		cb = p;
		return S_OK;
	}
	HRESULT ScheduleVideoFrame(IDeckLinkVideoFrame *f, BMDTimeValue t, BMDTimeValue d, BMDTimeScale s) override
	{
		if (!ok()) {
			return E_FAIL;
		}
		f->AddRef();
		queued.push_back({f, t, d, s});
		return S_OK;
	}
	HRESULT StartScheduledPlayback(BMDTimeValue, BMDTimeScale, double) override
	{
		if (!ok()) {
			return E_FAIL;
		}
		started = true;
		return S_OK;
	}
	HRESULT StopScheduledPlayback(BMDTimeValue, BMDTimeValue *, BMDTimeScale) override
	{
		started = false;
		return S_OK;
	}
	void complete()
	{
		assert(cb && !queued.empty());
		auto v = queued.front();
		queued.pop_front();
		cb->ScheduledFrameCompleted(v.f, bmdOutputFrameCompleted);
		v.f->Release();
	}
	~Card() { assert(!video && !sound && !started && !cb && queued.empty()); }
};
struct Device : StubIDeckLink {
	Card card;
	Attributes attributes;
	Keyer keyer;
	HRESULT GetModelName(CFStringRef *p) override
	{
		*p = CFSTR("Fake");
		CFRetain(*p);
		return S_OK;
	}
	HRESULT GetDisplayName(CFStringRef *p) override { return GetModelName(p); }
	HRESULT QueryInterface(REFIID id, void **p) override
	{
		if (!memcmp(&id, &IID_IDeckLinkKeyer, sizeof(id))) {
			*p = &keyer;
			return S_OK;
		}
		if (!memcmp(&id, &IID_IDeckLinkProfileAttributes, sizeof(id))) {
			*p = &attributes;
			return S_OK;
		}
		if (!memcmp(&id, &IID_IDeckLinkOutput, sizeof(id))) {
			*p = &card;
			return S_OK;
		}
		*p = nullptr;
		return E_NOINTERFACE;
	}
};
static void *create(obs_data_t *, obs_source_t *s)
{
	return s;
}
static void destroy(void *) {}
static const char *name(void *)
{
	return "synthetic receive source";
}
static void stop(obs_output_t *output)
{
	obs_output_stop(output);
	for (int i = 0; i < 100 && obs_output_active(output); ++i) {
		os_sleep_ms(2);
	}
	assert(!obs_output_active(output));
}
static bool healthy(obs_output_t *output, const char *expected = nullptr)
{
	calldata_t status;
	calldata_init(&status);
	assert(proc_handler_call(obs_output_get_proc_handler(output), "receive_status", &status));
	const bool ok = calldata_bool(&status, "healthy");
	const char *reason = calldata_string(&status, "reason");
	assert(ok ? !*reason : (*reason && (!expected || strstr(reason, expected))));
	calldata_free(&status);
	return ok;
}
static bool bind(obs_output_t *output, obs_source_t *source)
{
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_ptr(&cd, "source", source);
	const bool bound = proc_handler_call(obs_output_get_proc_handler(output), "bind_receive", &cd) &&
			   calldata_bool(&cd, "bound");
	calldata_free(&cd);
	return bound;
}
extern obs_output_info create_decklink_output_info();
int main(int argc, char **argv)
{
	assert(argc == 3);
	assert(obs_startup("en-US", nullptr, nullptr));
	Device sdk;
	DeckLinkDeviceDiscovery discovery;
	DeckLinkOutput selected(nullptr, &discovery);
	DeckLinkDevice device(&sdk);
	Mode sdkMode;
	DeckLinkDeviceMode mode(&sdkMode, 1);
	// The rendered owner: v210 black preroll, padded-row safety, late callbacks.
	selected.SetSize(48, 2);
	DeckLinkDeviceInstance rendered(&selected, &device);
	assert(rendered.StartOutput(&mode));
	assert(sdk.card.queued.size() == 3 && sdk.card.started);
	for (auto &v : sdk.card.queued) {
		assert(v.s == 30000 && v.d == 1001);
		void *b;
		v.f->GetBytes(&b);
		// 10-bit 4:2:2 Y'CbCr: 48 pixels are 128 bytes per row, and black is
		// Y'=64 with Cb=Cr=512 in every word, not zero bytes.
		assert(v.f->GetRowBytes() == 128 && v.f->GetPixelFormat() == bmdFormat10BitYUV);
		const uint32_t chroma = 512u | 64u << 10 | 512u << 20, luma = 64u | 512u << 10 | 64u << 20;
		for (unsigned word = 0; word < 64; word++) {
			uint32_t value;
			memcpy(&value, (uint8_t *)b + word * 4, 4);
			assert(value == (word % 2 ? luma : chroma));
		}
	}
	// Both directions of one-device capture/output exclusion, without a driver.
	DeckLinkDeviceInstance competitor(&selected, &device);
	assert(!competitor.StartOutput(&mode));
	assert(!device.TryAcquire(&competitor));
	uint8_t padded[400];
	memset(padded, 0xcd, sizeof(padded));
	memset(padded, 0x31, 128);
	memset(padded + 200, 0x72, 128);
	video_data pixels = {};
	pixels.data[0] = padded;
	pixels.linesize[0] = 200;
	rendered.UpdateVideoFrame(&pixels);
	sdk.card.complete();
	assert(sdk.card.queued.back().t == 3003);
	void *renderedBytes;
	sdk.card.queued.back().f->GetBytes(&renderedBytes);
	assert(((uint8_t *)renderedBytes)[0] == 0x31 && ((uint8_t *)renderedBytes)[127] == 0x31 &&
	       ((uint8_t *)renderedBytes)[128] == 0x72 && ((uint8_t *)renderedBytes)[255] == 0x72);
	auto *renderedCallback = sdk.card.cb;
	renderedCallback->AddRef();
	rendered.StopOutput();
	assert(!sdk.card.cb);
	// A retained SDK callback may arrive after Stop: it sees a detached owner.
	renderedCallback->ScheduledFrameCompleted(nullptr, bmdOutputFrameFlushed);
	renderedCallback->Release();
	assert(device.TryAcquire(&competitor));
	assert(!rendered.StartOutput(&mode));
	device.ReleaseOwner(&competitor);
	// Every fallible startup boundary rolls back the owner; a retry then works.
	for (int fail = 1; fail <= 10; fail++) {
		sdk.card.calls = 0;
		sdk.card.failAt = fail;
		assert(!rendered.StartOutput(&mode));
		assert(!sdk.card.video && !sdk.card.sound && !sdk.card.started && !sdk.card.cb &&
		       sdk.card.queued.empty());
	}
	sdk.card.failAt = 0;
	assert(rendered.StartOutput(&mode));
	rendered.StopOutput();
	puts("rendered DeckLink owner: preroll, padded rows, exclusion, rollback, restart and late callback PASS");
	// The registered output with the UI's caller-owned rendered queue.
	obs_audio_info ai = {48000, SPEAKERS_STEREO};
	assert(obs_reset_audio(&ai));
	obs_add_data_path(argv[2]);
	obs_video_info vi = {};
	vi.graphics_module = argv[1];
	vi.fps_num = 30000;
	vi.fps_den = 1001;
	vi.base_width = vi.output_width = 32;
	vi.base_height = vi.output_height = 32;
	vi.output_format = VIDEO_FORMAT_BGRA;
	vi.colorspace = VIDEO_CS_709;
	vi.range = VIDEO_RANGE_PARTIAL;
	assert(obs_reset_video(&vi) == OBS_VIDEO_SUCCESS);
	deviceEnum = &discovery;
	discovery.DeckLinkDeviceArrived(&sdk);
	auto outputInfo = create_decklink_output_info();
	obs_register_output(&outputInfo);
	auto *settings = obs_data_create();
	obs_data_set_string(settings, "device_hash", "Fake");
	obs_data_set_int(settings, "mode_id", 1);
	auto *realOutput = obs_output_create("decklink_output", "registered owner", settings, nullptr);
	assert(realOutput);
	auto *retained = static_cast<DeckLinkOutput *>(obs_obj_get_data(realOutput));
	video_output_info borrowedInfo={}; borrowedInfo.name="borrowed UI rendered queue";
	borrowedInfo.format=VIDEO_FORMAT_V210; borrowedInfo.width=48; borrowedInfo.height=2;
	borrowedInfo.fps_num=30000; borrowedInfo.fps_den=1001; borrowedInfo.cache_size=16;
	borrowedInfo.colorspace=VIDEO_CS_709; borrowedInfo.range=VIDEO_RANGE_PARTIAL;
	video_t *borrowedVideo=nullptr;
	assert(video_output_open(&borrowedVideo,&borrowedInfo)==VIDEO_OUTPUT_SUCCESS);
	bind_rendered_media(realOutput,borrowedVideo);
	// The receive selection is health metadata only: a source with audio and a
	// status proc, never a replacement media endpoint.
	obs_source_info mixedInfo = {};
	mixedInfo.id="ordinary_audio"; mixedInfo.type=OBS_SOURCE_TYPE_INPUT;
	mixedInfo.output_flags=OBS_SOURCE_AUDIO; mixedInfo.get_name=name;
	mixedInfo.create=create; mixedInfo.destroy=destroy;
	obs_register_source(&mixedInfo);
	auto *renderedSource=obs_source_create_private(mixedInfo.id,"ordinary mix source",nullptr);
	assert(renderedSource);
	assert(!healthy(realOutput, "no longer active")); // nothing is playing yet
	assert(bind(realOutput, renderedSource));
	for (unsigned attempt = 0; attempt < 4; ++attempt) {
		assert(obs_output_start(realOutput));
		assert(obs_output_active(realOutput) && sdk.card.started);
		assert(!obs_output_start(realOutput)); // an active output refuses a second start
		assert(obs_output_video(realOutput) == borrowedVideo);
		assert(video_output_get_info(obs_output_video(realOutput))->width == 48);
		assert(obs_output_audio(realOutput) == obs_get_audio());
		stop(realOutput);
		assert(!sdk.card.started && !sdk.card.cb);
	}
	// Settings of an active output cannot be replaced.
	assert(obs_output_start(realOutput));
	obs_data_set_int(settings, "mode_id", 999);
	obs_data_set_string(settings, "device_hash", "replaced-active-selection");
	obs_output_update(realOutput, settings);
	assert(retained->modeID == 1 && retained->deviceHash == "Fake");
	assert(healthy(realOutput));
	// A removed device is reported with a named reason; the UI watchdog drains.
	discovery.DeckLinkDeviceRemoved(&sdk);
	assert(!healthy(realOutput, "removed"));
	stop(realOutput);
	assert(!healthy(realOutput, "no longer active"));
	discovery.DeckLinkDeviceArrived(&sdk);
	auto *scene=obs_scene_create_private("ordinary receive scene");
	assert(obs_scene_add(scene,renderedSource));
	auto *otherSource=obs_source_create_private(mixedInfo.id,"second mix source",nullptr);
	assert(obs_scene_add(scene,otherSource));
	obs_set_output_source(0,obs_scene_get_source(scene));
	assert(!obs_source_get_monitoring_enabled(renderedSource));
	assert(obs_output_start(realOutput));
	assert(obs_output_audio(realOutput)==obs_get_audio());
	// Feed the actual OBS mixer. Only one rendered video callback establishes
	// the stock first-video epoch; subsequent PCM must flow without video pumps.
	video_frame renderedFrame = {};
	assert(video_output_lock_frame(borrowedVideo,&renderedFrame,1,os_gettime_ns()));
	memset(renderedFrame.data[0],0,renderedFrame.linesize[0]*2);
	video_output_unlock_frame(borrowedVideo);
	for(unsigned i=0;i<100 && !retained->start_timestamp;++i) os_sleep_ms(2);
	assert(retained->start_timestamp);
	float pcm[960]; for(auto &sample:pcm) sample=.5f;
	for(unsigned phase=0;phase<4;++phase) {
		obs_source_set_volume(renderedSource,phase ? .25f : 1.f);
		obs_source_set_muted(renderedSource,phase>=2);
		unsigned before=sdk.card.syncWrites;
		const uint64_t epoch=os_gettime_ns();
		for(unsigned i=0;i<50;++i) {
			obs_source_audio samples={}; samples.data[0]=reinterpret_cast<uint8_t *>(pcm);
			samples.data[1]=reinterpret_cast<uint8_t *>(pcm); samples.frames=960;
			samples.speakers=SPEAKERS_STEREO; samples.format=AUDIO_FORMAT_FLOAT_PLANAR;
			samples.samples_per_sec=48000; samples.timestamp=epoch+uint64_t(i)*20000000;
			obs_source_output_audio(renderedSource,&samples);
			if(phase==3) { // source selection is not a private audio solo
				obs_source_set_volume(otherSource,.25f);
				obs_source_output_audio(otherSource,&samples);
			}
			os_sleepto_ns(epoch+uint64_t(i+1)*20000000);
		}
		assert(sdk.card.syncWrites>before+10);
		std::lock_guard<std::mutex> lock(sdk.card.syncMutex);
		assert(!sdk.card.syncPCM.empty());
		const int expected=phase==2 ? 0 : phase==1 || phase==3 ? 4096 : 16384;
		for(auto sample:sdk.card.syncPCM) assert(abs(int(sample)-expected)<=1);
	}
	// Receive health: a gap in decoded frames only repeats the last rendered
	// frame on the card, so a stalled, ended or reconnecting receive never
	// drains the output.
	static struct { const char *state = "playing"; long long frames = 1; } receiveStatus;
	proc_handler_add(obs_source_get_proc_handler(renderedSource),
		"void get_status(out string state, out int frames)",
		[](void *, calldata_t *cd) {
			calldata_set_string(cd, "state", receiveStatus.state);
			calldata_set_int(cd, "frames", receiveStatus.frames);
		}, nullptr);
	assert(healthy(realOutput));
	os_sleep_ms(600); // no new frame for longer than half a second
	assert(healthy(realOutput));
	receiveStatus.state = "error";
	assert(healthy(realOutput));
	assert(bind(realOutput, nullptr)); // a running output rebinds on receive stop and reconnect
	assert(healthy(realOutput));
	assert(bind(realOutput, renderedSource));
	assert(healthy(realOutput));
	assert(obs_output_active(realOutput) && sdk.card.started);
	puts("receive health: frame gaps, stalls and live rebinds tolerated; inactive card and removed device named PASS");
	assert(!obs_source_get_monitoring_enabled(renderedSource));
	stop(realOutput);
	assert(bind(realOutput, nullptr));
	obs_set_output_source(0,nullptr);
	obs_scene_release(scene);
	obs_source_release(renderedSource);
	obs_source_release(otherSource);
	puts("registered rendered owner: OBS mixed PCM independent of video pumps, source gain/mute and monitoring-off PASS");
	assert(obs_output_video(realOutput) == borrowedVideo);
	assert(obs_output_audio(realOutput) == obs_get_audio());
	obs_output_release(realOutput);
	obs_queue_task(OBS_TASK_DESTROY, [](void *){}, nullptr, true);
	// The output must never close this borrowed UI endpoint.
	assert(video_output_get_info(borrowedVideo)->width==48);
	video_output_close(borrowedVideo);
	obs_data_release(settings);
	discovery.DeckLinkDeviceRemoved(&sdk);
	deviceEnum = nullptr;
	obs_shutdown();
	puts("registered decklink_output: Start/Stop, restart, borrowed media and active mutation rejection PASS");
}
