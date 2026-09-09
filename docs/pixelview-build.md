# Pixelview local macOS build

Run from any directory:

```sh
bash /Users/max/src/pixelview-desktop/cmake/macos/pixelview-build.sh
```

Artifact: `build_macos/frontend/RelWithDebInfo/Pixelview Desktop.app` (executable: `Contents/MacOS/Pixelview Desktop`). Quote paths containing spaces. Historical verification entries below retain the artifact names used at the time.
Bundle ID: `com.pixelview.desktop`; executable: `Pixelview` (arm64).
No installation into `/Applications` is performed. `DEVELOPER_DIR` selects Xcode locally, without changing global `xcode-select`.

Opt-in Developer ID signing, verified signed-build results, and the SDK compatibility fix are documented in [Developer ID signing](pixelview-developer-id.md). The signed app passed deep/strict signature verification and was launched locally; notarization is still pending.

## Pairing/audio UX build (41)

Build 41 implements the durable pairing-first gate, compact authenticated node/status row, guarded pairing/cleanup actions and common Mute/Listen/output row, while removing the redundant capture footer. Native build, deep/strict ad-hoc signature verification and all 84 regressions passed. Final log: `/tmp/pixelview-desktop-build40.log` (the processed bundle number is 41). See [pairing UX verification](pixelview-pairing-ux.md) for behavioral coverage, independent approval and parent-owned GUI scope.

## Recovery build (38)

Build 38 adds the pairing separator, branded one-Continue crash prompt, and fresh-authority automatic streaming recovery. Native compilation and deep/strict development ad-hoc signature verification passed. See [recovery verification](pixelview-recovery.md) for TDD, independent review and current acceptance scope; older build-35 no-auto-resume descriptions are historical.

## Desktop protocol build and harness (build 35)

The current bundle includes the actual Objective-C++ Desktop transport, macOS Keychain support and native WHIP plugin. `bash cmake/macos/pixelview-build.sh` returned `BUILD SUCCEEDED`; `codesign --verify --deep --strict build_macos/frontend/RelWithDebInfo/Pixelview.app` passed and `Pixelview --version` returned `OBS Studio - 32.1.0`. Build log: `/tmp/pixelview-desktop-build35.log`. Regression command: `uv run --with pillow python -m unittest discover -s test/pixelview -p 'test_*.py' -v` (69 passed).

Build the test-only control-plane harness, which does not start media:

```sh
clang++ -std=c++17 -fobjc-arc -fPIC -I. \
  -F.deps/obs-deps-qt6-2026-08-26-universal/lib \
  -framework QtCore -framework QtNetwork -framework Foundation -framework Security \
  -Wl,-rpath,"$PWD/.deps/obs-deps-qt6-2026-08-26-universal/lib" \
  frontend/utility/PixelviewDesktopMac.mm test/pixelview/desktop_backend_smoke.mm \
  -o /tmp/pixelview-desktop-backend-smoke
/tmp/pixelview-desktop-backend-smoke http://localhost:8000 < /private/path/to/one-time-code
```

Use a disposable node device and a private mode-0600 input file. The default harness removes the origin's Keychain credential afterward; do not use an origin holding a device you want to retain. Test-only `--keep-device` retains it for subsequent GUI/restart checks. There is no production CLI credential option or automatic streaming hook. The app reads only nonsecret `PixelviewDesktop/Origin`, `LocalDevelopment`, `NodeId`, `DesktopId`, `PairingDisabled`, and `CleanupComplete` from its isolated `user.ini`; tokens stay in Keychain. The exact origin, including localhost versus 127.0.0.1, identifies the Keychain account. Production uses HTTPS/WSS with normal trust validation. Local smoke is not a production TLS or media-delivery test.

## Experimental compatibility

Upstream requires Xcode/macOS SDK 26.5. `PIXELVIEW_LEGACY_TOOLCHAIN=ON` explicitly permits Xcode 15.3 / SDK 14.4 and excludes:

