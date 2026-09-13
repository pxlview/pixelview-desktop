# Pixelview Desktop

Pixelview Desktop is an OBS-derived desktop application built specifically for the [Pixelview.io](https://pixelview.io) streaming platform. It keeps OBS’s proven capture, audio, encoding, and output foundations while replacing the general-purpose broadcaster workflow with a focused Pixelview experience.

## Product direction

- Present a simplified, operator-friendly interface for Pixelview capture and streaming.
- Detect supported capture hardware automatically and keep essential controls close to the preview.
- Apply reliable Pixelview-oriented video, audio, and encoding defaults instead of exposing unnecessary OBS complexity.
- Preserve access to the native OBS capabilities that are needed for safe operation: capture-device settings, output controls, statistics, errors, and licensing.
- Maintain separate Pixelview configuration and application identity so it does not overwrite a user’s stock OBS setup.

## Current first draft

This first draft includes the simplified interface, Blackmagic/DeckLink-oriented capture workflow, native preview framing, audio meters and monitoring controls, fixed HD/FPS and encoding preferences, and native OBS start/stop streaming, status, and detailed statistics.

macOS implements the Desktop pairing/control protocol on the backend's standard player WebSocket: native HTTP pairing exchange, Keychain device storage, a `/desktop/ws?token=` control socket that answers the server's 20-second `SOCKET_SEND_PING` with `PONG_RESPONSE` (streaming flag plus OBS settings), and `DESKTOP_START`/`DESKTOP_STOP` around the existing Start/Stop control. There is no lease, fence, resume or client heartbeat: the pv-engine rejects a second publisher, `SOCKET_DESKTOP_REVOKED`/`SOCKET_DESKTOP_REPLACED` mutations announce terminal closes, and every other close is transient with 1–30 s backoff while media keeps running. HTTPS/WSS is required except explicitly enabled loopback development. Ingest settings stay in memory; existing service profiles are preserved. SRT fallback is intentionally unavailable. See `docs/pixelview-desktop-control.md`.

Build 29 compiled and passed deep/strict signing verification with native WHIP, DeckLink and VideoToolbox modules. All 129 local regression tests pass, including the release/version/updater contracts, compiled Qt protocol, real loopback HTTP/WebSocket authentication/4401 closure, Keychain save/read/delete, concurrent-exchange rejection and libcurl resource-origin checks. A test-only native harness also exchanged an actual admin-browser-created token, authenticated and received a heartbeat acknowledgement against the isolated PR207 backend; admin independently showed its device registration. Native media/admin/engine end-to-end verification is tracked separately in `docs/pixelview-acceptance.md`; do not infer media delivery from control-plane status.

The repository now contains a local-only Apple Silicon release pipeline for Pixelview `0.0.1` build `1`, with exact OBS `32.2.1-66-g6b3e55072`/base-commit provenance, Developer ID verification, Apple notarization, Sparkle signing, and atomic R2 publication to one stable feed. Secrets are resolved through 1Password at runtime. No release tag, GitHub release, production notarization, R2 upload, or public appcast has been created yet.

The device protocol does not provision engines, change billing, implement remote playout, or provide engine-enforced fencing. Non-macOS credential/transport implementations remain out of scope. Production notarization and clean-Mac release acceptance remain pending until the operator deliberately starts the documented release flow. Do not invent backend endpoints or reuse Uplink/node passwords; follow the authoritative `docs/desktop-protocol.md` in backend PR207.

## Current receiver checkpoint

The current development checkpoint adds compact side-by-side receive credentials, dedicated receiver Keychain persistence and noninteractive credential access, clearer login errors, shared fullscreen controls, preview zoom fixes, and program-only DeckLink settings with the keyer UI hidden. macOS packaging uses `Pixelview Desktop.app` and a Qt compatibility pin.

WHEP now probes native decoder capabilities and offers explicit H264, HEVC Main/Main10 and VP9 profiles0/2 alternatives with Opus. Limited-range BT709 ten-bit reception preserves precision through the main GPU texture, with transactional sender-canvas restoration and active-output guards. Full native build/signature checks and focused native tests passed. Full regression/release/GUI/hardware acceptance is not complete; see `plugins/pixelview-whep/tests/integration-status.md`.

The current working-state checkpoint adds the native VideoToolbox x422-to-v210 receive pipeline and source-bound DeckLink A/V scheduling, bounded queues and independent preview, finite-rate/header admission, and sanitized first-failure diagnostics. Main42210 advertisement is enabled in normal builds for the user-authorized 1080p25 limited-range Rec.709 test; the broader finite-rate investigations and their remaining gates are retained in `docs/pixelview-422-implementation-status.md`. Desktop defaults the WHEP jitter buffer to 100 ms and exposes a compact Receiving-panel link for persisted 0–2000 ms changes that apply to the next connection. Pair/Unpair now use asynchronous user-authorized Keychain actions, retain process-held credentials for reconnect, preserve unknown credential state on failure, and harden heartbeat/socket cancellation. Capture startup/discovery, local diagnostics and Pixelview branding are improved. The canonical local build/launch workflow requires stable Developer ID signing and separates app settings without replacing HOME or the Keychain domain; see `docs/pixelview-signed-development.md`.

The user reports receiving works without visible artifacts with OBS sending HEVC Main, Main10 and Main42210 ("runs fine"). This is user-observed behavior, not independently measured acceptance. Further verification is needed that Main42210 is truly received and output end-to-end as 10-bit 4:2:2. The checkpoint preserves prior focused test/build evidence and unresolved startup/admission, sustained/network, physical DeckLink/SDI and A/V fidelity gates; historical passes do not certify the current tree. No long hardware test, signing build, notarization or production release is implied by this commit. The previously observed cached decoder-probe/EOS limitation remains documented; the rejected isolated decoder patch is not bundled.

## New working-tree receive status (supersedes the observation above)

The later user Main10 glitch report supersedes the earlier “runs fine” observation; neither establishes its cause. Ordinary Main/Main10 now keep compressed AUs on a stock capsfilter, decoded video on direct clocked appsink→OBS delivery, and rendered DeckLink audio on the ordinary OBS mix. Native tap/raw-preview queue and latest-preview worker are native422-only. WHEP audio has no duplicate early sink/queue: a supported shared-PCM pad observer preserves preselection/native startup, then stops private native feeding when ordinary parsed CAPS is selected and removes itself on the next audio buffer. No ordinary private PCM is produced, including the obsolete rendered feed route. Desktop explicitly requests the persisted jitter setting, default 100 ms, and enables libobs async unbuffered mode to avoid rebuffering video after the clocked appsink; hardware smoothness remains unverified. Credential-safe WHEP timeout/EOS and GStreamer source/domain/code diagnostics now go to the normal OBS log, while raw messages remain suppressed. UI font/order changes are preserved.

Focused sanitizer tests exercise the actual production audio graph with exact PCM/segment PTS, native startup/routing and stale-generation teardown; the complete DeckLink owner regression verifies stock mixed audio without video pumps plus native exact v210/timestamped PCM. Prior Main/Main10 WHEP compressed-decode and native422-suite results are retained as earlier evidence, not rerun on this final audio revision. No integrated app build/sign/replacement/restart or physical SDI/live-glitch acceptance is implied. See `docs/pixelview-main10-receive-audit.md` for current scope and remaining gates.

## Remembered mode and clean macOS close (working tree)

The Sending/Receiving tab is remembered in the user configuration (`PixelviewReceive/Mode`) whenever the operator or a session deep link switches mode, and the next launch reopens on that tab before queued deep links are drained; shutdown never rewrites it. The main window's close was previously lost on macOS: the window has `WA_DeleteOnClose`, and the accepted close event deferred native stream/lease teardown, so Qt destroyed the window before that teardown resumed, leaving the process alive without a window and crashing later in `obs_shutdown` (DeckLink input destructor after module unload). The close event now keeps the window alive until `PixelviewShutdownReady()` reports native state settled (bounded to ten seconds, with the wait reason logged once), application quit forces completion synchronously on every platform, and a timed-out stream output is force-stopped. Verified on the rebuilt signed local bundle: automatic receive-mode restore at launch, and a termination-driven close that logs `Shutting down`, clears scene data, frees libobs with zero leaks, removes the crash sentinel and writes no crash report. The red close button was not clicked interactively in this session; it shares the same close-event path.

## Sidebar usability pass (working tree)

Both panels now name their groups (Pairing / Connection, Capture, Encoding; Session credentials, Output), the one primary action per mode (Start Streaming, Start receiving) is styled as such through the sidebar stylesheet without changing its font, and the sending panel uses 8 px spacing so it still fits the minimum window height. Receiving derives its idle guidance from the form ("Ready. Click Start receiving." versus which field is missing), keeps error text until the next attempt, offers a Show/Hide toggle on the password, places Start receiving directly under the credentials with DeckLink output settings in their own Output group below, and uses plainer Keychain wording. The capture item is no longer selected at startup or on device creation, so the canvas shows no editing outline until the operator clicks it or uses Fit. A hint above Start Streaming explains a missing device or pending connection once paired, mode tabs explain why they are locked, and the preview zoom readout uses a legible size. Verified visually on the rebuilt signed bundle at the 993×658 default window in both modes; the "Mute audio" and "Listen locally" wording and the OBS status bar were left unchanged. Native harness contracts were updated for the new receive ordering.

## Receiver refuses HEVC 4:2:2 10-bit (working tree)

Receiving no longer advertises or decodes HEVC Main 4:2:2 10. The single switch in `plugins/pixelview-whep/main422-25p.h` is now FALSE: the WHEP offer lists only Main/Main10 (plus H264/VP9), and the parsed-CAPS route selector refuses a `main-422-10` stream (or any other non-Main/Main10 HEVC profile) before its first access unit with a typed `GST_STREAM_ERROR_WRONG_TYPE` carrying a canonical reason. The plugin exposes that reason through `get_status.failure` (`unsupported-hevc-main-422-10` or `unsupported-hevc-profile`, never the wire profile string), logs one credential-safe line, and the Receiving panel stops the attempt with "The sender is streaming HEVC 4:2:2 10-bit, which cannot be received. Please use the HEVC Main or Main10 profile on the sender." The native VideoToolbox 4:2:2 code, its DeckLink v210 scheduling and their offline suites remain in the tree and admit the native branch explicitly per filter; flipping the switch back to TRUE restores the earlier behaviour end to end. Focused evidence for this change is listed in the commit; no app rebuild, live 4:2:2 sender or hardware run is implied.

## DeckLink receive output no longer drops on frame gaps (working tree)

The receiver's DeckLink output used to stop silently every few minutes while the canvas kept playing: the UI watchdog polled `receive_status` every 100 ms and the ordinary (Main/Main10) health check in `plugins/decklink/decklink-output-receive.inc` declared the output unhealthy whenever the source delivered no decoded frame for 500 ms, so any jitter-buffer or decoder hiccup drained the card permanently, without a log line, and AutoStart was one-shot so nothing restarted it. Logs from 2026-09-12/13 show three such stops (after 18 s, 16 min and 10 min) with a clean `Output 'decklink_output': stopping` and no preceding media error. The rendered path now stays healthy while the source is `playing` and has not turned native 4:2:2; leaving `playing` (the source's own 15-second stale/EOS/error detection), source removal, card removal or native scheduling failure still drain it. `receive_status` returns a `reason` string, the watchdog logs `[decklink-output-ui] receive output stopped after N ms: <reason>`, and it resumes automatically once the source reports ready again, bounded to three consecutive attempts that fail within ten seconds of starting (the budget resets on every bind). A manual Start that is refused (no bound source, source not ready, bind refused, device or mode unavailable) now logs `[decklink-output-ui] Start ignored: <reason>` instead of doing nothing. Offline evidence: the compiled Qt watchdog test now exercises resume and its budget, and the real-owner suite asserts a 600 ms frame gap stays healthy while state/native changes are named. Live check on the rebuilt signed bundle (2026-09-13, session 291056 at 1080p24 Main10 to the UltraStudio Monitor 3G): the output ran 607 s until the operator deliberately cut the stream, which produced the expected named stop (`state error, 15098 ms since last frame`) and an armed resume; after the receiver was restarted the output ran 28 minutes without a stop, longer than any interval in the failing logs. AutoStart was exercised once to bring the card up and then turned back off. Physical SDI picture was not inspected from the session, and the stall that previously triggered the 500 ms cutoff was not reproduced on demand.

