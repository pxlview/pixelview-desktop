# Native422 implementation status — normal-build 25p enabled for hardware testing

## Enabled in normal builds for the user-authorized hardware test

Main422 receive advertisement is enabled directly in normal code, without a
special app, compile option or environment token. SDP adds profile4, level120,
interop-constraints=1d0800000000; ordinary probed profiles/levels are unchanged.
Native playout remains exactly 1080p25 limited-range Rec.709 with strict RTP,
header/PTL, range and owner checks. This enables testing, not physical-output
certification; the historical701ms failure remains unresolved.

Full incremental Xcode26.6 build and deep/strict signature passed for
`build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app`.
Normal production-hook SDP and strict25p filter tests passed. Build completed
before the parent relaunched the ordinary instances; no second artifact was made.
Logs: `/tmp/pixelview-normal-25p-build.log`, `/tmp/pixelview-normal-25p-tests.log`.

The checkpoints below are historical; their Main422-off policy is superseded
by the explicit user enablement above, not by new certification evidence.

## Latest diagnostic retention and declared campaign

Safe typed first-failure production retention/reporting and boundary regressions
are implemented. One predeclared four-rate baseline plus one bounded reference-load
matrix passed944 native exact pairs/720 owner comparisons; historical701ms cause
and stable admission remain **unresolved**, Main422 **off**. Pre-jitter versus
ordered-marker timing identifies local native blocking in these passing cases,
not a fix for the original failure. See [exact results and limits](pixelview-422-diagnostic-results.md)
and [the prior declaration](pixelview-422-diagnostic-campaign.md). New independent
review remains required. Header coverage is77 total:71 negative,6 positive;
active-stale fixtures seed state rather than actually decoding a preceding frame.


## Latest predicate diagnostics / bounded RCA (2026-09-10)

**The original701ms first-frame failure remains unresolved; no root fix or admission change.**
[Final-tree RCA](pixelview-422-final-tree-rca.md) records precise production
RTP/AU refusal reasons and native session/decode/callback/pack/delivery timing.
The old failure occurred after activation, excluding the initial3s activation
deadline, but lacks the data to select its remaining predicate. An induced
650ms downstream block now proves `ordered-rtp-gap` with695726000ns observed gap
and654553000ns delivery cost; this is not the historical cold-start RCA.

One new strict four-rate fresh-registry O2 diagnostic matrix passed470 native
pairs/360 owner comparisons without reproducing the failure. **That does not
repair or supersede the preserved red evidence or certify stable acceptance.**
Final focused/native/77-header/whole-owner sanitizers and full build/deep strict
signature passed. Exact new logs/hashes: `finite-rate/rca-summary.json`.
No deadline/PTS/rate/owner policy changed; Main422 remains off. Parent independent
correction review, original startup RCA and sustained/network acceptance remain.


## Independent-review correction checkpoint — final-source HD matrix RED (2026-09-10)

**This supersedes the short-matrix acceptance statement immediately below.** The startup-deadline bypass, hvcC/VPS/SPS PTL and compressed-caps disagreements, and scanner-inherited observer/evidence clobber defects have actual-TU RED→GREEN corrections. Final focused policy,77 real-header cases, observer, native and whole-owner ASan+UBSan suites pass; full production build and deep/strict signature pass. Decoder-construction diagnostics now retain factory/topology/caps without removing the fatal assertion; the historical construction and24fps empty-startup failures remain unresolved.

**Final strict fresh-registry matrix is RED:** first24000/1001 generation delivered one native frame701,306,779ns late, then the finite-rate gate refused (`native=1 opus=178560 error=1`). No unchanged retry-to-green was performed. An earlier revision passed474 native pairs/948 locks/360 owner comparisons, but precedes final additional header guards and is not final-tree acceptance. No contemporaneous RTP snapshot proves the precise final failure predicate or root cause.

Read [the durable correction/RCA/commands report](pixelview-422-independent-review-corrections.md) and `plugins/pixelview-whep/.test-build/finite-rate/review-summary.json`. Earlier successful counts and all failure bytes/logs below are historical and preserved. Stable final-tree HD software acceptance remains blocked; Main422 stays unadvertised. No commit/push/deploy, sender/engine/user-app/config/auth/card changes.

## Resumed observer RCA — strict short four-rate matrix PASS; shipping CLOSED (2026-09-10)

This supersedes the earlier **short matrix still red** checkpoint below, not its preserved failures or the remaining sustained/shipping gates. Resume evidence **R**: `plugins/pixelview-whep/.test-build/finite-rate/matrix-rca/`; machine-readable `R/resume-final-summary.json`, final full logs/SDPs/hash records `R/final-exact-pass/`. No production source changed during this resume; all four production SHA256 values still match `finite-rate/evidence-summary.json`.

### Observer correction and limits of the RCA

The interrupted child's SHA writer was not integrated correctly: its runner still read SHA text as multi-megabyte RAW frames. The prior RAW observer also performed synchronous multi-MB file writes/close on the serialized native path. Preserved traces bracket a native freeze after `write_exit` and before the next `close_exit`/native return while audio/card slots continue; the attempted macOS sample failed CSSymbolicator creation, so **there is no valid syscall stack proving fclose alone**. Preserve `R/observer-trace-first/native-first-failure.yuv`:1,393,459,200 bytes, SHA256 `d25dba4758aebdb14355af8bf29381a1a92ccd01b6219b96c7a9db068843bc01` (168 native lock images).

A first resumed mmap-reference observer still failed: recorded reference-comparison stages took65–102ms at24/25fps, while extraction and hash stages were only a few milliseconds. Read-only mapping a multi-GB reference was not a bounded-cost substitute for writing it. Those failures and a separate30000/1001 early **encoded-preview Decoder/Video construction assertion** remain in `R/mmap-diagnostic-first/`; that construction failure is not explained by sample comparison and must not be erased by the later passes.

The final **test-only** observer prepares owned lossless-LZ4 independent reference frames before receiver startup (512 frames/256MiB compressed cap; measured about15.5–15.7MB here). It extracts both native locks with real strides/bit alignment, requires identical image identity and full-byte equality, uses SHA256 only to select candidates, then **decompresses and memcmps every reference sample byte**. An intentional matching-hash/wrong-byte negative rejects: no collision assumption replaces exact equality. Two owned native copies plus one temporary reference frame are used per active decoding thread;4096 fixed evidence records fail on overflow, never drop samples. Evidence is written only after teardown, with lock/pair/exact/pending counts reconciled. Native identities must be contiguous within exactly two generations. Acquisition before the first admitted random-access frame and frames after deliberate disconnect are not claimed as decoded acceptance frames.

`observer-deferred-{red,green}.log` and `observer-reference-{red,green}.log` record actual failures before their fixes. Final observer regression covers paired corruption, wrong independent reference, forced hash collision, unmatched pair, capacity refusal and deferred output. The extra reference-replacement test passed even for mmap; it is **not** claimed as a RED performance regression. Legacy RAW-mode native tests needed `-lcompression`; their observed link failure and corrected full suite are retained.

### Exact final executed result

`PV_DECKLINK_OPTIMIZE=1 PV_DECKLINK_WHEP_BUILD=1 PV_WHEP_APPROVED=1 python3 plugins/decklink/tests/run-receive.py`, then `python3 plugins/decklink/tests/run-whep-approved.py` exited0 (`final-o2-build.log`, `final-exact-matrix.log`). No trace/diagnostic/fault/timing flag was enabled in this final run. Actual captured Apple-VT HD payloads/recorded DTS → actual loopback WHEP/Opus → production native/source → whole real owner/refusing SDK fakes; Main422 offer remains test-injected.

| Rate | Native exact pairs (two generations) | Owner full-buffer comparisons | Distinct/45 slots, both generations |
|---|---:|---:|---|
|24000/1001|116 (58/58)|90|44/44|
|24/1|115 (58/57)|90|44/44|
|25/1|118 (60/58)|90|44/44|
|30000/1001|118 (58/60)|90|44/44|

Programmatically verified **467 native exact comparisons,934 observed locks,360 owner exact comparisons**. Original45 slots,≥35 distinct, nondecreasing owner identity, exact rational deadlines/durations, health/expiry, PCM energy and reconnect assertions remain intact; native contiguous-identity accounting was strengthened. A preceding changed-observer diagnostic matrix separately passed468 pairs; it is not merged into the final totals. There was no unchanged retry-until-pass or timestamp/threshold relaxation.

### 24fps diagnosis, negatives and regressions

The old24fps finite-rate rejection had increasing native age488ms→2.433s but lacks a contemporaneous full RTP snapshot. A separate actual24fps WHEP test deliberately blocks the **test** downstream callback for real650ms: native10 then rejection, contiguous first rejected sequence1470 after1469, unchanged SSRC, DISCONT=0, acquired24/1 and666,036,000ns observed packet gap. This proves the existing500ms policy can turn observer backpressure into refusal **without a wrong24 classification** (`24-stall.log` and `24-stall/`). It explains a concrete mechanism consistent with the old failure, not its uniquely proven original cause; no production tolerance was changed.

Actual WHEP negative tests passed: excluded30 and29999/1000 (zero native frames, real Opus and owner refusal);24fps with explicit test caps25/1 conflict (zero native); test-only wire marker change3750→3600 after frame70 (22 native then refusal). The latter intentionally corrupts negative RTP timestamps after the unchanged WHIP conversion, never positive clocks or production sender/engine code. Conflict is injected at the supported filter caps boundary, not alleged to be bitstream metadata. Logs `negative-final-verified.log`, `24-conflict.log`, `24-change.log`; each has loopback SDP/binding/marker evidence. These tests assert owner Start refusal after error, not active-card recovery.

ASan+UBSan finite-policy/conflict/change/loss/discontinuity suite, full native422/preview-dispatch suite, whole owner/source sanitizer regressions and legacy RAW observer mode all exited0 (`policy-resume.log`, `native-regression-resume-green.log`, `owner-regression-resume.log`). Deep/strict existing app signature reverified (`signature-resume.log`); no production rebuild was needed for these test-only changes.

**Remaining gates:** independently review this bounded observer and short matrix; diagnose the preserved early encoded-preview construction failure and establish sustained/stress repeatability; complete real-network loss/jitter/recovery and full PTL/native capability coverage. Main422 remains unadvertised. Physical card/driver/SDI/genlock/A/V drift and deployment remain unauthorized/unverified. No commit/push, sender-engine edit, user app/server/config/auth/card change; only isolated test processes were launched and cleaned up.

## Earlier approved finite-rate checkpoint — historical short matrix failure (2026-09-10)

**The arbitrary-rational blocker below is superseded by the user's explicit initial contract:1920×1080 progressive at24000/1001,24/1,25/1,30000/1001.** Production receiver code now observes ordered90kHz RTP and applies this finite contract using supported Gst pad/signal/parser and native VT APIs. No30fps guess, sender/engine edit, HEVC timing rewrite or private API was added. Main422 remains **unadvertised**. This is a substantial implementation/positive fidelity milestone, **not completed stable/sustained software acceptance**.

Read [the exact finite-rate policy](pixelview-422-finite-rate-policy.md) for32-marker acquisition,3s startup bound, strict loss/discontinuity policy, near-alias limits, exact selected-mode comparison, preserved PTS, caps/VPS/VUI consistency, level120/HD cropping, lifetime and shipping gates. Two legitimate Apple details required fixes: coded1088 with1080 conformance crop, and standard validated CBR filler NAL38. Existing preview/owner/audio production code is preserved; the source's only new wiring requests RTP policy on its filter. Generic low-level level255 fidelity tests are explicitly distinct from strict production WHEP admission.

### Fresh actual encoder captures: all FOUR approved rates

`PV_VT_APPROVED_RATES=1 python3 plugins/pixelview-whep/tests/run-obs-vt-cadence.py` produced `plugins/pixelview-whep/.test-build/obs-vt-approved/result.json` plus native packets/CSV, independent decoded frame hashes, actual header traces and linked WHIP conversion provenance. The background tool returned an inconclusive exit-code field; **the final four-entry result and every rate's artifacts were read and independently checked**, rather than treating that notification as success. Each capture submitted180 changing CPU P216 HD frames to the real existing libobs/Apple hardware VT encoder. No GUI/card or WHIP HTTP execution.

| Exact attached rate | Emitted / distinct independently decoded | Actual WHIP90k tick increments |
|---|---:|---:|
|24000/1001|178 /178|3754|
|24/1|179 /179|3750|
|25/1|179 /179|3600|
|30000/1001|178 /178|3003|

