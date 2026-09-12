// SPDX-License-Identifier: GPL-2.0-or-later
#include "decklink-device-instance.hpp"
#include "DecklinkOutput.hpp"
#include "decklink-devices.hpp"
#include "sdk-stubs.hpp"
#include "rendered-media.inc"
#include "../pixelview-whep/source-feed-queue.h"
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
	struct A {
		int64_t t;
		uint32_t count;
		std::vector<uint8_t> data;
	};
	std::vector<A> audio;
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
	unsigned partial = UINT32_MAX, bufferedAudio = 0;
	BMDTimeValue playedSamples = 0;
	bool clockFails = false;
	double clockSpeed = 1.0;
	HRESULT GetScheduledStreamTime(BMDTimeScale s, BMDTimeValue *t, double *speed) override
	{
		assert(s == 48000); *t = playedSamples; *speed = clockSpeed; return clockFails ? E_FAIL : S_OK;
	}
	bool cleanupFails = false;
	std::atomic<bool> blockAudio{false}, audioEntered{false}, releaseAudio{false};
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
		if (!p && cleanupFails) {
			return E_FAIL;
		}
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
	HRESULT BeginAudioPreroll() override { return ok() ? S_OK : E_FAIL; }
	HRESULT EndAudioPreroll() override { return ok() ? S_OK : E_FAIL; }
	HRESULT ScheduleAudioSamples(void *p, uint32_t n, BMDTimeValue t, BMDTimeScale s, uint32_t *written) override
	{
		assert(s == 48000);
		if (started) assert(!clockFails && clockSpeed == 1.0 && playedSamples >= 0 && t >= playedSamples);
		if (blockAudio && started) {
			audioEntered = true;
			while (!releaseAudio) {
				std::this_thread::yield();
			}
		}
		if (!ok()) {
			return E_FAIL;
		}
		*written = std::min(n, partial);
		audio.push_back({t, *written, std::vector<uint8_t>((uint8_t *)p, (uint8_t *)p + *written * 4)});
		return S_OK;
	}
	HRESULT GetBufferedAudioSampleFrameCount(uint32_t *n) override
	{
		*n = bufferedAudio;
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
	HRESULT FlushBufferedAudioSamples() override { return S_OK; }
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
struct Feed {
	std::mutex mutex;
	struct pv_feed_queue queue = {};
	uint64_t epoch = os_gettime_ns(), audioLead = 0;
};
static void feed_proc(void *p, calldata_t *cd)
{
	auto &f = *(Feed *)p;
	std::lock_guard<std::mutex> l(f.mutex);
	pv_feed_request(&f.queue, 1, (struct pv_feed_request *)calldata_ptr(cd, "request"));
	calldata_set_int(cd, "version", PV_FEED_VERSION);
}
static void *create(obs_data_t *, obs_source_t *s)
{
	auto *f = new Feed;
	proc_handler_add(obs_source_get_proc_handler(s), "void native422_feed(ptr request, out int version)", feed_proc,
			 f);
	return f;
}
static void destroy(void *p)
{
	auto *f = (Feed *)p;
	pv_feed_reset(&f->queue);
	delete f;
}
static const char *name(void *)
{
	return "synthetic private feed";
}
static void push(Feed &f)
{
	std::lock_guard<std::mutex> l(f.mutex);
	if (!f.queue.token) {
		return;
	}
	uint8_t bytes[256];
	memset(bytes, 0x59, sizeof(bytes));
	struct pv_feed_request r = {};
	r.data = bytes;
	r.bytes = sizeof(bytes);
	r.timestamp_ns = f.epoch;
	r.duration_ns = 33366666;
	r.width = 48;
	r.height = 2;
	r.stride = 128;
	r.fps_num = 30000;
	r.fps_den = 1001;
	pv_feed_push(&f.queue, 1, &r, false);
	int16_t pcm[960];
	for (auto &s : pcm) {
		s = 1234;
	}
	r.data = pcm;
	r.bytes = sizeof(pcm);
	r.audio_frames = 480;
	r.timestamp_ns -= f.audioLead;
	pv_feed_push(&f.queue, 1, &r, true);
}
extern "C" obs_source_t *pv_owner_fixture_create();
extern "C" void pv_owner_fixture_produce(obs_source_t *, const char *);
extern "C" void pv_owner_fixture_audio(obs_source_t *, bool, uint64_t);
extern "C" void pv_owner_fixture_controls(obs_source_t *);
extern obs_output_info create_decklink_output_info();
int main(int argc, char **argv)
{
	assert(argc == 5);
	assert(obs_startup("en-US", nullptr, nullptr));
	obs_source_info info = {};
	info.id = "test_private_feed";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
	info.get_name = name;
	info.create = create;
	info.destroy = destroy;
	obs_register_source(&info);
	auto *source = obs_source_create_private(info.id, "private", nullptr);
	assert(source);
	auto &feed = *(Feed *)obs_obj_get_data(source);
	Device sdk;
	DeckLinkDeviceDiscovery discovery;
	DeckLinkOutput selected(nullptr, &discovery);
	assert(!selected.BindReceive(nullptr, true));
	assert(selected.BindReceive(source, true));
	assert(selected.IsNativeReceive());
	DeckLinkDevice device(&sdk);
	Mode sdkMode;
	DeckLinkDeviceMode mode(&sdkMode, 1);
	DeckLinkDeviceInstance owner(nullptr, &device);
	std::thread producer([&] {
		for (int i = 0; i < 50; i++) {
			push(feed);
			os_sleep_ms(2);
		}
	});
	bool started = owner.StartNativeOutput(&mode, source);
	producer.join();
	assert(started);
	assert(!sdk.keyer.enabled && sdk.card.config.conversion == bmdNoVideoOutputConversion);
	void *callbackInterface = nullptr;
	assert(sdk.card.cb->QueryInterface(IID_IDeckLinkVideoOutputCallback, &callbackInterface) == S_OK);
	static_cast<IDeckLinkVideoOutputCallback *>(callbackInterface)->Release();
	assert(sdk.card.queued.size() == 3 && sdk.card.started);
	for (auto &v : sdk.card.queued) {
		assert(v.s == 30000 && v.d == 1001);
		void *p;
		v.f->GetBytes(&p);
		uint32_t expected = 512u | (64u << 10) | (512u << 20);
		assert(!memcmp(p, &expected, 4));
	}
	sdk.card.complete();
	assert(sdk.card.queued.back().t == 3003);
	void *p;
	sdk.card.queued.back().f->GetBytes(&p);
	assert(((uint8_t *)p)[0] == 0x59);
	assert(sdk.card.audio.size() >= 2);
	assert(sdk.card.audio[1].t == 4804);
	assert(sdk.card.audio[1].count == 480);
	// Both directions of one-device capture/output exclusion, without a driver.
	DeckLinkDeviceInstance competitor(nullptr, &device);
	assert(!competitor.StartNativeOutput(&mode, source));
	assert(!device.TryAcquire(&competitor));
	auto *lateCallback = sdk.card.cb;
	lateCallback->AddRef();
	owner.StopOutput();
	assert(!sdk.card.cb);
	assert(!feed.queue.token);
	assert(device.TryAcquire(&competitor));
	assert(!owner.StartNativeOutput(&mode, source));
	device.ReleaseOwner(&competitor);
	lateCallback->ScheduledFrameCompleted(nullptr, bmdOutputFrameFlushed);
	lateCallback->Release();
	// Every fallible startup boundary rolls back the actual owner. Retry on the
	// same instance must work; retained SDK callbacks may arrive after Stop.
	int boundaries = sdk.card.calls - 1;
	for (int fail = 1; fail <= boundaries; fail++) {
		sdk.card.calls = 0;
		sdk.card.failAt = fail;
		sdk.card.audio.clear();
		std::thread p([&] {
			for (int i = 0; i < 20; i++) {
				push(feed);
				os_sleep_ms(2);
			}
		});
		bool ok = owner.StartNativeOutput(&mode, source);
		p.join();
		if (ok) {
			owner.StopOutput();
		}
		assert(!sdk.card.video && !sdk.card.sound && !sdk.card.started && !sdk.card.cb &&
		       sdk.card.queued.empty() && !feed.queue.token);
	}
	sdk.card.failAt = 0;
	sdk.card.calls = 0;
	sdk.card.audio.clear();
	std::thread restartProducer([&] {
		for (int i = 0; i < 20; i++) {
			push(feed);
			os_sleep_ms(2);
		}
	});
	assert(owner.StartNativeOutput(&mode, source));
	restartProducer.join();
	sdk.card.partial = 123;
	sdk.card.complete();
	sdk.card.complete();
	sdk.card.complete();
	sdk.card.complete();
	assert(sdk.card.audio[1].t == 4804 && sdk.card.audio[2].t == 4927 && sdk.card.audio[4].count == 111);
	{
		std::lock_guard<std::mutex> l(feed.mutex);
		pv_feed_reset(&feed.queue);
	}
	assert(!owner.NativeOutputHealthy());
	sdk.card.complete();
	owner.StopOutput();
	sdk.card.partial = UINT32_MAX;
	sdk.card.audio.clear();
	feed.audioLead = 5000000;
	std::thread trimmedProducer([&] {
		for (int i = 0; i < 20; i++) {
			push(feed);
			os_sleep_ms(2);
		}
	});
	assert(owner.StartNativeOutput(&mode, source));
	trimmedProducer.join();
	sdk.card.complete();
	assert(sdk.card.audio[1].t == 4804 && sdk.card.audio[1].count == 240);
	owner.StopOutput();
	feed.audioLead = 0;
	// An expired pending packet must fail-stop before another SDK audio write.
	// Exercise zero and partial acceptance on an exact 30000/1001 card clock.
	for (unsigned accepted : {0u, 1u, 2u}) {
		sdk.card.partial = UINT32_MAX;
		sdk.card.playedSamples = 0;
		std::thread p([&] {
			for (int i = 0; i < 20; ++i) { push(feed); os_sleep_ms(2); }
		});
		assert(owner.StartNativeOutput(&mode, source)); p.join();
		sdk.card.partial = accepted;
		sdk.card.complete();
		sdk.card.bufferedAudio = accepted == 2 ? 30000 : 0;
		const size_t writes = sdk.card.audio.size();
		for (unsigned i = 1; i <= 30 && owner.NativeOutputHealthy(); ++i) {
			sdk.card.playedSamples = uint64_t(i) * 1001 * 48000 / 30000;
			sdk.card.complete();
		}
		assert(!owner.NativeOutputHealthy());
		const size_t expiredWrites = sdk.card.audio.size();
		sdk.card.partial = UINT32_MAX;
		sdk.card.cb->ScheduledFrameCompleted(nullptr, bmdOutputFrameCompleted);
		assert(sdk.card.audio.size() == expiredWrites);
		assert(expiredWrites >= writes);
		owner.StopOutput();
		sdk.card.bufferedAudio = 0;
	}
	sdk.card.playedSamples = 0;
	// Invalid clocks fail-stop without scheduling, including lookahead bypass.
	for (unsigned buffered : {0u, 30000u}) for (unsigned invalid = 0; invalid < 3; ++invalid) {
		std::thread p([&] { for (int i = 0; i < 20; ++i) { push(feed); os_sleep_ms(2); } });
		assert(owner.StartNativeOutput(&mode, source)); p.join();
		sdk.card.bufferedAudio = buffered;
		sdk.card.clockFails = invalid == 0;
		sdk.card.playedSamples = invalid == 1 ? -1 : 0;
		sdk.card.clockSpeed = invalid == 2 ? 0.5 : 1.0;
		const size_t writes = sdk.card.audio.size();
		sdk.card.complete();
		assert(!owner.NativeOutputHealthy() && sdk.card.audio.size() == writes);
		owner.StopOutput();
		sdk.card.clockFails = false; sdk.card.clockSpeed = 1.0;
		sdk.card.playedSamples = 0; sdk.card.bufferedAudio = 0;
	}
	// Existing rendered owner keeps ordinary output and padded-row safety.
	selected.SetSize(48, 2);
	DeckLinkDeviceInstance rendered(&selected, &device);
	assert(rendered.StartOutput(&mode));
	for (auto &v : sdk.card.queued) {
		void *b;
		v.f->GetBytes(&b);
		assert(((uint8_t *)b)[0] == 0);
	}
	uint8_t padded[400];
	memset(padded, 0xcd, sizeof(padded));
	memset(padded, 0x31, 192);
	memset(padded + 200, 0x72, 192);
	video_data pixels = {};
	pixels.data[0] = padded;
	pixels.linesize[0] = 200;
	rendered.UpdateVideoFrame(&pixels);
	sdk.card.complete();
	void *renderedBytes;
	sdk.card.queued.back().f->GetBytes(&renderedBytes);
	assert(((uint8_t *)renderedBytes)[0] == 0x31 && ((uint8_t *)renderedBytes)[192] == 0x72);
	auto *renderedCallback = sdk.card.cb;
	renderedCallback->AddRef();
	rendered.StopOutput();
	renderedCallback->ScheduledFrameCompleted(nullptr, bmdOutputFrameFlushed);
	renderedCallback->Release();
	for (int fail = 1; fail <= 10; fail++) {
		sdk.card.calls = 0;
		sdk.card.failAt = fail;
		assert(!rendered.StartOutput(&mode));
		assert(!sdk.card.video && !sdk.card.sound && !sdk.card.started && !sdk.card.cb &&
		       sdk.card.queued.empty());
	}
	sdk.card.failAt = 0;
	// Stop must wait for an in-flight scheduling callback, without holding the
	// callback gate while the SDK unregisters it.
	std::thread concurrentProducer([&] {
		for (int i = 0; i < 20; i++) {
			push(feed);
			os_sleep_ms(2);
		}
	});
	assert(owner.StartNativeOutput(&mode, source));
	concurrentProducer.join();
	sdk.card.blockAudio = true;
	std::thread completion([&] { sdk.card.complete(); });
	while (!sdk.card.audioEntered) {
		std::this_thread::yield();
	}
	std::atomic<bool> stopped{false};
	std::thread stopping([&] {
		owner.StopOutput();
		stopped = true;
	});
	os_sleep_ms(10);
	assert(!stopped);
	sdk.card.releaseAudio = true;
	completion.join();
	stopping.join();
	assert(stopped && !feed.queue.token);
	sdk.card.blockAudio = false;
	selected.BindReceive(nullptr, false);
	obs_source_release(source);
	obs_audio_info ai = {48000, SPEAKERS_STEREO};
	assert(obs_reset_audio(&ai));
	auto *native = pv_owner_fixture_create();
	pv_owner_fixture_controls(native);
	assert(native);
	sdkMode.width = sdk.card.mode.width = 1024;
	sdkMode.height = sdk.card.mode.height = 64;
	sdk.card.partial = UINT32_MAX;
	sdk.card.audio.clear();
	FILE *reference = fopen(argv[2], "rb");
	assert(reference);
	std::vector<uint8_t> expected(2816 * 64 * 3);
	assert(fread(expected.data(), 1, expected.size(), reference) == expected.size());
	fclose(reference);
	std::thread nativeProducer([&] { pv_owner_fixture_produce(native, argv[1]); });
	assert(owner.StartNativeOutput(&mode, native));
	nativeProducer.join();
	for (unsigned i = 0; i < 3; i++) {
		sdk.card.complete();
		auto &v = sdk.card.queued.back();
		assert(v.t == int64_t(i + 3) * 1001);
		void *b = nullptr;
		assert(v.f->GetBytes(&b) == S_OK);
		assert(!memcmp(b, expected.data() + i * 2816 * 64, 2816 * 64));
	}
	assert(sdk.card.audio.size() == 2 && sdk.card.audio[1].t == 4804 && sdk.card.audio[1].count == 480);
	for (unsigned i = 0; i < 960; i++) {
		int16_t sample;
		memcpy(&sample, sdk.card.audio[1].data.data() + i * 2, 2);
		assert(sample == ((i & 1) ? -16384 : 16384));
	}
	sdk.card.cleanupFails = true;
	owner.StopOutput();
	assert(device.Removed());
	// Driver retains a callback after refusing unregistration: it has a closed
	// self-owned gate, never a dangling DeviceInstance or source pointer.
	sdk.card.cb->ScheduledFrameCompleted(nullptr, bmdOutputFrameFlushed);
	sdk.card.cleanupFails = false;
	sdk.card.SetScheduledFrameCompletionCallback(nullptr);
	obs_source_release(native);
	obs_add_data_path(argv[4]);
	obs_video_info vi = {};
	vi.graphics_module = argv[3];
	vi.fps_num = 30000;
	vi.fps_den = 1001;
	vi.base_width = vi.output_width = 32;
	vi.base_height = vi.output_height = 32;
	vi.output_format = VIDEO_FORMAT_BGRA;
	vi.colorspace = VIDEO_CS_709;
	vi.range = VIDEO_RANGE_PARTIAL;
	assert(obs_reset_video(&vi) == OBS_VIDEO_SUCCESS);
	sdk.card.mode.width = 48;
	sdk.card.mode.height = 2;
	deviceEnum = &discovery;
	discovery.DeckLinkDeviceArrived(&sdk);
	auto outputInfo = create_decklink_output_info();
	obs_register_output(&outputInfo);
	auto *settings = obs_data_create();
	obs_data_set_string(settings, "device_hash", "Fake");
	obs_data_set_int(settings, "mode_id", 1);
	auto *realOutput = obs_output_create("decklink_output", "existing native owner", settings, nullptr);
	assert(realOutput);
	auto *realSource = obs_source_create_private(info.id, "source-bound", nullptr);
	auto &realFeed = *(Feed *)obs_obj_get_data(realSource);
	calldata_t bind;
	calldata_init(&bind);
	calldata_set_ptr(&bind, "source", realSource);
	calldata_set_bool(&bind, "native", true);
	assert(proc_handler_call(obs_output_get_proc_handler(realOutput), "bind_receive", &bind) &&
	       calldata_bool(&bind, "bound"));
	std::thread realProducer([&] {
		for (int i = 0; i < 20; i++) {
			push(realFeed);
			os_sleep_ms(2);
		}
	});
	assert(obs_output_start(realOutput));
	realProducer.join();
	assert(obs_output_active(realOutput));
	assert(obs_output_audio(realOutput) != obs_get_audio() && obs_output_video(realOutput) != obs_get_video());
	assert(!obs_output_start(realOutput));
	assert(obs_output_active(realOutput) && sdk.card.started);
	obs_data_set_int(settings, "mode_id", 999);
	obs_data_set_string(settings, "device_hash", "replaced-active-selection");
	obs_output_update(realOutput, settings);
	assert(static_cast<DeckLinkOutput *>(obs_obj_get_data(realOutput))->modeID == 1);
	assert(std::string(static_cast<DeckLinkOutput *>(obs_obj_get_data(realOutput))->deviceHash) == "Fake");
	sdk.card.complete();
	assert(sdk.card.queued.back().t == 3003);
	discovery.DeckLinkDeviceRemoved(&sdk);
	calldata_t status; calldata_init(&status);
	assert(proc_handler_call(obs_output_get_proc_handler(realOutput), "receive_status", &status));
	assert(!calldata_bool(&status, "healthy")); calldata_free(&status);
	obs_output_stop(realOutput);
	for (int i = 0; i < 100 && obs_output_active(realOutput); i++) {
		os_sleep_ms(2);
	}
	assert(!obs_output_active(realOutput));
	// Exercise the actual UI media binding with its caller-owned rendered queue.
	video_output_info borrowedInfo={}; borrowedInfo.name="borrowed UI rendered queue";
	borrowedInfo.format=VIDEO_FORMAT_BGRA; borrowedInfo.width=48; borrowedInfo.height=2;
	borrowedInfo.fps_num=30000; borrowedInfo.fps_den=1001; borrowedInfo.cache_size=16;
	borrowedInfo.colorspace=VIDEO_CS_709; borrowedInfo.range=VIDEO_RANGE_FULL;
	video_t *borrowedVideo=nullptr;
	assert(video_output_open(&borrowedVideo,&borrowedInfo)==VIDEO_OUTPUT_SUCCESS);
	bind_rendered_media(realOutput,borrowedVideo);
	// Retain the registered output, not merely a private-media helper.
	discovery.DeckLinkDeviceArrived(&sdk);
	auto *retained = static_cast<DeckLinkOutput *>(obs_obj_get_data(realOutput));
	for (unsigned route = 0; route < 4; ++route) {
		assert(retained->BindReceive(realSource, false));
		assert(obs_output_video(realOutput) == borrowedVideo);
		assert(obs_output_start(realOutput));
		assert(obs_output_active(realOutput));
		assert(obs_output_video(realOutput) == borrowedVideo);
		assert(video_output_get_info(obs_output_video(realOutput))->width == 48);
		assert(obs_output_audio(realOutput) == obs_get_audio());
		assert(!realFeed.queue.token); // rendered output must not attach any source PCM feed
		obs_output_stop(realOutput);
		for (int i = 0; i < 100 && obs_output_active(realOutput); ++i) os_sleep_ms(2);
		assert(!obs_output_active(realOutput));
		assert(retained->BindReceive(realSource, true));
		std::thread p([&] { for (int i = 0; i < 20; ++i) { push(realFeed); os_sleep_ms(2); } });
		assert(obs_output_start(realOutput)); p.join();
		assert(obs_output_active(realOutput));
		assert(obs_output_video(realOutput) != borrowedVideo);
		assert(obs_output_audio(realOutput) != obs_get_audio());
		assert(realFeed.queue.token && realFeed.queue.route == PV_FEED_NATIVE);
		const unsigned syncBefore=sdk.card.syncWrites;
		sdk.card.complete();
		assert(sdk.card.queued.back().t==3003);
		assert(sdk.card.audio.back().t==4804 && sdk.card.audio.back().count==480);
		assert(sdk.card.syncWrites==syncBefore);
		obs_output_stop(realOutput);
		for (int i = 0; i < 100 && obs_output_active(realOutput); ++i) os_sleep_ms(2);
		assert(!obs_output_active(realOutput));
	}
	// Ordinary selection has no native feed proc at all: it is only a health
	// selection, never a source PCM attachment or a replacement media endpoint.
	obs_source_info mixedInfo = {};
	mixedInfo.id="ordinary_audio"; mixedInfo.type=OBS_SOURCE_TYPE_INPUT;
	mixedInfo.output_flags=OBS_SOURCE_AUDIO; mixedInfo.get_name=name;
	mixedInfo.create=[](obs_data_t *, obs_source_t *s)->void * { return s; };
	mixedInfo.destroy=[](void *){};
	obs_register_source(&mixedInfo);
	auto *renderedSource=obs_source_create_private(mixedInfo.id,"ordinary mix source",nullptr);
	auto *scene=obs_scene_create_private("ordinary receive scene");
	assert(obs_scene_add(scene,renderedSource));
	auto *otherSource=obs_source_create_private(mixedInfo.id,"second mix source",nullptr);
	assert(obs_scene_add(scene,otherSource));
	obs_set_output_source(0,obs_scene_get_source(scene));
	assert(!obs_source_get_monitoring_enabled(renderedSource));
	assert(retained->BindReceive(renderedSource, false));
	assert(!retained->IsNativeReceive());
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
	assert(!obs_source_get_monitoring_enabled(renderedSource));
	obs_output_stop(realOutput);
	for (int i=0; i<100 && obs_output_active(realOutput); ++i) os_sleep_ms(2);
	assert(!obs_output_active(realOutput));
	assert(retained->BindReceive(nullptr, false));
	obs_set_output_source(0,nullptr);
	obs_scene_release(scene);
	obs_source_release(renderedSource);
	obs_source_release(otherSource);
	puts("actual stock rendered owner: OBS mixed PCM independent of video pumps, native gain/mute and monitoring-off PASS");
	assert(obs_output_video(realOutput) == borrowedVideo);
	assert(obs_output_audio(realOutput) == obs_get_audio());
	obs_output_release(realOutput);
	obs_queue_task(OBS_TASK_DESTROY, [](void *){}, nullptr, true);
	// The native owner must never close this borrowed UI endpoint.
	assert(video_output_get_info(borrowedVideo)->width==48);
	video_output_close(borrowedVideo);
	obs_source_release(realSource);
	obs_data_release(settings);
	calldata_free(&bind);
	discovery.DeckLinkDeviceRemoved(&sdk);
	deviceEnum = nullptr;
	obs_shutdown();
	puts("registered decklink_output: real Start/Stop/private accounting and active mutation rejection PASS");
	puts("production encoded-filter -> native VT -> versioned source A/V -> real owner/fake SDK: all 3 exact v210 frames and timed source PCM PASS");
	puts("real DeckLink owner: private v210, A/V epoch, partial PCM, rollback, restart, reset, exclusion and late callback PASS");
}