## Standard control-socket protocol (working tree)

The Desktop control socket was rewritten to the backend's standard player protocol (backend branch `feature/desktop-pairing-20260905T185111`, commit e119c35): token-in-URL upgrade with no first message, `{"mutation"}`/`{"message"}` envelopes, server-driven `SOCKET_SEND_PING`/`PONG_RESPONSE` liveness, `DESKTOP_START`/`DESKTOP_STARTED`/`DESKTOP_ERROR`, `DESKTOP_STOP`/`DESKTOP_STOPPED`, and `SOCKET_DESKTOP_REVOKED`/`SOCKET_DESKTOP_REPLACED` as the terminal signals ahead of their close. Removed: auth first message, resume_fence/fence/lease_expires_at, the 15-second heartbeat timer, the 30-second acknowledgement deadline, `busy`/lease-lost handling, the stop-acknowledgement wait, and the old protocol docs (`pixelview-desktop-heartbeat.md`, `pixelview-control-recovery.md`), replaced by `docs/pixelview-desktop-control.md`. The client keeps a 10-second ready budget, treats 60 seconds without a server ping as a dead socket, keeps media running through control drops (reconnecting with the existing backoff and without re-sending a start), and only treats an upgrade rejected with 401/403 as terminal by code. A refused upgrade for a revoked token arrives as HTTP 403 because the backend closes before accepting; the client reports it as a refused connection without discarding the pairing. The receiver path (`/login/player` + `/wsocket`) is untouched. Verified: the compiled policy, frontend and loopback transport suites (`test_desktop.py`, `test_desktop_retry.py`, `test_media_failure_pairing.py`, `test_desktop_control.py`, `test_control_socket.py`, `test_whip_retry.py`, `test_pairing_ux.py`) pass, and a live run against the local backend on this Mac (backend branch commit e119c35, `dev.sh --k8s`) covered: pairing exchange and `DESKTOP_READY`; pongs recorded in the backend's Redis presence with the parsed settings object; a backend reload that closed the socket with code 1005 (which the old client treated as terminal) followed by automatic reconnect; admin revocation delivered as `SOCKET_DESKTOP_REVOKED`, which cleared presence, persisted the pairing-disabled state and showed the revoked message; Unpair and a fresh pairing. The rewritten `desktop_backend_smoke.mm` exercised pong-before-ready, `DESKTOP_START` → `DESKTOP_STARTED` with a WHIP grant and `DESKTOP_STOP` → `DESKTOP_STOPPED` against the same backend. Media start to the local engine was not exercised: no capture input is attached to this Mac. Three backend observations were reported and fixed on the backend branch at ab4aa77: `DESKTOP_READY` now precedes the ping loop (the client answers pings regardless of readiness anyway), `DESKTOP_START` handler failures answer `DESKTOP_ERROR {code: start_failed}` (mapped to "Pixelview could not start the stream") instead of closing the socket, and an invalid or revoked token is accepted then closed with 4401 (which the client treats like `SOCKET_DESKTOP_REVOKED`). Against ab4aa77 on this Mac: a DB-only revoke followed by a forced reconnect produced `control socket closed (code 4401, terminal)`, the revoked message and pairing disabled; the admin path (`SOCKET_DESKTOP_REVOKED` then close) did the same; Unpair and re-pairing succeeded after each, and the final pairing reports presence with settings.