- Metal renderer: requires Swift 6; use OpenGL.
- mac-avcapture (including legacy): uses `AVCaptureDevice.backgroundReplacementActive`, absent in SDK 14.4.

Native `decklink`, VideoToolbox and `obs-webrtc` (WHIP) remain enabled. Browser, the unrelated inbound obs-websocket plugin, scripting, virtual camera, AJA, VST, Syphon and VLC are disabled by the script. Outbound Desktop WebSocket uses macOS NSURLSession with platform TLS verification, not the obs-websocket plugin. Sparkle is disabled by empty `SPARKLE_APPCAST_URL` and `SPARKLE_PUBLIC_KEY`, not an `ENABLE_SPARKLE_UPDATER` input option.

Dependencies remain upstream's 2026-08-26 obs-deps and Qt6 archives with the SHA256 pins in CMakePresets.json; CMake downloaded and verified them successfully. No SDK or dependency headers were fabricated or replaced.

The version override `32.1.0` supplies CMake's required version for the shallow/tagless checkout; it does not assert this fork is an official release.

## Verification

Initial native RelWithDebInfo build returned `BUILD SUCCEEDED`; `Pixelview --version` returned `OBS Studio - 32.1.0`. `codesign --verify --deep --strict` passed for the bundle including the native DeckLink plugin. GUI capture, isolated settings, and editable preview are separate acceptance checks owned by the UI/parent agent.

## FPS selector build

The native FPS selector was rebuilt with `bash cmake/macos/pixelview-build.sh`; full output is `/tmp/pixelview-fps-build.log`, ending `** BUILD SUCCEEDED **`. The rebuilt arm64 app and embedded arm64 DeckLink plugin passed `codesign --verify --deep --strict`; bundle ID remains `com.pixelview.desktop`, and the non-GUI `--version` check returned `OBS Studio - 32.1.0`.

FPS verification: `python3 -m unittest discover -s test/pixelview -p 'test_*.py' -v` returned `Ran 10 tests` / `OK`; standalone capture policy returned `Pixelview fit policy passed`. The FPS tests execute the actual header-only rate/transaction policy with injected reset/save operations; native Qt/config wiring is a source contract. These are not real GPU failure injection or hardware tests. Parent-owned GUI acceptance must verify all eight choices, exact native FPS logs, immediate persisted HD dimensions, restart preservation, and unchanged scene transforms. No GUI launch/quit was performed during the FPS implementation build.

When changing CMake options, rerun the configure script: Xcode's incremental ZERO_CHECK did not regenerate the plugin target list reliably during this build.

## Native Apple hardware encoders

`mac-videotoolbox` is enabled in the compatibility build. `vt-compat.h` bridges only the missing macOS 15 spatial AQ declarations: SDK <15 resolves the **actual exported CFString variable** `kVTCompressionPropertyKey_SpatialAdaptiveQPLevel` with `dlsym`, checking for NULL; SDK >=15 uses Apple's normal symbol. Both paths require runtime macOS >=15. Public enum values are default `-1` and disable `0`, also documented in [FFmpeg's compatibility implementation](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/videotoolboxenc.c). No invented SDK header or hardcoded substitute CFString is used. The native test confirms the resolved export is `SpatialAdaptiveQPLevel`. Only the SDK14.4 branch has been compiled locally; the modern-SDK branch remains untested.

