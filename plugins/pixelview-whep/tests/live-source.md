# Live production-pipeline regression

Run `python3 plugins/pixelview-whep/tests/live-source.py` from the repo root with the authorized viewer keeper and engine ingress already running. The harness reads `whep_url` from `/Users/max/src/pixelview-whep-spike/runtime/viewer-private.json` (override with `PIXELVIEW_LIVE_PRIVATE`). No credentials enter argv or pipeline text. Raw diagnostics are captured in mode600 `.test-build/live-private.log`; stdout contains sanitized error/caps/result lines.

The harness compiles the current native C implementation against an isolated copy of the built plugin runtime in `.test-build/live-runtime`. It invokes the actual `make_pipeline`, jitter callback, and native OBS video/audio delivery callbacks, not synthetic sources. It requires at least60 decoded video buffers,48000 audio sample frames, no bus error, and actual webrtcbin latency readback50. It does not exercise the GUI or the worker's control-proc lifecycle (covered separately by run-native.py). It never stages or rebuilds the app. To refresh the isolated runtime after a parent rebuild, remove only `.test-build/live-runtime` before running again.

Observed RED: video0/audio0/jitter50, upstream HTTP400 `failed to set local description: unable to start track, codec is not supported by remote`. Transceiver caps offered H264 profile42e016 with no packetization-mode (RFC6184 defaults to0). Engine mapper requires packetization-mode1. Decoder factories were present; this failed before decoding.

Minimal native fix: set H264 transceiver codec-preferences packetization-mode to string1 in on-new-transceiver, attached from webrtcbin-ready. Profile and payload mappings remain unchanged.

Observed GREEN on existing ingress: video60/audio141120/jitter50, repeated video61/audio143040/jitter50, both exit0. run-native.py lifecycle/cancellation and existing bundled-module checks also passed. The already-built module is not rebuilt by this test; parent must rebuild/package the C change for GUI acceptance.
