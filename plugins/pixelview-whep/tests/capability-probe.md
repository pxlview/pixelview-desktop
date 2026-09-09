# Runtime receiver capability probe

## Integration contract (parent owns production wiring/CMake)

Compile `capability-probe.c` into the plugin; it includes the checked-in
`capability-fixtures.h` and needs the existing gstapp/gstvideo/GStreamer/GLib
linkage. No software-codec library, fixture install rule, runtime file, ffmpeg,
encoder, network access, authentication or new runtime plugin is needed.

After `pixelview_gst_init()` has succeeded in `obs_module_load`, call this from
the **receiver worker**, inside its new-connection branch before `make_pipeline`
and before any transceiver or local SDP is created. Do not hold `r->lock` or
`r->delivery` while waiting:

```c
struct pixelview_receive_capabilities capabilities;
if (!pixelview_capability_probe_get(&capabilities, cancelled, &generation_context)) {
    /* cancelled/stale or runtime not initialized; wipe endpoint, abandon attempt */
}
/* Store this by value in this connection's receiver context. */
/* In configure_transceiver: */
GstCaps *offer = pixelview_profile_offer_caps(caps, capabilities.profiles,
                                            capabilities.hevc_level_id);
/* NULL/empty video caps or zero profiles: abort, never restore unprobed caps. */
```

`cancelled(void *)` returns `gboolean`; have it briefly lock the receiver and
check `quit` or whether the snapshotted generation differs from `r->generation`.
Its opaque data is used synchronously by the waiting caller only, never retained
by the process-owned probe. Recheck generation before installing the connection.

The cache is immutable once complete/deadline-expired. All concurrent callers
share one worker and the original three-second monotonic deadline. Cancellation
returns zero output without poisoning the shared probe. Waiting polls at 20ms;
each fixture's appsink polling has a 450ms budget. A timeout seals only already
completed successes; late successes cannot change the offer of later connections.
No retry loop or one-thread-per-reconnection growth is possible.

**Lifetime requirement:** arbitrary third-party VideoToolbox/GStreamer calls,
including `set_state(NULL)`, cannot safely be killed in-process. Waiters remain
bounded even if a driver call sticks, but one background thread could remain.
The plugin code and GStreamer must stay loaded until process exit; do not call
`gst_deinit()` or unload the module while that thread could exist. This is a
bounded-caller design, not a promise of forcibly bounded driver execution. A hard
worker-lifetime guarantee would require a separately packaged helper process.

## What the result means

- H264 constrained baseline: 1920x1080, **60fps, level4.2** fixture, parsed through
  `rtph264pay` (MTU1200, aggregation enabled), explicit RTP packetization-mode1,
  `rtph264depay`, `h264parse`, `vtdec_hw`, native NV12 appsink.
- HEVC Main/Main10: actual 1920x1080 **60fps, main-tier level4.1** streams,
  `h265parse`, **only** `vtdec_hw`, native NV12/P010_10LE respectively.
- VP9 profiles0/2: actual 1920x1080 **60fps** packets, `vp9parse`, **only**
  `vtdec_hw`, native NV12/P010_10LE respectively. VP9 frame headers do not encode
  cadence: generator validates IVF timing; runtime supplies compressed caps/PTS.
- Pass requires parser-reported expected profile, dimensions and 60/1 rate;
  HEVC main tier/level4.1 or H264 level4.2; matching decoded dimensions/native
  format and 60/1 rate; mapped non-flat luma on **all three** frames; 60fps PTS
  (one 90kHz RTP tick tolerance), durations (one nanosecond rounding tolerance),
  and EOS within the profile deadline. Appsink is bounded to three buffers with
  dropping disabled. No videoconvert, decodebin or software fallback is used.
- `profiles` uses `profile-offer.h` bits. `hevc_level_id` is **123**, or zero if
  neither HEVC profile completed. Never promote it to180 to satisfy old fmtp.
  The public API, immutable cache and three-second caller budget are unchanged.
