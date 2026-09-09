# Offline receive profile policy and engine compatibility

## Authorized engine fix: current integration contract (supersedes audit below)

Production Main/Main10 are established working paths; the historical audit below
is an offline selector audit, not evidence that production streams were broken.
No production server was deployed/restarted. No Safari device SDP was captured.

Engine changes replace byte-equality with parsed HEVC profile/level/tier/constraint
checks. Unknown extensions and order do not break matching; unsupported PTL,
duplicate parameters and profile3 as422 fail closed. RFC7798 missing profile means
Main only, never infer Main10 from it. Explicit known level180 Main/Main10 variants
remain supported. Main42210 is profile4 with `interop-constraints=1d0800000000`
(frame-only, max12/max10/max422, lower-bit-rate constraints); no desktop422 offer.
The x265 fixture trace independently reports profile4 and those range constraints
(with progressive-source flag additionally set, yielding9d08).

Primary Main/Main10 track capabilities explicitly identify their profile but omit
level-id (RFC default93). This is important with pinned Pion's generic HEVC matcher:
a primary hardcoded level93 otherwise binds Main10 to Main's first payload when
both remote payloads use123. A real failing Bind test reproduced that new-offer
case. Existing explicit180 variants and profile-less Main93 Pion bindings pass.

**Remaining engine boundary:** the selector knows formatID, not the actual live
SPS/VPS PTL. It checks mapper/default required level93 (or explicit output level
when supplied to `codec.MatchesReceiver`), not a proven live stream requirement.
This does not implement full stream-level admission, bitrate/DPB conformance,
SDP sublayer negotiation or high-tier support. Do not call this complete PTL
validation for arbitrary passthrough sources. H264 fallback must now be present
on an active video m-line and advertise packetization-mode1; Pion still handles
its profile binding. Preference order, transcode filtering and Opus are unchanged.

Parent should prefer:

```c
struct pixelview_receive_limits limits = {
    .profiles = verified_mask,
    .hevc_level_id = verified_level, // 123 for locally sampled HD60 HEVC
    .max_width = 1920, .max_height = 1080, .max_fps = 60
};
GstCaps *out = pixelview_profile_offer_caps_limited(caps, &limits);
```

Call in on-new-transceiver for video AND audio, set returned caps and unref.
NULL/empty video must stop safely. Keep `<H265,H264,VP9>` and `<OPUS>`. This API
validates configured HD bounds and HEVC picture/sample-rate level capacity; it
cannot encode an exact HD ceiling into RFC7798 SDP. **Enforce matching width,
height and fps bounds on the raw receiver pipeline too.** The older three-argument
`pixelview_profile_offer_caps` remains available for unconstrained test fixtures;
it signals the caller's exact valid level without inventing180. The limited API
rejects dimensions above1920x1080, fps above60, and HEVC levels above123. For
1080p30 level120 has sufficient sample-rate capacity;1080p60 requires123. These
are policy envelopes, not universal hardware certification or safe defaults on
unprobed devices.

Verification commands:

```sh
python3 plugins/pixelview-whep/tests/run-profile-offer.py
python3 plugins/pixelview-whep/tests/engine-profile-selection.py
python3 plugins/pixelview-whep/tests/run-profile-hd-decode.py
# in pv-engine:
PV_TEST_GST_OFFER=/Users/max/src/pixelview-desktop/plugins/pixelview-whep/.test-build/profile-offer/policy-whep.sdp go test ./internal/av/whep -run TestCapturedGStreamerOfferBinding -v
go test ./...
```

HD decode runner reuses the isolated signed decoder-profile harness. Actual
1920x1080 samples decoded24 frames each in hardware: HEVC Main/Main10 level4.1,
main tier,60fps caps; VP9 profiles0/2 (existing harness timestamps VP9 at24fps).
Main10/VP92 explicitly output P010; eight-bit outputs NV12. This is short sample
decoding, not sustained60fps, all-level bitstream conformance, OBS integration or
physical output. Results remain in private `.test-build/profile-offer/hd-decode`.
Full app integration and output acceptance belong to the parent worker.

