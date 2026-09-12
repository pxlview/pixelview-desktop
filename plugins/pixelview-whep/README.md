# Pixelview native WHEP source (macOS arm64)

Source ID: **`pixelview_whep_source`**. Native GStreamer WHEP receive input, not a browser source, pipeline editor, encoder, filter, or general-purpose GStreamer plugin. Based on the proven raw appsink/OBS integration pattern from Florian Zwoch's GPL-2.0-or-later obs-gstreamer; source attribution retained.

## Native422 diagnostic checkpoint

`get_status` additionally returns `native422_diagnostic`: empty until a known
native422 finite refusal, then bounded canonical reason/uint64 data for the
current generation. The first snapshot survives error teardown; connect and
disconnect clear it. Arbitrary Gst errors/debug/URLs are never copied into this
field or the production terminal warning. Main422 is enabled in normal builds for the user-authorized strict1080p25 hardware test (not certified); see
[the bounded campaign and unresolved acceptance gate](../../docs/pixelview-422-diagnostic-results.md).

## Frontend API

Create the source with empty settings. Obtain `obs_source_get_proc_handler(source)` and use `proc_handler_call` with calldata:

| Proc | Calldata |
|---|---|
| `connect` | `endpoint` string; optional `latency` int (omitted: native upstream default; explicit 0–2000 ms) |
| `disconnect` | no arguments |
| `get_status` | outputs `state` string, `frames` int, `audio_frames` int, `latency` int, `jitter_latency` int, `native422_frames` int |

States: `idle`, `connecting`, `playing` (decoded video observed), `error`, `ended`. `frames` counts video buffers; `audio_frames` counts decoded PCM sample frames. Counters reset on connect. `latency` reports the explicit requested milliseconds, or **-1 when unset**; `jitter_latency` is **-1 until internal webrtcbin exists**, then the actual property readback. The `webrtcbin-ready` callback only sets the property when connect explicitly supplied an override. Omission leaves the native GStreamer default untouched; zero remains a valid explicit override.

Desktop now omits `connect.latency`, superseding the earlier user-requested 50 ms experiment. No numeric upstream default is hardcoded: the pinned GStreamer1.28.3 runtime was observed at **200 ms** in the hardware-free production test, which compares against a fresh `webrtcbin` and verifies no latency property notification/write on omission. Explicit0/50/100/2000 overrides and invalid rejection remain tested. This is a jitter-buffer target, not measured end-to-end latency. The pipeline, audio/UI, native422 routing, DeckLink preroll, finite-rate/freshness/acquisition limits and all other buffers are unchanged. Historical explicit50 fixtures remain override evidence, not default acceptance.

No saved-source migration is needed: Desktop creates private `Pixelview Receive` with null settings in private `Pixelview Receive Canvas`, neither loaded from scene JSON. The plugin does not consume saved `settings.latency`; its unused OBS latency-setting default has been removed. Only explicit connection calldata selects an override. The running app is unchanged until a later build and relaunch; no app build/restart or live jitter/glitch acceptance was performed.

Connect copies the URL into private worker memory; caller can immediately free calldata. Endpoint is never interpolated into pipeline text, logged, returned by telemetry, or used as a scene-setting transport. `endpoint` and legacy `pipeline` settings are erased on create/update/save; nothing starts from serialized settings. HTTPS required; loopback HTTP allowed for development. Embedded userinfo, fragment, overlong URL and invalid latency fail safely. The caller must never put credentials into source name/other arbitrary settings.

Control procs are thread-safe and asynchronous. Source destruction joins the media worker; teardown may wait for the upstream WHEP signaller's HTTP cancellation. Keep an OBS reference while calling. Visibility does not implicitly stop playback. Disconnect sets idle immediately and schedules media teardown. A bus error/EOS or 15 seconds without video ends the attempt, clears video, and surfaces a safe state; there is **no native automatic reconnect loop**. Frontend owns bounded viewer/token reauthorization and reconnect policy.

## Media path

