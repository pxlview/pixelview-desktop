# Native limited-range HEVC Main42210 reception → DeckLink

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


## Latest independent-review correction checkpoint (2026-09-10)

**Final-tree strict HD matrix remains RED; do not use the earlier short pass below as current acceptance.** Actual-TU deadline and real-header PTL/caps regressions now pass; scanner-executable/PID isolation and exclusive evidence creation prevent inherited reference loading/clobber without filtering duplicate records. Focused sanitizers and full build/deep signature pass. The final fresh-registry run failed on24000/1001 after one native frame701ms late and finite-rate refusal; it was not retried to green. Its exact predicate/root cause, old24 empty startup, and old2997 decoder-construction failure remain unresolved (future construction failures now capture factory/topology/caps while retaining the assertion).

Read [independent-review corrections](pixelview-422-independent-review-corrections.md), the newest implementation-status section, and `finite-rate/review-summary.json` before continuing. Existing multi-GB references were reused read-only with fresh independent byte verification; original evidence is intact. Parent independent review and stable HD software acceptance remain required; Main422 is still unadvertised. No user app/card/sender/engine/config/auth change or commit/push.

## Current resumed checkpoint (2026-09-10)

The strict **short** actual Apple-VT HD four-rate WHEP/Opus→native→whole-owner/fake-SDK matrix now passes without trace/fault flags:467 native full-byte independent-reference comparisons,934 observed locks,360 scheduled-v210 comparisons,44 distinct images in each45-slot generation. A bounded nonshipping observer replaced streaming RAW file I/O and a slow mmap reference with deferred evidence plus owned losslessly compressed references; hashes are only lookup keys, never substitutes for full memcmp. Production source hashes are unchanged.

Read the newest status section and `plugins/pixelview-whep/.test-build/finite-rate/matrix-rca/resume-final-summary.json`. All prior failures/RAW bytes remain. Actual unsupported, caps-conflict, wire-rate-change and induced650ms downstream-stall negatives pass. The latter demonstrates24fps refusal through the existing500ms gap policy, but the old24 failure lacks complete contemporaneous evidence. A separate preserved early encoded-preview decoder-construction assertion is still unexplained. **Shipping remains closed** pending independent review, sustained/stress/network recovery and capability/PTL acceptance. No user app/card/sender/engine/config/auth change or commit/push.

## Earlier approved finite-rate checkpoint (historical short-matrix failure)

The approved specification below **supersedes the old arbitrary-rate blocker and no-whitelist warning**. Production RTP-based finite-rate admission is implemented for the explicit four-rate contract, with short actual Apple-VT HD WHEP/Opus→native→real-owner/fake-SDK reference success at every approved rate. **Shipping remains closed:** individual passes do not make a passing sustained matrix; an intermittent native stall/repeat/owner refusal still fails the strict multi-rate run. Do not attribute that new software acceptance failure to the superseded arbitrary-rational requirement.

Read the leading section of `pixelview-422-implementation-status.md` and `pixelview-422-finite-rate-policy.md` for exact code, positive/negative evidence, preserved failures, final full build/signature and remaining gates. No sender/engine/header rewrite, private API, physical device or user configuration change. Main422 remains unadvertised pending stable software acceptance, complete capability/PTL coverage and independent review.

## Historical arbitrary-rate checkpoint (2026-09-09; scope superseded)

Read the newest section of `pixelview-422-implementation-status.md` before implementation. Fresh actual OBS/Apple VT HD Main422 captures at30/1,30000/1001 and29999/1000 all declare profile4/main-tier/level120 and limited709 ten-bit422, but no VPS/VUI timing. Actual linked WHIP conversion rounds30 and29999/1000 alike to3000 RTP ticks per frame; thus timestamps cannot establish an unambiguous exact rational from an unspecified source-rate set. Six executable counterexample checks include the exact30fps ceiling and2997 alias. Actual native-payload loopback WHEP/Opus reaches the production parser with PTS but no framerate/duration, then zero native video and the existing owner refuses Start. This is **negative HD end-to-end evidence, not working compatibility**.

No production admission, sender, transport or capability change was made. The trustworthy rational is attached`video_output_info.fps_num/fps_den` → VT`CMTime` before sender quantization. The first standard metadata seam to evaluate, with explicit sender-scope approval, is`mac-videotoolbox/encoder.c:handle_keyframe`, where native VPS/SPS/PPS become keyframe/extra_data bytes. No verified public VT HEVC timing-insertion property was found; changing integer`ExpectedFrameRate` alone does not establish emitted exact timing. Do not infer a finite source-rate whitelist, inject guessed caps or silently add a protocol/bitstream rewrite. Positive VT HD native/owner fidelity, sustained run, malformed/conflicting cadence policy, complete PTL/level/cadence gate and independent review remain pending; Main422 stays unadvertised.

