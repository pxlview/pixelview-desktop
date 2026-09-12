# Ordinary receive: stock rendered DeckLink output

This change supersedes the DeckLink rendered-audio plumbing described in the historical `pixelview-main10-receive-audit.md`. It does not establish the cause of the user's Main10 glitch or certify live playback.

## Production path

Ordinary Main/Main10 reception uses the existing OBS rendered program output: main texture, native UI BGRA staging/video queue, `decklink_output`'s ordinary StartOutput, and `obs_get_audio()` through the existing raw-audio conversion/first-video timing handler. Source PCM is **not** polled on rendered video callbacks. The custom rendered feed attachment, queue, trim and pump have been removed from the DeckLink owner.

`bind_receive(source, native=false)` now retains source identity solely for the receive watchdog. It neither requires the native feed ABI nor attaches a source PCM consumer. Existing readiness, source freshness and native-format-change fail-stop checks remain. The UI still chooses native422 explicitly; this change does not enable desktop audio, microphone sources, monitoring, hardware or AutoStart.

Ordinary audio intentionally follows the OBS mix: native source gain, mute, mixer routing and active scene membership apply. Muting the receive source does not mute other active sources in the same output mix. Monitoring is separate and is not enabled by this change. The frontend's receive-scene/previous-canvas and capture-deactivation ownership remains outside this change.

Rendered SDR DeckLink output remains **8-bit BGRA**, not faithful ten-bit 4:2:2 SDI. Receiving Main10/P010 does not change that output precision.

## Native isolation and ownership

Native422 retains the existing private idle libobs media endpoints, native v210 frame path, timestamped scheduler and source-only PCM. No scheduler, admission, preroll, rate/envelope, expiry or jitter default was changed. Jitter was100ms at that checkpoint. A subsequent50ms experiment was superseded by the explicit user request to remove the override: current Desktop omits latency and leaves the native GStreamer property untouched (pinned runtime observed200ms, not hardcoded), without changing this path; see `pixelview-main10-receive-audit.md`.

The existing `DeckLinkPrivateMedia::Restore` already distinguishes its owned queues from borrowed caller endpoints. Ordinary output no longer opens private media at all. Rebinding a retained native output restores the external video/audio before another native Open can close/recreate private queues; borrowed UI queues are not closed by the owner. No shared ABI change was required in this DeckLink patch.

## Executed offline verification

The regression first failed on the actual rendered endpoint assertion (`obs_output_audio(realOutput) == obs_get_audio()`) before the production change.

The complete production owner/output translation units were then compiled with ASan/UBSan and refusing SDK implementations generated from the bundled interfaces. No SDK dispatcher or physical discovery was linked. The passing run exercises:

- Actual UI `obs_output_set_media` statement with a caller-owned rendered queue and the real OBS mixer.
- Four retained native→rendered→native cycles: actual endpoint identity, no rendered feed token, native token/route, native video slot and PCM timestamps, no native synchronous mixed-audio write.
- Borrowed queue usability after the actual output destruction barrier.
- Real OBS private scene/audio sources, one video-queue frame, and sustained mixed PCM writes without subsequent video pumps. Observed S16 values match unity gain, quarter gain and mute; another active source remains in the mix while the selected receive source is muted. Monitoring remains off.
- Unchanged native zero/partial-write expiry, running-card-clock refusal, rollback, source reset, exclusion, cleanup quarantine and retained callback tests.
- Production native source-control gain/mute and concurrent disconnect safety; encoded-filter→VideoToolbox→source ABI→owner exact v210 fixture comparison and timestamped PCM.
- Real private-media activity isolation and compiled Qt receive selection/readiness/watchdog tests.

Command (from repository root):

```sh
PV_DECKLINK_FIXTURES="$PWD/plugins/decklink/.test-build/native-fixtures" \
PV_DECKLINK_SANITIZE=1 python3 plugins/decklink/tests/run-receive.py
python3 test/pixelview/test_decklink_output_ui.py
```

Both passed; the latter ran four tests. `git diff --check` passed. Log: `/tmp/decklink-final.log`; UI log: `/tmp/decklink-ui.log`. Existing native fixtures were absent, so the upstream native fixture-generation/verification portion was executed in memory with its WORK redirected to the DeckLink ignored test directory and STAGE to the existing pinned `.deps/pixelview-gstreamer` runtime (no runtime copy or application replacement). Its limited/full/HDR/unknown cases passed; this small elementary-file fixture is not shipping HD/native422 admission evidence.

The harness uses existing built libobs/frameworks, not a newly rebuilt application. Expected injected driver failures, pre-graphics source-cleanup diagnostics and existing deprecation warnings are present; there were no sanitizer findings. No app build, signing, replacement/restart, actual stream/card, Keychain access, commit or push was performed. Physical SDI cadence, long-run A/V sync and live Main10 glitch acceptance remain unverified.