All four are actual profile4/main-tier/level120, limited709 ten-bit422, with no VPS/VUI timing. Bounded asynchronous stop does not flush every submitted frame. These lossy Apple samples are compared against an independent decode of **the encoded payload**, not asserted equal to encoder input. Previous arbitrary-rate audit artifacts remain separate and unchanged.

### Positive actual WHEP/Opus → native → whole owner/fake SDK

Evidence directory **F**: `plugins/pixelview-whep/.test-build/finite-rate/`. `F/evidence-summary.json` parses/deduplicates the four individual successful logs and hashes final production sources. New runner `plugins/decklink/tests/run-whep-approved.py` uses captured Apple AUs/recorded DTS, actual libdatachannel H265 packetization plus the unchanged WHIP conversion, existing isolated loopback-only Pion WHEP, real Opus, production finite-rate/native/source code, and whole production owner TUs against refusing SDK fakes. Only the capability/offer admission bit is test-injected; finite-rate/native metadata gates are not overridden. Native x422 is independently recorded before packing; every recorded pair agrees with the independent software reference SHA256. Scheduled v210 is full-buffer `memcmp` against independent FFmpeg v210 reference frames.

| Individual passing run / log in F | Native frame reference matches | Owner v210 comparisons | Distinct scheduled images, two generations |
|---|---:|---:|---|
|24000/1001 `whep-pop.log`|119|90|44 /44|
|24/1 `whep-24-diagnostic.log`|111|90|42 /39|
|25/1 `whep-25-o2.log`|109|90|40 /37|
|30000/1001 `whep-uninstrumented.log`|119|90|44 /44|

Programmatic totals: **458 native reference matches,360 owner v210 comparisons**. Each passing case used two loopback endpoints with the same source/owner, real nonzero timestamped source-only Opus PCM, exact selected rate/duration and rational fake-card absolute deadlines, wrong-rate owner refusal, and stopped-generation owner invalidation. All media candidates were loopback-only. O1 uninstrumented builds were used for the first/last cases;24 used test-only diagnostic logging, and25 used O2. The independent observer is optimized nonshipping code. This is RTP replay through actual WHEP, not actual OBS WHIP HTTP/RTCP sender synchronization or physical SDI.

### IMPORTANT: acceptance remains RED, failed runs preserved

**Do not sum those individual successes into a passing matrix or sustained claim.** `F/whep-release-matrix.log`, the latest O2 matrix, still fails on the second24000/1001 generation: native video stops advancing around image72, repeated scheduling continues until existing freshness/owner guards refuse, and the strict schedule assertion fails. Earlier `whep-all-four-final.log` records an actual24fps finite-rate rejection; `whep-all-four.log` and `whep-25.log` preserve23/29 distinct-image failures. The minimum35 distinct images per45 scheduled slots is unchanged. The precise intermittent drop/stall origin remains unresolved; no unsupported sender alias is blamed for it.

HD ASan+UBSan runs also failed owner cadence; an arrival audit recorded native age growing223ms→483ms while Opus stayed near current clock. Removing instrumentation allowed positive short runs but did **not** establish sustained throughput or eliminate the intermittent matrix failure. The small-fixture sanitizer suites still pass. No source PTS rewrite, larger production loss/expiry tolerance, ignored sanitizer diagnostic, weakened identity threshold or capability-probe retry was used. Stable bounded sustained HD A/V across all four and actual-network conflicting/changing-rate/jitter/discontinuity acceptance remain pending; continuing a long run after this short failure would not certify them.

### Negatives, regressions and final isolated app

- `run-native-422-policy.py`: ASan+UBSan passed four-rate/window/wrap, unsupported/changing-marker, SSRC/loss/DISCONT/stale/backward-clock, real GstRTPBuffer/public future-depay topology and late-observer lifetime, timing conflict/overflow, and legal/malformed filler checks. `filler-red.log` records actual pre-fix rejection; `policy-final.log` is green. These deterministic negatives are not claimed to be a complete real-network recovery suite.
- `PV_DECKLINK_SANITIZE=1 PV_DECKLINK_WHEP_BUILD=1 PV_DECKLINK_WHEP_CADENCE_AUDIT=1 PV_WHEP_APPROVED=1 .../run-receive.py`, then `.../run-whep-approved-negative.py`: `F/negative-final.log` and `F/negative/whep-result.json` pass actual30/1 and29999/1000 native-payload WHEP/Opus rejection with zero native frames and real owner refusal. This exercises the new finite gate, not merely the former missing-framerate error. Existing actual HTTP406 is covered by the ordinary-codec suite.
- Final executed and exited0: native422 regression (including preview-dispatch sanitizers), native-only source lifecycle, codecs, video precision, deterministic production offers, whole owner/source ASan+UBSan, six-case actual existing-codec WHEP, and two compiled offline Qt telemetry/receive-lifecycle tests. Logs: `F/{native-regression,native-source-final,codecs-final,precision-final,offers-final,owner-sanitize-final,existing-whep-final,ui-final}.log`. Historical pinned-VT startup omissions remain unresolved; one passing run is not a stability repair.
- Full isolated build initially failed linkage because Xcode had not regenerated the new gstrtp dependency. Explicit `cmake -S . -B build_macos_native422` retained the existing cache; subsequent full `cmake --build build_macos_native422 --config RelWithDebInfo -j 8` **succeeded** (`full-build-configured.log`). Deep/strict codesign passed (`signature-final.log`), bundle ID readback `com.pixelview.desktop`. Private HOME/CFFIXED_USER_HOME; artifact `build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app`. No user-app launch/install or credentials/configuration changed.

**Remaining shipping gates:** diagnose/reproduce the intermittent finite-rate/HD native stall and schedule repeats; complete actual-network conflicting/changing/loss/jitter tests; finish full PTL/constraint/caps consistency and genuine native capability probing; bounded sustained HD A/V all four; parent independent review. Physical card support/driver behavior, SDI code equality/cadence/genlock/common A/V drift remain separately unauthorized and unverified. All uncommitted work and HEAD `d043346e4` are preserved; no commit/push/deploy, engine/sender modification, user-app/server/card/auth/config change.

## Historical arbitrary-rate audit — superseded scope, preserved evidence (2026-09-09)

**No shipping code changed or Main422 bit enabled in this stage. Actual OBS/Apple VT → WHEP compatibility is still blocked, not implemented.** The receiver cannot unambiguously recover the sender's exact nominal rational cadence from the currently transmitted timing. This is now demonstrated with fresh native encoder payloads, the actual linked WHIP conversion and packetizer, and actual loopback WHEP/Opus → production native gate → existing owner/fake SDK refusal. No guessed30 or inferred finite source-rate whitelist was added. All preceding uncommitted implementation and review corrections remain intact.

### Fresh native sender evidence (not FFmpeg-generated HEVC)

Evidence directory **C**: `plugins/pixelview-whep/.test-build/obs-vt-cadence/`.

`python3 plugins/pixelview-whep/tests/run-obs-vt-cadence.py` exited0. It compiles an isolated CPU/libobs packet-sink harness, loads only the existing `build_macos_native422` Apple VT plugin and libobs read-only, and submits180 temporally distinct1920×1080 P216 limited709 frames per rate. Native logs report hardware encoding. It captures actual payload bytes and packet `pts`, `dts`, `timebase_num/den`, `dts_usec`, keyframes and byte counts. Every emitted frame independently software-decodes to a distinct full-frame hash. Bounded stop does not flush all submitted asynchronous frames; no lossless-input or sustained-decoder claim is made.

Final `C/result.json` / per-rate CSVs and header traces:

| Attached native rate | Captured/distinct decoded frames | Actual `dts_usec` deltas | Actual linked WHIP RTP increment |
|---|---:|---|---|
|30/1|178/178|33333 or33334|3000 on every interval|
|30000/1001|179/179|33366 or33367|3003 on every interval|
|29999/1000|179/179|33334 or33335|3000 on every interval|

All three bitstreams have actual **profile4, tier0, level120, chroma2, luma/chroma depth10,1920×1080, limited BT709**, and both `vps_timing_info_present_flag=0` and `vui_timing_info_present_flag=0`. These assertions read encoded headers and decoded stream metadata, not only encoder settings. FFmpeg's guessed elementary-stream frame rate is deliberately **not** used as cadence evidence. `result.json` records payload, plugin/libobs/libdatachannel and WHIP source SHA256 provenance. These existing binaries were not rebuilt in this stage; source-to-binary reproducibility is not asserted.

### Why a bounded native RTP cadence estimator is insufficient

- `WHIPOutput::Data` (`plugins/obs-webrtc/whip-output.cpp:130–136`) differences `packet->dts_usec`; `Send:727–734` converts each microsecond delta separately with `RtpPacketizationConfig::secondsToTimestamp` and accumulates it. The audit **compiles the exact current conversion statements and calls the actual bundled libdatachannel implementation**. That implementation rounds: do not describe this observed build as truncating or claim30/2997 drift from a hypothetical floor implementation. Those two common rates really produce3000/3003 ticks.
- Nevertheless **30/1 and29999/1000 are different valid rates ≤30 and have identical3000-tick increment sequences**. This is not just a short-window coincidence: for constant rational periodT, adjacent differences of `floor(n*T)` are only floor(T) or ceil(T). The possible microsecond deltas for those two rates are33333/33334 and33334/33335; the actual library maps **all** of them to3000. A longer clean RTP-marker window cannot recover the discarded distinction. Similar arithmetic gives a3003-tick alias for30000/1001 versus29999/1001; even30001/1000 (>30) can emit3000 ticks, so an exact≤30 gate is not proven by that increment alone.
- Six executable counterexample tests (`test-obs-vt-cadence.py`, `C/counterexamples.log`) verify common-rate separation, real capture evidence, exhaustive adjacent-delta aliases, the≤30 boundary, and equal observations after identical wrap/loss/outlier/arrival-jitter transformations. This last test is an **information-loss counterexample, not an implemented network loss/recovery estimator**. No classifier, confidence threshold, outlier tolerance or hold/release policy is being smuggled into production.
- Public RTP pad probes/`GstRTPBuffer` can inspect marker/sequence/SSRC/90k timestamp before depayloading, but cannot recover information already lost at the sender. `GstRTPSourceMeta` carries SSRC/CSRC, not nominal frame rate. `GstReferenceTimestampMeta`/RTCP NTP mappings represent clock correlation, not an exact nominal rational declaration; bounded observation with finite timestamp precision and unspecified timing/scheduling error is not a proof of a symbolic source rate. Gst running PTS may include jitter/skew correction and must not be rounded into a nominal cadence. Decoder callback timing is supplied by the compressed sample; it is not independent encoder cadence discovery.
- A deliberately specified finite source-rate contract could make an RTP classifier useful **within that contract**. The current wire path neither declares nor enforces that contract; the native encoder accepts the demonstrated nearby rational. Assuming only the usual DeckLink rates would silently admit that wrong sender rate. This audit does not claim that useful approximate/common-rate classification is impossible—only that the requested unambiguous exact admission is not justified here.

### Actual declared-level HD WHEP negative, including real Opus and owner refusal

Build and run:

```sh
PV_DECKLINK_SANITIZE=1 PV_DECKLINK_WHEP_BUILD=1 \
PV_DECKLINK_WHEP_CADENCE_AUDIT=1 \
python3 plugins/decklink/tests/run-receive.py
python3 plugins/decklink/tests/run-whep-cadence-audit.py
python3 plugins/pixelview-whep/tests/test-obs-vt-cadence.py -v
```

All exited0 as **negative audit acceptance**, not working compatibility. Build log `C/owner-build.log`; result `C/whep-result.json`; individual `C/whep-{30-1,30000-1001,29999-1000}/` logs, SDP, binding and replay evidence.

The replay uses the captured native AUs/packet `dts_usec`, the same supported `rtc::H265RtpPacketizer` StartSequence API, and extracted current WHIP conversion. It does not assign a guessed FFmpeg input rate, modify receiver PTS/duration, change codec headers or add cadence caps. The capture file includes the actual extradata before the first actual AU; repeated identical parameter sets are already accepted by the production parser. Its arbitrary RTP epoch is test-local. UDP is127.0.0.1-only into the existing isolated Pion sender, followed by actual WHEP HTTP offer/answer, ICE/DTLS/SRTP, HEVC depayload/parse and real Opus decoding. This is **not actual OBS WHIP HTTP/peer-connection execution** and does not claim actual sender RTCP synchronization. The engine source and existing loopback server are used read-only; no pv-engine edits.

