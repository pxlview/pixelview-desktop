# Initial native422 finite-rate policy — shipping gate CLOSED

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


## Latest correction / acceptance status

The acquisition deadline is now checked before initial activation regardless of AU flags: exactly3s is accepted,3s+1ns and the reviewer's4s non-delta case reject. Strict hvcC/VPS/SPS profile constraints and subset-summary consistency plus compressed caps contradictions are tested on real Apple headers; see [the exact declarations and tests](pixelview-422-independent-review-corrections.md). Summary-mask omission is not incorrectly treated as byte inequality. The finite source-rate set, packet-gap/PTS/owner thresholds and original timestamps remain unchanged.

**The final-source fresh-registry matrix is RED**, superseding the older pass section below: one24000/1001 native frame arrived701ms late, then finite-rate refusal. No retry-to-green; the exact RTP/deadline cause lacks contemporaneous evidence. Focused sanitizers/full build/signature pass but do not establish stable HD acceptance. Main422 remains unadvertised.

The approved publisher contract is displayed **1920×1080 progressive, limited
BT709 ten-bit422**, exactly **24000/1001,24/1,25/1,30000/1001**. It excludes30p,
50/60p, interlace and PsF. This explicitly supersedes the earlier unspecified
arbitrary-rational requirement; sender/engine changes are not needed to explore
this contract. Unsupported near-rate aliases remain indistinguishable on the wire.

## Implemented receiver boundary

- `request_encoded_filter` always requests the finite RTP policy for production
  filters. rswebrtc0.15.2 emits that signal **before** creating parsebin/depay;
  observe the later `deep-element-added` signal and the public `rtph265depay`
  sink with `GstRTPBuffer`, after jitter handling. An initial recursive-bin scan
  was demonstrated too early and replaced. No upstream/private interface changed.
- Observer state has its own references and mutex; its signal and pad lifetimes
  contain no borrowed source/filter pointer. The serialized native chain reads
  a snapshot. Ordinary codecs ignore this native-only state and pass unchanged.
- Require32 consecutive marker intervals. Tick classes are3753/3754→24000/1001,
  3750→24,3600→25,3003→30000/1001. Actual OBS WHIP's adjacent-delta rounding emits
  **3754 on every23.976 interval**, not the ideal3753.75. This deliberately
  recognizes a declared finite publisher contract, not arbitrary rational truth.
  Arrival timing and skew-corrected Gst PTS do not select the rate.
- Acquisition and waiting for the next non-delta access unit are bounded to3s
  from the first observed packet, evaluated on packet/AU entry; silence still
  relies on the existing source/owner freshness watchdogs rather than a new timer.
  Nothing compressed is retained during this
  wait; decoding starts at the next parser-marked random-access frame. No
  already-started decoder loses reference AUs to a leaky compressed queue.
- SSRC change, noncontiguous16-bit sequence, later DISCONT, an unsupported or
  changing marker interval, backward arrival clock, >500ms packet gap, or
  >16384 packets without a marker fail closed. RTP sequence/timestamp wrap is
  modular. There is **no packet-loss repair/classifier outlier forgiveness** in
  this policy; upstream recovered packets must already be contiguous. Jitter
  that preserves ordered RTP within these bounds does not alter classification.
  If depay withholds output after loss, source/owner freshness guards still
  fail; a probe failure is consumed when the next native AU reaches the tap.
- Missing parser framerate/duration is legitimate. After observation, an owned
  caps copy receives the contract's exact rational; duration is normalized to its
  integer-nanosecond floor. Existing duration must agree within
  one nanosecond. **Original PTS and Gst segment clock mapping are preserved**,
  not rewritten to hide jitter or processing lag. PTS must advance and cannot
  jump forward by more than100ms between delivered AUs. Present caps rate must
  exactly equal the classified rational. Source-feed rational reaches the
  existing owner's exact selected-mode comparison; no nearest-mode selection.
- Native strict configuration checks displayed HD geometry, no-reorder SDR
  Main42210 constraints, main-tier level120 in hvcC/VPS/SPS, single VPS layer and
  temporal sublayer. Apple legitimately codes1920×1088 with bottom conformance
  cropping to1080; that is accepted, while CM/CV/display geometry must be1080.
  Present VPS/VUI time-scale/unit fields must agree with the rate, including
  proportional-POC tick factors;128-bit arithmetic avoids hostile-field overflow.
  Absent timing is not an error under this finite contract. This consistency
  check is not a general proof of fixed-picture cadence from VUI tick fields.
- Byte-identical repeated parameter sets remain required. Standard Apple CBR
  filler NAL38 is now accepted **only** as `ff* 80` after its two-byte header;
  malformed filler and unknown non-picture NAL/SEI semantics remain rejected.
- The generic native decoder's preexisting isolated fidelity API still supports
  the small level255 reference fixtures. Only the production filter applies the
  strict initial-format setter. Elementary-file tests explicitly bypass RTP
  observation and are not admission evidence. Main422 WHEP offer injection is
  still test-only; production capability/offer mask does not advertise Main422.

## Latest evidence: short matrix passes; shipping remains closed

The resumed bounded test observer now proves every observed native frame with full-byte independent-reference comparison, using hashes only for indexing. Final actual-VT four-rate/two-generation WHEP/Opus/real-owner matrix passes467 native comparisons and360 scheduled-v210 comparisons, with44 distinct images per45 slots. Production policy and timing/expiry thresholds are unchanged. See the leading implementation-status section and `finite-rate/matrix-rca/resume-final-summary.json` for exact logs, failed alternatives and limits.

Actual30/29999 negatives, an injected caps25-versus-RTP24 conflict and explicit negative wire3750→3600 change pass. A separately injected real650ms downstream block at24fps causes a666ms observed RTP gap and correct sticky refusal despite continuous wire3750 ticks: downstream instrumentation can exercise the existing500ms rule. This reproduces a mechanism, not uniquely the insufficiently traced historical24 failure. Sustained acceptance, real-network loss/jitter/recovery, native capability/PTL coverage, unexplained earlier preview-decoder construction and independent review remain gates; Main422 is still not advertised.

## Historical acceptance failure (preserved; short matrix superseded above)

See `pixelview-422-implementation-status.md` for exact executed results. Four
actual Apple-VT HD streams have individually passed real WHEP/Opus→native→real
owner/fake-SDK reference checks and two-generation reconnect. These are short
compatibility/fidelity runs, **not stable cadence acceptance**: the unchanged
strict matrix still fails intermittently with repeated/stalled native video,
owner refusal and a finite-rate rejection. Instrumented HD processing can also
fall behind. No timeouts, image-distinctness threshold, source timestamps, owner
expiry rule or rate aliases were relaxed to call the matrix green.

Shipping stays closed pending diagnosis and independently reviewed software
acceptance. Remaining work includes stable bounded sustained HD A/V across all
four rates, actual-network conflicting/changing-rate/loss/jitter cases, complete
PTL/constraint/caps consistency and native capability coverage. A short native
hardware session is not sustained throughput or every-level-bitstream support.
Physical card/driver mode support, genlock/SDI sample equality, A/V drift and
hardware teardown remain separately unauthorized and unverified.