After explicit CMake regeneration, build only this plugin (safe while another worker edits frontend):

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
cmake --preset macos  # reuses existing cache; initial setup uses flags in pixelview-build.sh
cmake --build build_macos --config RelWithDebInfo --target mac-videotoolbox -j 8
```

The plugin-only build does **not** embed the plugin into an existing app. Run the full build script after frontend changes are ready to package it. Verified standalone artifact:
`build_macos/plugins/mac-videotoolbox/RelWithDebInfo/mac-videotoolbox.plugin` (arm64, strict signature verification passed).

### Encoder settings and color pipeline

Hardware encoders are discovered from `VTCopyVideoEncoderList`; on this Apple M1 Pro the HEVC ID is `com.apple.videotoolbox.videoencoder.ave.hevc`. Prefer codec/hardware capability discovery in frontend logic rather than assuming the ID on every Mac. The actual plugin exposes:

| Setting | Requested value |
|---|---|
| `rate_control` | `"CBR"` |
| `bitrate` | integer kbps |
| `keyint_sec` | `1` |
| `bframes` | `false` |
| `spatial_aq_mode` | `1` (Auto; Disabled=2, Enabled=3) |
| `profile` | `"main"`, `"main10"`, `"main42210"` |

Auto deliberately enables AQ only for CRF; **Auto + CBR sets spatial AQ to disabled**, preserving upstream behavior. Plugin defaults are unchanged (`keyint_sec=2`, `bframes=true`); the frontend must supply the requested values explicitly. CBR is supported on Apple Silicon/macOS >=13.

**Profile does not choose the input color format.** In `encoder.c`, `update_params()` obtains `voi->format` from `video_output_get_info()` and passes it to `set_video_format()` before reading the profile. `obs_module_post_load()` registers no `get_video_info` conversion callback. `obs_to_vt_profile()` selects only the codec profile; the single coercion is `main` + P010 → Main10, not the reverse.

Use the OBS Advanced/global video color format (or the supplied video output pipeline) to select:

| Intended encoding | OBS input | Native CoreVideo input |
|---|---|---|
| Main / 8-bit 4:2:0 | NV12 | `kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange` (full range also mapped) |
| Main10 / 10-bit 4:2:0 | P010 | `kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange` (full range also mapped) |
| Main42210 / 10-bit 4:2:2 | P216, limited range | `kCVPixelFormatType_422YpCbCr16BiPlanarVideoRange` |

P216 full range is explicitly rejected by the plugin. Setting a 10-bit profile while retaining NV12 does not create 10-bit source precision or 4:2:2 chroma. The upstream properties list is OS-gated, not a per-device profile-support query; the native test below separately verified all three profiles on this Mac.

### Repeatable checks

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
xcrun clang -std=c11 -Wall -Wextra -Werror plugins/mac-videotoolbox/tests/test-spatial-aq.c \
  -framework VideoToolbox -framework CoreFoundation -o /tmp/test-spatial-aq
/tmp/test-spatial-aq
xcrun clang -std=c11 -Wall -Wextra -Werror plugins/mac-videotoolbox/tests/test-hardware-hevc.c \
  -framework VideoToolbox -framework CoreFoundation -framework CoreVideo -framework CoreMedia \
  -o /tmp/test-hardware-hevc
/tmp/test-hardware-hevc
python3 plugins/mac-videotoolbox/tests/test-plugin-properties.py build_macos
```

SDK14.4 regression test was run RED (undeclared spatial AQ symbols), then GREEN with the compatibility header. The hardware test discovers the real hardware HEVC encoder, creates each profile with matching pixel buffers, sets and reads back hardware=true, CBR=6,000,000 bps, 1-second GOP, no frame reordering, and AQ disabled. All three emitted 65 real compressed frames at 1280×720/60 fps, with keyframes at 0 and 60. It tests native VT encoding, not the complete libobs recording/muxing path. The separate libobs test loads the actual built plugin and asserts registration and real property lists. It emitted only an unrelated global-hotkey permission warning; no GUI was launched.

Logs: `/tmp/pixelview-vt-configure.log`, `/tmp/pixelview-vt-build.log` (`BUILD SUCCEEDED`), `/tmp/pixelview-vt-hardware.log`, `/tmp/pixelview-vt-properties.log`. App packaging and end-to-end libobs recording remain parent-owned acceptance checks.