- `whepclientsrc`: probed H264, HEVC Main/Main10, VP9 profiles0/2 and Opus. The parsed CAPS selector leaves ordinary compressed video on a stock capsfilter; only explicit Main42210 creates the native tap/raw-preview queue.
- Ordinary video: decodebin3 → P010 limited709 policy → clocked appsink → direct `obs_source_output_video2`. Eight-bit encoded sources are upconverted, not original ten-bit information. Only optional native422 preview uses latest-sample worker delivery; teardown joins it before a new ordinary generation.
- Audio: static bounded queue → audioconvert/audioresample → interleaved stereo F32/48 kHz → bounded clocked queue → synchronized appsink → `obs_source_output_audio`. There is no tee or duplicated early-audio queue/sink. A supported pre-queue buffer probe supplies native timestamped PCM before OBS clock waiting; it exists during codec preselection (audio can arrive first), then removes itself on the next audio buffer after ordinary parsed CAPS selection. Selection immediately disables private native copies and clears any preselection native feed. Rendered output consumes the stock OBS mix, not a second source PCM feed.
- Both branches use pipeline running-time PTS plus a common base clock; synchronized sinks preserve A/V timing. This is a CPU raw-frame path, not zero-copy.
- **Limited709 SDR only:** ten-bit420 preview precision is documented in `tests/video-precision.md`; native422 work and incomplete DeckLink acceptance are documented in `../../docs/pixelview-422-implementation-status.md`. Main422 is enabled in normal builds for the user-authorized strict1080p25 hardware test (not certified). A new production `request-encoded-filter` tap can decode admitted Main42210 to public native x422 and exactly packed v210, exposing a version2 route-bound (native early PCM; legacy rendered route no longer produces PCM) source-bound `native422_feed(ptr request, out int version)` pull API with owned bounded video/source-only audio queues and explicit attach/reset/detach (see `source-feed.h`). No consumer callback runs under decoder/lifecycle locks. The native DeckLink owner implements source-bound v210 and timestamped A/V scheduling; ordinary receive uses the stock rendered output and OBS mixed audio. Physical SDI fidelity/cadence and sustained A/V acceptance remain unverified; see `../../docs/pixelview-decklink-stock-rendered.md` and `../../docs/pixelview-main10-receive-audit.md`. Consumers own destination buffers and retain the exact OBS source for each synchronous proc call; reset requires flushing card A/V and fresh route admission.
- H264/HEVC/VP9 decoding uses Apple's OS-provided VideoToolbox; Opus is bundled libopus. No proprietary codec binary or gst-libav/x264/x265/FDK plugin is shipped by this runtime packager. FFmpeg/x265 in native422 tests are independent test tooling only.

## Build and bundle

`cmake/macos/pixelview-build.sh` always stages the pinned runtime before CMake, including normal local builds. Missing deps or a changed input hash fails the build. Stage manually with:

```sh
python3 plugins/pixelview-whep/scripts/build-rswebrtc.py
python3 plugins/pixelview-whep/scripts/bundle-runtime.py stage .deps/pixelview-gstreamer
cmake --build build_macos --config RelWithDebInfo --target pixelview-whep -j 4
```

Configure the normal Pixelview preset/helper first. CMake links staged libraries and embeds the runtime in `pixelview-whep.plugin/Contents/Resources/GStreamer`; POST_BUILD rewrites module references, signs each nested Mach-O using the requested identity, then Xcode signs the plugin/app. Resources placement is intentional: a raw non-bundle directory under Frameworks fails Apple's nested-code validation. Developer ID/notarization acceptance is a parent release gate, not established by ad-hoc signing.

Pinned inputs: official public GStreamer1.28.3 runtime/development packages and **source-built patched rswebrtc0.15.2**. `runtime-lock.json` pins package SHA256s and every curated SDK-relative binary input. `fetch-gstreamer.py` verifies and extracts packages locally without running installers; libnice comes from that SDK. No host GStreamer/Homebrew runtime is used. See `scripts/UPSTREAM.md` for acquisition, trust boundaries and tests.

**Minimum supported packaged OS is macOS14**, from the actual bundled dylib load commands. Both the macOS preset and Pixelview helper explicitly target14.0; existing app builds must be rebuilt before acceptance. The official-distribution runtime closure has53 arm64 Mach-O files, with no X11 chain; applemedia brings MoltenVK through its Vulkan dependency. Read the current generated SBOM rather than the historical Homebrew inventory.

Runtime initialization refuses an already initialized external GStreamer registry, replaces all plugin search/scanner paths with the bundled paths, uses `/dev/null` rather than an external registry, disables Gst debug/dot dumps, and checks required plugin origins. Packaging recursively rewrites/validates all load commands; missing dylibs/factories fail. Do not initialize a second GStreamer distribution in the same process.