Evidence and reproduction: `plugins/pixelview-whep/.test-build/obs-vt-cadence/`, `tests/run-obs-vt-cadence.py`, `tests/test-obs-vt-cadence.py`, and `plugins/decklink/tests/run-whep-cadence-audit.py`. Full commands, exact counts, provenance, supported-API reasoning and limits are in the status. Earlier implementation and user configuration are preserved.

## Approved initial format specification

The user approved **1920×1080 progressive only** at exactly **24000/1001 (23.976), 24/1, 25/1, and 30000/1001 (29.97)** for the initial native422 implementation. This is an explicit finite supported-rate contract, not arbitrary rational-rate recovery. Exclude 30p, 50p/59.94p, interlaced and PsF from this initial scope; the earlier 1080i50 discussion did not expand this approved specification.

Continue implementation and testing against this set without changing stock OBS or rewriting HEVC headers. Validate observed media timing against the finite contract and exact selected DeckLink mode; document admission tolerances, observation period, ambiguity, missing/conflicting metadata, loss and discontinuity behavior. Do not claim RTP quantization can distinguish every unsupported near-rate alias. Keep exact rational arithmetic internally. Enable shipping advertisement only after appropriate production capability and software acceptance, with physical acceptance limitations explicit.

## Product contract — read first

**Implementation preference:** stay as close as possible to supported native OBS, GStreamer, VideoToolbox and DeckLink APIs and existing ownership/lifecycle. Prefer minimal, justified adapters over custom replacements or fragile hooks. Do not guess cadence, silently relabel formats, or ship test-only overrides. Where a native boundary cannot meet the requirement, document the limitation and rationale before adding a workaround.

Implement faithful ten-bit 4:2:2 DeckLink output from WHEP HEVC Main42210. Mac preview/fullscreen need NOT be ten-bit. Receive is limited-range only, initially BT709 SDR. Validate stream metadata; reject unsupported full range, HDR or unresolved color metadata explicitly, never relabel values as limited. Preserve existing Main/Main10/H264/VP9/Opus reception and credentials/lifecycle behavior.

### Full versus limited: signal-flow matrix

| Stage | Range behavior / responsibility |
|---|---|
| Editing software SDI/video output | May be full or limited; actual output can differ from project settings |
| DeckLink capture source in OBS | Match actual incoming range; controls input interpretation, NOT output policy |
| OBS global video output | Default Partial/Limited; independently controls output conversion |
| HEVC Main + NV12 | Normally limited, eight-bit420; configured Full is supported by Apple VT |
| HEVC Main10 + P010 | Normally limited, ten-bit420; configured Full is supported by Apple VT |
| HEVC Main42210 + P216 | Ten-bit422 limited works; Apple VT rejects Full |
| Pixelview sender product policy | Keep encoded output limited; capture range remains selectable |
| Engine passthrough | Does not normalize range; WHIP already reads SPS for format identity |
| Browser viewing | Limited is appropriate; correctly interpreted black remains black; no player-side manual expansion |
| Desktop receiver | Validate limited/BT709 contract, preserve native422 samples |
| DeckLink output | Ten-bit422 v210, no RGB round trip or second range conversion |
| Mac preview | Separate optional conversion, not the grading-output precision boundary |

Limited is not inherently SDR, a smaller gamut, or grey black. Full→limited conversion is not mathematically lossless. Browser appearance is not calibrated sample-fidelity proof; no comparative Safari/Chrome range experiment was performed. The measured encoder audit covers this Mac's Apple VT, not all platforms/encoders.

**Implementation versus intent:** checkpoint sender defaults Partial but preserves saved Full for Main/Main10. Do not claim the limited sender policy is already enforced. Receiver validation remains necessary. Do not silently rewrite existing sender configuration in this receiver implementation.

## Read these documents in order

1. This handoff and repository `AGENTS.md`.
2. [OBS and Pixelview color-range investigation](pixelview-obs-color-range.md) — tracked customer explanation, full-flow matrix, real CPU/GPU encoder results, source citations and reproduction. Original local evidence: `/Users/max/src/obs-vt-range-audit/README.md`.
3. `/Users/max/src/native-vt422-decklink-spike/README.md` — proven native hardware decode surfaces and exact sample tests.
4. `/Users/max/src/decklink-422-audit/README.md` — output ownership, packing, scheduling and v210 allocation hazard.
5. `docs/pixelview-receive-precision-audit.md` — existing rendered path boundaries; not the desired direct422 architecture.
6. `plugins/pixelview-whep/tests/integration-status.md`, `production-integration.md`, `capability-probe.md`, `video-precision.md`, `whep-loopback.md` in that tests directory — current implementation and limitations.
7. `/Users/max/src/main42210-fidelity/README.md` — rejected AYUV conversion path; do not repeat as a faithful solution.

Absolute external audit paths are local evidence, not portable repository assets. Bring necessary sanitized fixtures/tests into the repository for reproducible implementation; do not copy runtime binaries or secrets.

## Starting state and boundaries