## Repository status discipline

### Independent receiver backend selection (working tree)

New Pair defaults and Receiving now share the tiny native `PixelviewBackend.hpp`
helper: only exact runtime `PIXELVIEW_LOCAL_DEVELOPMENT=1` selects
`http://localhost:8000`; all other values select `https://api4.pixelview.io`.
Receiving no longer inherits the saved sending origin or development permission.
Its startup/authorized Keychain reads, writes, form edits, deep links and Start use
its independent origin; controller login/socket and returned-endpoint validation
use that origin/permission. Existing receiver origin/session/revision binding and
sender saved-origin reconnect/Keychain identity are preserved. The environment
never retargets saved sending device tokens; successful Unpair is still required
before changing an existing pairing backend.

TDD evidence: the new compiled production-origin/configuration test first failed
for inherited sender origin, then for inherited loopback permission, then passed.
The final focused command `PYTHONPATH=test/pixelview python3 -m unittest
test_backend_selection test_pairing_defaults test_pairing_ux test_receiver
test_receive_ui -v` passed 24 tests, including actual Qt dialog/controller/UI,
loopback NSURLSession transport, receiver Keychain restart/origin isolation and
saved HTTPS/loopback sender reconnect under both environment values. Known Qt
headless-plugin/font messages are present. No integrated app build, replacement,
restart, live media/hardware acceptance, commit or push was performed for this
change. Full regression status is recorded separately below when complete.

