# Independent-review corrections — final HD matrix remains RED

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


Checkpoint 2026-09-10, uncommitted on `d043346e4fa4f9d9f5eac2d957b52a17eb9145f7`.
Main422 remains **unadvertised**. This corrects the three reported defects; it does
**not** establish stable final-tree software acceptance. Parent independent review
is still required. All preceding evidence and reviewer scratch are preserved.

Evidence prefix **E**: `plugins/pixelview-whep/.test-build/finite-rate/`.
Machine-readable results, final source hashes and earlier/final matrix separation:
`E/review-summary.json`.

## 1. Acquisition deadline bypass

`native-422-filter.c` previously tested the 3-second deadline only inside the
pending-rate/delta-AU branch. An already classified rate plus the first non-delta
AU at 4 seconds therefore selected strict decoder rate, emitted RAW caps and
activated the native route before failing later on the intentionally invalid AU.

`tests/native-422-deadline.m` includes the actual filter and native decoder TUs,
substituting only the monotonic clock at compilation. Its first case reproduces
that reviewer failure: `active=1 expected=0`, preserved in
`review-deadline-red.log`. It also tests 3 seconds minus 1ns, exactly 3 seconds,
and plus 1ns across acquired/pending and delta/non-delta combinations. The
assertions inspect activation **and the decoder rate**, not just the eventual
flow error from invalid compressed bytes. No VT session is required for this seam.

The production chain now snapshots time once and checks acquisition expiry
**before every initial activation**, independently of AU flags. Exactly 3 seconds
is permitted; later entry is refused. Backward clocks are explicit refusals.
Already-active streams retain their separate packet/PTS/freshness checks. Silence
still depends on existing watchdogs; this is not an asynchronous deadline timer.
Final sanitized policy/deadline result: `review-policy-final2.log`, exit0.

## 2. HEVC declarations and compressed caps

The reviewer repro was ported to `tests/native-422-headers.m` and
`run-native-422-headers.py`. It uses the actual captured Apple-VT `24-1.hevc`,
public h265parse hvcC, and the **actual production configure function**. Initial
`review-caps-red.log` and `review-hvcc-red.log` both show `accepted=1 hardware=1`
for contradictory high-tier/level5 caps and zeroed hvcC constraint bytes6–11.
`review-vps-red.log` independently shows hardware acceptance after clearing the
VPS max12 constraint. Subsequent REDs cover color, compressed depth, reserved
array bits, parameter-set temporal ID, and an unavailable VPS base layer.

Strict initial admission now validates:

- Profile space0, profile4, main tier, declared level120 in hvcC and VPS/SPS.
- Main 4:2:2 10 range-extension constraints: max12/max10/max422 and lower-bit-rate
  set; max8/max420/monochrome/Intra/one-picture-only clear. Reserved PTL bits are
  zero; the defined `general_inbld_flag` is not incorrectly treated as reserved.
  Parameter-set progressive-source/no-interlace checks remain explicit.
- hvcC compatibility/constraint bits are **subset assertions** against each
  VPS/SPS, not blanket byte equality. The profile-defining constraints must be
  present independently. Optional summary non-packed/compatibility bits may be
  omitted; positive tests prove these legal omissions remain accepted.
- hvcC reserved fields, chroma2/depth10 declarations, unsupported temporal-layer
  counts, reserved constant-rate value, exactly three parameter arrays, array
  reserved bit, parameter-set temporal ID1 and internal/available VPS base layer.
  Existing public-parser syntax, single-layer/sublayer, SPS no-reorder,
  crop/display geometry, timing, color and unchanged parameter-set checks remain.
- Present compressed caps profile/tier/level/chroma/interlace/color/depth cannot
  contradict the contract. Numeric colorimetry is parsed semantically by public
  GStreamer APIs. h265parse emits **uint** bit depths; both numeric int/uint10
  are accepted, while string/wrong-depth declarations reject. Missing optional
  declarations remain established by the actual parameter sets.
