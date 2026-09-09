# Synthetic WHEP acceptance — all five video alternatives pass

Run `python3 plugins/pixelview-whep/tests/run-whep-loopback.py` from the desktop checkout. `PV_LOOPBACK_CASE=main10` selects one case; the unfiltered command asserts all five media cases plus the HTTP406 negative. Results are saved in `tests/whep-loopback-results.json` and private `.test-build/whep-loopback/results.json`.

## Scope

- A test executable compiles the current production source and calls its registered `connect`, `get_status`, and `disconnect` procs, including the capability probe/worker, offer hooks, WHEP dynamic pads, appsinks, and real OBS audio/video delivery.
- Test-only wrappers clear default STUN, restrict libnice to127.0.0.1, inspect bus errors/decoded caps, and audit immediately before calling real libobs delivery. No shipping graph or raw callback is replaced. This is not a separately dlopened packaged-plugin/signature or GPU/canvas/display test.
- Offline Go uses the engine's pinned go.mod/go.sum and extracts current registration, parser/selector, semantic validation, per-offer H264 registration selection and binding adapter. The harness records the actual TrackLocal.Bind return, not sender.GetParameters().Codecs. Engine provenance hashes include `h264_binding.go`.
- HTTP, Pion/libnice ICE and UDP relays are loopback-only. FFmpeg uses `localaddr=127.0.0.1`. No credentials, auth service, external STUN/TURN, GUI app, capture hardware, scene/profile edits, global staging, full app build, commits, deploys or existing-server restarts are used. HOME and CFFIXED_USER_HOME are isolated. Existing OBS/Pixelview processes are untouched.
- The runtime is copied from the existing isolated decoder-profile runtime and linked only to that copy. Synthetic ramps, encoded metadata, SDP, logs, bound payloads and source hashes remain under private `.test-build/whep-loopback/`.

## Verified complete six-case run

| Alternative | Bound video PT | Decoded frames | Opus sample frames | First-frame luma codes |
|---|---:|---:|---:|---:|
| H264 constrained baseline4.2 | 97 | 30 | 92160 | 183 |
| HEVC Main | 96 | 31 | 95040 | 183 |
| HEVC Main10 | 120 | 32 | 96000 | 876 |
| VP9 profile0 | 98 | 31 | 87360 | 183 |
| VP9 profile2 | 121 | 33 | 91200 | 876 |

All media cases delivered stereo Opus PT99 with nonzero PCM energy, jitter50, and P010 limited-range SDR BT709 video through the real OBS delivery boundary. Both native ten-bit sources retained876 distinct first-frame luma codes (>256). Eight-bit sources are upconverted, not native ten-bit. Offers and answers contained only127.0.0.1 ICE candidates. The unsupported-server case received an actual POST, responded HTTP406, observed jitter50, entered error, and delivered zero audio/video.

## H264 binding correction

The actual probe SPS is `42 c0 2a`, and the desktop still truthfully offers `profile-level-id=42c02a;packetization-mode=1`. The engine's browser primary remains `42e01f`.

Pinned `github.com/pxlview/pion-webrtc/v4 v4.2.12-pxlview.1` compares the first two H264 profile bytes literally, at both MediaEngine negotiation and track binding. The original native `TestCapturedGStreamerOfferBinding` failed H264 with `unable to start track, codec is not supported by remote`; semantic selection alone was insufficient.

The engine now validates RFC6184 constrained-baseline masks, exact six-hex syntax, packetization mode1, asymmetry flag values and receiver level bounds. It selects one validated offered H264 fmtp for a fresh peer-local MediaEngine without mutating the shared mapper or expanding payloads. A narrowly scoped binding adapter preserves router RTP/sample writer registrations, PT/RTX and actual wire fmtp while bridging Pion's literal comparison. Other codec matching and pinned dependencies are unchanged. It is not a global MIME fallback or a rewritten desktop offer.

After binding was fixed, H264 correctly failed the production color gate because an old synthetic fixture lacked transfer/primaries. The harness now regenerates stale color metadata and explicitly sets x264/x265 BT709 VUI. AVC/HEVC require separate VUI fields; VP9 has a single BT709 color-space enum. Actual decoded caps remain audited in every case; no receiver color acceptance was weakened.

## Native verification and evidence

```
cd /Users/max/src/pv-engine
GOPROXY=off GOSUMDB=off go test ./...
PV_TEST_GST_OFFER=/Users/max/src/pixelview-desktop/plugins/pixelview-whep/.test-build/whep-loopback/h264/offer.sdp GOPROXY=off GOSUMDB=off go test ./internal/av/whep -run 'TestCapturedGStreamerOfferBinding|TestH264PionReceiverBinding|TestWHEPFlexFECNegotiation' -count=1 -v
```

Both commands pass. Native binding tests cover browser42e01f, desktop42c02a, equivalent42/4d/58 constraint patterns, RTP and sample tracks, RTX association, invalid/incompatible/below-minimum profiles, and shared mapper immutability. Captured GStreamer SDP binds all five video alternatives and stereo Opus. Existing HEVC explicit180/legacy Main and FlexFEC behavior remain tested.

RED/GREEN and full-suite logs are saved under `.test-build/whep-loopback/engine-binding-logs/`; per-case receiver/server logs, encoded metadata, offer/answer and bound.json are beside each case. Headless libobs emits expected missing-graphics-context teardown diagnostics.

## Remaining boundaries

This short1024x128 ramp test proves transport/profile binding and the OBS delivery boundary, not sustained HD60 throughput, end-to-end display precision, calibration, Internet recovery or A/V endurance. The mapper's level3.1 H264 / implicit level93 HEVC minimum is not the live encoder/passthrough SPS/VPS PTL. Actual live-output profile/level admission remains unverified and must be enforced separately; copying a receiver capability does not certify arbitrary live bitstreams. This does not establish packaged-app integration, release acceptance or hardware capture/playback.
