# Pixelview Desktop

Pixelview Desktop is a macOS application derived from OBS Studio for the
[Pixelview.io](https://pixelview.io) streaming platform. It keeps OBS's
capture, audio, encoding and output foundations and replaces the general
broadcaster workflow with two focused modes:

- **Sending** — capture a Blackmagic/DeckLink input on a fixed 1920 × 1080
  canvas, encode with fixed Pixelview-oriented defaults, pair the machine with
  a Pixelview node, and stream to that node over WHIP under the control of a
  backend WebSocket.
- **Receiving** — log in to a Pixelview session with its credentials, decode
  the WHEP stream natively, show it on the canvas and optionally play it out
  through a DeckLink output card.

Native OBS capabilities that operators still need (device settings, output
controls, statistics, license information, updates) remain reachable; the
rest of the OBS surface is hidden. The app has its own bundle identity and
its own settings root under `~/Library/Application Support/pixelview/obs-studio`,
so it never touches a stock OBS installation.

What is implemented, how it behaves and what has been verified is kept in
[`docs/features.md`](docs/features.md). Building, testing, signing and
releasing are described in [`docs/build-and-release.md`](docs/build-and-release.md).
Release notes live under [`docs/releases/`](docs/releases/).

## Repository structure

The tree is upstream OBS Studio (`32.2.1`, see `version.json`) plus these
Pixelview additions:

| Area | Where |
| --- | --- |
| Sidebar, modes, pairing, streaming control, receive panel, deep links, shutdown gate | `frontend/widgets/OBSBasic_Pixelview*.inc`, hooks in `frontend/widgets/OBSBasic.cpp` and `OBSBasic_Streaming.cpp` |
| Control-socket policy, transport, Keychain, receiver controller | `frontend/utility/Pixelview*.hpp`, `Pixelview*.cpp`, `Pixelview*.mm` |
| Native WHEP receiver (GStreamer + VideoToolbox) | `plugins/pixelview-whep` |
| DeckLink receive output and its watchdog | `plugins/decklink` (`decklink-output-receive.inc`), `plugins/decklink-output-ui` (`decklink-receive-ui.inc`) |
| WHIP output hardening | `plugins/obs-webrtc` |
| Offline and loopback tests | `test/pixelview`, `plugins/decklink/tests`, `plugins/pixelview-whep/tests` |
| Signed development build and launcher | `cmake/macos/pixelview-build.sh`, `pixelview-signed-development.py`, `pixelview-launch.py` |
| Release pipeline (notarization, Sparkle, R2) | `release/`, `cmake/macos/pixelview-release.sh`, `version.json` |

Inherited OBS files (`README.rst`, `CONTRIBUTING.md`, `CODESTYLE.md`,
`SECURITY.md`, `COPYING`, `AUTHORS`) are kept unchanged for attribution and
rebasing.

## Company information

Pixelview is a brand of Cinecode OÜ.

```text
Cinecode OÜ
Ahtri 12
10151 Tallinn
Estonia
https://pixelview.io
```

These company details identify the business behind Pixelview; they do not
replace upstream copyright notices or change the applicable software
licenses. Pixelview Desktop remains an OBS-derived GPL project: upstream
source and attribution are retained, and binaries ship with the complete
license and third-party notices described in `docs/build-and-release.md`.