- **Integration action:** use max_width1920/max_height1080/max_fps60 with HEVC123.
  H264 HD60 requires level4.2 (level_idc42 / hexadecimal2a), not level4/4.1 or
  the common WebRTC level3.1 value. The real fixture SPS is `42c02a`. This probe
  does not rewrite SDP: the profile-offer helper currently only adds mode1 and
  copies its incoming H264 profile-level-id unchanged. Parent must verify/set a
  compatible H264 offer or omit H264 for HD60; do not mislabel a lower-level SPS.

This certifies only short fixture decode/native-output compatibility, not every
legal bitstream of that level, real-time throughput, general stream drain/frame-loss
correctness, WHEP interoperability, chroma fidelity, HDR/color accuracy, or
end-to-end OBS/DeckLink/display precision. Main42210, VP9 profiles1/3 and 12-bit
variants are intentionally absent. A ten-bit surface is not a fidelity proof.

## Fixtures and redistribution

`capability-fixtures.h` embeds synthetic compressed bytes generated solely by
FFmpeg's `lavfi testsrc2`, nearest-neighbor scaled from192x108 to1920x1080, three
frames per profile. No captured, downloaded or third-party media is included.
The synthetic fixture data is dedicated under **CC0-1.0**
(<https://creativecommons.org/publicdomain/zero/1.0/legalcode>); surrounding source
code is GPL-2.0-or-later. `tests/capability-fixtures.json` preserves generation
commands, FFmpeg version, SHA256s and ffprobe stream metadata. HEVC headers declare
HEVC level123/main tier and H264 level42, checked by the generator; the runtime
independently checks parser caps. The generator extracts actual VUI timing using
FFmpeg trace_headers: H264 time_scale120 / (2 * num_units_in_tick1) =60, HEVC
vui_time_scale60 / vui_num_units_in_tick1 =60. Raw elementary-stream ffprobe
r_frame_rate can be misleading (this FFmpeg reports H264120/1 and avg25/1 despite
60fps VUI); those original values are preserved, never edited into fake evidence.
Each access unit is embedded separately, with its own 60fps timestamp/duration;
per-packet SHA256s bind the generated header to provenance. ffprobe software
count_frames independently confirms three real decoded frames per fixture.
The total embedded compressed payload is small enough for process-local startup,
not an encoder dependency shipped to customers. VP9 IVF container headers are
stripped by the generator; compressed packet bytes are unchanged.

Regenerate only with the existing local FFmpeg (test tooling, not production):

```sh
python3 plugins/pixelview-whep/tests/generate-capability-fixtures.py
```

## Offline verification

```sh
python3 plugins/pixelview-whep/tests/run-capability-probe.py
```

Uses the existing isolated `.test-build/decoder-profile/runtime` read-only; pass
`--runtime PATH` to select another already-prepared curated copy. Nothing is
staged into `.deps` or an app, and no global install/full OBS build is performed.
Both test-instrumented and production variants compile with `-Wall -Wextra
-Werror`; `nm` verifies test hooks do not exist in the production object.

The native suite checks pre-init failure without cache poisoning; eight concurrent
waiters; one worker only; immediate immutable cache reuse; absent-decoder and
malformed-data controls; independent Main10 corruption; caller cancellation;
three-second timeout plus rejection of a late *successful* decode. Current native
positive assertions deliberately describe the audited M1 Pro host (mask31): on
another Mac, inspect the observed mask rather than interpreting a reduced mask as
an implementation failure. Production contains no machine/model assumptions.

A separate test-only run compiles the existing decoder-audit VT session observer
and injects it into the test process. It requires five actual
`UsingHardwareAcceleratedVideoDecoder=true` session observations. The hook is
never part of the production object or runtime closure. Noninstrumented runs must
emit no stderr; no GStreamer error/debug strings are copied into production logs.

HD60 local results: mask31/HEVC123 in205ms; absent and all malformed controls
mask0/level0; Main10-only corruption mask27/level123; wrong-output-rate and
wrong-PTS controls both mask0/level0; hardware-observed run156ms with five actual
hardware=true sessions. All eight concurrent waiters agree;
cancellation returns promptly while a subsequent waiter receives the full cache;
delayed successful decode returns zero at3000ms and cannot mutate the sealed cache.
TDD first changed the expected result to123 and observed the real native assertion
fail with120 !=123 before regenerating fixtures/changing runtime behavior.

### Repetition failure: upstream vtdec EOS pause race (not fixed)

The all-three-frame requirement exposes a real **GStreamer output-drain loss**,
not evidence that VideoToolbox cannot decode the profile, and not an appsink
consumer/EOS-query race. No acceptance threshold or runtime behavior was relaxed.

Fresh-process investigation against the unchanged curated1.28.3 runtime:

- Initial `run-capability-probe.py --repeat-real 100` failed its first positive
  process with mask15 (missing VP9 profile2).
- A separate complete, noninstrumented100-process batch passed96 and failed4
  (iterations25,37,44,69). Seven controls passed with empty stderr: absent,
  malformed, Main10-only malformed, cancellation, late-success timeout,
  wrong-rate and wrong-timing. Timeout sealed at3000ms; partial mask27/HEVC123.
- The checked-in `trace-capability-drain.py --repeat 100` reproduced a RED native
  assertion at process30: mask29, HEVC Main missing. Its VT callback count was3,
  appsink dequeue count2, EOS true, and frame2 was flushed only during teardown.
  All other profiles had three callbacks, three appsink buffers and EOS.
- Earlier detailed VP9 profile0 evidence shows the same sequence: frame2 callback
  with a non-NULL native buffer at264.351ms; insertion into vtdec's reorder queue
  at264.380ms; output task paused at266.352ms; downstream EOS at266.393ms; frame2
  still in that queue and flushed on teardown at699.696ms. No third buffer was
  ever enqueued to appsink. Polling appsink for the full450ms cannot retrieve it.

Root cause in pinned [upstream vtdec.c](https://github.com/GStreamer/gstreamer/blob/1.28.3/subprojects/gst-plugins-bad/sys/applemedia/vtdec.c):
`gst_vtdec_drain_decoder` waits for VT callbacks, then
`gst_vtdec_pause_output_loop` sets `pause_task` and calls `gst_pad_pause_task`
(lines513–527,1790–1854). That external call can mark the GstTask paused between
output-loop iterations, before its cooperative queue-empty check executes.
The comment claiming it pauses only after all frames are out is not guaranteed.
The observed queue entry survives EOS and is discarded by teardown. HEVC uses
an internal16-frame reorder depth even for these no-B-frame streams, so simply
waiting for all three samples **before** sending EOS would deadlock/fail HEVC.

A sound fix belongs in the upstream decoder's cooperative drain/pause handshake,
with a decoder-level regression test and rebuild of the curated applemedia
plugin. That lies outside the assigned capability-only/no-runtime-staging scope.
No speculative sleeps, retry-until-success, extra disposable frames, private
GStreamer task manipulation or support-bit hardcoding were introduced. **TDD
remains RED; the intermittent omission is unresolved.** A stricter probe correctly
fails closed when its actual pipeline loses output. Independent driver/startup
work exceeding450ms can also fail closed; heavy trace logging perturbs scheduling.

Reproduce (the first command compiles both shipping and test variants):

```sh
python3 plugins/pixelview-whep/tests/run-capability-probe.py --repeat-real 100
python3 plugins/pixelview-whep/tests/trace-capability-drain.py --repeat 100
```

The tracer stops on the first short-EOS observation, returns failure for any
native failure, and saves each result batch as
`.test-build/capability-probe/drain-trace-results.json`, with failure logs beside
it. This investigation's additional artifacts in that same directory are
`baseline-100-results.json`, `drain-race-before.log` (VP9 profile0), and
`drain-trace-failure-30.log` (HEVC Main). They contain synthetic-only test data,
not credentials or live media. The trace is a diagnostic reproducer, not a claim
of deterministic timing or a passing regression suite.

The separate `run-profile-hd-decode.py` evidence confirms 24 HEVC Main/Main10
frames with compressed/raw60fps, level4.1 and hardware=true. Its VP9 results
reported compressed/raw24fps despite a60fps IVF; that older harness is **not**
VP9 HD60 rate evidence. This probe now independently validates VP9 compressed and
raw60fps plus packet/output timing. Three frames certify neither throughput nor
endurance, level conformance, or a production engine/OBS path.
