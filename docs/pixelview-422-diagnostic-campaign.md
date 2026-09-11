# Native422 diagnostic campaign declaration — bounded, first failure stop

Declared before executing the campaign. Shipping admission stays CLOSED.

- Order: one fresh-process/fresh-registry baseline at 24000/1001, 24/1, 25/1,
  30000/1001; only if all pass, the same four once with one controlled load factor:
  one additional independent-reference LZ4 decompression/full-byte comparison
  per observed native pair. No external stress process, user-app control, purge,
  artificial network fault, retry, warmup receive session, or cold-cache claim.
  "Cold" means first receiver/native session in a new process, not cold OS caches.
- Maximum eight rate cases, two generations each, existing finite clip and
  25-second receiver watchdog per case. Stop the entire campaign on the first
  failed predicate/assertion/timeout, missing instrumentation, record overflow,
  or unsafe resource prerequisite. No replacing the failed case with a pass.
- Disk prerequisite: at least 4 GiB free immediately before each case. Reuse
  existing reference files READ-ONLY; owned lossless compressed native references
  retain existing 256 MiB/512-frame cap. No duplicate multi-GB reference creation,
  shared deletion, per-frame production disk I/O or sender/engine changes.
- Instrument only isolated test binaries: public rtpjitterbuffer sink pad
  (pre-jitter) versus rtph265depay sink (ordered RTP), correlated by generation,
  sequence and timestamp. Marker-only timings are not full packet-loss evidence.
  Paired OBS uptime-before / GLib monotonic / uptime-after records bound clock
  correlation; GLib timestamps are ns units with microsecond resolution.
- Fixed in-memory campaign capacity 2048 records and native observer capacity
  2048 paired records in campaign mode; overflow is failure, never sampling away
  decoded frames. Flush after teardown, or one snapshot on terminal error.
  Record native decode/session/wait/pack/preview, delivery, owner reference load
  and per-slot comparison costs separately. Native observer extraction/hash/
  decompression-comparison costs must be retained independently.
- Unchanged acceptance: exact every observed native byte versus independent
  decode, paired locks and contiguous identities within two generations; exact
  owner full-buffer comparisons; 45 rational slots and >=35 distinct images per
  generation; source-only real nonzero PCM; native freshness <500ms; RTP gap
  <=500ms; acquisition <=3s; PTS preserved, strictly advancing and <=100ms delta;
  duration within +/-1ns; exact selected mode and reconnect/refusal semantics.
- A supported bounded nonleaky scheduling separation is justified only if
  measured local blocking identifies the cause. No queue/clock/grace/admission
  change just to pass. A passing campaign does NOT fix the historical 701ms,
  empty-startup or decoder-construction failures or approve stable admission.

Evidence target: `plugins/pixelview-whep/.test-build/diagnostic-acceptance/`;
per-phase new directories, command results and hashes in the final manifest.
No physical cards, user apps/config/auth, commits, pushes, deploys or advertisements.