## Historical audit (before the authorized fix)

## Run (no app build, staging, engine writes or credentials)

From the desktop checkout:

```sh
python3 plugins/pixelview-whep/tests/run-profile-offer.py
python3 plugins/pixelview-whep/tests/engine-profile-selection.py
# Intentional negative control: exits nonzero on unprobed video advertisement.
python3 plugins/pixelview-whep/tests/run-profile-offer.py --legacy
```

The first runner copies `.deps/pixelview-gstreamer-upstream` once into a private
`.test-build/profile-offer/runtime`, compiles only the C helper/harness against the
pinned official SDK, and captures actual GStreamer offers. Delete that isolated
runtime copy when intentionally changing the runtime under test. The raw-output
`whepclientsrc` capture uses a closed loopback endpoint, explicitly disables the
upstream default Google STUN server, and reads local-description before any
answer. Local offers may contain host ICE candidates; do not publish them.
`raw-whep.caps` records the actual pre-policy transceiver caps.

The engine test compiles the exact current `parseSupportedCodecs` and
`findBestCodecMatch` function bodies read from `/Users/max/src/pv-engine`, removing
logging and substituting small external-type shims only. It extracts actual
format/preference/fmtp fixtures from the current mapper, retains its Go module
pins, and uses cached modules with GOPROXY/GOSUMDB off. Generated source, fixture,
and SHA256 provenance live in the isolated test directory. Router transcoding
is exercised; config-driven 4K limiting is not. This tests selection, NOT Pion
answer binding, RTP decoding, latency, hardware capability or physical output.
Python's stdout newline normalization and the harness's final blank line are
removed/restored to canonical SDP CRLF before passing offers to Pion's parser.

## Findings (engine source 759cc12ad191f6e28178049722322cbdea3304c8)

- `internal/av/codec/mapper.go:88-129`: Main and Main10 BOTH accept ambiguous
  `level-id=93;tx-mode=SRST`. Never use that to advertise Main-only hardware:
  the selector will consider it Main10 support as well.
- Their only explicit accepted forms are, byte-for-byte and in this order:
  - Main: `level-id=180;profile-id=1;tier-flag=0;tx-mode=SRST`
  - Main10: `level-id=180;profile-id=2;tier-flag=0;tx-mode=SRST`
- `mapper.go:165` incorrectly labels Main42210 with
  `level-id=93;profile-id=3;tier-flag=0;tx-mode=SRST`. HEVC profile 3 is Main
  Still Picture, NOT Main42210. Main42210 belongs to range extensions
  (general_profile_idc 4 plus the applicable constraint flags). Do not copy the
  engine's incorrect profile 3 to get a successful selector result. Proper 422
  negotiation requires an engine fix and verified receiver/decoder/output path.
- `server.go:408-510,578-617`: parses rtpmap/fmtp into a payload-keyed map, then
  chooses in engine preference order. Client m-line codec order does not override
  engine preference. Fmtp is exact-string matched, not a parameter dictionary:
  reordering, added parameters, whitespace, or explicit level153 instead of180
  cause HEVC to fall back. Blank VP9 fmtp is accepted only as profile0.
- `server.go:581-583` unconditionally selects H264 if reached, even if absent
  from the offer. A selected format is not proof a subsequent answer/track binds.
- VP9 profile0 = 8-bit420, profile2 = 10/12-bit420, profile3 = 10/12-bit422/444.
  `mapper.go:339,368,398` uses 2 for 10-bit420 and the SAME 3 for 10-bit422 and
  10-bit444; profile-id alone cannot constrain these bit depths/chroma variants.
  The helper intentionally offers only separately verified 0 and/or2. No VP93.
