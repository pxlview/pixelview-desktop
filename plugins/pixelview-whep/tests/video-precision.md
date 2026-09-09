# Limited-range SDR receive precision implementation

## Verified boundary

Run `python3 plugins/pixelview-whep/tests/run-video-precision.py` on the existing Apple Silicon build. It copies the curated GStreamer runtime into a temporary directory, compiles only standalone tests against existing libobs, and uses private HOME/CFFIXED_USER_HOME. No authentication, network media, capture hardware, full app build, staging, or configuration persistence is involved.

Actual M1 Pro execution after the production changes:

| Path | Result |
| --- | --- |
| HEVC Main10 lossless 1024x128 ramp → `vtdec_hw` → **production `video_sample`** → real `obs_source_output_video2` | 90 deliveries; every first-row input code 64..940 remains exact in P010 at the OBS call boundary |
| Same decoded ramp → NV12 SDR main canvas | 256 distinct red values; 125 readbacks |
| Same decoded ramp → **production frontend precision-reset method**, P010/709/partial main canvas | 877 distinct red values; 124 readbacks |
| Production frontend method restores sender canvas | Actual reset returns to NV12/709; asserted after GPU readback |
| Full-range encoded Main10 → `vtdec_hw` → production callback | Decoded caps retain `1:3:5:1` full-range colorimetry; callback rejects the actual decoded sample, zero OBS deliveries |
| H264 eight-bit and HEVC Main eight-bit → native NV12 callback | 30 frames each |
| Same eight-bit codecs → production P010 raw caps → callback | 30 frames each; this is **upconversion**, not original ten-bit information |
| Opus encode/decode → unchanged production audio callback | 28,800 audio frames in each codec test run |

`test/pixelview/test_receive_precision_canvas.py` executes the actual frontend transaction body with reset-failure injection. It verifies output-busy refusal, save/restore without profile writes, rollback after failed entry/exit, and sticky fault after failed rollback. Seven selected offline UI/policy tests passed, including native Qt receive lifecycle and unavailable-credential behavior. The UI-only fixture uses a reset seam; the separate precision probe above executes the same production reset body with real libobs/OpenGL. `git diff --check` passed.

Expected test-only warnings: missing hotkey permissions, deprecated `obs_add_data_path`, Qt font alias warning, and the older audio/codec-only harness's no-graphics-context cleanup debug messages. These are not claims of a warning-free full app build.

## Production policy

- `video-format.c/.h` accepts P010, NV12, BGRA, and **only genuinely supplied** I422_10LE → OBS I210. It validates mapped positive strides, padded/odd plane extents, mapped buffer bounds, large-width overflow, progressive layout, explicit complete caps colorimetry, matrix, range, primaries and transfer. Invalid/missing caps metadata cannot become GstVideoInfo's resolution-dependent defaults.
- Supported YUV policy is **limited-range BT.709 SDR only**. Full-range YUV, PQ, HLG, non-709 primaries/matrix, unknown metadata and unsupported layouts fail closed. BGRA compatibility is explicit full-range sRGB/709-primary RGB only.
- OBS has no distinct BT.709 transfer enum. YUV uses `VIDEO_TRC_DEFAULT`, deliberately retaining OBS's existing SDR processing rather than falsely tagging BT.709 as exact sRGB. The OBS SDR shader uses an sRGB inverse; **this is not proof of grading/colorimetric accuracy**.
- Production raw decoder output requires P010_10LE. A list with P010 first still let vtdec select NV12, giving only 220 ramp values even on the high-precision canvas. Requiring P010 was necessary to preserve ten-bit decoding. The callback retains native NV12/BGRA compatibility, and eight-bit coded inputs work through P010 upconversion. Do not report decoded P010 as proof the original source was ten-bit.
- The receive canvas transaction only changes in-memory `obs_video_info`. It preserves the sender format/colorspace/range and graphics-module name, refuses active OBS outputs (including DeckLink), restores on switch back, and rolls back reset failures before source/preview-lock mutation. A failed rollback blocks further mode changes until restart. Ordinary sender `ResetVideo` is blocked while receive precision owns the canvas.
- UI Stop retains receive precision and preview lock. Switching back restores the prior sender canvas. Shutdown does not reset underneath active outputs; normal OBS teardown owns that case.

## Integration and remaining limits

Link `video-format.c` wherever `pixelview-whep.c` is built/included (parent integration worker owns CMake and existing runners). New `run-video-precision.py` also links `profile-offer.c` and `capability-probe.c`. Preserve the negotiation worker's codec-list and capability changes.

**Keep Main422 withheld**: decoding Main422 into P010 removes vertical chroma; AYUV64 fidelity/range is not proven. The I210 mapper is not a codec offer. Main10/VP9 profile2 are not HDR declarations; keep offer policy restricted to proven 4:2:0 profiles and reject unsupported decoded color. Actual WHEP/rswebrtc dynamic-pad negotiation is distinct from this offline decoder/callback test and remains the integration worker's acceptance gate.

Full-range P010 fidelity is intentionally not admitted; this test verifies rejection, not a full-range decode roundtrip. Main-texture level counting is not calibrated display output, exact RGB code accuracy, chroma siting/impulse fidelity, resize/filter behavior, HDR, long-run network/A/V behavior, or hardware playout. Fullscreen and native SDR DeckLink remain acknowledged eight-bit boundaries. The 1024x32 lossless fixture initially failed VT session creation; using 1024x128 fixed that fixture constraint. Lossy QP1 yielded 875 rather than 877 levels, so the final acceptance uses lossless coding.