### Pairing production/development defaults (working tree)

New Pair uses `https://api4.pixelview.io` with only the masked one-time admin code
visible. Only exact runtime `PIXELVIEW_LOCAL_DEVELOPMENT=1` exposes the backend
origin and local-development checkbox, defaulting to `http://localhost:8000` with
the box checked. Invoke from this repo with
`PIXELVIEW_LOCAL_DEVELOPMENT=1 python3 cmake/macos/pixelview-launch.py` after a
separately authorized build. Unset/empty/other values retain production defaults.
Do not turn this into a build-time flag or rewrite a saved origin: existing
pairings reconnect with their persisted origin/development flag and unchanged
Keychain identity. Explicit successful Unpair is required before changing backend.
TLS, loopback and resource-origin validation remain mandatory. See
`docs/pixelview-pairing-ux.md` for invocation, overrides and verification scope.

The new real-Qt dialog tests followed production RED→GREEN and development
RED→GREEN; the focused pairing run passed 12 tests. The later 206-test full run
had two errors (concurrent control-recovery changes outgrew the pairing-completion
fixture; source inventory size mismatch) and one skip. No integrated app rebuild,
replacement, launch, signing, publication, commit or push was performed for this
pairing-defaults change. Do not infer release acceptance from these tests.

Update this file in every commit that changes Pixelview Desktop’s delivered features, verification status, or known limitations. Keep the **Current first draft** section accurate: state what has been added, what has been verified, and what remains intentionally out of scope. Do not present planned Pixelview backend, authentication, or remote-control work as implemented.

This is a separate Pixelview fork. Never open a pull request, push a branch, or target work against the original OBS Studio repository (`obsproject/obs-studio`). Pull requests, if used, must remain within the Pixelview repository and use Pixelview-owned branches.

## Upstream and licensing

This is a fork of OBS Studio and remains an OBS-derived GPL project. Preserve upstream notices, licensing information, and the source/build materials required for corresponding-source distribution. Product-specific changes should remain clearly documented and should not remove upstream attribution.

See `PIXELVIEW.md` and `docs/` for implementation, build, verification, and distribution details.
