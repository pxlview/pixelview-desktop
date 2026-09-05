/* Real libobs serialization; no plugins, GPU, GUI, capture or playback. */
#include <obs.h>
#include <AudioToolbox/AudioToolbox.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fail closed if DO_NOT_SELF_MONITOR cannot suppress platform output. */
static OSStatus forbidden_output(const AudioStreamBasicDescription *a, AudioQueueOutputCallback b,
                                void *c, CFRunLoopRef d, CFStringRef e, UInt32 f, AudioQueueRef *g)
{
	(void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g;
	fputs("FAIL: attempted AudioQueueNewOutput (no playback permitted)\n", stderr);
	exit(2);
}
__attribute__((used, section("__DATA,__interpose")))
static const struct { const void *replacement; const void *original; } no_output = {
	(const void *)forbidden_output, (const void *)AudioQueueNewOutput
};
static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static const char *source_name(void *unused) { (void)unused; return "Serialization fixture"; }
static void *source_create(obs_data_t *settings, obs_source_t *source) { (void)settings; return source; }
static void source_destroy(void *unused) { (void)unused; }
static struct obs_source_info fixture = {
	.id = "pixelview_serialization_fixture", .type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_SELF_MONITOR,
	.get_name = source_name, .create = source_create, .destroy = source_destroy,
};
static obs_source_t *make_source(bool muted, bool monitoring)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "device_id", "default");
	obs_data_set_string(settings, "unchanged_device_property", "fixture-original");
	obs_source_t *s = obs_source_create(fixture.id, "fixture", settings, NULL);
	obs_data_release(settings);
	if (!s) exit(2);
	obs_source_set_volume(s, 0.375f);
	obs_source_set_audio_mixers(s, 0x15);
	obs_source_set_muted(s, muted);
	obs_source_set_monitoring_enabled(s, monitoring);
	return s;
}
static void verify(obs_source_t *s, bool muted, bool monitoring, uint32_t mixers)
{
	CHECK(s != NULL);
	if (!s) return;
	CHECK(obs_source_muted(s) == muted);
	CHECK(obs_source_get_monitoring_enabled(s) == monitoring);
	CHECK(obs_source_get_volume(s) == 0.375f);
	CHECK(obs_source_get_audio_mixers(s) == mixers);
	obs_data_t *settings = obs_source_get_settings(s);
	CHECK(strcmp(obs_data_get_string(settings, "device_id"), "default") == 0);
	CHECK(strcmp(obs_data_get_string(settings, "unchanged_device_property"), "fixture-original") == 0);
	obs_data_release(settings);
}
static void roundtrip(bool muted, bool monitor, bool version33)
{
	unsigned before = failures;
	obs_source_t *s = make_source(muted, monitor);
	obs_data_t *saved = obs_save_source(s);
	CHECK(obs_data_get_int(saved, "prev_ver") == obs_get_version());
	CHECK(obs_data_has_user_value(saved, "monitoring"));
	obs_data_item_t *item = obs_data_item_byname(saved, "monitoring");
	CHECK(obs_data_item_gettype(item) == OBS_DATA_BOOLEAN);
	obs_data_item_release(&item);
	CHECK(obs_data_get_bool(saved, "monitoring") == monitor);
	item = obs_data_item_byname(saved, "monitoring_enabled");
	CHECK(obs_data_item_gettype(item) == OBS_DATA_NUMBER);
	CHECK(obs_data_item_numtype(item) == OBS_DATA_NUM_INT);
	CHECK(obs_data_item_get_int(item) == monitor);
	obs_data_item_release(&item);
	if (version33) obs_data_set_int(saved, "prev_ver", MAKE_SEMANTIC_VERSION(33, 0, 0));
	obs_data_t *parsed = obs_data_create_from_json(obs_data_get_json(saved));
	obs_source_release(s);
	s = obs_load_source(parsed);
	verify(s, muted, monitor, 0x15);
	obs_source_release(s);
	obs_data_release(parsed);
	obs_data_release(saved);
	printf("%s real JSON roundtrip muted=%d monitor=%d version=%s\n", failures == before ? "PASS" : "FAIL", muted, monitor, version33 ? "33.0.0 fixture" : "native");
}
static void compatibility(uint32_t version, int type, int canonical, int fallback, bool expected,
                          bool monitor_by_default)
{
	unsigned before = failures;
	obs_source_t *s = make_source(true, false);
	obs_data_t *saved = obs_save_source(s);
	obs_source_release(s);
	obs_data_set_int(saved, "prev_ver", version);
	obs_data_set_int(saved, "monitoring_type", type);
	obs_data_erase(saved, "monitoring");
	obs_data_erase(saved, "monitoring_enabled");
	if (canonical >= 0) obs_data_set_bool(saved, "monitoring", canonical != 0);
	if (fallback >= 0) obs_data_set_int(saved, "monitoring_enabled", fallback);
	if (monitor_by_default) {
		obs_data_set_string(saved, "id", "pixelview_serialization_default_fixture");
		obs_data_set_string(saved, "versioned_id", "pixelview_serialization_default_fixture");
	}
	obs_data_t *parsed = obs_data_create_from_json(obs_data_get_json(saved));
	if (fallback >= 0) {
		obs_data_item_t *item = obs_data_item_byname(parsed, "monitoring_enabled");
		CHECK(obs_data_item_gettype(item) == OBS_DATA_NUMBER);
		CHECK(obs_data_item_numtype(item) == OBS_DATA_NUM_INT);
		obs_data_item_release(&item);
	}
	s = obs_load_source(parsed);
	/* Preserve the existing pre-23.2.2 default-monitor migration's mixer reset. */
	verify(s, true, expected, monitor_by_default ? 0x3F : 0x15);
	obs_data_release(parsed);
	obs_data_release(saved);
	obs_source_release(s);
	printf("%s compatibility version=%u type=%d canonical=%d integer_fallback=%d default=%d expected=%d\n",
	       before == failures ? "PASS" : "FAIL", version, type, canonical, fallback, monitor_by_default, expected);
}
int main(void)
{
	if (!obs_startup("en-US", NULL, NULL)) return 2;
	struct obs_audio_info ai = {.samples_per_sec = 48000, .speakers = SPEAKERS_STEREO};
	if (!obs_reset_audio(&ai)) return 2; /* CPU mixer only; no audio submitted. */
	obs_register_source(&fixture);
	printf("Native API version: %u (header %u)\n", obs_get_version(), LIBOBS_API_VER);
	CHECK(obs_get_version() == LIBOBS_API_VER);
	for (int muted = 0; muted < 2; muted++)
		for (int monitor = 0; monitor < 2; monitor++) {
			roundtrip(muted, monitor, false);
			roundtrip(muted, monitor, true);
		}
	struct obs_source_info default_fixture = fixture;
	default_fixture.id = "pixelview_serialization_default_fixture";
	default_fixture.output_flags |= OBS_SOURCE_MONITOR_BY_DEFAULT;
	obs_register_source(&default_fixture);
	const uint32_t v33 = MAKE_SEMANTIC_VERSION(33, 0, 0);
	const uint32_t v32 = MAKE_SEMANTIC_VERSION(32, 0, 0);
	for (int type = 0; type <= 2; type++) {
		compatibility(v32, type, -1, -1, type != 0, false);
		compatibility(v32, type, type == 0, type == 0, type != 0, false);
	}
	compatibility(v33, 0, -1, 1, true, false);
	compatibility(v33, 2, -1, 0, false, false);
	compatibility(v33, 2, 0, 1, false, false);
	compatibility(v33, 0, 1, 0, true, false);
	compatibility(v33, 2, -1, -1, false, false);
	compatibility(MAKE_SEMANTIC_VERSION(23, 2, 1), 0, 0, 0, true, true);
	obs_shutdown();
	printf("%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