- Nonzero hvcC average rate is only a consistency summary in fps*256 units, with
  at most one representation unit of rounding. Zero means unspecified. It never
  selects the finite rate or rewrites PTS; the exact RTP-selected rational and
  existing caps/VPS/VUI rate checks remain authoritative.

The bounded general PTL-prefix reader uses public parsed NAL data, removes
emulation-prevention bytes, and reads no more than16 RBSP bytes. Full VPS/SPS
parsing remains the bundled public GStreamer parser. It exists because the
public parsed PTL struct does not expose all reserved/inbld bits; no private
GStreamer reader or sender header rewrite was added.

Standards basis: H.265 7.3.3 and Annex A profile constraints, and ISO/IEC14496-15
8.3.3.1 configuration-summary semantics. The subset rule is also quoted and
implemented by FFmpeg's `hvcc_update_ptl`:
https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/hevc.c
General bitstream conformance, sustained bitrate/HRD performance and native
capability admission are **not** certified by these declaration checks.

Final `review-headers-final2.log`: **77 cases (71 negative,6 positive)**, ASan+UBSan, exit0; real positive
sessions still read back hardware=true. Mutation tests reconstruct EBSP escaping
and NAL lengths when changing actual VPS/SPS PTL bytes. The generic level255
isolated fidelity API is not converted into strict shipping admission.

Preserved test-development failures: an intermediate int-only caps check rejected
valid uint10 parser caps (`review-depth-type-diagnostic.log`); it was corrected,
not worked around by changing fixture caps. `review-average-rate-red.log` ran
while that mistake existed and is **not a valid RED for average rate**. The true
average-rate RED is `review-rate-boundary-actual-red.log` (`accepted=1 hardware=1`).

## 3. Nonshipping observer identity and evidence ownership

The injected dylib previously initialized in gst-plugin-scanner too, loading the
reference and writing a zero-lock summary to the same output on teardown. This
could add contradictory evidence and truncate the intended receiver's file.

Before loading any reference, the constructor now compares the designated
receiver executable's device/inode with `_NSGetExecutablePath`, then binds its
observer state to that process PID. Other executable images and fork children do
not observe locks, load references or publish records. Every observer runner now
passes the intended executable explicitly. Legacy RAW mode remains exercised.
Hash evidence uses exclusive, no-follow, mode0600 creation at teardown; duplicate
owners/preexisting files fail instead of truncating evidence. The runner's strict
single-summary/count reconciliation is unchanged—no duplicate records are filtered.

`test-native-422-observer.py` preserves actual scanner RED (`fd>=0` loading a
missing reference) and evidence-clobber RED. Final tests use the real scanner,
an absent fresh registry, inherited injected dylib and deliberately nonexistent
reference; require factory inspection/registry success without any reference or
observer record; and retain exact-pair, wrong-reference, forced-hash-collision,
unmatched-pair, record-capacity and deferred-I/O negatives. The observer's actual
TU runs under ASan+UBSan. The separate injected scanner dylib uses normal O2.

An initial sanitizer attempt used the machine's default **Clang15 Command Line
Tools** and failed during ASan initialization. Explicit per-process Xcode Clang21
selection fixes that harness toolchain issue. Both failed logs remain. No ASan
check was suppressed and no observer/interpose branch was removed. Final log:
`review-observer-final3.log`, exit0.

## 4. Decoder-construction evidence, not a historical RCA

`plugins/decklink/tests/whep-feed.c` retains the fatal Decoder/Video assertion.
Immediately before it, it now emits factory/class/ancestor path, factory pad
caps, and the graph topology/current caps. It avoids querying negotiation or
printing element properties/endpoints. `test-decoder-construction.py` injects
an actual `vtdec_hw` construction through the same whole-source callback;
`review-construction-red.log` lacks diagnostics, while
`review-construction-final3.log` verifies diagnostics **and the original failure**.

