# Pairing-first native UX

Pixelview's settings now require a durably saved, authenticated pairing identity. The HTTP exchange alone does not unlock controls. Capture-device selection/properties/Fit, FPS and encoding, preview transform/zoom input, monitoring-device selection, and source audio actions are locked before pairing. Internal startup encoder defaults and normalization still run under native-output busy checks; no live settings or Keychain entries are changed by the tests.

Once paired, the category collapses into one row: node ID and control connection state, plus a small text-only Unpair action. Connected is green only while the Desktop controller is authenticated and ready; offline/reconnecting is muted. The ordinary secondary Connected/Streaming text is hidden, while offline, lease-request/preparing and reconnect details remain available. Pair is absent when paired. Pair and Unpair have independent handler guards through native preparation/streaming/stopping and retained retry intent; the existing cancellable Stop/reconnect path is retained.

Cleanup failure is distinct from pairing identity: failed Keychain removal, failed identity persistence, or rejected first-ready identity exposes Retry cleanup once teardown drains. Ordinary unpaired state hides that action. Pair is blocked while cleanup needs retry. The exchange identity is retained only as an expected identity until authenticated ready validates it and persistence succeeds.

The redundant bottom capture-status toolbar is removed. Capture diagnostics remain in the device tooltip; source-creation failure uses a visible warning. The source meter remains below the canvas. Mute audio, Listen locally and the monitoring-output selector share one aligned row. Existing master-mute/listen behavior, serialization, rollback and meter lifetime are unchanged.

## Verified artifact

The complete regression suite passed: **84 tests**, `/tmp/pixelview-ux-suite-final.log`. Native compilation returned **BUILD SUCCEEDED**; the processed bundle is **build 41**, `com.pixelview.desktop`, arm64. (The final log name remains `pixelview-desktop-build40.log`; configure attempts increment the actual bundle number.) Deep/strict code-signature verification passed, including embedded WHIP, DeckLink and VideoToolbox modules. This is a local **ad-hoc runtime signature**, not Developer ID/notarization. The non-GUI executable check returned `OBS Studio - 32.1.0`. Independent final verdict is `passed: true` with no security or logic findings in `/tmp/pixelview-ux-review-approved.json`.

## Verification scope

- Behavioral RED/GREEN logs: `/tmp/pixelview-ux-red-gate.log`, `-red-ui.log`, `-red-wiring.log`, `-red-audio.log`, `-red-footer.log`, `-red-init.log`, `-red-collapse.log`, `-red-pairing-row.log`, `-red-durable.log`, `-red-expected-identity.log`, `-red-mismatch-cleanup.log` (all share the `pixelview-ux` prefix).
- `test/pixelview/test_pairing_ux.py` compiles actual Qt row construction, visibility/enabled-state refresh, pairing/configuration guards and cleanup transitions with fault-injected storage boundaries. It creates no production app, capture source, credential or network connection.
- `desktop_retry_frontend.cpp` executes real frontend ready/cancel/drain callbacks with configurable identity save success, expected-identity mismatch, retained Stop cancellation and fresh-lease recovery.
- The existing native encoding save harness now covers unpaired user rejection with zero writes and explicit internal initialization while unpaired, preserving its rollback and native-busy cases.
- Native macOS offscreen layout tests exercise actual audio construction at minimum/wide preview sizes, long output names and mute/listen common-row geometry. They are not an audible-output test.
- Full suite log: `/tmp/pixelview-ux-suite-final.log`. Final native packaging log: `/tmp/pixelview-desktop-build40.log`. Independent review artifacts: `/tmp/pixelview-ux-review.json`, `/tmp/pixelview-ux-review-final.json`, `/tmp/pixelview-ux-review-approved.json`.

GUI acceptance is parent-owned: verify the real unpaired first-run gate, pair/authenticate, compact connected row, streaming settings/Unpair lock and usable Stop. The implementation agent does not quit or unpair the running app, change the live config, touch Keychain credentials or operate the backend. If retained credentials exist, use the compiled isolated frontend harness rather than deleting registration to test first-run state. The parent observed the real app already unpaired during this task, so actual unpaired GUI verification needs no destructive fixture.

`AGENTS.md` is deliberately unchanged under the explicit protected-file instruction; current scope and verification are recorded here instead. No backend/engine changes, push, notarization or customer-release claim is included.
