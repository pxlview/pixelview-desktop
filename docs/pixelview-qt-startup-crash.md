# macOS Qt startup crash regression

## Cause and correction

The original `Pixelview-2026-09-08-170532.ips` (PID 99143) traps in
`CFGetTypeID -> CGColorSpaceGetType -> CGImageCreate -> QImage::toCGImage ->
Qt Cocoa cursor -> QWidget::setCursor -> OBSBasic::ClearSceneData` (line 1560),
during scene-collection activation in `OBSInit`.

The bundled OBS Qt package `2026-08-26` contains Qt 6.11.1. In that release,
`qt_mac_cgImageFormatForImage()` creates a local `QCFType<CGColorSpaceRef>` and
returns a `vImage_CGImageFormat` containing its **raw** pointer. The local owner
releases the color space before the caller invokes `CGImageCreate`. The shipped
arm64 binary confirms the same sequence: store x20 into the returned structure
at helper +816; call `CFRelease(x20)` at +848. This is not a custom Pixelview
cursor or evidence of a Keychain failure. CoreGraphics caching can conceal the
invalid ownership sequence in a small uninstrumented process.

References:
- https://github.com/qt/qtbase/blob/v6.11.1/src/gui/painting/qcoregraphics.mm
- https://github.com/qt/qtbase/blob/v6.11.1/src/gui/image/qimage_darwin.mm
- https://github.com/obsproject/obs-deps/releases/tag/2026-05-21

The macOS-only dependency override selects OBS's Qt 6.10.3 package
`2026-05-21`, SHA-256
`2acfe38a2a2e524e08f39d48f76a192f19f2e8c542c605cbb0f8447cacd282a4`.
Windows Qt and all non-Qt pins remain unchanged. No binary interposition or
cursor workaround ships in Pixelview. CMake removes stale cached Qt component
locations when the exact platform pin changes, so an existing build tree cannot
silently retain Qt 6.11.1. Revisit the pin only with a verified upstream ownership
fix and this native regression passing.

## Safe native regression

```sh
python3 test/pixelview/test_qt_image_lifetime.py -v

# Deliberate RED comparison, if the old dependency is still installed:
PIXELVIEW_TEST_QT_PREFIX="$PWD/.deps/obs-deps-qt6-2026-08-26-universal" \
  python3 test/pixelview/test_qt_image_lifetime.py -v
```

The test compiles a standalone Qt application, shows a temporary test window,
sets the same native wait cursor, and converts 6,000 tagged/untagged QImages to
CGImages while checking dimensions, color model, and backing-data lifetime.
It loads no OBS plugins, reads no user configuration, and accesses no credentials
or network. Its test-only dyld guard detects a released Create reference at
`CGImageCreate` **before** dereferencing it. This makes the ownership violation
deterministic even when system color-space caching masks the actual dangling
pointer. A second run without interposition verifies the real unmodified Qt path.

Observed: Qt 6.11.1 fails with exit 91 and
`color-space Create reference released before CGImageCreate`; Qt 6.10.3 passes
both instrumented and uninstrumented runs. This guard is not shipped in the app.
The standalone test is not a substitute for checking the rebuilt real Pixelview
main window and stability after `OBSInit`.

## Build and real-app verification status

- Xcode 26.6 / SDK 26.5 `RelWithDebInfo` build succeeded. The Qt 6.10.3
  `qyieldcpu.h` also requires the real `<arm_acle.h>` declaration before Qt
  headers on Apple Silicon under `-Werror`; the frontend-only, version-gated
  forced include supplies it. The native regression passes with the same Xcode
  compiler and `-Werror`.
- Deep/strict app signature verification and `Pixelview --version` passed.
  All ten cached Qt component paths select the pinned package.
- Real app PID 15529 launched from the normal `build_macos` bundle with the
  existing user configuration. The pre-existing unclean-shutdown dialog's
  `Continue` button was pressed; no permission dialog was pressed.
- Log `2026-09-08 17-21-25.txt` records compiled/runtime Qt 6.10.3 and
  `17:23:34.210: ==== Startup complete`. No newer Pixelview crash report appeared.
- **Main GUI is NOT verified.** A post-init process sample shows the main thread
  blocked in `OBSBasic::InitPixelviewDesktop -> ConnectPixelviewDesktop ->
  pixelview::loadDevice -> SecItemCopyMatching -> SecurityServer::decrypt`,
  before the main window appears. This is a separate synchronous startup
  credential-read blocker, not the fixed CoreGraphics crash. No visible
  SecurityAgent prompt could be captured. No credential test, config reset,
  Keychain-policy change, or further app launch was performed. User assistance
  or a separately authorized investigation is required before GUI acceptance.