- Current `pixelview-player/src/composables/player/TrickleWHEPAdapter.js:145-171`
  adds recvonly video/audio and uses the browser's `createOffer()` unchanged for
  video. Its only SDP edit adds Opus NACK. Safari detection in
  `composables/auth/useLogin.js:117-134` is login metadata, not HEVC profile
  discovery or policy. No actual Safari device offer was captured in this audit.

Standards reference: RFC7798 section7.1 defines profile-id/general_profile_idc,
level-id/general_level_idc and interop-constraints. SDK
`gst/codecparsers/gsth265parser.h:162-164` explicitly identifies Main=1,
Main10=2, MainStillPicture=3. Level180 is level6, not an innocuous string copied
from Safari; level153 is5.1 and93 is3.1. Hardware factory availability or a tiny
roundtrip cannot establish level6 capability.

## Real bundled behavior and policy

Pinned raw-output rswebrtc emits one payload per codec name, with no H265 fmtp,
no VP9 fmtp, H264 `profile-level-id=42e016` without packetization mode, and Opus
`OPUS/48000` without `/2`. The helper sets H264 mode1 (preserving its original
profile-level-id), explicitly describes stereo Opus, and expands verified Main,
Main10, VP90 and VP92 into independent RTP payload alternatives. It removes and
reinserts HEVC string fields in engine order: GstSDP preserves insertion order
and only string-valued fmtp fields are appropriate here.

A real failing test caught naive extra-payload allocation colliding with Opus:
rswebrtc adds video at96/97/98, then audio99 AFTER the new-video-transceiver hook.
Taking the next free PT99 broke BUNDLE, including duplicated automatic repair
payloads. Extra profiles now take120/121, leaving upstream's low original PTs
alone; actual bundled offers pass uniqueness checks across all primary, RTX,
RED and ULPFEC entries, while retaining upstream NACK behavior. This allocation
is deliberately scoped to one raw-output H264/H265/VP9 video transceiver and one
Opus audio transceiver. Additional tracks/codecs need a session-wide allocator.
RED/ULPFEC/RTX are upstream repair formats, not alternate primary audio codecs;
no interoperability with engine FlexFEC is claimed.

## Integration contract for parent

Only `profile-offer.[ch]` and new tests are owned by this work; no changes were
made to `pixelview-whep.c` or CMake. Parent should:

1. Add `profile-offer.c` to plugin and native-harness compile sources and include
   `profile-offer.h`. Include it in tests that directly include production C.
2. Keep `video-codecs="<H265,H264,VP9>" audio-codecs="<OPUS>"` (or the same
   three video codecs reordered). Profile names are NOT accepted codec names.
3. At existing `on-new-transceiver` callback, get codec-preferences, call
   `pixelview_profile_offer_caps(caps, verified_mask, verified_hevc_level_id)`,
   set its returned caps and unref both. Apply to audio too for Opus `/2`.
   The tests' `transceiver()` callback demonstrates the integration seam.
4. Pass an immutable per-session probe result; do not hardcode mask31/level180
   from the negotiation tests. Those are test fixtures, not hardware claims.
   Main/Main10 need independently verified receive capability AND level>=180
   until the engine accepts proper semantic level negotiation. A weaker level
   intentionally removes HEVC. No implicit Main support from Main10 is assumed.
5. Treat NULL or empty video result as unsupported and stop safely, never leave
   permissive original/ANY caps active. With no verified mask, no video codec is
   offered. Preserve the separately verified H264 fallback if available.
6. Await the hardware/capability worker before setting real defaults; then test
   actual Pion answer binding and end-to-end decoding/output. Main42210 remains
   blocked rather than advertised falsely. Hardware-supported422 alone does not
   cure the engine's incorrect profile-id.

Observed RED→GREEN cycles cover unprobed defaults, Main/H264/Opus offer fields,
profile expansion, cross-transceiver payload collision and stereo Opus. The
engine suite exercises passthrough, VP9 fallback, required transcoding, ordering,
extra-fmtp, level mismatch, the ambiguous legacy HEVC form, incorrect422 ID,
VP93 selection, and the unconditional H264 fallback.
