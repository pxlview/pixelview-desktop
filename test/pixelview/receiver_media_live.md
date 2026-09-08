# Native controller + packaged-worker live diagnostic

Run `python3 test/pixelview/receiver_media_live.py` from the repository. Requires the existing isolated backend/sender and private `session-private.json`. Opens the exact module from `PixelviewReceiveFinal.app`, links its libobs, uses actual PixelviewReceiver/NSURLSession login and registration, and calls actual `connect`, `get_status`, and `disconnect` procs. No graphics, capture device, audio monitoring, GUI, backend changes, staging or module rebuild.

## Observed root cause

Unmodified authentication endpoint yields `authorized=1 state=error video=0 audio=0 jitter=50`. The test-only worker bus ERROR probe reports **WHEP resource Location must be same-origin without userinfo**. The fresh login returns HTTP `localhost:8000`, whereas the preexisting successful private viewer endpoint uses HTTP `127.0.0.1:8000`; their paths and query key sets match. These hosts are different web origins even when both resolve to loopback. The security check must not be weakened.

A single-variable test-only override (`PIXELVIEW_TEST_CANONICAL_LOOPBACK=1 python3 test/pixelview/receiver_media_live.py`) changes only the fresh callback endpoint's loopback hostname to `127.0.0.1` and preserves its fresh authorization token/viewer registration. With the **unchanged exact packaged production module**, both actual worker sessions delivered video/audio:

- cycle0: video60, PCM sample frames132480, actual jitter50.
- cycle1: video60, PCM sample frames127680, actual jitter50.
- Stop asserted immediate idle and no subsequent video/audio counter increments for1.5 seconds; second authentication/restart delivered fresh media.

This proves the worker/cancellation/controller registration path works. It is **not** an unmodified GUI green result or a production fix. Correct the isolated backend's advertised endpoint/resource origin consistently, with explicit permission from its owner. Do not automatically rewrite arbitrary production URLs or accept cross-origin Location.

## Test-only diagnostics

`python3 plugins/pixelview-whep/tests/build-worker-probe.py` copies the packaged runtime into ignored `.test-build/controller-media/Probe.plugin`, compiles the actual source worker with one injected bus ERROR print in the generated test copy only, and never changes production logging. Select it with `PIXELVIEW_MEDIA_MODULE="$PWD/plugins/pixelview-whep/.test-build/controller-media/Probe.plugin/Contents/MacOS/probe"` when running the live script. Raw output goes only to a chmod600 private log. The Python wrapper prints allowlisted media summaries only. Production/native/controller sources remain unchanged.