- Actual server marker counts:16 at30/1,18 at30000/1001,18 at29999/1000 before receiver shutdown; successive increments are3000/3003/3000 respectively. All bind `hevc-10bit-422`; candidates in both SDPs are loopback-only.
- Test-only public h265parse source-pad inspection observes one first parsed HD hvc1/AU buffer per run with **valid PTS, `GST_CLOCK_TIME_NONE` duration, no framerate caps**, actual profile`main-422-10`/tier`main`/level`4`, and the real codec_data. The only observed attached meta in the captured2997 sample is `GstVideoSEIUserDataUnregisteredMetaAPI`, not nominal cadence. No production test hook was added.
- Real source Opus sample-frame counts are63360/51840/51840. Each run then fails native admission, returns zero native video frames, and calls the actual existing `DeckLinkDeviceInstance::StartNativeOutput`, which refuses. Fake card has no started playback, callback or queued video. This verifies safe rejection of a legitimate missing-timing sender; it does **not** call that sender malformed or claim any owner-v210 sample comparison.
- Native/owner/source test objects were ASan+UBSan-built; no sanitizer diagnostic in these final receiver logs. Linked frameworks/drivers are not instrumented. No physical SDK dispatch, card or user application was opened.

### Exact trustworthy metadata seam / scope decision

The exact rational exists at the **attached encoder media boundary**, not `ExpectedFrameRate`:

1. `video_output_get_info(obs_encoder_video(enc->encoder))->fps_num/fps_den` is copied into `enc->fps_num/fps_den` at `mac-videotoolbox/encoder.c:741–742`; `VTCompressionSessionEncodeFrame` receives `CMTimeMake(enc->fps_den,enc->fps_num)` duration and exact frame PTS at1120–1122. In current OBS, packet DTS increments byfps_den, not always1; `libobs/obs-internal.h:55–58` divides by`timebase_den` when producing microseconds. The audit's initial assumption of one DTS unit per frame was corrected before final evidence; no production timestamp change followed.
2. `ExpectedFrameRate` at543 is integer division (2997 becomes29). Apple's installed public header explicitly calls this an expectation and says actual rate may vary. Correcting it to a floating value might improve encoder configuration, but **does not establish an exact transported cadence or promise emitted VUI**. No supported public HEVC timing-insertion property was found in the inspected`VTCompressionProperties.h`; no private property name was invented.
3. The existing **standard codec-metadata output seam** is `handle_keyframe:889–923`, where `CMVideoFormatDescriptionGetHEVCParameterSetAtIndex` obtains VPS/SPS/PPS and the same bytes populate every keyframe and initial`extra_data`. A future authorized sender change could carry the exact attached rational in coherent standard HEVC timing syntax here, with explicit POC/fixed-rate semantics and consistent repeated parameter sets. That entails a validated bitstream writer/rewrite (or first demonstrating that a supported native encoder option emits the required timing); merely adding a CM dictionary extension or changing local caps does not transmit it. Both rate metadata and timestamp consistency need verification. No sender bitstream rewrite was implemented or implicitly authorized by this audit.
4. Sender RTP should independently preserve cumulative rational media time (convert an absolute media-time delta relative to a stable origin rather than independently quantizing every interval) if nonintegral90k periods are supported. That prevents accumulating per-interval rounding error but **alone does not provide exact nominal-rational admission from an arbitrary bounded observation**. A new SDP/control extension is not proposed: standard codec timing is the first seam to evaluate. If unavailable, explicitly approve a narrowly stated source-rate contract or a versioned exact metadata path before widening scope.

### Remaining gates and deliberate nonclaims

- **Blocked:** positive actual-VT HD WHEP → native x422/v210 → owner equality, sustained HD output, common A/V/drift, and loss/jitter/discontinuity/stream-change admission state machine. A sustained positive run is not feasible while the first legitimate AU fails; no guessed rate was inserted to obtain one. Three short native encoder runs are not endurance proof.
- Production still has the earlier strict SPS chroma/depth/progressive/limited709 checks and byte-identical parameter-set rule, but its scaffold is **not complete shipping admission**: `configure` accepts positive caps fps up to60, does not cross-check caps cadence against VPS/VUI, and `valid_sps` does not enforce a level120 ceiling. Do not claim the newly inspected actual level120 fixtures fixed those policy gaps. Level255 lossless fidelity fixtures stay separate and unchanged. Missing timing must remain distinct from malformed/conflicting timing in a future explicit admission API; none is implemented in this stage.
- Existing `run-native-422.py` and deterministic `run-production-offer.py --deterministic-only` both exit0 (`C/native-regression.log`, `C/offers-regression.log`). They retain earlier malformed/color/layout/refusal coverage, not new real-network hostile-cadence coverage. Historical pinned-VT startup omissions, independent capability/admission review, physical SDI/card, GUI and release gates remain unchanged.
- Production receiver SHA256 remains`9429305b4d8ab8c80801d84c8d24aa5cb149d496f880901819333f49f69a0079`; sender/transport/capability/owner production files are untouched. No new app build/signature/GUI acceptance claim is made. Existing user OBS/Pixelview processes were observed read-only and not altered.

New test files: `plugins/pixelview-whep/tests/{obs-vt-cadence.cpp,obs-vt-rtp-replay.cpp,run-obs-vt-cadence.py,test-obs-vt-cadence.py}` and `plugins/decklink/tests/{whep-cadence-reject.cpp,run-whep-cadence-audit.py}`. Existing test-only `run-receive.py` gains a separately named negative binary target; `whep-feed.c` gains opt-in public parser-pad observation. This status and the handoff are updated. No commit/push/deploy, user configuration/authentication, global runtime or pv-engine modification. Parent independent review remains required; this stage delivers an experimentally grounded blocker, not a self-approved compatibility implementation.

## Mixed native → ordinary preview ordering correction (2026-09-09)

**Implemented and verified on the current production source; parent independent review remains required. Main422 remains unadvertised.** This section supersedes the previous section's claim that new-generation previews were all ordered by the worker: ordinary codec generations could bypass it. No decoder/filter, owner scheduler, audio route, capability/offer, frontend, transport or production timestamp changes were made in this correction.

### Exact common ordering and lifetime contract

- The first admitted native RAW preview lazily creates the existing source-owned worker. **Once that worker exists, every subsequent video sample—including ordinary H264/Main/Main10/VP9 generations—and every NULL clear use it until source destruction.** The serialized Gst video appsink and pipeline NULL teardown drain the preceding generation before starting another. Stop also rechecks worker existence after NULL teardown, covering a last old-pipeline callback that creates it during teardown. Ordinary-only reception before worker creation retains its original direct/serialized delivery path.
- A worker-local snapshot records whether the owned sample is native. Its final reservation predicate is `!quit && (!native_preview || preview_enabled) && !changed && accept_samples && generation == sample_generation && active_generation == sample_generation`. Cancellation before this mutex-protected reservation suppresses output. Exactly one irrevocably reserved OBS call may **enter or finish after Stop/disable returns**; cancellation is not a callback-entry or completion fence. The same worker must finish that call before later clears and frames of either route. No receiver/attempt/delivery mutex is held across worker OBS delivery.
- Disconnect remains a nonblocking request; pending old data is invalidated by teardown/generation checks. Destruction joins the worker through OBS return, unmap and sample release before freeing source storage; permanently blocked third-party code still has no universal shutdown deadline. No detached thread, second pipeline, new worker type, new lock or shipping test hook was introduced; the added boolean distinguishes the pending sample's route.
- Queue remains **one latest owned GstSample plus one in flight**; replacement releases the old pending reference. Native RAW retains its existing `1920*1080*3` byte bound; subsequent ordinary RAW is bounded before mapping by `1920*1080*4` (also accommodates supported BGRA/I210). Ordinary callback validation/error returns are retained before enqueue; the worker remaps the same owned RAW storage READ-only for its lifetime. There is no ordinary P010→eight-bit conversion. Ordinary current-generation counters/playing/last-video readiness update after OBS returns; old reserved frames cannot count toward the new generation. Native readiness and separate audio/feed counters remain unchanged.
- `set_native_preview(false)` suppresses native previews, not ordinary samples. The ordinary route preserves exact P010 codes, range/TRC/color matrix and callback rejection of unsupported color. Native NV12 display copies (including those carried in P010 containers) remain deliberately **eight-bit display information**, not recovered ten-bit precision.

### Deterministic RED → GREEN

Evidence directory **M**: `plugins/pixelview-whep/.test-build/preview-dispatch/`.

- **Before production edits**, `run-preview-dispatch.py` compiled the actual receiver TU under ASan+UBSan. `M/mixed-red.log` records the precise bug: held old-native reservation, actual new ordinary P010 delivery, then actual old data/NULL calls: **`3,1,0` instead of `1,0,3`**. The test injects owned GstSamples only at the appsink pull seam and records actual production OBS call boundaries; it does not infer output from counters or generation mutation alone.
- `M/mixed-dispatch-final.log` verifies **`1,0,3`** at both post-reservation/pre-OBS and inside-OBS barriers, then reverse/repeated route switches on the identical worker. Additional coverage starts with ordinary direct delivery before native worker creation, checks all P010 luma codes64..940 and color parameters, native-disable/ordinary readiness, latest-pending replacement, blocked ordinary reconnect, and actual `destroy()` joining before storage release. The destruction fixture uses real libobs source controls with a no-transport media teardown thread; the actual WHEP test independently exercises the real source destruction queue. Existing **30 cancellation-boundary cases** still pass.
- The first mixed implementation accidentally deferred ordinary color rejection into the worker. `M/mixed-color-red.log` caught a full-range callback returning OK. Final code retains the original synchronous ordinary validation: callback returns `GST_FLOW_NOT_NEGOTIATED` (`-4`), with no OBS output.

### Native WHEP acceptance failure investigated and corrected in the test fixture

Initial real blocked/normal native WHEP runs **failed**, and those failures are retained—not relabeled passes. `M/mixed-whep-{blocked,normal}-final.log` and `M/mixed-{blocked,normal}-first-failure/` preserve logs/results/SDP. Blocked compared45 exact owner frames but only18 distinct identities; normal compared45 each but had20 then17 identities. Native/Opus delivery and owner health were good; the unchanged minimum20 identity assertion failed, not a capability probe or sanitizer.

The fixture advanced its fake card by exactly1001/30000 each iteration but used a relative33ms sleep **plus all iteration work**. Replay of the logged bounded FIFO and unchanged scheduler selection reproduces the exact18/20/17 distinct counts (`M/mixed-{blocked,normal}-trace-replay.json`). In the failed blocked attempt, source timestamps advanced1802179714ns versus1468133333ns of fake card time: its target fell334046381ns behind the newest queued sample. This is a test clock pacing defect, not permission to change production timestamps or relax card expiry.

`tests/test-whep-pacing.py` compiles the **actual fixture's pacing prefix** with a controlled clock and9ms iteration cost. `M/mixed-pacing-red.log` fails at slot1 (75ms versus66.733333ms). `whep-receive.cpp` now uses native `os_sleepto_ns` with absolute rational deadlines; `M/mixed-pacing-green.log` passes all45 slots. The test runner executes this regression before rebuilding. Byte equality, monotonic identities, minimum20 distinct images, real audio, expiry, callback blocking and lifecycle assertions are unchanged. No production owner/scheduler fix or assertion weakening was used.

A fresh sanitizer build and one verification run per mode after this correction both exit0:

- **Blocked:** `M/mixed-whep-blocked-paced.log` / `-paced-result.json`:90 exact owner frames over two generations, **45 distinct per generation**,159 independent native pre-pack frame comparisons, real Opus, actual HTTP406 refusal, four decodebin3 instances/159 RAW inputs/**zero video decoders**. Disconnect request measurements1125ns/3667ns; the actual source destruction FIFO remains blocked until preview callback release.
- **Normal:** `M/mixed-whep-normal-paced.log` / `-paced-result.json`:90 exact owner frames, **45 distinct per generation**,161 independent native comparisons, real Opus, HTTP406, four decodebin3 instances/161 RAW inputs/zero video decoders. Disconnect7167ns/7041ns. Normal reuses the freshly sanitizer-built binary, not a stale pre-fix executable.
- `M/mixed-final-summary.json` aggregates results and diagnostic scans. No ASan/UBSan diagnostic in final dispatch/native422/owner or either paced WHEP log. Linked frameworks/system drivers are not sanitizer-instrumented. The level255/1024×64 test-only admitted fixture remains transport/ownership acceptance, **not shipping profile/level admission, sustained HD cadence, physical card or common A/V drift certification**. Absolute fixture deadlines remove accumulated work drift, not OS scheduling latency.

### Other final-source verification / artifact

All exited0 on the same final production SHA256 `9429305b4d8ab8c80801d84c8d24aa5cb149d496f880901819333f49f69a0079`:

- `run-native-422.py` and `PV_DECKLINK_SANITIZE=1 .../run-receive.py`: `M/mixed-{native422,owner-sanitize}-final.log` (native packing/layout/RAW/source ownership, actual owner/audio route tests and dispatch sanitizer coverage).
- `run-native.py --native-only`, `run-video-precision.py`, deterministic production offers: `M/mixed-{native,precision,offers}-final.log`. Precision observes exact877 P010 callback/main-texture levels; NV12 canvas control has256. Existing source-only rendered Opus remains monotonic/nonzero.
- Existing **six-case actual WHEP** suite: `M/mixed-existing-whep-final.log` passes H264/Main/Main10/VP9-0/VP9-2 and real HTTP406 once, without probe retries/mask override. Historical pinned-VT startup omissions remain unresolved.
- Two offline compiled Qt tests (preview telemetry and native receive lifecycle): `M/mixed-ui-final.log`, passed. Not a user GUI/hardware acceptance run.
- Full isolated `cmake --build build_macos_native422 --config RelWithDebInfo -j 8`: `M/mixed-full-build-final.log`, **BUILD SUCCEEDED**. Deep/strict codesign: `M/mixed-signature-final.log`, valid on disk and satisfies designated requirement; `M/mixed-bundle-id-final.log`: `com.pixelview.desktop`. Artifact: `build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app`. Later pacing changes touch tests only; production has not changed since this build.

Files changed by this correction: `plugins/pixelview-whep/pixelview-whep.c`, `plugins/pixelview-whep/tests/preview-dispatch.c`, `plugins/decklink/tests/{whep-receive.cpp,run-whep-receive.py,test-whep-pacing.py}`, and this status. All prior uncommitted changes and parent-owned handoff are preserved. `git diff --check` passed. No commit/push/deploy, physical-card dispatch, user-app launch/replacement, saved user configuration or authentication changes. Main422 admission, actual sender PTL/VUI/cadence, sustained HD/common A/V drift, hardware and release gates remain pending, as does independent review.

## Stale native-preview dispatch correction (2026-09-09)

**Corrected and regression-tested; independent re-review still required. Main422 remains unadvertised.** Only production `plugins/pixelview-whep/pixelview-whep.c` changed in this correction. Native RAW queues, decoder/filter, precision, source-feed/owner/audio, supported upstream APIs and sender admission are unchanged. No shipping test hook, new mutex, thread, adapter, or callback API was added.

### Exact dispatch contract (supersedes the narrower “already entered” wording below)

The worker's dequeue snapshot is only a preparation hint. After any preceding clear, caps validation, READ mapping and OBS-frame preparation, it takes the existing receiver mutex and evaluates `!quit && preview_enabled && !changed && accept_samples && generation == sample_generation && active_generation == sample_generation`. A true local `dispatch` value is the **irrevocable reservation/linearization point** for exactly one prepared frame. Cancellation ordered before that point suppresses delivery. Cancellation ordered after it does not revoke the reservation: that one OBS call may **enter**, not merely finish, after Stop/disable returns if the worker is descheduled immediately after unlock. This interval is explicitly tested; it is not mislabeled already-entered OBS or hidden behind counters.

The single source-owned worker orders all its subsequent clears and new-generation previews after the reserved call, and owns the sample/mapping through return. It cannot reserve another frame while that call is outstanding. Disconnect remains a nonblocking cancellation **request**, not a callback-entry/completion fence. Destruction still joins the worker before releasing receiver/source storage. A strict “no OBS entry after Stop returns” promise would require cooperation at the OBS entry boundary or waiting for dispatch quiescence; neither is provided by this minimal supported-API contract. No receiver/attempt/delivery mutex is held across third-party OBS. Current-generation counters independently revalidate all admission predicates after return.

### Deterministic RED → GREEN

Evidence directory **D**: `plugins/pixelview-whep/.test-build/preview-dispatch/`.

- Read and preserved reviewer scratch `.../pv422-preview-review-sqnjaxs9/stale-preview-review.c` and `.log`. In-repository `tests/preview-dispatch.c` includes the actual production receiver translation unit and uses test-local wrappers around real video-info/frame conversion, mutex unlock, unmap and OBS boundaries. No production hook/state was added.
- Ran `python3 plugins/pixelview-whep/tests/run-preview-dispatch.py --red` **before changing production**. `D/red.log` fails with `boundary=1 cancel=0 outputs=1 expected=0 generation=3 counter=0`: precisely the old-frame call hidden by a zero counter.
- Final `run-native-422.py` now invokes the new standalone ASan+UBSan runner. `D/native422-final.log` contains **30 passing deterministic boundary cases**: before mapping, after conversion/before reservation, inside a preceding clear, immediately after reservation unlock/before OBS entry, and inside OBS; crossed with disconnect/reconnect generation1→3, preview disable, accepting=false, quit, active-generation mismatch and changed=true. Pre-reservation cases deliver zero; post-reservation/entered cases deliver exactly the one irrevocably reserved call. Cancellation returns within the asserted500ms bound while barriers remain held. Tests wait on conditions and join, not timing sleeps or preview counters.
- `D/initial-harness-timeout.log` is retained: an initial test-only unlock wrapper incorrectly blocked the cancelling thread too. Final test uses a thread-local preparation marker to pause only the prepared worker's handoff; no production synchronization workaround was retained.

### Final executed regressions and artifact

All following commands exited0 on final production source:

- `python3 plugins/pixelview-whep/tests/run-native-422.py` — `D/native422-final.log`, including exact native/v210/layout/RAW/source ownership and new sanitized dispatch cases.
- `PV_DECKLINK_SANITIZE=1 python3 plugins/decklink/tests/run-receive.py` — `D/owner-sanitize-final.log`, whole production owner/source adapters; linked frameworks are not instrumented.
- `PV_DECKLINK_SANITIZE=1 PV_WHEP_PREVIEW=blocked python3 plugins/decklink/tests/run-whep-receive.py`, then `PV_WHEP_SKIP_BUILD=1 PV_WHEP_PREVIEW=normal ...` — `D/whep-{blocked,normal}-final.log` and copied `D/whep-{blocked,normal}-result.json`. Each compares90 owner v210 frames over two generations on the same source/owner; native pre-pack comparisons167 blocked /169 normal. Both exercise real Opus, loopback RTP, HTTP406 refusal and zero redundant video decoders. Blocked mode retains its actual source-destruction FIFO barrier until OBS callback release. No ASan/UBSan diagnostic in these final sanitizer logs. NV12/disabled WHEP modes were not rerun in this correction (previous independent review evidence remains historical).
- Native-only source, video precision and deterministic production offers — `D/{native,precision,offers}-final.log`.
- Existing six-case real WHEP suite — `D/existing-whep-final.log`: H264, Main, Main10, VP9-0, VP9-2 and actual HTTP406 all pass once, without probe retry/mask overrides. This does not resolve the historical pinned-VT startup omission.
- Offline compiled Qt telemetry/lifecycle tests `test_receive_ui.ReceiveUI.test_native422_preview_telemetry` and `.test_offline_native_receive_lifecycle` — `D/ui.log`, two tests pass. No GUI/user application or Keychain access; not physical UI acceptance.
- Full isolated `cmake --build build_macos_native422 --config RelWithDebInfo -j 8` — `D/full-build-final.log`; deep/strict codesign verify — `D/signature-final.log`. Both exit0. Bundle ID readback `com.pixelview.desktop`; artifact `build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app`. Private HOME/CFFIXED_USER_HOME and explicit Xcode environment. No app install/launch, release signing, commit/push/deploy, physical SDK dispatch, user configuration or authentication changes.

Final production SHA256: `466a23430dfdf93826c6753090c1687ebfbd8afec0ca5ca87b2762c6cfd28f1c`. Changed/added files for this correction: `pixelview-whep.c`, `tests/{preview-dispatch.c,run-preview-dispatch.py,run-native-422.py}`, and this status. Preexisting uncommitted changes and parent-owned handoff remain intact. Sender cadence/PTL/VUI admission, sustained HD/common A/V drift, hardware/card and release gates remain unchanged; this is not Main422 admission.

## Native422 RAW preview independence (2026-09-09)

**Implemented and exercised; Main422 remains unadvertised. Parent independent review remains pending.** This section supersedes older encoded-tee/preview-readiness descriptions below; historical evidence and limitations are retained.

### Production changes and lifetime contract

- `native-422.m/.h` creates an optional owned limited709 NV12 display copy from the validated decoded x422 image before releasing it. Card packing remains exact and separate. Borrowed `d->packed` is copied synchronously into the source feed and never escapes. The display copy intentionally truncates luma and averages adjacent chroma rows; it is not grading fidelity.
- `native-422-filter.c/.h` replaces the native encoded tee with a serialized native transform followed by a bounded drop-oldest **RAW** queue (3 buffers, 16MiB, 200ms limits). Existing codecs pass unchanged through a nonleaky queue. A compressed-to-native change on an already configured filter rejects rather than making queued reference AUs leaky. Native HEVC input explicitly requires hvc1/AU; byte-stream negotiation is refused. Display NV12 can be carried as eight-bit values in P010 containers for the existing P010-first receiver; this is not recovered ten-bit preview precision.
- The pinned rswebrtc `parsebin → encoded-filter → decodebin3` topology is retained, **not assumed removed**. Real WHEP deep-element instrumentation aborts if any Decoder/Video element is constructed, and probes decodebin3 RAW input. All four modes observed four decodebin3 instances, 164/167/167/167 RAW inputs (blocked/normal/NV12/disabled), and **zero video decoder instances**. Thus decodebin3 demonstrably passes RAW through for this route; no Rust patch is needed. This removes the old encoded preview decoder boundary rather than testing a decoder that eventually unblocks.
- `pixelview-whep.c` routes tagged native RAW previews into one source-owned worker: at most one latest pending sample plus one in flight. It holds no receiver/attempt/delivery lock across OBS video output. `set_native_preview(enabled)` disables delivery; generation changes discard pending samples. Native source-feed copies, readiness, and watchdog use native progress independently of preview counters. The frontend likewise prefers native telemetry.
- **Boundedness is narrow and honest:** disconnect request and same-source reconnect do not wait for a blocked native preview callback. An already entered OBS call may complete after Stop; pending old-generation samples are invalidated. Source destruction joins the worker and deliberately waits for third-party callback quiescence, preserving source/sample/module references rather than detaching into a UAF. Permanent OBS, VT, GStreamer state-change, audio, or SDK blocking does not have a universal hard shutdown deadline. Ordinary non-native OBS delivery retains its prior locking contract. Disabling preview avoids OBS delivery, not all display-copy CPU work.

### Executed acceptance on the final implementation

- `python3 plugins/pixelview-whep/tests/run-native-422.py`: exit0 (`.test-build/native-422/raw-caps-green.log`). Production RAW preview ownership/content/timing, route-change nonleaky rejection, hvc1 caps, native readiness with zero preview frames, filter GValue nonfloating ownership, exact native packing and existing malformed/layout cases pass. RED logs include `preview-red`, `raw-readiness-red`, `raw-ownership-red`, `raw-route-red`, and `raw-caps-red`.
- `PV_DECKLINK_SANITIZE=1 PV_WHEP_PREVIEW=blocked python3 plugins/decklink/tests/run-whep-receive.py`, then `PV_WHEP_SKIP_BUILD=1` normal/NV12/disabled runs: **all exit0** using the sanitizer-built owner/receiver. Real loopback WHEP/RTP/Opus, native VT, actual source and retained production output owner with refusing fake SDK; no physical card. Each mode reconnects the same source/owner, compares 90 scheduled v210 frames, and rejects actual HTTP406. Independent native pre-pack comparisons: normal168, NV12168, disabled167, blocked166. Each owner attempt observes at least20 distinct temporal identities and source-only PCM with nonzero energy. Source timestamps/durations are recorded. No ASan/UBSan diagnostics in final logs.
- Fixture is 300 unique lossless 1024×64 images at30000/1001 with per-frame Y identity and independent software references. Default sparse motion isolates ownership/order from UDP/decoder throughput; opt-in spatial-pattern stress is not admission. Exact temporal identity checks are stronger than the previous repeated-image fixture, but not sustained HD cadence/A/V drift acceptance.
- In blocked mode the first OBS callback stays blocked through both output attempts, disconnects, same-source reconnect and HTTP406. Disconnect request measurements were **7584ns and12458ns** (assertion ceiling500ms). Actual destruction-queue barrier stays blocked until callback release, then completes. The CPU-only harness must use `obs_queue_task(OBS_TASK_DESTROY, ..., true)` because `obs_wait_for_destroy_queue()` returns false without a graphics thread; the earlier failing quiescence assertion was a harness barrier error, not a bounded-destruction success. Prior diagnostics remain available.
- Whole-owner sanitizer suite `PV_DECKLINK_SANITIZE=1 python3 plugins/decklink/tests/run-receive.py`: exit0 (`raw-final-owner-sanitize.log`). Deterministic production offers and native-only frontend telemetry were rerun: exit0 (`raw-offer-final.log`, `raw-ui-final.log`). Precision and existing-codec WHEP passed earlier in this continuation (`raw-precision.log`, `raw-existing-whep.log`), before final filter/lifecycle refinements; they are not a final-tree whole-suite assertion. Historical pinned VT capability omissions remain unresolved.
- Full isolated `cmake --build build_macos_native422 --config RelWithDebInfo -j 8` and `codesign --verify --deep --strict --verbose=2 'build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app'`: exit0, private HOME/CFFIXED_USER_HOME and explicit Xcode environment. Bundle identifier remains `com.pixelview.desktop`. Logs `plugins/decklink/.test-build/whep/raw-full-build-final.log` and `raw-signature-final.log`. Development artifact only, no release/GUI/hardware acceptance.

Per-mode `plugins/decklink/.test-build/whep/{normal,nv12,disabled,blocked}/result.json` and `raw-accept-*.log` contain final results. Modified production scope for this continuation: native decoder/filter and headers, receiver, and frontend receive telemetry. Tests changed under `plugins/pixelview-whep/tests/`, `plugins/decklink/tests/`, and `test/pixelview/` (RAW audits, temporal references, ownership, preview modes, lifecycle and UI). All preceding owner/audio work and parent documents are preserved; no commit/push/deployment or user configuration/authentication changes.

### Remaining gates

1. Parent independent code/lifecycle review; no self-approved completion of review.
2. Sustained HD rational cadence, drops/repeats under realistic load/loss and common flash/click A/V drift; fake SDK sample equality alone is insufficient.
3. Shipping native capability/admission and real sender PTL/VUI/cadence. The level255 lossless fixture is NOT level120 conformance. Earlier native HD profile4/tier0/level120 30000/1001 evidence is only three frames. Actual VT sender missing VPS/VUI timing remains rejected; do not invent framerate or advertise profile4.
4. Pinned ordinary VT startup capability omissions and the rejected cooperative-pause patch remain historical unresolved limitations.
5. Authorized physical connector/mode/v210-no-conversion, independent SDI sample capture, audio/drift, unplug/restart, SDK failure/quiescence and sustained performance acceptance.

## Focused audio review corrections (2026-09-09)

**P1 early/rendered PCM routing, P2 streaming source-control reads, and decoded-audio pre-map bounds are corrected and exercised. Main422 is still unadvertised.** This section supersedes the previous continuation's shared-feed audio description, not its remaining admission limitations. All preexisting uncommitted edits are preserved; no commit/push/deploy, user app, credentials/configuration, or physical-card SDK dispatch was used. Parent independent review is still required.

### Corrections and ownership contract

- Source-feed ABI is now **version2**, with required `PV_FEED_NATIVE` / `PV_FEED_RENDERED` route on ATTACH and every subsequent request. One exclusive token owns one immutable route and generation. Route mismatch and wrong-generation detach are invalid; old version1 and missing/unknown route are rejected. Detach clears queued copies; switching routes requires fresh attachment/token. Existing owner tests retain their real registered output across native/rendered transitions.
- Only `native_audio_sample` publishes **early unsynchronized PCM to a native-route token**, for the existing timestamped card scheduler. Only the synchronized ordinary `audio_sample` publishes **clocked PCM to a rendered-route token**. Normal OBS audio delivery and counters remain unchanged; native audio counters remain separate. The rendered output still uses its private source audio endpoint, never the global mix. No timestamp rewriting, duplicate second feed, native expiry weakening, or new global-mixer callback was introduced. Rendered synchronous SDK writes remain an ordinary-route limitation, not a new card-clock synchronization claim.
- Source gain/mute are initialized to libobs's create-time unity/unmuted defaults and updated from source `volume` / `mute` signal calldata under the same receiver mutex used by both PCM callbacks. No source-control getter remains on a streaming thread. Inspection of actual libobs confirmed volume emits **before** assigning `user_volume`, mute emits **after** assigning `user_muted`, scene load uses setters, and this nonduplicable source avoids libobs's silent duplicate-field-copy path. The snapshot represents source signal values, not arbitrary later third-party rewrites of volume calldata. Connect occurs during source create before starting the worker; disconnect runs outside receiver/delivery locks before receiver destruction. libobs's per-signal dispatch mutex drains in-flight callbacks during disconnect, and its signal handler outlives the plugin destroy call. This is not a claim that our mutex synchronizes libobs's ordinary field writers.
- Both PCM callbacks now validate F32LE stereo48k byte framing and `gst_buffer_get_size` **before** READ mapping, then independently check mapped size/alignment. Invalid media resets the feed and returns an error. The existing timestamp/layout/size checks in the feed conversion remain. Sole-owner, two-memory-block oversized samples are rejected with both original GstMemory identities and block count unchanged; no oversized coalescing happens first.

### RED → GREEN and final executed evidence

Evidence prefix **R** is `plugins/decklink/.test-build/receive/`; **W** is `plugins/decklink/.test-build/whep/`.

- Recompiled and ran the supplied `/private/tmp/pv422-readonly-review-ag6h8odj/rendered-future-pcm.cpp`: it printed **one WriteAudioSamplesSync call for PCM +600ms beyond rendered start**. The in-repo replacement executes whole production owner translation units, actual registered output Start/Stop and production source audio callbacks. `R/audio-route-red.log` fails at `sdk.card.syncWrites == 0`; `R/audio-route-green.log` passes. The final owner test sees zero early writes, then exactly one clocked source-only write and no duplicate on repeated rendered frames. It controls the callback delivery boundary; it is not a physical-clock measurement.
- `R/audio-controls-red.log` fails at `streaming_getters==0`; `R/audio-controls-green.log` passes after removing those reads. Final controls tests cover initial unity without a setter, gain/mute/unmute, rendered/native route-specific gain,100 PCM packets concurrent with one serialized control-writer thread, coherent per-packet values, both mute and volume disconnect while dispatch is blocked, and real signal dispatch after callback storage is freed. No test races two libobs setter writers against each other or claims whole-libobs TSan cleanliness.
- `R/audio-bounds-red.log` fails at the expected two-memory-block assertion after the old callback coalesces oversized audio. Final sanitizer owner run passes both early and clocked oversized negatives.
- **Final `PV_DECKLINK_SANITIZE=1 python3 plugins/decklink/tests/run-receive.py` exited0** (`R/audio-final-sanitize.log`). Whole source adapters/owner test objects use ASan+UBSan; no sanitizer diagnostics. Linked libobs/Qt/GStreamer/system frameworks are not instrumented. Existing deliberate owner failure injections, deprecated test-only data-path calls and no-hotkey-permission notices remain, not new functional failures.
- **Actual Main422 WHEP four modes passed**: `normal`, `nv12`, `disabled`, `blocked`, each with two generations on the same source/owner, real Opus and actual HTTP406 fail-closed. Fresh normal build followed by `PV_WHEP_SKIP_BUILD=1 PV_WHEP_PREVIEW=<mode>` runs. Persisted `W/audio-summary.json` records **360 exactly compared owner-v210 frames and701 exactly compared native-x422 frames**; per-mode native counts178/177/174/172. `W/audio-{normal,nv12,disabled,blocked}.log` and case result files contain evidence. Test-only admission and repeated-image level255 fixture are unchanged; this is not shipping profile admission or temporal-content-order proof.
- **Existing codecs**: `run-codecs.py` exited0 (`R/audio-existing-codecs.log`):30 H264 frames,30 HEVC frames,28800 decoded Opus sample frames; new rendered source-feed check pulled10 bounded PCM packets with nonzero energy594039762961 and monotonic timestamps. `run-video-precision.py` also passed this new audio regression (`R/audio-precision.log`). These codec callback probes are separate from the actual WHEP suite.
- **Existing six-case real WHEP suite passed once** with no probe retries/mask override (`R/audio-existing-whep.log`): H264/Main/Main10/VP9-0/VP9-2 plus actual HTTP406 negative. The historical intermittent pinned-VT capability omission is not fixed by this one pass.
- `run-native-422.py`, `run-native.py --native-only`, `run-production-offer.py --deterministic-only`, and `run-native-422-envelope.py` all exited0 (`R/audio-{native422,native,offer,envelope}.log`). The separate declared-level120 HD30000/1001 envelope again compared all3 native and v210 frames exactly against independent decoded reference; not an endurance/cadence certificate.
- **Full isolated `cmake --build build_macos_native422 --config RelWithDebInfo -j 8` exited0**, `BUILD SUCCEEDED` (`R/audio-full-build.log`). Deep/strict codesign verify exited0 (`R/audio-signature.log`); bundle ID `com.pixelview.desktop`. Artifact is still `build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app`, not installed/launched or deployed.

### Unchanged blockers / admission gates

Encoded preview queues can still backpressure a stalled preview decoder. The OBS delivery mutex can still hold disconnect until a blocked callback returns; tests explicitly release that callback for teardown. UI readiness/watchdog still depend on ordinary video delivery. Actual captured VT sender cadence lacks demonstrated positive VUI/parser timing (and VPS/cadence provenance remains an admission gate); no guessed cadence was introduced. The repeated-image RTP fixture does **not** prove temporal content order, HD declared-level RTP integration, throughput or preview-decoder independence. Main422 capability-cache/PTL/bitrate admission, physical SDI/cadence/common A/V drift, hotplug, GUI and driver-call bounds remain unverified/unauthorized. No shipping Main422 bit was enabled.

Files changed by this correction: `plugins/pixelview-whep/{pixelview-whep.c,source-feed.h,source-feed-queue.h,README.md}`, `plugins/pixelview-whep/tests/{source-feed.c,native-422-integration.c,codecs.c}`, `plugins/decklink/{decklink-receive.hpp,decklink-output-receive.inc}`, `plugins/decklink/tests/{production-feed.c,receive.cpp}`, and this status. Previous implementation edits remain intact.


## Actual Main422 WHEP integration continuation (2026-09-09)

**Substantial transport/owner acceptance now passes, but this is NOT shipping admission or completion of preview independence.** All earlier uncommitted implementation and parent-owned handoff/color documents remain preserved. No commit, push, deploy, credentials, user application, saved configuration, server configuration or physical SDK dispatch was used.

### New exercised path

`python3 plugins/decklink/tests/run-whep-receive.py` compiles the whole existing DeckLink owner against the existing generated refusing SDK/fake card, and the actual production WHEP source/worker/filter/native decoder. It reuses the desktop loopback server builder, which extracts the current PR51 engine registration/selection/binding read-only from `/Users/max/src/pv-engine-whep-pr`. Discovery factories are null; no installed DeckLink SDK dispatch is linked. This is **not a replacement byte sink**. WHEP HTTP offer/answer, loopback ICE/DTLS/SRTP, HEVC depayloading/parsing and real Opus decoding are active.

- Main422 offer admission exists **only in `tests/whep-feed.c`**: a test-local probe snapshot and wrapper around the existing production offer helper select profile4 / main-tier level120 / `interop-constraints=1d0800000000`. Shipping `profile-offer.*` and `capability-probe.*` have not been enabled or assigned a native422 capability. The test snapshot is injection, not measured capability.
- The lossless transport fixture deliberately remains **level255**, 1024×64, 30000/1001, one repeated generated ramp/chroma-detail image. Its test-only offer override is a transport diagnostic, **not validation that the stream conforms to the offered level120**. All300 encoded frames independently FFmpeg-decode exactly to the raw reference. Native x422 is independently observed before packing; scheduled owner v210 is compared byte-for-byte including padded rows.
- Final four modes each pass two receive generations on the **same OBS source and same owner**, actual disconnect/new-endpoint reconnect, fresh queues/epoch, and a third actual HTTP406 offer refusal with zero media and refused owner Start. Stop invalidates the source feed and makes the owner unhealthy before cleanup. This exercises `DeckLinkDeviceInstance::StartNativeOutput`; registered-output/UI Start/Stop remains covered by the separate owner suite, not driven by this RTP harness.
- **360 owner-scheduled frames and694 native x422 frames** compared exactly across `normal`, `nv12`, `disabled`, `blocked`; counts are aggregated from persisted per-case results in `.test-build/whep/summary.json`. Each generation checks45 exact rational card slots and receives real nonzero Opus PCM at the scheduler. Every fake SDK audio submission after playback start asserts timestamp >= current card clock.
- Actual sender RTP marker timestamps advance3003 ticks at90kHz. Source delivery uses unmodified Gst segment/running timestamps and33366666ns parsed video durations; Opus produces960-frame/20ms packets. No test assigns receiver PTS/durations. GStreamer jitter/skew corrections mean source PTS differences are not asserted equal to a constant33366666ns; monotonicity and durations are checked separately. Raw elementary FFmpeg replay initially produced occasional3004-tick increments; explicitly specifying the fixture's sender input rate30000/1001 fixes that test-source timestamp generation. This is not receiver retimestamping.

### Audio integration defect found and corrected

The first real RTP→owner test failed: the synchronized OBS audio appsink delivered source PCM roughly600ms behind the unsynchronized native video branch because the preview decoder contributes pipeline latency. The card's conservative expired-PCM policy correctly stopped scheduling when that late audio finally crossed the native epoch. `trace-red.log` records source A/V timestamps and the failed slot; no scheduler expiry assertion was relaxed.

Production now tees **decoded, converted F32 stereo PCM** into a bounded unsynchronized `native-audio` appsink before clocked OBS audio delivery. That callback copies source PCM into the existing versioned feed under the receiver lock only, never the preview delivery lock or a card call. The normal audio appsink retains OBS delivery and its old `audio_frames` counter; `native_audio_frames` separately counts early decoded source audio and resets on connect. Preview audio has a bounded2-second/100-buffer/1MiB downstream-leaky queue so an OBS consumer cannot hold the native PCM branch; only decoded preview PCM may be discarded, never compressed reference AUs. Native PCM retains existing bounded queue/drop telemetry, source gain/mute, common source timestamps and S16 saturation behavior.

An intermediate regression moved the old audio counter, breaking the standalone codec harness. That change was corrected by retaining the old counter and exposing a distinct early-audio counter; `precision-regression-green.log` passes. A first200ms preview-audio queue was shorter than measured pipeline latency and discarded routine playback; the final2-second bound prevents that observed regression. These limits are not a driver deadline or network A/V synchronization certification.

### Truthful native HD fixture prerequisite

`python3 plugins/pixelview-whep/tests/run-native-422-envelope.py` generates a distinct **1920×1080,30000/1001, profile4, main-tier level120** CRF18/VBV12000 fixture with explicit limited709 metadata. It checks actual ffprobe and HEVC trace_headers PTL/chroma/depth/VUI timing, not a factory mask. Three hardware native x422 frames and production v210 outputs are exactly equal to an independent software decode of that encoded stream. `raw_source_lossless:false` is deliberate: the encoder is lossy; exactness is measured against decoded compressed reference, not falsely against pre-encode input. Evidence: `plugins/pixelview-whep/.test-build/native-422-envelope/{result.json,headers.log,commands.json}`. This short fixture is a native capability **prerequisite**, not integrated immutable capability-cache admission, a60fps claim, or a throughput/bitrate-conformance certificate.

Read-only inspection also tested the actual previously captured Apple VT sender stream from `/Users/max/src/obs-vt-range-audit/cpu/hevc-main42210-partial.hevc`: three copied AUs are rejected by the production decoder. Its actual PTL is profile4/main-tier120, progressive/no reorder, limited70942210, absent chroma location (standard default), but **VUI timing is absent and h265parse emits no framerate**. Thus `configure`'s required positive rational caps rate fails before native decode. The inspected stream contains no demonstrated disallowed SEI in these copied AUs; general SEI admission is still deliberately narrow. Evidence: `sender-parser.log` and `native-422-envelope/sender-headers.log`. Do not invent30fps/60fps from absent timing, relax color/SPS checks, or claim real Apple-sender WHEP admission from the x265 fixture.

### Preview scope — incomplete software gate

The passing test modes cover the **OBS decoded-video consumer boundary**: ordinary eight-bit NV12 negotiation, suppressed OBS video delivery (decode still runs), and blocking the real OBS video callback for the entire owner scheduling interval, then explicitly releasing it before source teardown. Native output bytes/rational slot timing and source PCM remain valid. These are not UI toggle/disabled-decoder or physical-display acceptance.

The encoded tee still has bounded **nonleaky encoded queues**. A blocked/hung preview **decoder**, rather than an OBS display consumer after its leaky raw queue, can still backpressure native decode. No blind encoded-AU dropping was introduced to disguise this. Also, the frontend still has its P010 canvas transaction, and its `ready`/watchdog depends on advancing ordinary video delivery; a blocked display callback can therefore make UI readiness stale even while direct owner tests remain healthy. Full preview independence, UI readiness separation and bounded driver/teardown behavior remain software work, not merely hardware testing.

### Additional verification / remaining admission

- `PV_DECKLINK_SANITIZE=1 python3 plugins/decklink/tests/run-receive.py` passes on the final source: whole owner/source test objects instrumented, no ASan/UBSan diagnostics. Added explicit failed-card-clock, negative-clock and non-1.0-speed cases both with and without hardware lookahead backpressure; no postfailure writes. Added real externally replaced video/audio endpoint restoration; previous source readiness/retained-output tests remain intact. Linked frameworks are not instrumented.
- Native422 full suite, deterministic production-offer suite, native-only source suite and precision suite pass. The final full **six-case existing-codec WHEP suite passed once**, including VP9 profile2 and realHTTP406, without probe retries/mask overrides. This does **not** fix or erase the historical intermittent pinned-VT capability omission. Evidence `existing-codecs-loopback.log` plus persisted standard loopback results.
- Full isolated `cmake --build build_macos_native422 --config RelWithDebInfo -j 8` exited0 (`** BUILD SUCCEEDED **`), and deep/strict codesign verification passed. Bundle ID readback `com.pixelview.desktop`; artifact remains `build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app`. Evidence `full-build.log`, `signature.log`. Build registration with LaunchServices is not a user-app launch or replacement.
- Main422 remains **unadvertised**. Still required before admission: actual Apple-sender cadence provenance/transport handling, production PTL/bitrate enforcement tied to the measured native capability cache, HD declared-level RTP→owner coverage, encoded-decoder/GUI preview independence, sustained throughput, and independent review. Physical connector/mode/v210/no-conversion/keyer guards remain in the existing owner; physical SDI sample equality, card cadence/common A/V epoch/drift, hotplug and GUI acceptance are still unauthorized/unverified. Fake SDK success never certifies a card.

All continuation runners use isolated HOME/CFFIXED_USER_HOME. Reproduce the preview variants after the default build with `PV_WHEP_SKIP_BUILD=1 PV_WHEP_PREVIEW=nv12 python3 plugins/decklink/tests/run-whep-receive.py`, replacing `nv12` with `disabled` or `blocked`. Run the default without `PV_WHEP_SKIP_BUILD` after source edits; the skip flag intentionally reuses a previously compiled test executable. `git diff --check`, Python syntax checks and added-source security scans passed; independent review remains for the parent.

New files: `plugins/decklink/tests/{whep-feed.c,whep-receive.cpp,run-whep-receive.py}`, `plugins/pixelview-whep/tests/run-native-422-envelope.py`. Modified in this continuation: production `pixelview-whep.c`; owner test runner, fake card/private media/production PCM fixture tests; `tests/native-422.c`, `native-422-integration.c`, `run-whep-loopback.py`, `whep-loopback.go.in`; this status. Earlier changes are preserved. Logs above are under `plugins/decklink/.test-build/whep/` unless stated otherwise. No whole-feature/independent-review completion is claimed.

## Independent-review corrections (2026-09-09)

Three review defects are corrected in the uncommitted tree; this is **not capability admission or feature completion**. HEAD remains `d043346e4fa4f9d9f5eac2d957b52a17eb9145f7`. Main422 remains unadvertised.

- **Retained native → rendered endpoint lifetime:** private media now remembers only caller-owned external video/audio endpoints and restores them before replacing/closing its own queues. Rebinding and unbinding restore the original endpoints; an explicitly supplied replacement endpoint is preserved. Rendered preparation refuses a null video endpoint. Caller-owned media must outlive the retained output, as required by libobs's borrowed-media API. The regression retains a real registered output after Start/Stop/device removal, rediscovers the fake device, and executes four rendered/native Start/Stop cycles on that same output, checking endpoint identity, valid video info, private audio, activity and final unbind restoration.
- **Expired pending PCM:** scheduling now queries `GetScheduledStreamTime` in the 48000-sample timescale before each audio submission/retry. An expired next sample, failed clock query, negative clock or unexpected playback speed causes sticky scheduler failure; no further audio/video is scheduled until Stop/new Start. Pending PCM expiry is checked even while the hardware lookahead limit suppresses writes. This is a conservative **fail-stop**, not seamless audio recovery; UI watchdog performs the existing output drain. Tests cover zero writes, slow partial writes and high hardware-buffer backpressure with exact 30000/1001 clock progression; existing 123-sample partial-write and pre-epoch trim tests still pass. Queued hardware media can finish before the watchdog drains; this is not proof of physical presentation or a driver-call deadline.
- **Readiness before UI activation:** production source `get_status` now returns a mutex-protected `ready` snapshot requiring current/active generation equality, accepting/nonchanging/nonquitting state, `playing`, a delivered frame and a monotonic last-video age under 500ms. Both manual Start's pre-bind gate and AutoStart consume this fail-closed field instead of cumulative frames. Production status tests reject error/ended/idle/connecting, stalls, old generations, empty reconnects and cancelled/changing attempts, then admit a fresh generation. Compiled Qt tests execute the actual watchdog include and extracted manual Start predicate with real libobs procs; stale counters cannot authorize either route. Manual predicate extraction also checks its position before the hardware-start call. These are offline policy/lifecycle tests, **not complete native GUI hardware activation acceptance**.

### RED → GREEN evidence

All logs below are under `plugins/decklink/.test-build/receive/`. Original reviewer scratch runners/sources were read; their reproductions were ported into the in-repository suite rather than retaining absolute scratch dependencies.

- `rebind-red.log`: expected assertion on original rendered endpoint restoration; `rebind-green.log` exited0 after the lifetime fix. The final test was strengthened to real repeated registered Start/Stop, not only PrepareReceive.
- `audio-red.log`: expected assertion that expired PCM must make the owner unhealthy. Initial GREEN debugging exposed a test mistake: 123-sample writes finished before expiry, and callbacks cannot keep popping an already failed/drained fake queue. Expiry cases now use zero/one/two-sample acceptance and call the retained late callback directly after failure. `audio-buffered-red.log` independently caught expiry bypass at the hardware lookahead limit.
- `readiness-source-red.log`, `readiness-ui-red.log`, `readiness-manual-red.log`: expected failures of source readiness, stale-frame AutoStart and the actual manual predicate respectively.
- Final **`PV_DECKLINK_SANITIZE=1 python3 plugins/decklink/tests/run-receive.py` exited0**: `corrections-final.log`. No ASan/UBSan diagnostics. Whole production owner translation units and source adapters are instrumented; linked libobs/Qt/GStreamer/system frameworks are not. Existing intentional failure-injection and synthetic OpenGL warnings remain in the logs.
- Focused regressions all exited0: `run-native-422.py` (`corrections-native422.log`, including nine restart-frame comparisons and fifteen packing widths); `run-production-offer.py --deterministic-only` (`corrections-offer.log`); `run-native.py --native-only` (`corrections-native.log`); `run-video-precision.py` (`corrections-precision.log`). Legacy harnesses used private HOME/CFFIXED_USER_HOME. No full WHEP retry was performed; the historical VP9-2 failure remains unresolved.
- Full isolated `cmake --build build_macos_native422 --config RelWithDebInfo -j 8` exited0, **BUILD SUCCEEDED** (`corrections-build.log`). Deep/strict codesign verification exited0 (`corrections-signature.log`); bundle ID readback is `com.pixelview.desktop`, with embedded DeckLink/output-UI modules. The build registers its isolated bundle with LaunchServices; no user application was launched/replaced.

Correction files: `plugins/decklink/{decklink-private-media.hpp,decklink-output-receive.inc,decklink-receive.hpp}`, `plugins/decklink-output-ui/{decklink-receive-ui.inc,decklink-ui-main.cpp}`, `plugins/pixelview-whep/pixelview-whep.c`, `plugins/decklink/tests/{receive.cpp,production-feed.c,receive-ui.cpp,run-receive.py}`, and this status. All preceding implementation edits are preserved. No commit, push, deploy, physical-card SDK dispatch, credentials, saved user configuration or user-app operations occurred.

**Remaining gates unchanged:** parent independent review; Main422 WHEP offer/answer/RTP/Opus integration and capability admission; preview independence; real GUI/SDI/cadence/audio/hotplug acceptance; sustained throughput and driver-call bounds. The decoder-only runner's historical `decklink_scheduler_implemented: false` field describes that runner's scope, not the separately executed owner suite. Do not treat its JSON as whole-application admission.

## Previous continuation: real DeckLink owner (2026-09-09)

The existing `DeckLinkOutput` / `DeckLinkDeviceInstance` now consumes the production version1 exact-source feed. This supersedes the historical “no DeckLink consumer” statements below, which describe the previous worker checkpoint. End-to-end Main422 WHEP and physical fidelity acceptance remain incomplete.

### Implemented

- `decklink-receive.hpp` schedules native v210 and source-only 48k S16 stereo on one card clock: initialized limited-black preroll plus timestamped silence, common source epoch, rational video slots, bounded last-frame repeats, old-frame drops, partial/zero audio-write retention, prefix/overlap trimming, and discontinuity/stall refusal. Requires SDI, exact progressive mode/rate/geometry, ten-bit YUV support, read-back no output conversion, and disabled keyer. No RGB conversion or global mixed audio enters this native path.
- The **existing** device instance acquires an atomic device-use lease, preventing capture/output overlap in both directions. Native and ordinary start failures roll back acquired resources. Callback gates drain in-flight work and detach owner pointers; retained late callbacks are safe. Native cleanup failures quarantine the device lease rather than permit unsafe reuse. Driver calls themselves have no hard timeout guarantee.
- `decklink-private-media.hpp` resolves the ignored begin-capture-flags architectural blocker with actual private libobs video/audio endpoints. Normal registered-output activity/start/end accounting remains real; the idle endpoints do not carry v210 or fabricate PCM. Native SDK counters are available through `receive_status`; ordinary OBS frame-output totals do not count direct native schedules.
- `decklink-output-receive.inc`, existing output registration and frontend/UI bind the exact source through in-memory procs and weak UI references. Start pins the source; active mutation is rejected. UI-thread watchdog stops on loss/reset/card removal/stall; AutoStart waits for receive readiness and is one-shot, not an automatic retry after failure. Ordinary unbound output remains available. Non-native receive retains rendered video but uses private source audio, not the global mixer; this rendered route is not a native422 fidelity claim.
- Ordinary owner regressions found/fixed during tests: initialized preroll frames, padded source-stride row copying, start-failure rollback, and late callback owner detachment.

### Executed acceptance

`PV_DECKLINK_SANITIZE=1 python3 plugins/decklink/tests/run-receive.py` exited0 on the final implementation. It compiles whole production owner translation units against refusing SDK implementations generated from bundled SDK declarations; SDK dispatch is not linked and discovery factories return null. Tests cover real registered `decklink_output` Start/Stop, private activity/media separation, active settings rejection, device removal, capture exclusion, reset, restart, allocation/SDK failure injection, partial audio writes, stop versus blocked audio callback, failed cleanup and retained late callbacks. The production encoded-filter → hardware VT → source A/V adapters → real owner/fake SDK route compares **all three** distinct fixture v210 frames exactly and checks source PCM/card timestamps. This remains an offline elementary-stream fixture: WHEP is NULL; no Main422 RTP or Opus packet delivery is claimed.

The same runner executes real libobs private-media tests and compiled Qt tests of the production selection/watchdog include with controlled start/stop boundaries. The existing compiled receive-UI regression also exited0. Sanitized owner/source test code emitted no ASan/UBSan diagnostic; linked libobs, Qt, GStreamer and system frameworks are not sanitizer-instrumented. Harness logs include OpenGL context warnings during synthetic setup; this is not GUI validation.

Final full isolated `cmake --build build_macos_native422 --config RelWithDebInfo -j 8` exited0 (`BUILD SUCCEEDED`). `codesign --verify --deep --strict --verbose=2` exited0 for `build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app`, including DeckLink, output UI and WHEP plugins. Evidence: `plugins/decklink/.test-build/receive/{final.log,final-build.log,final-signature.log}`. No commit/push/deploy, physical SDK dispatch, credential access, server changes or user-app launch was performed. Xcode's normal build registers its isolated bundle with LaunchServices.

### Remaining gates / handoff

- Parent independent review; actual Main422 WHEP offer/answer/RTP/Opus → owner fixture; capability admission based on that validated route. **Main422 remains unadvertised.** Existing VP9-2 loopback failure below is not reclassified or retried away.
- Physical card scheduling/audio behavior, external SDI sample readback, full GUI Start/Stop/AutoStart acceptance and long-running HD60 throughput remain unverified. Fake SDK correctness is not hardware fidelity.
- Preview tee remains backpressured/non-independent. General HEVC conformance, production level/bitrate envelope, driver-call deadlines and non-macOS integration remain uncertified.
- UI watchdog owns fail-stop activity for receiver attempts; a third-party caller using the output proc directly must poll `receive_status` and stop an unhealthy attempt. Native counters report scheduling events (including empty audio-buffer polls), not physical presentation verification.
- New production files: `plugins/decklink/{decklink-receive.hpp,decklink-private-media.hpp,decklink-output-receive.inc}`, `plugins/decklink-output-ui/decklink-receive-ui.inc`. Tests: `plugins/decklink/tests/{run-receive.py,receive.cpp,private-media.cpp,production-feed.c,receive-ui.cpp}`. Existing owner/device/output/frontend files are modified; all foundation edits preserved.

## Historical source-only checkpoint


## Outcome / admission

The native decoder, exact x422→v210 packing, and a production WHEP encoded-filter/versioned source-bound A/V pull seam are implemented and exercised offline. A full isolated application build and deep/strict ad-hoc signature verification passed.

**The requested end-to-end feature is NOT complete.** There is now a versioned source-bound video/audio feed, but no DeckLink consumer, common card A/V scheduler, or Main422 WHEP capability admission yet. Main422 remains unadvertised: `profile-offer.*` and `capability-probe.*` were deliberately not expanded. The fake output in these tests is a byte sink, **not a fake implementation of the existing DeckLink SDK owner**. No physical-output or shipping fidelity claim is justified.

Starting checkpoint was `39bf2c5e08a09ec0f1bbbb7babbfeba46161d50c` on `pixelview/minimal-capture`. Parent subsequently committed only its documentation; observed HEAD is `d043346e4fa4f9d9f5eac2d957b52a17eb9145f7`. Implementation changes remain uncommitted. This agent did not stage/commit/push. Parent-owned `pixelview-422-implementation-handoff.md` and `pixelview-obs-color-range.md` were not edited.

## Continuation: independent-review fixes and source ABI

- **TDD bounds fix:** `native-422.m` checks `gst_buffer_get_size` before mapping codec_data (64KiB maximum) and compressed AUs (16MiB maximum), while retaining both mapped-size guards. `tests/native-422-bounds.m` uses sole-owner two-memory buffers one byte over each boundary and checks both memory count and original memory identities after refusal. Both tests first failed with one merged memory block; both now retain two. An initial test retained an extra buffer reference and incorrectly passed because GStreamer would not mutate that buffer; the current test removes that test flaw. Evidence: `bounds-red.log`, `bounds-au-red.log`, and subsequent green suite logs.
- **Restart equality fix:** the test now truncates on the first run and appends subsequent runs. Runner compares the complete nine-frame output against three copies of the independently verified three-frame v210 reference. The strengthened assertion failed before fixing the truncation. Evidence: `restart-red.log`, final suite `restart_frames_compared: 9`.
- **Versioned source ABI:** new `source-feed.h` is Gst/OBS/CoreVideo-independent. One source-scoped token owns the feed; wrong-version/busy/stale consumers fail closed. Reads copy into caller-owned storage; insufficient capacity does not consume a frame. Detach frees queued storage and leaves no retained consumer pointers or callbacks. Source stop/error invalidates the feed; generation changes produce RESET, requiring fresh attach/admission rather than silently joining a new stream. Callers must retain the exact OBS source while invoking its proc. This eliminates the prior native subscriber reentrancy contract, **not** card callback/driver quiescence (no card consumer yet).
- **Bounded source queues:** three video frames, ten audio packets, with byte caps of one HD v210 frame and one120ms48k stereo packet respectively. Overflow drops the oldest decoded packet with counters; encoded AUs are still not dropped. Allocation failure/invalid media invalidates the feed. One additional bounded allocation can exist transiently during enqueue. Queue primitives require the receiver mutex; the public proc and producers take it. ASan+UBSan tests cover owned copies/reused input, video overflow, insufficient-buffer retry, exclusive token, version refusal, reset and detach. Concurrent stress/fuzz acceptance is still pending.
- **Source-only audio:** the real production audio appsink also copies48k interleaved float stereo to bounded S16LE stereo for the direct feed, with source mute/gain and strict running-time+pipeline-base timestamps. Preview/meter/Listen remain on the existing OBS source output path. Native tests execute the actual adapter with positive/negative endpoints, half-scale, NaN→silence, near-full-scale rounding saturation, source mute and common-base timestamps. This is synthetic PCM adapter acceptance, **not Main422 RTP/Opus or card A/V acceptance**. The near-full-scale test exposed/witnessed a rounding wrap before the explicit post-round clamp was added.
- **Existing-owner investigation, no owner edits:** `obs_output_begin_data_capture` and `obs_output_can_begin_data_capture` ignore their flags argument in this checkout. Passing VIDEO-only flags cannot suppress the global audio attachment on the existing AV output. The next implementation must bind genuinely private media/activity queues or introduce a tested libobs accounting seam; it must not claim source-only output while retaining `obs_get_audio()`. Existing owner also needs null-mode admission, transactional rollback, source-loss activity handling and explicit callback drain. No second output driver or untested card-start branch was added here.

Continuation verification (all exited0): final `run-native-422.py`, deterministic `run-production-offer.py --deterministic-only`, `run-native.py --native-only`, `run-video-precision.py`, full incremental `cmake --build build_macos_native422 --config RelWithDebInfo -j 8`, and deep/strict codesign verification of its bundle. Logs under `.test-build/native-422/`: `continuation-final.log`, `continuation-offer.log`, `continuation-native-regression.log`, `continuation-precision-regression.log`, `continuation-build.log`, `continuation-signature.log`. These results do not replace the previously failed full WHEP VP9-2 regression below; that failure was neither retried away nor reclassified. No Main422 offer/RTP/Opus→fake SDK test was completed. No physical device, GUI/user app/server, credentials or saved configuration were touched.

Additional files in this continuation: `source-feed.h`, `source-feed-queue.h`, `tests/source-feed.c`, `tests/native-422-bounds.m`; modified existing native decoder/source, native integration/filter tests and runner plus this status and README/test status. **No DeckLink/frontend source changes; owner/scheduler integration remains a substantial software gate.**

## Implemented production code

- `plugins/pixelview-whep/native-422.h`, `native-422.m`: worker-confined hardware VT decoder accepting parsed hvc1 access units. Uses the already bundled GStreamer HEVC parser to inspect VPS/SPS/PPS and explicit SPS VUI before native session creation. Requires profile4 ten-bit422 constraints, progressive/no-reorder/single temporal layer, explicit limited709 SDR, supported left chroma location, bounded HD geometry and positive rational rate no greater than60fps. It requests **public x422**, requires actual hardware-session readback, validates actual native format/plane geometry/strides/color attachments, then packs active codes to v210 without color conversion, filtering or clipping. `ceil(width/48)*128` arithmetic avoids the existing libobs V210 alignment bug. Padding is limited black. Compressed samples and result buffers are owned copies; no pointer into a Gst mapping escapes. A rejection is sticky until decoder destruction/new generation.
- `plugins/pixelview-whep/native-422-filter.h`, `native-422-filter.c`: a parsed encoded-video tee with bounded nonleaky encoded queues and an unsynchronized native appsink. **Encoded AUs are not dropped** (reference dependencies); normal preview keeps its existing decoder and raw appsink. Main/Main10/H264/VP9 pass through without a second native decode. Once native422 starts, a profile/format change rejects rather than silently selecting the preview route. The filter owns/releases its attempt callback context.
- `plugins/pixelview-whep/pixelview-whep.c`: installs the real rswebrtc `request-encoded-filter` signal in `make_pipeline`; preserves transport/signaling/jitter/authorization. Native frames enter a source-owned bounded queue; `native422_feed(ptr request, out int version)` provides version1 exclusive attach, copied video/audio reads, reset and detach. The former borrowed `native422_video` signal has been removed. `get_status` adds `native422_frames`. Timestamp is validated Gst segment running time + the same pipeline base used by ordinary A/V. Stale/detached attempts do not deliver. Native delivery holds only attempt/receiver locks while making the bounded source-owned copy, never an output callback, SDK call, or receiver delivery mutex. Missing filter construction becomes a terminal safe pipeline error, not a lossy fallback. **No consumer callback is registered or invoked.** Callers retain an OBS source reference for each synchronous proc call and own the destination memory. No consumer opens hardware.
- `plugins/pixelview-whep/CMakeLists.txt`: builds the native production files and links system Foundation/VideoToolbox/CoreMedia/CoreVideo plus the **already bundled** codecparsers dylib. No new runtime codec package or software decoding fallback.

### Decoder boundaries not yet certified

The small lossless fixture declares level255. It establishes exact samples, NOT level123/HD60 throughput. The decoder currently bounds geometry/rate but does not establish a production HEVC level/bitrate envelope. Do not advertise Main422 based on this fixture or hardcode a native capability bit. Cropped HD, real RTP duration availability, multiple temporal layers, reordered pictures, interlace/PsF, general SEI, full/HDR, scaling and arbitrary colorimetry are not admitted/certified. Unsupported SEI is conservatively rejected; malformed-stream coverage is not exhaustive bitstream conformance/fuzz certification. Third-party VT calls can block; bounded queues do not imply a hard driver-call/shutdown deadline.

## Executed native acceptance

```sh
python3 plugins/pixelview-whep/tests/run-native-422.py
```

Final run exited0. The runner uses private HOME/CFFIXED_USER_HOME, an isolated curated runtime copy, per-command Xcode selection, local synthetic files and no network/card/GUI/authentication. All native test compilation uses `-Wall -Wextra -Werror`.

- Generates three **different** 1024×64 ten-bit422 frames at30000/1001: adjacent luma codes, chroma permutations, one-sample chroma impulses, and opposite adjacent-row detail. Matching input/output color tags prevent an accidental FFmpeg RGB conversion. Synthetic raw fixtures are CC0-1.0; test/production source is GPL-2.0-or-later. Fixtures are regenerated from in-repo code, not copied external binaries.
- Independent FFmpeg software HEVC decoding is byte-equal to generated raw input before native comparisons. A **nonshipping** lock observer independently saves actual native x422 components **before packing**; all Y/Cb/Cr codes of all three limited frames equal the independent reference. Independent FFmpeg v210 decoding of the final production packed output is also byte-equal. Observer code is not included by CMake or in the app.
- Full-range, PQ, unknown transfer/matrix and a separately encoded ten-bit420 negative produce no native frames. Malformed AU, missing PTS, resolution-caps change and profile-caps change after one valid frame reject the remaining samples; refusal stays latched. These are explicit fixture/caps mutations, not a claim of live-stream renegotiation acceptance.
- Production filter: three separate stop/recreate runs deliver nine timed frames and release the opaque owner exactly once per run. Buffer reuse is exercised and packed bytes match the independent result.
- Production `make_pipeline` really registers `request-encoded-filter`; the integration fixture emits that signal on actual whepclientsrc, feeds parsed synthetic AUs through the returned filter and reads the actual production versioned source feed proc. Exact copied bytes and common-base timestamps pass. Actual `stop_pipeline` detaches the attempt; retained `rx` cannot create a late filter. Removing `tee` from the test-process registry reproduces missing-filter failure and now asserts terminal refusal. **The transport remains NULL in this test: no Main422 offer/answer/RTP is claimed.** Elementary-file PTS are explicitly supplied by the offline transport fixture.
- The actual production pack function is compiled with **ASan+UBSan** and exercised against native x422 buffers of widths2,4,6,8,46,48,50,94,96,98,128,720,1024,1280,1920, eight rows each, distinct padded strides and unaligned input addresses. Full ten-bit-domain code permutations, destination guard bytes, nonzero low-bit rejection and unsupported chroma-site rejection pass. All15 outputs independently FFmpeg-decode byte-equal to generated planar references. No sanitizer diagnostics. This sanitizer claim is for the packing/layout test, not the whole app/driver or card scheduler.

Evidence under `plugins/pixelview-whep/.test-build/native-422/`:

- `native-suite.log`, `acceptance.json` (written only after the complete native suite passes), `commands.json`, `hashes.json`;
- generated `.h265`, raw `.yuv`, native observer `.yuv`, `.v210`, independent decoded references and per-width layout outputs;
- `native-suite-red-filter.log`: expected red assertion before fixing missing-filter fallback;
- ignored `debug.py` / `debug.m`: temporary diagnostics, not production assets.

Initial implementation tests exposed two real integration assumptions: uncropped GstH265SPS crop dimensions are zero, so geometry uses coded dimensions when `conformance_window_flag` is absent; h265parse can retain initial parameter sets with hvc1 caps, so the decoder accepts only byte-identical copies of the validated codec_data sets. Changed in-band sets still reject. A missing destroy callback in the test-only OBS owner caused an initial teardown crash and was corrected; final native suite exits normally.

## Existing-codec regression results

Passed (exit0):

```sh
# Use private HOME and CFFIXED_USER_HOME for the legacy harnesses too.
export HOME="$PWD/plugins/pixelview-whep/.test-build/native-422"
export CFFIXED_USER_HOME="$HOME"
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
python3 plugins/pixelview-whep/tests/run-production-offer.py --deterministic-only
python3 plugins/pixelview-whep/tests/run-production-offer.py
python3 plugins/pixelview-whep/tests/run-native.py --native-only
python3 plugins/pixelview-whep/tests/run-video-precision.py
python3 plugins/pixelview-whep/tests/run-profile-offer.py
```

The precision runner itself also creates a private HOME/runtime. Logs: `regression-production.log`, `regression-native.log`, `regression-precision.log`, `regression-profile.log`. Deterministic production hooks passed their seven admitted-envelope cases and empty/NULL/stale negatives. Default production/worker probing passed in this run; that does not establish startup-repeatability.

**WHEP regression run is NOT all green:**

```sh
PIXELVIEW_ENGINE_SOURCE=/Users/max/src/pv-engine-whep-pr \
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
python3 plugins/pixelview-whep/tests/run-whep-loopback.py
```

- Original default engine checkout lacks `negotiation.go` after the intentional reset; no engine file was modified. Located the already existing PR worktree and used the runner's explicit source override read-only.
- Current PR added `receiverCodec` and changed the binding adapter parameter to `codec.Codec`. Updated **only the desktop test extraction glue**, preserving those real production functions, so offline Go compilation succeeds. Future provenance includes receiver_codec.go.
- Actual full six-case run: H264:32 video/88320 Opus sample frames; Main:31/94080; Main10:31/94080; VP9 profile0:32/89280; HTTP406 negative passed. Jitter50 and nonzero Opus energy passed in the four media cases.
- VP9 profile2 failed with zero media. Its captured offer includes **only VP9 profile0**; server400 says `unexpected fallback vp9-8bit-420`. This is consistent with the known fail-closed startup capability omission, but this run did not trace the upstream drain race and does not prove the same cause. No retry-until-green, fixed-mask override or lowered assertion was used. The full suite remains failed.
- Logs: `regression-whep.log` (missing default engine file), `regression-whep-pr.log` (old extraction API failure), `regression-whep-pr2.log` (actual six-case result); original per-case logs/SDP remain under `.test-build/whep-loopback/`. This is existing-profile RTP/Opus regression evidence, **not Main422 RTP acceptance**.

The full repository test discovery was not run: live Keychain/GUI/release prerequisites must not be bypassed. No claim that those historical failures are fixed.

## Full build / signature

```sh
PIXELVIEW_BUILD_DIR=build_macos_native422 \
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
bash cmake/macos/pixelview-build.sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
cmake --build build_macos_native422 --config RelWithDebInfo -j 8
codesign --verify --deep --strict --verbose=2 \
  'build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app'
```

The initial background tool notification had an inconclusive exit status; it was **not** treated as success. Actual build log contains `** BUILD SUCCEEDED **`. Subsequent foreground full incremental builds exited0; the final build includes the missing-filter fail-closed correction, and final deep/strict signature verification exited0. Bundle ID readback: **`com.pixelview.desktop`**. Artifact: `build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app`.

Logs: `build.log`, `build-final.log`, `signature-final.log`. Helper rebuilt/staged the pinned dependency closure and embedded independent copies in the new build directory. Existing app bundles/processes were not replaced or restarted. This is an ad-hoc development build of an uncommitted tree, not a release, Developer ID/notarization, GUI or physical-device acceptance.

## Exact edited/added files

Production: `plugins/pixelview-whep/{CMakeLists.txt,pixelview-whep.c,native-422.h,native-422.m,native-422-filter.h,native-422-filter.c}`.

New tests: `plugins/pixelview-whep/tests/{run-native-422.py,native422_build.py,native-422.c,native-422-filter.c,native-422-integration.c,native-422-layout.m,native-422-observer.c}`.

Existing harness linkage updates (all compile/link the real native files, no decoder stubs): `plugins/pixelview-whep/tests/{build-worker-probe.py,live-source.py,run-codecs.py,run-native.py,run-production-offer.py,run-upstream.py,run-video-precision.py,run-whep-loopback.py}`. Live-source/worker build scripts were updated for link completeness, **not run against credentials**. run-whep-loopback also has the PR extraction adaptation above.

Documentation: this file, `plugins/pixelview-whep/README.md`, `plugins/pixelview-whep/tests/integration-status.md`. No DeckLink/frontend source or saved settings were modified. Reusable parser/filter lessons were added to the default profile's existing `videotoolbox-422-fidelity` skill.

`git diff --check` passed. Added-line/untracked-source static checks found no hardcoded credential assignment, shell=True or eval additions. Independent review is pending with the parent; no self-approved/verified commit is claimed.

## Remaining implementation — must precede enabling Main422

1. Wire the version1 source-bound A/V pull ABI to the existing DeckLink owner. Source-side ownership/reset/detach is implemented; card-side quiescence, output activity and lifecycle integration remain unimplemented.
2. Wire the **existing** native DeckLink owner, not a second driver, to a bounded typed receive queue. Validate actual connector/mode/rational FPS/v210 support with conversion off. Handle every SDK failure and rollback; initialize all preroll/repeat/slate buffers to legal limited black.
3. Add one card scheduler owning timestamped video AND source-only48k audio, common epoch/preroll, rational slots, late/drop/repeat policy, partial audio writes, underrun/driver telemetry and deterministic callback quiescence. Do not attach global OBS mixed audio alongside raw source audio. Preserve Mute/Listen and native output activity accounting.
4. Implement Start/Stop/Auto Start readiness, capture/output mutual exclusion, source/device/mode change rejection/drain, unplug/error rollback and reconnect behavior. Do not reuse the last receiver image or silently fall back to sender canvas when receive disappears.
5. Add **actual Main422 WHEP offer/answer → RTP/Opus → native decode → existing output adapter with fake DeckLink SDK** tests. Add a validated native capability fixture/envelope and explicit profile4 range-extension fmtp only after this route is usable. Existing probes/offers must remain fail-closed; do not infer level123 from the lossless level255 sample.
6. Add sustained real-time/HD, actual stream SPS/crop/profile transitions, hostile-input and callback-concurrency coverage. Ordinary8bit/disabled preview must be tested independently of card precision; existing preview canvas transaction has not been removed here.

## Physical acceptance still pending authorization

No capture/output device or DeckLink driver/module was opened in tests. Full app compilation merely embeds the modules.

- Actual card/connector/mode admission with v210 and no conversion.
- Independently captured SDI active Y/Cb/Cr equality for ramps, impulses, adjacent-row chroma and excursions; correct709/limited/siting, no HDR metadata.
- Exact rational cadence, common flash/click A/V epoch and sustained drift/late/drop/underrun behavior.
- Stop/restart, loss/reconnect, source/mode changes, failed preroll/partial audio writes, hot unplug and callback drain.
- Capture↔output exclusivity, native Start/Stop/Auto Start, and identical card bytes/timing with ordinary preview on/off/fullscreen.

These are not the only remaining gates: substantial **software integration** above is unfinished even before physical testing.