This does **not** identify the factory or root cause of the older 30000/1001
construction failure. That original log and the old 24fps empty-startup log lack
necessary contemporaneous evidence and remain unresolved.

## Final execution and remaining blocker

Final focused runs exited0:

```sh
python3 plugins/pixelview-whep/tests/run-native-422-policy.py
python3 plugins/pixelview-whep/tests/run-native-422-headers.py
python3 plugins/pixelview-whep/tests/test-native-422-observer.py
PV_NATIVE422_SANITIZE=1 python3 plugins/pixelview-whep/tests/run-native-422.py
PV_DECKLINK_SANITIZE=1 python3 plugins/decklink/tests/run-receive.py
python3 plugins/decklink/tests/test-decoder-construction.py
```

Logs are `review-{policy,headers,native}-final2.log`, `review-observer-final3.log`,
`review-owner-executed-final.log`, and `review-construction-final3.log`.
The separate WHEP_BUILD sanitizer runs compile the WHEP binary and execute
private-media/UI tests; **they do not execute the whole owner receive fixture**.
The explicitly listed no-WHEP owner command above does, and passed. Linked
frameworks/drivers are not sanitizer-instrumented.

Matrix command (all strict identity/timing/health/PCM thresholds retained):

```sh
PV_DECKLINK_OPTIMIZE=1 PV_DECKLINK_WHEP_BUILD=1 PV_WHEP_APPROVED=1 \
  python3 plugins/decklink/tests/run-receive.py
PV_APPROVED_EVIDENCE=/absolute/path/to/a/NEW/evidence-directory \
PV_APPROVED_REUSE_REFERENCES=1 \
  python3 plugins/decklink/tests/run-whep-approved.py
```

Each rate gets a fresh receiver registry and real scanner. Existing references
are reused **read-only** and compared in bounded chunks against fresh independent
FFmpeg decoding before use. Native reference storage remains owned/bounded LZ4
in RAM. No multi-GB duplicate references were generated or old evidence deleted;
free disk was checked before work/build (initially4.2GiB). Non-reuse fixture
generation now refuses insufficient disk space.

- **Earlier, not-final revision:** `review-fresh-matrix/` passed all four rates:
  118/119/117/120 native pairs, **474 pairs/948 locks/360 owner comparisons**.
  This preceded the final array-reserved/parameter-temporal/VPS-base guards.
- **Final source:** `review-fresh-matrix-final2/` and its `.log` are **RED** on the
  first24000/1001 generation. Actual WHEP delivered one native frame with
  `ARRIVAL V age_ns=701306779`, then `Native 422 finite-rate contract failed`;
  transport reports `native=1 opus=178560 error=1`, failing the unchanged
  `video>=10 && !error` assertion. The final matrix completed zero rates and
  was **not retried to green**. Abort prevents a completed observer summary, so
  no final native full-byte comparison count is claimed. Only one reference
  preload is logged; this is not the previous scanner duplicate-summary failure.

The frame age is evidence of late first native delivery, **not a contemporaneous
RTP snapshot or proof of which deadline predicate fired**. No stack proves its
cause. Cold decoder/configuration cost, packet delay or downstream backpressure
must be distinguished with a separately labelled diagnostic run. Do not blame
new header checks, declare the observer repaired all startup failures, or loosen
the500ms/3s policy based on this log. Stable final-tree HD acceptance remains a
software blocker; earlier successful matrices must not replace this failure.

Full final production build exited0 (`review-full-build.log`, BUILD SUCCEEDED)
using private HOME/CFFIXED_USER_HOME and explicit Xcode. Deep/strict codesign
exited0 (`review-signature.log`); bundle ID `com.pixelview.desktop`, artifact
`build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app`. No app
launch/install, card/SDK dispatch, sender/engine/config/auth change, commit,
push or deployment. Capability/offer sources remain untouched. Shipping,
sustained/network recovery, independent review and physical acceptance stay closed.
