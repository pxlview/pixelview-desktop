# Pixelview encoding preferences

Pixelview keeps the native editable OBS preview on the right of the settings column. The canvas/output remain1920×1080; the existing FPS control still selects the exact OBS frame rate. No stream or recording is started by changing preferences.

## Actual OBS integration

The first initialization chooses from registered OBS encoders, excluding internal/deprecated entries and non-streaming codecs. On Mac, Apple hardware HEVC is preferred, then hardware HEVC, other hardware H.264/AV1, and x264 fallback. This OBS branch has no hardware capability bit. Pixelview conservatively recognizes exact public backend IDs for VT, NVENC, QSV, AMF and VAAPI, and only offers them when OBS actually registers them. Unknown IDs are excluded rather than guessed from display names or texture capabilities. Names are encoder names, not invented physical GPU devices.

Apple Silicon hardware IDs are `com.apple.videotoolbox.videoencoder.ave.avc` and `.ave.hevc` (matching upstream SimpleOutput and native VT hardware metadata); the legacy Intel `.h264.gva` ID is identified by upstream AdvancedOutput. The `.h264` and `.hevc.vcp` software IDs are excluded. Other allowlisted IDs come from `plugins/obs-nvenc/nvenc.c`, `plugins/obs-ffmpeg/obs-ffmpeg-nvenc.c`, `plugins/obs-qsv11/obs-qsv11.c`, `plugins/obs-ffmpeg/texture-amf.cpp` and `plugins/obs-ffmpeg/obs-ffmpeg-vaapi.c`. Registration/classification does not establish successful device/session initialization.

Settings are written to the current isolated Pixelview profile:
- `basic.ini`: `Output/Mode=Advanced`, `AdvOut/Encoder`, `AdvOut/AudioEncoder`, `AdvOut/ApplyServiceSettings=false`, `AdvOut/RescaleFilter=OBS_SCALE_DISABLE` (the actual AdvancedOutput consumer key, not the obsolete boolean `Rescale`).
- `streamEncoder.json`: native encoder properties. This OBS version reads `AdvOut/Encoder`, **not** `AdvOut/StreamEncoder`.

Opus is selected by registered codec; no audio controls are exposed. Existing OBS advanced track bitrate defaults remain in use. Active video/outputs and pending native stream setup block edits and disable controls. The real AdvancedOutput replacement is constructed before committing basic.ini and swapping handlers, without starting it. The prior handler remains alive on construction/save failure. JSON and video rollback failures are explicitly reported; deletion uses nonthrowing filesystem error reporting. This is an in-process rollback transaction, not a crash-atomic commit across two files.

## Defaults and overrides

Quick video bitrate accepts1–12Mbps, initially6Mbps (6000kbps). The Advanced dialog uses upstream `OBSPropertiesView`, including the encoder's native modified callbacks and actual property choices. Advanced bitrate values outside the quick range remain saved and are shown as actual kbps in the control tooltip. Merely refreshing or focusing a control does not clamp them; editing quick bitrate explicitly replaces that field.

New encoder selections initialize native defaults, then only supported properties are set:
- CBR where offered; otherwise the native rate-control default.
-1-second keyframes, B-frames off (`bframes=false` or `bf=0` where exposed).
- HEVC Main where offered.
- H.264 baseline where offered, including Apple hardware H.264; only x264 receives ultrafast and zerolatency.
- Spatial AQ retains the plugin's default. For Apple VT this is Auto (`spatial_aq_mode=1`), which deliberately disables spatial AQ under CBR in this plugin; Auto does not mean forced On.

Advanced changes, except prohibited B-frame overrides, persist through refresh/restart and subsequent basic bitrate/profile changes.

B-frames are a permanent Pixelview frontend policy, not just an initial default. The loader merges saved `streamEncoder.json` into native defaults and then enforces `bframes=false` for VT (integer `bframes=0` for QSV) and `bf=0`. Startup saves that normalized data through the existing transaction and reconstructs the native output handler before it can be reused. Every save normalizes before and after native property callbacks. If startup normalization cannot be committed, the existing rollback/warning behavior remains; the failure must not be reported as a successful migration.

The x264 consumer parses space-separated `x264opts` after preset/tune selection and applies options in order. Pixelview removes competing `bframes=…` tokens, preserves unrelated tokens, and appends one final `bframes=0`, idempotently. This prevents an advanced options string or slower preset/tune from overriding the policy. Other native encoder defaults, profile/color mapping, bitrate, keyframe and spatial AQ overrides remain untouched.

The native NVENC registrations also remove accepted `frameIntervalP` overrides from `opts` and normalize incompatible UHQ tuning to the native supported HQ value, since UHQ otherwise forces B-frames after initialization. For native AMF AVC/AV1, codec-specific direct B-picture count overrides are removed from `ffmpeg_opts`; exact names and integer types were verified against official AMF SDK commit `c35f613aea2e5057a688c979e75b1cf24253297e`. Unrelated option bytes and other backends are preserved. These guards are tested against the real OBS options parser and backend source consumers, not NVIDIA/AMD hardware on this Mac.

The Advanced dialog hides `bframes`, `bf` and `bframe_ref_mode`, recursively in groups, **without deleting property objects or replacing native callbacks**. A narrowly scoped properties-view pre-render signal reapplies the visibility policy after every native modified callback/reload that rebuilds the form. Tests compile the real filtering function against repeated callback/reload/group scenarios and verify native callback identities survive. Switching to a different encoder explicitly initializes that encoder's defaults. Cancel in Advanced discards changes.

Periodic refresh does not rebuild unchanged profile entries and defers model/selection synchronization while a selector has focus or its popup is visible.

## HEVC profiles and color

Profiles come from the native property list, not a hard-coded UI menu. Native enumeration alone does not prove that the device can successfully encode a profile.

Pixelview explicitly maps Main→NV12, Main10→P010, Main42210→P216 and resets the OBS video pipeline before saving. Main42210 with full range is rejected because the native VT P216 path requires limited range. Unknown HEVC mappings are rejected with an explanation. Existing colorspace/range are preserved; ordinary profiles therefore remain SDR Rec.709 rather than silently enabling HDR.

Upstream `obs_to_vt_profile` only promotes Main to Main10 when the existing encoder input is P010. Choosing a profile does **not** generally choose P010/P216 or change the OBS canvas format automatically.

Ten-bit encoding does not restore source detail that was already captured in eight bits. DeckLink capture mode/pixel format and the native Allow10Bit setting are independent and are not silently modified.

## Verification boundary

`python3 -m unittest discover -s test/pixelview -p 'test_*.py'` exercises compiled policy, actual SavePixelviewEncoding and profile-refresh source blocks with fault-injected OBS/Qt I/O, and source integration contracts. The save harness also executes AdvancedOutput's actual rescale-consumer statements against preexisting rescale settings for Main/Main10/Main42210. These are headless behavior tests, not a full AdvancedOutput or Qt runtime. GUI build, properties interaction, restart persistence, and actual encoded packet/profile/bit-depth validation are separate acceptance checks. No claim of hardware support follows from these unit tests alone.
