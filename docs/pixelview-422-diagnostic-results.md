# Native422 diagnostic acceptance results — historical cause unresolved

The predeclared small campaign completed once: four fresh-process/fresh-registry
baseline cases, then four with one additional bounded independent-reference
LZ4 decode/memcmp per native pair. **All eight short cases passed; this does not
fix the historical701ms failure or approve stable native422 admission.**
Main422 stays unadvertised. Prior deadline/PTL/caps/observer corrections were
independently approved; these new diagnostics/tests still need independent review.

## Production reporting

`native-422-diagnostic.h` defines27 canonical native422 reasons and32 uint64
fields. The filter posts typed Gst error details through the supported API.
The production worker consumes only that allowlist, never Gst error/debug text,
unknown properties, URLs, payloads or raw SSRC. It retains one fixed snapshot
(under512 bytes), emits one safe terminal OBS warning, and exposes bounded
`get_status.native422_diagnostic` text (tested below2048 bytes at uint64 maxima).
The snapshot survives ordinary error teardown; connect/disconnect/destruction
clear it. Quit, changed intent and old active-generation messages cannot refill
it. First known failure wins. Unknown reasons or incorrectly typed known numeric
fields reject the complete diagnostic; unknown properties are ignored.

The preceding RCA's terminal Gst error alone was NOT production reporting:
the application discarded it, while the test bus printed it. This gap is now
closed without per-frame production logs, disk I/O, new workers, queue changes,
clock rewriting, grace, retries or admission changes.

## Regression evidence

Evidence **D**: `plugins/pixelview-whep/.test-build/diagnostic-acceptance/`.
`red.log` is compile-time RED for the missing production retention seam, not a
reproduction of the historical media failure. `diagnostic-final.log` is actual
receiver-TU ASan+UBSan GREEN: hostile raw error/debug/unknown-property/reason
strings, wrong numeric type, sticky snapshot, all32 numeric fields, bounded
max-value formatting and connect/disconnect/late-generation reset.
`policy-final.log` additionally checks the actual filter's emitted typed details.

Added packet policy regressions:3s-1ns/exact/+1ns acquisition,16384/16385 packets
without markers,3753-only and alternating3753/3754, sticky first failure time and
delta. Existing500ms boundaries remain. Active-stale fixtures explicitly seed
`rate_active` and `decoded_count=1`; they do NOT previously decode a frame.
New actual-AU PTS tests use real captured Apple24fps HD HEVC with production
native decode:100ms-1ns/exact/+1ns, missing/equal/backward PTS, duration floor
+/-1ns accepted and +/-2ns refused. Successful native callback preserves original
PTS and normalized duration. This is controlled AU timing, not wire-rate discovery.
`headers.log` is exactly **77 cases =71 negative +6 positive**, including real
hardware readback on positive cases; not77 negatives.

Final source native/source/preview and whole-owner ASan+UBSan passed
(`native-final.log`, `owner-final.log`); observer regressions passed
(`observer-final.log`). System frameworks/drivers are not sanitizer-instrumented.
Full isolated app build `cmake --build build_macos_native422 --config
RelWithDebInfo -j 2` and deep/strict signature passed (`build-final.log`,
`signature-final.log`). No app install/launch or release claim.

## Exact campaign result

Declaration: [bounded campaign](pixelview-422-diagnostic-campaign.md), whose
SHA256 was retained BEFORE execution in `D/campaign.json`. Every case has a
fresh registry and two generations on the same source/real owner/refusing SDK.
No media retry, trace-log flood, external load process or user-app manipulation.

| Rate | Baseline native exact pairs | Added-reference-load pairs | Distinct per45 slots, every generation |
|---|---:|---:|---|
|24000/1001|118|116|44|
|24/1|118|116|44|
|25/1|119|117|44|
|30000/1001|118|122|44|

Programmatic aggregate: **944 native exact byte comparisons,720 owner full-buffer
comparisons**. Paired native locks/independent memcmp/contiguous identities,
45 slots and>=35 distinct, exact rational mode/deadlines, source-only real PCM,
healthy/expiry and reconnect checks all unchanged. Each case used856–931 total
in-memory timing/reference records, below the combined4096 cap. Existing RAW
reference files were reverified READ-ONLY and native references owned as bounded
lossless compressed memory; no multi-GB duplicate reference artifacts were made.
Disk remained about8GiB free, above the predeclared4GiB prerequisite.

## What timings establish — and what they do not

`D/analysis.json` derives every statistic and retains largest-gap same-packet
correlations. Supported public `rtpjitterbuffer.sink` and `rtph265depay.sink`
probes observed pre-jitter and ordered video markers; every ordered marker has
its matching ingress generation/sequence/timestamp. These are marker timings,
NOT a full packet-loss trace or a physical network receive clock.

- Baseline largest ordered marker gaps168.456–208.557ms occurred while the
  corresponding next marker had already arrived before jitter. The overlapping
  native decode spans134.054–166.372ms include first-session creation
  106.649–135.234ms. The paired incoming marker's delay to ordered observation
  was133.982–166.085ms. This separates **observed local serialized decode blocking**
  from ingress at these short-run events; none crossed the500ms policy bound.
- Largest controlled-load ordered gap240.407ms at30000/1001: the next marker was
  already at pre-jitter ingress205.725ms earlier. The overlapping native decode
  took205.675ms, including178.154ms session creation. This is another measured
  local delay, not a reproduction of701ms age or a proven historical root cause.
- Reference comparison median costs changed from1.592–1.7255ms baseline to
  3.066–3.182ms with exactly one added decode/memcmp. Extraction, pair/hash and
  reference costs are retained separately from native/VT/pack/preview spans.
  Pack/preview include observer work; they are wall intervals, not CPU/driver
  attribution. Positive delivery measurement isolates source-feed copy cost
  (maximum1.180ms), **not full callback lock waiting**; full previous delivery
  duration remains in the production terminal diagnostic if a refusal occurs.
- Owner reference loading took293.080–816.527ms, but finished before first native
  decode in every case (paired-clock intervals in JSON). It did not overlap
  those measured first native decode spans. This does not exclude other memory
  pressure or prove the old failure's owner-reference behavior.
- Each record brackets GLib monotonic with OBS uptime-before/after; maximum
  bracket14625ns. GLib values are nanosecond UNITS with microsecond resolution;
  cross-domain comparisons use those pairs plus quantization allowance, not
  assumed identical raw clock epochs.

**No scheduling separation was justified or implemented.** Local blocking is
measured below the unchanged failure threshold, but no original refusal recurred.
The old701ms,24fps empty-start and decoder-construction failures remain unresolved.
Stable/sustained real-network acceptance, native capability/shipping admission,
new independent review and separately authorized physical SDI/card/genlock/drift
acceptance remain gates. No retry-until-green campaign, timestamp rewrite,
compressed-AU dropping, sender/engine edits, cards, auth/config changes,
commit/push/deploy or user-app operations occurred.