Repository `/Users/max/src/pixelview-desktop`, branch `pixelview/minimal-capture`, pushed checkpoint `39bf2c5e08a09ec0f1bbbb7babbfeba46161d50c`. Inspect live git state before edits; preserve unrelated changes and this handoff. Engine compatibility PR: https://github.com/pxlview/pv-engine/pull/51 targeting dev; do not edit engine or assume merged. Original engine main was reset to remote intentionally.

Preserve Pixelview spelling, `com.pixelview.desktop`, settings and Keychain identities. Do not unlock Keychain, touch credentials/user profiles, open physical capture/output devices, restart user apps/servers, deploy, publish, commit or push without further authorization. Synthetic native hardware-codec tests and local builds are allowed; use private HOME/CFFIXED_USER_HOME and loopback-only transports without public STUN. Keep runtime staging isolated from active applications.

Load skills obs-gstreamer-whep, obs-local-macos-build, videotoolbox-422-fidelity, obs-capture-preview, test-driven-development and requesting-code-review before implementation. Inspect code before choosing the integration seam.

## Established evidence and rejected routes

- Native VT: limited→x422 and limited→v210 preserve every tested Y/Cb/Cr code; full→xf22 also exact, but full is out of scope. Twelve hardware sessions / 36 frames passed isolated checks. This is not physical DeckLink proof.
- Full→v210 is NOT exact. Wrong-range native format requests change values.
- Pinned GStreamer1.28.3 VT caps omit x422/xf22/v210. Main422→P010 loses vertical chroma; AYUV64 conversion alters horizontal chroma and mishandles full range. Neither is the grading path.
- Prefer public x422/P210 native surfaces plus exact byte packing, or proven limited native v210; avoid undocumented p422/pf22 contracts.
- libobs V210 row allocation uses invalid power-of-two alignment arithmetic for alignment48. Private I210 queue avoids that path. v210 stride must use arithmetic ceiling to 48 pixels, 128 bytes per block; validate multiple widths/partial blocks.
- Existing DeckLink SDR output is rendered BGRA; HDR RGB10/PQ shader is not an SDR422 solution.
- Existing video callback-counter scheduling and synchronous audio writes do not prove timestamped receive A/V sync.
- Pinned VT capability probe can lose the final queued frame at EOS; repeated baseline 96/100 passed. A cooperative-pause patch passed startup100/100 but deadlocked reentrant renegotiation; it was REJECTED. Do not promote it, retry around failures, lower acceptance or hardcode capabilities.

## Implementation job

1. Add a faithful native Main42210 decode route integrated with the real WHEP receiver. Reuse transport, jitter and authorization; inspect compressed-output/dynamic-pad options rather than building a second signaling client blindly.
2. Route source-bound native ten-bit422 frames directly to the existing DeckLink owner, outside RGB canvas. Keep ordinary preview independent and preserve Start/Stop/Auto Start behavior and capture/output mutual exclusion.
3. Establish explicit frame ownership, bounded queues/backpressure, timestamp epoch/preroll, rational cadence and one synchronized card audio/video scheduler. Handle stop, reconnect, source/mode changes, late callbacks, device removal and errors safely.
4. Gate Main422 advertisement on actual validated production native decoding and usable route. Use explicit HEVC profile4 with appropriate range-extension constraints; profile3 is Main Still Picture. Preserve truthful level/geometry/fps limits, profile isolation and Opus. No shipping fidelity claims before actual integration tests.
5. Add deterministic and real native tests, build the full app, verify deep/strict signature, and document precisely what is complete versus hardware-gated. Do not stop at a standalone decoder stub or packing demo.

## Acceptance and honest limitations

- Lossless independent HEVC references must reproduce raw ten-bit ramps/chroma impulses/adjacent-row detail before comparison. Assert exact Y/Cb/Cr codes at native decode AND final v210 output boundaries, not PSNR/caps/depth alone.
- Cover valid limited709, full/HDR/unsupported metadata rejection, malformed stream and resolution/profile changes. Use codec-specified metadata defaults only when standards justify them, not guesses.
- Exercise actual production WHEP offer/answer, RTP→decoder→output adapter path with a fake DeckLink sink; verify real Opus audio/timestamps, restart/stop/reconnect, bounded resource lifetime and existing codec regressions.
- Test multiple widths, padded/nontrivial strides, rational frame rates, buffer reuse and negative420/range controls; run packing sanitizers.
- Run full native build and appropriate regression suites; report existing unrelated Keychain/GUI/probe failures separately without calling the full suite green.
- Physical card/mode admission, captured SDI sample equality, sustained cadence/A/V drift and device interaction remain pending until separately authorized hardware testing. Keep unverified Main422 capability gated if necessary rather than exposing an unsafe route.

Deliver a reviewed working implementation with exact files, commands/results, known blockers and a hardware acceptance checklist. Update this handoff and associated tests/docs as implementation progresses.
