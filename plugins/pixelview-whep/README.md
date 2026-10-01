# Pixelview native WHEP source (macOS arm64)

Source ID: **`pixelview_whep_source`**. Native GStreamer WHEP receive input, not a browser source, pipeline editor, encoder, filter, or general-purpose GStreamer plugin. Based on the proven raw appsink/OBS integration pattern from Florian Zwoch's GPL-2.0-or-later obs-gstreamer; source attribution retained.

## Frontend API

Create the source with empty settings. Obtain `obs_source_get_proc_handler(source)` and use `proc_handler_call` with calldata:

| Proc | Calldata |
|---|---|
| `connect` | `endpoint` string; optional `latency` int (omitted: native upstream default; explicit 0–2000 ms) |
| `disconnect` | no arguments |
| `get_status` | outputs `state` string, `frames` int, `audio_frames` int, `latency` int, `jitter_latency` int, `failure` string, `ready` bool |

`failure` is empty or one of two canonical reasons for the current generation: `unsupported-hevc-main-422-10` (the sender is streaming HEVC 4:2:2 10-bit and this Mac's probe did not decode it) or `unsupported-hevc-profile` (another non-Main/Main10 HEVC profile). Both mean the receiver stopped before decoding and the operator must switch the sender to HEVC Main or Main10; Desktop renders that guidance. Connect and disconnect clear it. The profile string from the wire is never copied into telemetry or logs.

States: `idle`, `connecting`, `playing` (decoded video observed), `error`, `ended`. `frames` counts video buffers; `audio_frames` counts decoded PCM sample frames. Counters reset on connect. `latency` reports the explicit requested milliseconds, or **-1 when unset**; `jitter_latency` is **-1 until internal webrtcbin exists**, then the actual property readback. The `webrtcbin-ready` callback only sets the property when connect explicitly supplied an override. Omission leaves the native GStreamer default untouched; zero remains a valid explicit override.

Desktop defaults the receive jitter buffer to **100 ms** and passes it explicitly on every connection. A small **Buffer: 100 ms** link in the Receiving panel opens a 0–2000 ms setting; changes are persisted and apply to the next connection. The dialog recommends keeping 100 ms normally and increasing it if glitches occur. Other callers may still omit `connect.latency` to retain the native upstream default. This is a jitter-buffer target, not measured end-to-end latency. The pipeline, audio, DeckLink preroll and all other buffers are unchanged.

No saved-source migration is needed: Desktop creates private `Pixelview Receive` with null settings in private `Pixelview Receive Canvas`, neither loaded from scene JSON. The selected buffer is ordinary non-secret user configuration and is supplied through connection calldata; the plugin does not consume saved source settings.

Connect copies the URL into private worker memory; caller can immediately free calldata. Endpoint is never interpolated into pipeline text, logged, returned by telemetry, or used as a scene-setting transport. `endpoint` and legacy `pipeline` settings are erased on create/update/save; nothing starts from serialized settings. HTTPS required; loopback HTTP allowed for development. Embedded userinfo, fragment, overlong URL and invalid latency fail safely. The caller must never put credentials into source name/other arbitrary settings.

Control procs are thread-safe and asynchronous. Source destruction joins the media worker; teardown may wait for the upstream WHEP signaller's HTTP cancellation. Keep an OBS reference while calling. Visibility does not implicitly stop playback. Disconnect sets idle immediately and schedules media teardown. A bus error/EOS or 15 seconds without video ends the attempt, clears video, and surfaces a safe state; there is **no native automatic reconnect loop**. Frontend owns bounded viewer/token reauthorization and reconnect policy.

## Media path

- `whepclientsrc`: probed H264, HEVC Main/Main10/Main 4:2:2 10, VP9 profiles0/2 and Opus. The parsed CAPS selector (`codec-route.c`) is a stock capsfilter that pins the codec and HEVC profile for the attempt; it admits `main`, `main-10` and, when its probe bit is set, `main-422-10`. Any other parsed HEVC profile (and `main-422-10` without the probe bit) is refused before its first AU with `GST_STREAM_ERROR_WRONG_TYPE` and the `pixelview-unsupported-profile` details structure.
- Video: decodebin3 → `{P010_10LE, v210}` policy → clocked appsink → direct `obs_source_output_video2`. The patched vtdec picks v210 only for a 4:2:2 stream; everything else is P010. Eight-bit encoded sources are upconverted, not original ten-bit information.
- Audio: static bounded queue → audioconvert/audioresample → interleaved stereo F32/48 kHz → bounded clocked queue → synchronized appsink → `obs_source_output_audio`. There is no tee, probe or second PCM path: the DeckLink output consumes the stock OBS mix.
- Both branches use pipeline running-time PTS plus a common base clock; synchronized sinks preserve A/V timing. This is a CPU raw-frame path, not zero-copy.
- The source enables libobs async unbuffered mode because the synchronized appsinks already pace delivery on the pipeline clock. This avoids a second OBS-side video rebuffer; hardware smoothness remains to be checked.
- Media failures write credential-safe diagnostics to the normal OBS log: timeout or EOS, or the GStreamer error source/domain/code. Raw GStreamer messages and debug strings remain suppressed because they can contain credential-bearing URLs.
- H264/HEVC/VP9 decoding uses Apple's OS-provided VideoToolbox; Opus is bundled libopus. No proprietary codec binary or gst-libav/x264/x265/FDK plugin is shipped by this runtime packager. FFmpeg/x264/x265/libvpx in the loopback tests are independent test tooling only.

## Build and bundle

`cmake/macos/pixelview-build.sh` always stages the pinned runtime before CMake, including normal local builds. Missing deps or a changed input hash fails the build. Stage manually with:

```sh
python3 plugins/pixelview-whep/scripts/build-rswebrtc.py
python3 plugins/pixelview-whep/scripts/build-applemedia.py
python3 plugins/pixelview-whep/scripts/bundle-runtime.py stage .deps/pixelview-gstreamer
cmake --build build_macos --config RelWithDebInfo --target pixelview-whep -j 4
```

Configure the normal Pixelview preset/helper first. CMake links staged libraries and embeds the runtime in `pixelview-whep.plugin/Contents/Resources/GStreamer`; POST_BUILD rewrites module references, signs each nested Mach-O using the requested identity, then Xcode signs the plugin/app. Resources placement is intentional: a raw non-bundle directory under Frameworks fails Apple's nested-code validation. Developer ID/notarization acceptance is a parent release gate, not established by ad-hoc signing.

Pinned inputs: official public GStreamer1.28.3 runtime/development packages and **source-built patched rswebrtc0.15.2** and **source-built patched applemedia (gst-plugins-bad1.28.3)**. `runtime-lock.json` pins package SHA256s and every curated SDK-relative binary input. `fetch-gstreamer.py` verifies and extracts packages locally without running installers; libnice comes from that SDK. No host GStreamer/Homebrew runtime is used. See `scripts/UPSTREAM.md` for acquisition, trust boundaries and tests.

**Minimum supported packaged OS is macOS14**, from the actual bundled dylib load commands. Both the macOS preset and Pixelview helper explicitly target14.0; existing app builds must be rebuilt before acceptance. The official-distribution runtime closure has51 arm64 Mach-O files, with no X11 chain. The patched applemedia is built without Vulkan, so neither libgstvulkan nor MoltenVK is shipped. Read the current generated SBOM rather than the historical Homebrew inventory.

Runtime initialization refuses an already initialized external GStreamer registry, replaces all plugin search/scanner paths with the bundled paths, uses `/dev/null` rather than an external registry, disables Gst debug/dot dumps, and checks required plugin origins. Packaging recursively rewrites/validates all load commands; missing dylibs/factories fail. Do not initialize a second GStreamer distribution in the same process.

Do not run multiple stages/builds concurrently against the same `.deps/pixelview-gstreamer` directory: current staging recreates that directory. Parent should stage once before building. Source archive and build inputs live alongside it in `.deps/gst-build-inputs`. No system/Homebrew libraries are modified.

## Patched vtdec: reorder depth and 4:2:2 output

`patches/gst-plugins-bad-1.28.3-vtdec-reorder-v210.patch` changes
`sys/applemedia/vtdec.c` and `sys/applemedia/helpers.m`. Upstream sizes the HEVC output reorder queue from a
level-agnostic worst case (16 frames at 1080p) and cannot report that latency
when caps carry no framerate, as WebRTC caps do: every received HEVC frame was
held ~640 ms at 25 fps behind a declared 120 ms pipeline latency. The patch
takes the depth from the stream's SPS (`sps_max_num_reorder_pics` of the
highest sub-layer, 0 without B-frames) and keeps the upstream worst case only
when the codec data cannot be parsed. H.264 (baseline needs no reordering) and
VP9 are unchanged.

Upstream vtdec has no 4:2:2 raw output: a Main 4:2:2 10 stream asked for P010
is decoded in hardware and then subsampled to 4:2:0. The patch maps v210 to
`kCVPixelFormatType_422YpCbCr10` and prefers it when the input caps say
`chroma-format=4:2:2` (or `profile=main-422-10`) and downstream lists v210;
every other stream negotiates exactly as before. The receiver's raw policy is
`{P010_10LE, v210}`, so 4:2:0 streams still arrive as P010.

`build-applemedia.py` verifies the official gst-plugins-bad1.28.3 tarball
SHA256, extracts it fresh, applies the patch and builds only the applemedia
plugin with meson1.9.1/ninja1.13.0 from a private virtualenv, against the
pinned SDK (`-Dauto_features=disabled -Dapplemedia=enabled -Dgl=enabled`).
Output is `.deps/applemedia-upstream-patched/output/libgstapplemedia.dylib`
with provenance, the patch and LGPL text; it never stages the runtime, and the
packager rejects missing or stale provenance and never falls back to the SDK
module.

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
