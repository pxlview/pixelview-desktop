# Pixelview Desktop — capture and encoding prototype

A local OBS fork with capture/encoding settings on the left and an editable Blackmagic preview on the right. Choose a device, adjust its native settings, and drag/resize the picture on a fixed **1920 × 1080** canvas. **Fit** resets framing without stretching the input.

The compact sidebar uses the original `pv-home` logo plus **Desktop**, grouped left-aligned controls, adjacent FPS/Mbps fields, and automatic device discovery without a Refresh button. B-frame controls are hidden and the no-B-frame policy is enforced on saved and Advanced settings. Native device and encoder properties remain available.

The native **Start/Stop Streaming** button is bottom-anchored in the sidebar; configuration locks during streaming while operational audio controls remain usable. Native audio meters, **Mute audio** (stream and local playback), and **Listen locally** with an output selector sit under the preview. The compact live status bar and **Show stats** retain detailed native telemetry. macOS **Help** includes offline license information and, in release builds, **Check for Updates…**. There is no destination configured by default. See [audio controls and verification](docs/pixelview-audio.md) and [current integration verification](docs/pixelview-acceptance.md).

See [distribution licensing requirements](docs/pixelview-distribution-license.md) before shipping binaries; public GitHub source alone is not a completed release compliance audit.

## Company information

Pixelview is a brand of Cinecode OÜ.

```text
Cinecode OÜ
Ahtri 12
10151 Tallinn
Estonia
https://pixelview.io
```

These company details identify the business behind Pixelview; they do not replace upstream copyright notices or change the applicable software licenses.

## Open

```sh
open build_macos/frontend/RelWithDebInfo/Pixelview.app
```

## Build

```sh
bash cmake/macos/pixelview-build.sh
```

This prototype does not replace `/Applications/OBS.app`. Its settings are separate, under `~/Library/Application Support/pixelview/obs-studio/`.

**Native capture, encoding and Pixelview WHIP:** On macOS, the **Pairing / Connection** category above capture shows persistent paired node identity separately from connection/streaming state and offers **Unpair this installation** (local-only, stop-before-release; no other device is revoked). Use **Pair with Pixelview**, enter your backend origin and a one-time code from the node's admin Settings → Pixelview Desktop. Device credentials live in Keychain. Start Streaming requests an exclusive backend lease before configuring an in-memory native WHIP service; Stop ends actual output before releasing the reservation. SRT-only/v2 nodes report WHIP unavailable instead of falling back. Connections recover with backoff, never automatic stream resumption. An active project/engine and valid subscription must already exist in admin. Remote playout and engine provisioning are not implemented. Close other capture apps if they are holding the selected hardware.

- [Verified behavior and remaining hardware tests](docs/pixelview-acceptance.md)
- [Toolchain/build instructions and compatibility limits](docs/pixelview-build.md)
- [UI implementation details](docs/PIXELVIEW-UI.md)
- [Encoding defaults, native Advanced controls and HEVC color formats](docs/pixelview-encoding.md)

This remains an OBS-derived GPL project; upstream source and attribution are retained. Development builds are ad-hoc signed and updater-free. Start with the top-level [Pixelview release checklist](PIXELVIEW_RELEASE.md); the detailed Apple Silicon customer-release path is in [the macOS release runbook](docs/pixelview-macos-updates-and-releases.md). Release creation requires local Developer ID signing, Apple notarization, a signed Sparkle appcast, and R2 publishing.
