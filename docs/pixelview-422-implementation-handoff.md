# Native limited-range HEVC Main42210 reception → DeckLink

## Product contract — read first

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
