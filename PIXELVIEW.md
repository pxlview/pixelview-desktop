# Pixelview Desktop — capture and encoding prototype

A local OBS fork with capture/encoding settings on the left and an editable Blackmagic preview on the right. Choose a device, adjust its native settings, and drag/resize the picture on a fixed **1920 × 1080** canvas. **Fit** resets framing without stretching the input.

The compact sidebar uses the original `pv-home` logo plus **Desktop**, grouped left-aligned controls, adjacent FPS/Mbps fields, and automatic device discovery without a Refresh button. B-frame controls are hidden and the no-B-frame policy is enforced on saved and Advanced settings. Native device and encoder properties remain available.

The native **Start/Stop Streaming** button is bottom-anchored in the sidebar; configuration locks during streaming while operational audio controls remain usable. Native audio meters, **Mute audio** (stream and local playback), and **Listen locally** with an output selector sit under the preview. The compact live OBS status bar and **Show stats** retain the detailed native telemetry. macOS **Help → License information** shows offline OBS attribution and the full GPL. There is no destination configured by default. Current verified local build: **27**, with 63 passing tests, native audio serialization checks, real loopback SRT audio/mute tests, and clean quit/restart verification. See [audio controls and verification](docs/pixelview-audio.md). Authentication and external Pixelview-server integration remain separate work.

See [distribution licensing requirements](docs/pixelview-distribution-license.md) before shipping binaries; public GitHub source alone is not a completed release compliance audit.

## Open

```sh
open build_macos/frontend/RelWithDebInfo/Pixelview.app
```

## Build

```sh
bash cmake/macos/pixelview-build.sh
```

This prototype does not replace `/Applications/OBS.app`. Its settings are separate, under `~/Library/Application Support/pixelview/obs-studio/`.

**Native capture, encoding and manual streaming:** Pixelview account/login, backend WebSocket integration and remote playout are not implemented. No destination is configured by default; the native stream path was verified against a loopback-only SRT receiver. Close other capture apps if they are holding the selected hardware.

- [Verified behavior and remaining hardware tests](docs/pixelview-acceptance.md)
- [Toolchain/build instructions and compatibility limits](docs/pixelview-build.md)
- [UI implementation details](docs/PIXELVIEW-UI.md)
- [Encoding defaults, native Advanced controls and HEVC color formats](docs/pixelview-encoding.md)

This remains an OBS-derived GPL project; upstream source and attribution are retained. The bundle is locally ad-hoc signed, not a notarized customer release.
