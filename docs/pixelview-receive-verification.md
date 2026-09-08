# Pixelview Desktop receive — local verification

## Public-runtime and sidebar follow-up

- Replaced host Homebrew runtime inputs with SHA256-pinned official GStreamer1.28.3 runtime/development package downloads, locally extracted without installation. The curated bundled runtime now has53 arm64 Mach-O files, including source-built patched rswebrtc0.15.2.
- Full normal build helper passed after moving an inspection-only raw package extraction (containing dangling framework symlinks) out of the recursive `.deps` xattr scan. Final app deep/strict signature and embedded-runtime verification passed.
- Updated GUI passed135seconds uninterrupted live reception: final sample3,436 video buffers,6,228,480 PCM sample frames, jitter50. Exact backend viewer cleanup, mode roundtrip and restart passed.
- All147 frontend/release/source/License tests passed; public-runtime packaging suite11tests passed separately. Public release metadata validation passed. No production signing/notarization, DMG upload or R2 publication was performed.
- Native screenshot confirms logo above equal-width sidebar tabs, optional DeckLink settings in the Receiving configuration area, and only Stop receiving in the footer. Fullscreen remains in View.
- License dialog now has License, Third-party notices and Source & build info pages. Required source/notices/inventory artifacts are bound to the release manifest and immutable publication transaction. **Compliance is not complete:** `release/source-inventory.json` blocks release pending whole-app source/license review and materials. Development fallback pages do not claim a complete notice set or public source availability.

## Implemented

- Sending/Receiving tabs; shared native OBS canvas, audio controls and native DeckLink output settings.
- Session ID, masked password and receiver name (hostname default). Native player login and viewer registration; receive does not require sender pairing.
- Start/Stop receiving, private receive scene/source, fullscreen projector and return to sending output.
- Bundled GStreamer WHEP source with actual internal jitter-buffer readback of 50 ms. This is not measured glass-to-glass latency.
- Credential-bearing media URL stays in private memory, not scene settings. Same-origin resource enforcement, rejected redirects, generation/cancellation gating and stale-event guards.

## Executed acceptance

Local backend + engine + synthetic H264/Opus publisher, through normal backend ingress. Final app built with Xcode, `codesign --verify --deep --strict` passed; embedded runtime verifier passed. GUI testing used a cloned ad-hoc app with a nonshipping injected driver and isolated HOME/CFFIXED_USER_HOME, not the user's normal OBS settings.

- Final GUI: 135 seconds uninterrupted video/audio; every five-second sample advanced, no counter resets or viewer replacement. Last sample: 3,428 video buffers, 6,258,240 PCM sample frames, jitter50.
- Exact unique backend viewer/name correlation; Stop removed that live record. Password cleared, mode tabs unlocked, Sending/Receiving roundtrip and new Start recovered media.
- Native Listen enabled monitoring; master Mute disabled it; Unmute left monitoring off.
- Fullscreen displayed advancing test-pattern timecode. Sidebar native display menu and existing projector Escape QAction close verified. OS-synthesized Escape was unreliable in automation; do not equate that with an application shortcut failure.
- DeckLink settings opened and enumerated UltraStudio Monitor 3G. No physical output was started in the new-build test. The user's earlier successful DeckLink test used the stock-OBS proof, not this final build.
- Final frontend regression suite: 135 tests passed. Targeted native lifecycle/codec tests and patched-signaller origin tests also passed during implementation.
- Independent controller/native-media fresh-auth tests each passed135seconds; media recorded3,260 video buffers and5,866,560 PCM sample frames with jitter50, one endpoint and zero stops.

## Defects found and corrected

1. H264 offer lacked packetization-mode1 required by the engine/Pion track. Set the actual transceiver codec preference before SDP creation.
2. Backend development media URL used localhost while its WHEP resource used127.0.0.1. Aligned only the backend development SERVER_URL default with the engine; production defaults unchanged. Four URL tests passed. Never relax same-origin protection to hide this mismatch.
3. NSURLSession resource lifetime30seconds also ended healthy WebSockets. Set shared lifetime24hours, beyond controller refresh23hours; retain independent login30s, registration15s and heartbeat65s deadlines. A heartbeat regression first failed after30seconds, then passed beyond it.
4. Cancelled/superseded pipelines could still start or deliver media. Generation and actual-delivery cancellation checks now guard those boundaries.
5. Upstream WHEP redirects/resource Location were insufficiently constrained. Bundle source-built patched rswebrtc0.15.2 with real two-origin security regressions.

## Artifacts and reproduction

- Build: `bash cmake/macos/pixelview-build.sh`.
- App: `build_macos/frontend/RelWithDebInfo/Pixelview.app`.
- Frontend tests: `uv run --with pillow python -m unittest discover -s test/pixelview -p 'test_*.py'`.
- Runtime and native tests: see `plugins/pixelview-whep/README.md`.
- Live native acceptance: see `test/pixelview/receiver_media_live.md`.
- Private local GUI evidence/harness: `~/src/pixelview-whep-spike/runtime/verify-desktop-stable.py`, `desktop-stable-acceptance.json`, projector geometry/close evidence and screenshots. These depend on private local credentials and the nonshipping driver; they are not customer runtime dependencies.

## Explicit remaining release/hardware gates

- New-build physical DeckLink output/capture-release test; cadence, SDI color and long-run A/V sync.
- Internet loss/recovery, extended soak and measured glass-to-glass latency.
- Integrated HEVC end-to-end, HDR/10-bit/4:2:2 fidelity and zero-copy are not established. Current received raw output is SDR BGRA8/stereo48k; standalone bundled HEVC decode passed.
- macOS14 minimum, arm64-tested. No older-OS/universal qualification claimed.
- Developer ID/notarization and complete transitive license/corresponding-source/relinking audit before distribution. See `plugins/pixelview-whep/LICENSES.md`.
- No commit, push or release performed by this implementation task.
