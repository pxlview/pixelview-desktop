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

macOS now implements the node-scoped Desktop pairing/control protocol from backend PR207: native HTTP exchange, Keychain device storage, authenticated outbound WebSocket, OBS-settings heartbeats, and lease-gated WHIP using the existing Start/Stop control. HTTPS/WSS is required except explicitly enabled loopback development. Ingest settings stay in memory; existing service profiles are preserved. SRT fallback is intentionally unavailable. Disconnect, protocol errors and conservative monotonic acknowledgement deadlines stop output; reconnect never resumes streaming automatically.

Build 29 compiled and passed deep/strict signing verification with native WHIP, DeckLink and VideoToolbox modules. All 129 local regression tests pass, including the release/version/updater contracts, compiled Qt protocol, real loopback HTTP/WebSocket authentication/4401 closure, Keychain save/read/delete, concurrent-exchange rejection and libcurl resource-origin checks. A test-only native harness also exchanged an actual admin-browser-created token, authenticated and received a heartbeat acknowledgement against the isolated PR207 backend; admin independently showed its device registration. Native media/admin/engine end-to-end verification is tracked separately in `docs/pixelview-acceptance.md`; do not infer media delivery from control-plane status.

The repository now contains a local-only Apple Silicon release pipeline for Pixelview `0.0.1` build `1`, with exact OBS `32.2.1-66-g6b3e55072`/base-commit provenance, Developer ID verification, Apple notarization, Sparkle signing, and atomic R2 publication to one stable feed. Secrets are resolved through 1Password at runtime. No release tag, GitHub release, production notarization, R2 upload, or public appcast has been created yet.

The device protocol does not provision engines, change billing, implement remote playout, or provide engine-enforced fencing. Non-macOS credential/transport implementations remain out of scope. Production notarization and clean-Mac release acceptance remain pending until the operator deliberately starts the documented release flow. Do not invent backend endpoints or reuse Uplink/node passwords; follow the authoritative `docs/desktop-protocol.md` in backend PR207.

## Current receiver checkpoint

The current development checkpoint adds compact side-by-side receive credentials, dedicated receiver Keychain persistence and noninteractive credential access, clearer login errors, shared fullscreen controls, preview zoom fixes, and program-only DeckLink settings with the keyer UI hidden. macOS packaging uses `Pixelview Desktop.app` and a Qt compatibility pin.

WHEP now probes native decoder capabilities and offers explicit H264, HEVC Main/Main10 and VP9 profiles0/2 alternatives with Opus. Limited-range BT709 ten-bit reception preserves precision through the main GPU texture, with transactional sender-canvas restoration and active-output guards. Full native build/signature checks and focused native tests passed. Full regression/release/GUI/hardware acceptance is not complete; see `plugins/pixelview-whep/tests/integration-status.md`.

The current working-state checkpoint adds the native VideoToolbox x422-to-v210 receive pipeline and source-bound DeckLink A/V scheduling, bounded queues and independent preview, finite-rate/header admission, and sanitized first-failure diagnostics. Main42210 advertisement is enabled in normal builds for the user-authorized 1080p25 limited-range Rec.709 test; the broader finite-rate investigations and their remaining gates are retained in `docs/pixelview-422-implementation-status.md`. WHEP defaults to a 100ms receive buffer. Pair/Unpair now use asynchronous user-authorized Keychain actions, retain process-held credentials for reconnect, preserve unknown credential state on failure, and harden heartbeat/socket cancellation. Capture startup/discovery, local diagnostics and Pixelview branding are improved. The canonical local build/launch workflow requires stable Developer ID signing and separates app settings without replacing HOME or the Keychain domain; see `docs/pixelview-signed-development.md`.

The user reports receiving works without visible artifacts with OBS sending HEVC Main, Main10 and Main42210 ("runs fine"). This is user-observed behavior, not independently measured acceptance. Further verification is needed that Main42210 is truly received and output end-to-end as 10-bit 4:2:2. The checkpoint preserves prior focused test/build evidence and unresolved startup/admission, sustained/network, physical DeckLink/SDI and A/V fidelity gates; historical passes do not certify the current tree. No long hardware test, signing build, notarization or production release is implied by this commit. The previously observed cached decoder-probe/EOS limitation remains documented; the rejected isolated decoder patch is not bundled.

## Repository status discipline

Update this file in every commit that changes Pixelview Desktop’s delivered features, verification status, or known limitations. Keep the **Current first draft** section accurate: state what has been added, what has been verified, and what remains intentionally out of scope. Do not present planned Pixelview backend, authentication, or remote-control work as implemented.

This is a separate Pixelview fork. Never open a pull request, push a branch, or target work against the original OBS Studio repository (`obsproject/obs-studio`). Pull requests, if used, must remain within the Pixelview repository and use Pixelview-owned branches.

## Upstream and licensing

This is a fork of OBS Studio and remains an OBS-derived GPL project. Preserve upstream notices, licensing information, and the source/build materials required for corresponding-source distribution. Product-specific changes should remain clearly documented and should not remove upstream attribution.

See `PIXELVIEW.md` and `docs/` for implementation, build, verification, and distribution details.
