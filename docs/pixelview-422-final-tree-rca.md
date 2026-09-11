# Final-tree finite-policy RCA — original intermittent failure unresolved

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


Checkpoint 2026-09-10; Main422 remains **unadvertised**. No admission, timestamp,
deadline, sender/engine, source-feed or owner scheduling policy changed. This is
**bounded RCA plus production diagnostics**, not a root fix or stable acceptance.
Parent independent correction review is still required.

Evidence prefix **E**: `plugins/pixelview-whep/.test-build/finite-rate/`.
New machine-readable evidence: `E/rca-summary.json`. Original
`review-summary.json`, `review-fresh-matrix-final2/`, all earlier failed bytes/logs,
and the preceding uncommitted implementation remain intact.

## What can actually be concluded about the original 701 ms first frame

The original receiver logged one native delivery with age **701306779 ns**,
then the generic finite-rate failure, `native=1 opus=178560 error=1`.
Read-only parsing of its server/replay logs finds **64 video markers**, all
successive marker timestamp increments **3754**, ending at timestamp336502,
marker sequence1654. Those server records do not contain arrival wall clocks or
all packet sequences and do not prove receiver continuity or absence of gaps.

The first native delivery proves `rate_active` was already true and a classified
rate existed. Nothing resets activation within that tap. Therefore the
**subsequent finite refusal was not the initial 3-second activation deadline**,
nor the initial caps-rate comparison / RAW caps / segment activation predicates.
A decoder metadata or delivery failure uses a different error. The remaining
finite paths are the sticky RTP observer failure, AU-side RTP freshness/clock
checks, or subsequent PTS/duration consistency. The old log does not identify
which. There was no owner Start/scheduling yet: the unchanged transport-readiness
assertion failed before owner Start, so fake-card scheduling/owner expiry is not
the producer of this particular error.

The age is `os_gettime_ns() - (segment running time + pipeline base)`, measured in
the test source-feed wrapper. It includes media/pipeline latency; **701ms age is
not a measurement of VT session creation or the >500ms packet-gap quantity**.
Production OBS uses CLOCK_UPTIME_RAW; finite/native diagnostic timestamps use
GLib monotonic time. Compare durations within each domain, and require an explicit
clock-pair observation before interpreting cross-domain absolute differences.
No contemporaneous original native event, packet clock, scheduler trace or stack
separates cold VT cost, parser/driver delay, reference observer work, memory/I/O
pressure, network/jitter/reorder, or harness descheduling. The owner harness also
loads its full v210 reference after starting the source; overlap is a plausible
measurement target, not an established cause or permission to weaken freshness.

**No phase-accounting exception is justified yet.** Cold first-session work can
block this serialized path, but neither moving the acquisition epoch nor granting
arbitrary startup grace follows from the old frame-age value. All thresholds,
PTS and timing contracts remain unchanged. The old decoder-construction and24fps
empty-start failures remain separate unresolved evidence.

## Production diagnostics added

- `native-422-rate.h`: fixed, predicate-specific reasons for ordered RTP DISCONT,
  SSRC change, sequence gap, backward clock, >500ms gap, acquisition expiry,
  marker absence, unsupported/changed/ambiguous tick classification. The first
  failure latches its reason, observation time, sequence and marker delta; later
  packets cannot replace it. Packet/interval/candidate/rational counters accompany
  the snapshot. No payload, URL, credential or raw SSRC is logged.
- `native-422-filter.c`: distinct AU uninitialized/stale/backward-clock,
  acquisition expiry/backward-clock, caps-rate conflict, RAW caps/segment refusal,
  PTS missing/nonmonotonic/gap and duration conflict reasons. Map/size and
  observer attachment failures have fixed reasons too. Existing Gst error
  publication includes safe numeric phase/counter/clock data and the previous
  decode, source delivery and RAW push durations. All predicates retain their
  old order, inequalities and fail-closed behavior.
- `native-422.m/.h`: worker-confined numeric diagnostic snapshot around native
  entry/configuration, VT session create enter/return, DecodeFrame submit/return,
  asynchronous wait completion, actual native callback (including first callback),
  packing and display-copy completion. Callback state is read only after the
  existing native wait. These are wall-clock intervals around real native calls,
  not CPU-time attribution or proof of a driver stack. Packing/preview intervals
  include any injected CoreVideo lock observer work.
- The production streaming path adds no diagnostic disk I/O, per-frame log,
  buffer retention, thread, retry, grace, or timestamp rewrite. Diagnostic data is
  emitted on the terminal finite refusal through the existing Gst error path.
  These snapshots do not capture a native call that never returns; an external
  stack/event trace is still required for that case.

## Deterministic and actual negative evidence

`rca-rate-red.log`, `rca-tap-red.log`, and `rca-native-timing-red.log` record
missing diagnostic fields/APIs before implementation (compile-time RED, not an
assertion reproducing the historical media failure). Final
`rca-policy-final.log` is ASan+UBSan GREEN:

- Actual finite packet policy at500ms-1ns, exactly500ms, +1ns, with sticky first
  reason/sequence/time/count after a second attempted fault.
- Actual filter TU active-phase AU freshness at the same boundaries, with seeded
  `rate_active=true`/`decoded_count=1` and elapsed acquisition time beyond3s;
  this fixture does not actually decode a preceding frame. A deliberately
  invalid AU at the inclusive boundary fails later metadata validation, not the
  finite predicate; tests inspect the reason, activation and decode count.
