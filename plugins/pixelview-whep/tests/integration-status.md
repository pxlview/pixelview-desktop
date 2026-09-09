# Local receive integration status

## Verified

- Final full native application build succeeded; deep strict code-signature verification passed.
- Application: `/Users/max/src/pixelview-desktop/build_macos/frontend/RelWithDebInfo/Pixelview Desktop.app`.
- Current engine `go test ./...` passed after peer-local H264 alias negotiation/binding changes. Existing browser primary42e01f remains; Desktop offers probe-derived42c02a.
- Complete synthetic loopback run recorded all five codecs (H264, HEVC Main/Main10, VP9 profiles0/2) plus HTTP406 negative passing. Evidence: `whep-loopback-results.json` and `whep-loopback.md`.
- Production callback/main-texture precision runner independently passed:877 limited-range BT709 luma levels; sender canvas restored; full-range input rejected.

## Unresolved, do not call release acceptance complete

- Pinned upstream applemedia can send EOS before delivering the last decoded probe frame. A100-process diagnostic had96 passes/4 failures, affecting different profiles. Capability probing correctly withholds failed profiles, but caches the reduced mask for the process.
- Parent final loopback rerun (`/tmp/pixelview-whep-final-loopback.log`) passed H264/Main/Main10 media delivery but failed its fixed payload assertion: Main10 was bound to PT96 instead of expected120. This is consistent with a reduced offered mask; that rerun did NOT complete all six cases. Do not replace this result with the earlier passing run or claim repeatability.
- Isolated decoder cooperative-pause candidate is REJECTED: despite100/100 startup passes, it self-deadlocks on downstream renegotiation. `/Users/max/src/vtdec-drain-isolated/README.md` has evidence. No candidate binary or patch was promoted into the production runtime.
- GUI acceptance remains unverified: the existing older app process had no accessible window. Do not disturb its session or bypass Keychain security.
- Full repository regression discovery was not entirely green; see `/tmp/pixelview-desktop-receive-regressions.log`. The receive zoom fixture compile issue was subsequently fixed and zoom/precision transaction tests passed. Live Keychain and release inventory prerequisites remain separate acceptance boundaries.
- Main42210 remains unadvertised; tested AYUV64 route alters horizontal chroma and mishandles full range. Fullscreen/SDR DeckLink ten-bit fidelity, calibrated grading accuracy, HDR, sustained throughput and actual live SPS/VPS admission remain unclaimed.

No commits, deployment, existing server restart, Keychain unlock or security-setting changes were performed for this integration.
