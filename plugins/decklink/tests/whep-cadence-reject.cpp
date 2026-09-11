// SPDX-License-Identifier: GPL-2.0-or-later
// Negative admission evidence, real receiver and production owner; no SDK dispatch.
#define main legacy_owner_main
#include "receive.cpp"
#undef main
extern "C" obs_source_t *pv_whep_create(const char *);
extern "C" void pv_whep_status(obs_source_t *, uint64_t *, uint64_t *, bool *);
extern "C" void pv_whep_disconnect(obs_source_t *);
int main(int argc, char **argv)
{
 assert(argc == 2); setbuf(stdout, nullptr);
 assert(obs_startup("en-US", nullptr, nullptr));
 obs_audio_info ai = {48000, SPEAKERS_STEREO}; assert(obs_reset_audio(&ai));
 auto *source = pv_whep_create(argv[1]);
 Device sdk; sdk.card.mode.width = 1920; sdk.card.mode.height = 1080;
 DeckLinkDevice device(&sdk); DeckLinkDeviceMode mode(&sdk.card.mode, 1);
 DeckLinkDeviceInstance owner(nullptr, &device);
 uint64_t video=0, audio=0; bool error=false;
 for (unsigned i=0; i<150 && !error; ++i) { os_sleep_ms(100); pv_whep_status(source,&video,&audio,&error); }
 fprintf(stderr,"VT_CADENCE_REFUSAL native=%llu opus=%llu error=%d\n",video,audio,error);
 if(getenv("PV_TEST_DOWNSTREAM_STALL") || getenv("PV_TEST_RATE_CHANGE")) assert(error && video>=9);
 else assert(error && !video);
 assert(!owner.StartNativeOutput(&mode,source));
 assert(!sdk.card.started && !sdk.card.cb && sdk.card.queued.empty());
 pv_whep_disconnect(source); obs_source_release(source);
 obs_queue_task(OBS_TASK_DESTROY, [](void *) {}, nullptr, true);
 obs_shutdown();
 puts("ACTUAL_WHEP_VT_HD_OWNER_REFUSED");
}