Do not run multiple stages/builds concurrently against the same `.deps/pixelview-gstreamer` directory: current staging recreates that directory. Parent should stage once before building. Source archive and build inputs live alongside it in `.deps/gst-build-inputs`. No system/Homebrew libraries are modified.

## Patched WHEP signaling boundary

`patches/rswebrtc-0.15.2-same-origin.patch` is an MPL-preserving change to the
upstream WHEP client only. POST redirects are rejected (the backend returns
201 directly); PATCH and DELETE never auto-follow redirects. Session Location
is resolved against the **actual accepted response URL**, not the mutable
endpoint setting, and must retain scheme, host and effective port with no
userinfo. Relative and same-origin absolute resources remain supported.
The native source still owns initial endpoint validation.

`build-rswebrtc.py` requires rustup's pinned1.94.0 toolchain and builds only the
registry crate's library, with `--locked --release`, upstream default features,
arm64 and deployment target14.0. It verifies the original crate SHA256 before
extracting and applying the checked-in patch. The separate output is
`.deps/rswebrtc-upstream-patched/output/libgstrswebrtc.dylib`; it never stages the runtime.
The packager rejects missing/stale/tampered output or patch provenance, never
falls back to Homebrew, and records the built input hash in the runtime SBOM.
Source archive, applied patch, manifest, lock and provenance accompany its MPL
notice. This is a pinned build recipe, not a claim of bit-identical output
across different SDK/linker versions.

Reproduce the security regression at the actual Rust signaller seam:

```sh
# Expected failure: six security tests fail on original upstream source.
python3 plugins/pixelview-whep/scripts/build-rswebrtc.py --test --unpatched
# Expected success: six security tests plus three upstream unit tests.
python3 plugins/pixelview-whep/scripts/build-rswebrtc.py --test
python3 plugins/pixelview-whep/scripts/build-rswebrtc.py
```

The real two-origin HTTP tests observe zero unauthorized target requests for
201/406 bad Location and301/302/303/307/308 POST/PATCH/DELETE redirects.
Additional parser cases use explicitly constructed accepted HTTPS responses
to isolate TLS downgrade, host, port, userinfo and default-port normalization;
those matrix cases are not claimed as real TLS-server tests.

## Executed verification

```sh
python3 plugins/pixelview-whep/tests/test_packaging.py
python3 plugins/pixelview-whep/tests/run-native.py
python3 plugins/pixelview-whep/tests/run-codecs.py
python3 plugins/pixelview-whep/scripts/bundle-runtime.py verify .deps/pixelview-gstreamer
```

- RED/GREEN lifecycle/private-setting, media/proc and actual jitter readback tests.
- Native synthetic raw A/V, repeated connect/disconnect, invalid HTTPS policy.
- Production Xcode plugin built; `codesign --verify --deep --strict` passed.
- Real module open/init with current built libobs, source creation, actual WHEP signaller/internal webrtcbin readback **50**, closed-loopback failure safely reported `error`, and destruction passed with GST/DYLD environment cleared.
- Bundled-only encode/decode harness: **H26430 video frames**, **HEVC30 video frames**, **Opus28,800 audio frames** through the same native OBS output callbacks. Fixtures are generated in-process, not a claimed live stream.
- Factory inspection/closure verification covers scanner, WHEP, libnice, RTP/depay/parsers, VideoToolbox, Opus, DTLS/SRTP. No graphics device, capture card, audio monitoring or existing stream is opened by these harnesses. OBS logs harmless null-graphics cleanup diagnostics in headless tests; repeated synthetic audio restarts can report timestamp smoothing.

Logs in `/tmp/pixelview-whep-{build,native,codecs}.log`; generated executables/isolated runtimes in `.test-build/` (ignored). **Not yet verified here:** live engine authentication/media, DeckLink output with the new source, long-run drift/loss/reconnect, GUI integration, clean Mac, signed Developer ID/notarization. Parent performs integrated app/media acceptance. Existing stock OBS receiver was not altered or stopped.

See [LICENSES.md](LICENSES.md) for actual license findings, generated notices/SBOM/provenance, and explicit pending source-hosting/transitive Rust/legal release gates. Do not represent the inventory as legal clearance.
