# Production capability/offer integration

Current checkpoint status and remaining acceptance gates are authoritative in
[`integration-status.md`](integration-status.md); this document records the
focused production-hook tests, not full application or hardware acceptance.

## Implemented

`pixelview-whep.c` probes on its connection worker after successful module runtime initialization, outside receiver locks, before pipeline construction. A stack waiter context checks quit/generation; a stale result cannot overwrite the current attempt's capabilities. Zero capabilities never create a pipeline. The process-wide probe does not retain receiver data.

Each graph owns an immutable capability/generation/latency snapshot in a reference-counted signal context. Context detachment is synchronized with active callbacks before teardown; retained signaller/webrtcbin objects cannot touch a freed receiver. Both video and audio pass through `pixelview_profile_offer_caps_limited`. Only verified H264/HEVC/VP9 profiles and Opus remain. H264 mode1 and stereo Opus are explicit. HEVC uses the actual probe level, never a fabricated upgrade. The raw `video-policy` capsfilter requires P010 and bounds dimensions to1920x1080; the frame-rate bound is30 when offered HEVC requires level120 and60 for level123 or admitted H264/VP9-only masks (HEVC level0). One shared ceiling supplies both the raw filter and offer limits.

**Empty codec preferences alone are not sufficient:** real webrtcbin still generates a valid rejected-media SDP. A production `create-offer` RUN_LAST signal guard returns a negotiation error before the default action for failed/unverified/stale attempts. No NULL/ANY/default-codec fallback is restored.

Before runtime initialization the module retains its own actual OBS module binary with `dlopen(RTLD_NOW|RTLD_NODELETE)`, intentionally never closing that handle or calling `gst_deinit`. Linked GStreamer code remains process-owned if the bounded probe waiter outlives a driver call. Failure to acquire the retain refuses module initialization.

## Verified commands

- `python3 plugins/pixelview-whep/tests/run-production-offer.py --deterministic-only`: seven injected admitted-profile cases check the exact production P010 HD capsfilter and real transceiver/create-offer hooks: H264-only, each VP9 profile alone, both VP9 profiles, HEVC Main/Main10 at123/60 and120/30, and mixed H264/HEVC/VP9 at120/30. Also runs the three negative/stale hooks. This mode skips all hardware decoder probes and worker probe tests; graph construction does not start WHEP/network activity.

- `python3 plugins/pixelview-whep/tests/run-production-offer.py`: actual production NULL/empty/stale transceiver hooks reject `create-offer`; detached late callbacks remain safe; actual raw-output whepclientsrc local SDP matches the live probe mask/level, Main/Main10 and VP9 profile gates, H264mode1, stereo Opus, BUNDLE-wide payload uniqueness, actual raw caps envelope and jitter50. Latest full run observed **mask31 level123**. This harness clears rswebrtc's default STUN server and uses only a closed loopback endpoint; SDP host candidates are not printed.
- The same runner's real receiver-worker tests cover absent decoder, cancellation, and timeout/late cache completion. A test-only parser boundary counts pipeline construction: all three require zero pipelines, therefore zero SDP/media/network work. Cancellation leaves a newer capability sentinel untouched; receiver storage is freed before background completion. The testing define affects only the standalone fixture executable.
- `python3 plugins/pixelview-whep/tests/run-native.py --native-only`: actual source lifecycle, construction cancellation/replacement, OBS frame2/audio delivery cancellation, private settings, reconnect churn, and jitter. Its isolated runtime adds only official SDK videotestsrc/audiotestsrc test plugins; the shipping closure intentionally excludes them. No Homebrew GStreamer is linked.
- `python3 plugins/pixelview-whep/tests/run-upstream.py`: rebuilt only an isolated real production module and standalone harnesses. Tests fail-closed module pin denial with test-only dyld interposition and checks code residency after OBS unload; actual module jitter50/error; H26430 video frames, HEVC30 video frames, Opus28800 audio frames. No app rebuild or global staging.
- `git diff --check`: passed.

CMake and direct-C/include runners link video-format.c, profile-offer.c and capability-probe.c. Native/app fallback paths use `Pixelview Desktop.app`. The duplicate capability object in run-video-precision.py was removed.

## Remaining boundaries

These focused tests do **not** complete actual WHEP offer/answer → RTP → rswebrtc dynamic-pad decode → OBS delivery. Separate loopback and application verification is recorded in `integration-status.md`. No hardware playout or live credentials were exercised by these focused tests.

The H264 offer explicitly uses the fixture's constrained-baseline level4.2 `42c02a` and packetization-mode1; it does not inherit rswebrtc's profile-level-id. See `integration-status.md` for engine/loopback verification and the unresolved upstream probe EOS limitation. Module residency was tested after normal unload and probe timeout was tested in a standalone process; forced module unload while a deliberately stuck driver is active was not simulated.

The host's CommandLineTools AddressSanitizer crashes during its own allocator initialization, before main; no ASan-clean claim. LLDB noninteractive process debugging is permission-blocked. Native harnesses print expected OBS no-graphics-context cleanup and hotkey-permission diagnostics; production does not add raw endpoint/SDP logging.