- Existing actual-TU3s±1ns /4s activation cases, four rates/wrap, loss/SSRC/DISCONT,
  unsupported/changed rate, backward clocks, filler and metadata negatives.
- Real native fixture tests assert actual session/submit/return/wait/callback/
  pack event order; native bytes and restart comparisons remain unchanged.

`run-whep-rca-negatives.py` runs existing loopback negative tests into a **new**
evidence tree, without overwriting old results. `rca-negatives-final/` and its log
pass all exact reason assertions with real Opus and owner Start refusal:

- Excluded30/1 and29999/1000: `ordered-rtp-rate-unsupported`, zero native frames.
-24fps with an injected downstream650ms callback block: `ordered-rtp-gap` after
  native10. Last accepted sequence1469, rejected1470, marker delta3750,
  classified24/1, packet count1471, acquired32 intervals. Gap **695726000 ns**;
  prior delivery **654553000 ns**, prior complete native decode **32979000 ns**.
  The previous decode's VT call took1645000ns, wait49000ns, pack20553000ns,
  preview10647000ns; that session's creation took112916000ns earlier. This
  **proves the induced downstream block dominates this refusal**, not cold native
  creation, a cadence change or a sequence loss. It does not identify the original
  701ms failure. A post-jitter/depay sink observer is not a network-ingress clock.
- Explicit negative3750→3600 tick change: `ordered-rtp-rate-change`.
- Test caps25/1 versus observed RTP24/1: `au-caps-rate-conflict`, zero native frames.

The first new negative runner stopped after the correctly refused induced stall
because its inherited assertion expected obsolete test-only `RTP_REJECT_CLOCK`
text. `rca-negatives.log` and `rca-negatives/` preserve that failure. The assertion
now reads the actual production reason, immutable packet clocks and delivery
interval, retaining the >500ms and >=650ms requirements. The subsequent changed-
assertion run passes; no production policy changed to accommodate it. Controlled
loss remains deterministic policy coverage, not a new real-network loss suite.

## Exactly one diagnostic positive matrix; not a repaired failure

Commands, exited0:

```sh
PV_DECKLINK_OPTIMIZE=1 PV_DECKLINK_WHEP_BUILD=1 PV_WHEP_APPROVED=1 \
  python3 plugins/decklink/tests/run-receive.py
PV_APPROVED_EVIDENCE=/Users/max/src/pixelview-desktop/plugins/pixelview-whep/.test-build/finite-rate/rca-predicate-matrix \
PV_APPROVED_REUSE_REFERENCES=1 \
  python3 plugins/decklink/tests/run-whep-approved.py
```

`rca-o2-build.log`, `rca-predicate-matrix.log`, and `rca-predicate-matrix/` preserve
this **single** full four-rate/two-generation fresh-registry O2 run. It passed
470 native full-byte reference pairs and360 owner comparisons with all original
strict identity/health/PCM/deadline checks. Existing multi-GB references were
reused read-only with fresh independent byte verification. No trace/fault or
threshold-relaxation flag was used. The matrix did **not reproduce** the old
failure; diagnostics alone are not a root fix. Do not replace the historical red
with this passing run or claim stable/sustained acceptance.

Final executed regressions, all exit0: `rca-policy-final.log`,
`rca-native-final.log` (ASan+UBSan/native/preview suite including real timing
assertions), `rca-headers-final.log` (77 cases), `rca-owner-final.log` (whole
no-WHEP owner/source sanitizer execution, not merely WHEP binary compilation).
The negative binary was separately sanitizer-built (`rca-negative-build.log`).
Linked frameworks/system drivers are not sanitizer-instrumented.

Full production build `cmake --build build_macos_native422 --config RelWithDebInfo
-j 8` passed with private HOME/CFFIXED_USER_HOME and explicit Xcode;
`rca-full-build.log`. Deep/strict codesign passed, `rca-signature.log`.
The isolated app was neither launched nor installed. Source hashes are recorded
in `rca-summary.json`; capability/offer files remain unchanged by this RCA.

Disk was checked before artifacts: initial3.6GiB free subsequently fell toENOSPC
before production edits. The incomplete test addition was restored in place when
atomic patching also failed. Space later recovered to8.6GB without this worker
deleting files or controlling another process; work then resumed. The origin of
that transient disk pressure is unknown and is not retroactively blamed for the
historical failure. No shared reference, user file or prior evidence was deleted.

## Remaining gates

1. Capture an actual recurrence with these precise production reasons/native
   events; separately observe pre-jitter ingress and test reference/owner scheduling
   costs if needed. No unchanged retry-until-green campaign.
2. Justify any cold-session phase accounting with actual events and fail-closed
   late/loss/rate-change tests before changing semantics. No arbitrary grace.
3. Parent independent correction review; sustained/stress four-rate HD and
   actual-network loss/jitter/recovery acceptance; native capability admission.
4. Preserve unresolved historical decoder-construction and24fps empty-startup.
5. Physical cards/SDI/genlock/drift/driver acceptance remain separately unauthorized.

No commit/push/deploy, physical device, user-app/config/auth operation, sender or
engine change. Main422 stays off.
