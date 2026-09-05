# Pixelview UI prototype — implementation and verification

## Native streaming, statistics and licensing (build 23)

- The sidebar now hosts the actual native OBS Start/Stop Streaming button, including its preparing/connecting/stopping states and existing validation/error dialogs. No stream starts automatically.
- The existing OBS bottom status bar shows CPU/FPS and, while streaming, time, bitrate, network-dropped frames and connection indicators. Show stats opens the original detailed Stats window (render misses, encoding skips, output frame drops, memory and disk). Frame-drop metrics are not packet-level loss telemetry.
- FPS and encoding changes are blocked while native stream setup is pending as well as while output/video is active.
- macOS Help → License information opens a dedicated offline viewer with OBS attribution and full unchanged COPYING. It does not instantiate the network-fetching upstream About dialog. The equivalent action remains in the Pixelview Desktop menu on other platforms.
- Build 23 was compiled, deeply/strictly signature-verified and exercised on this Mac. All 45 tests and independent focused review passed. A real GUI-started loopback-only SRT stream carried 1920×1080 HEVC Main (no B-frames) and 48 kHz stereo Opus; Stop Streaming ended the receiver and restored editable controls. A closed loopback receiver produced the native Failed to connect to server popup. Missing configuration warning, detailed Stats reuse/reopen and offline license reopen were also exercised.
- The temporary SRT service was removed and the original no-destination state restored. Capture sources/settings/transforms and stock OBS configuration hashes were preserved. Native save added scene resolution metadata 1920×1080; no source refit occurred.
- Authentication/backend integration, external-server publishing, network impairment/reconnect/delay scenarios and Windows/Linux runtime remain unverified. Release-wide third-party licensing/source-distribution review is still a release gate; see [distribution checklist](pixelview-distribution-license.md).


## Integration

All executable UI changes are in existing compilation units; **no new CMake source entries** are needed. The local build script handles bundle branding, compiler compatibility, dependencies, and compilation.

Modified UI files:
- `frontend/widgets/OBSBasic.cpp`: minimal shell, native device discovery/selection, Fit, fixed video dimensions, startup suppression, branded title.
- `frontend/widgets/OBSBasic.hpp`: controller declarations and Qt-owned widget/timer pointers.
- `frontend/widgets/OBSBasic_SceneCollections.cpp`: no default desktop/microphone capture sources.
- `frontend/OBSApp.cpp`: isolate both configuration path helper entry points.
- `frontend/obs-main.cpp`: omit unrelated first-launch permission checklist.
- New header-only `frontend/utility/PixelviewCapturePolicy.hpp` and `PixelviewConfig.hpp`.

## Behavior

- Keeps `OBSBasicPreview` and upstream selection/drag/stretch/crop machinery, scene persistence and source lifetime management. The default single internal scene holds one named `Pixelview Capture` DeckLink source. Selecting another device updates this source, not another scene item. The upstream scene/mixer/output/streaming menus and docks are inaccessible in the minimal shell; file drops and preview context menus are disabled.
- Canvas and output are forced to **1920 × 1080** at every video reset and saved to the isolated profile on startup and after FPS changes. Existing macOS upstream default is already **OpenGL**.
- The main toolbar **FPS** selector offers **23.976, 24, 25, 29.97, 30, 50, 59.94, 60**. Fractional rates use exact `24000/1001`, `30000/1001`, `60000/1001`; every selection saves `FPSType=2`, `FPSNum`, `FPSDen`. Existing common/integer/fraction settings are read, not overwritten at startup; a valid custom rate appears as its rational value. Legacy common `24 NTSC` stays 23.976; common `24` means exactly 24.
- FPS changes call the real `ResetVideo()` and only save after `OBS_VIDEO_SUCCESS` (zero). Reset or safe-save failure restores the prior FPS configuration and resets video back, restores the selector, and shows an error. Failed rollback explicitly asks for restart. The selector is disabled while OBS video/outputs are active and the handler rechecks before making changes. FPS changes do not Fit or edit scene transforms, and do not change the DeckLink input mode.
- First source creation applies native `OBS_BOUNDS_SCALE_INNER`, centered within a 1920 × 1080 bounds box. This works even before source dimensions arrive. Fit explicitly clears rotation/crop and resets position/scale/bounds. Polling, hotplug, mode changes, and restored sources do not write transforms. Bounds naturally preserve aspect ratio when native dimensions change.
- Device list is obtained fresh from `obs_get_source_properties("decklink-input")` / `device_hash`. Disabled saved-device placeholders are excluded. No fake devices, hard-coded device hashes, or hard-coded mode tables.
- Selection is revalidated immediately before use. Native property modified callbacks populate the device's dependent lists; supported saved choices are retained, unsupported mode/video/audio-connection choices fall back to the first enabled native option. Unsupported channel layouts prefer the supported native default (stereo), while explicit None and supported layouts are preserved. Selecting the same device is a no-op, retaining custom HDMI/SDI/mode/audio settings.
- **Device settings…** opens the existing native OBS properties dialog, exposing the plugin's complete current property set, dependencies, cancel/apply behavior, and hotplug refresh. No replacement/simplified property schema.
- A two-second UI timer (no Refresh button) enumerates only: no periodic source update/restart or Fit. The native DeckLink plugin owns disconnect/reconnect activation. Polling stops at shutdown, with no extra held OBS object references.
- Status distinguishes missing plugin/driver, no devices, no selection, disconnected selection, and available device. **Device availability is not signal lock.** Available status reads “Device selected • Local preview”; its tooltip explicitly says input signal is unverified. Disconnected status warns about retained frames. No “Live” claim based on dimensions or a stale last frame.
- App settings/profile/scenes/logs are under `~/Library/Application Support/pixelview/obs-studio/` on macOS. Prefixing the configuration root, including null-name callers, prevents stock OBS legacy migration/reads. Portable paths get their own `config/pixelview` root too. Bundle identifier is handled separately by the build parent.
- No first-run wizard, news/auth onboarding, automatic upstream replacement updates, or default desktop/mic devices. Streaming/backend/login features were not implemented; upstream internals remain compiled for compatibility, but are not part of the product surface.

## TDD and actual verification

Failing tests were run before each production slice (fit policy, configuration isolation, minimal shell, discovery/status policy, native workflow wiring, status width regression, same-device/valid-option regression), then rerun green.

```sh
python3 -m unittest discover -s test/pixelview -v
c++ -std=c++17 -Wall -Wextra -pedantic test/pixelview/test_capture_policy.cpp -o /tmp/pixelview-policy
/tmp/pixelview-policy
git diff --check
```

Final local result: **10 Python tests passed**, standalone C++ capture policy executable passed without compiler warnings, diff whitespace check passed. Configuration tests compile and execute the actual configuration helper. C++ tests execute the actual policy for one-shot fit, reconnect status, same-device no-op, and retaining supported native values. `test_fps.py` compiles and executes the production FPS rate table and transaction (success, active guard, reset failure, save failure, rollback failure). Its native UI wiring test and the other Python UI tests are **source integration contracts**, not Qt interaction or hardware tests; they do not prove compilation or signal capture.

## Encoding addition

The fixed-width left settings toolbar now holds device/FPS controls and video encoder, Mbps bitrate, native profile list and Advanced encoding dialog; the existing preview remains on the right. `OBSBasic_PixelviewEncoding.inc` is included in the existing compilation unit, with `PixelviewEncoding.hpp` policy and compiled extracted-production regression harnesses. See [encoding implementation](pixelview-encoding.md) for actual AdvancedOutput keys, native default/override behavior, transactions and HEVC format mapping. The current suite includes 16 Python tests; the earlier 10-test result above refers to the capture/FPS revision.

## Compact sidebar revision

The left toolbar contains one expanding QWidget and a real QVBoxLayout (16px margins, 12px spacing), so toolbar action layout cannot stretch each button vertically. Device selection is full width; compact Settings… and Fit actions share a left-aligned row. Encoder selection is full width, FPS and bitrate share a row with visible fps/Mbps units, the native Profile selector keeps a short inline label, and Advanced… is a quiet left-aligned action. Controls are 36px high. Normal encoding diagnostics live in tooltips; only an unavailable-encoder warning occupies panel space. The preview, immediate/manual FPS transaction, two-second automatic enumeration, device status and explicit Fit semantics are unchanged.

The header embeds `:/res/images/pixelview-wordmark.png` through `frontend/forms/obs.qrc`, rendered at approximately 200×25 logical pixels with device-pixel-ratio-aware scaling, followed only by **Desktop**. The supplied asset is the authentic `pxlview/pv-home` wordmark, not recreated typography. Parent-provided provenance: commit `8092872f34e5db3f043a9fc100bdecde7690a740`, `src/assets/media/logo-light.png`; original SHA256 `2b2ac499820e5cc981755debedc0fa53e43fc9fe8263d38d9dab553850367413`. The original is preserved at `frontend/data/images/pixelview-logo-light.png`; `pixelview-wordmark.png` only trims transparent margins (3317×411). Existing Pixelview text is the resource-load fallback. Executable, bundle, configuration paths and app identity are unchanged.

Property form labels and button text are left aligned for the Pixelview device and encoder dialogs, including native refreshes. One opt-in `OBSPropertiesView::PropertiesAboutToRefresh` signal permits instance-local filtering before each rendering pass, after native modified callbacks or reload. No plugin property objects or callbacks are removed/replaced, and other properties-view instances keep their upstream behavior. B-frame policy and regression coverage are described in [encoding preferences](pixelview-encoding.md).

The initial compact-sidebar source handoff passed 20 Python tests. The completed revision passes **27 tests**, including native-parser-backed NVENC/AMF safeguards, compiled production B-frame normalization, callback-preserving visibility filtering, and footer/custom-FPS contracts. Build **19** was compiled, signed, independently reviewed, and exercised in the native app. At 900px minimum width all controls remain visible; their measured heights are consistently 36px after polishing the theme before fixing geometry. The footer uses one expanding left-aligned label to avoid competing labels wrapping into narrow columns. Startup normalization, native Advanced callback refresh/Cancel, immediate FPS and Mbps edits, restart persistence, and unchanged capture settings/transforms were verified. See the acceptance report for evidence and limits; headless tests alone are not GUI/hardware verification.

## Runtime acceptance

The current prototype was compiled and exercised against the connected UltraStudio Recorder 3G. See [local verification](pixelview-acceptance.md) for the exact hardware/UI evidence and remaining tests. Fixture-based missing-device and non-HD geometry checks are distinguished there from actual hardware checks.

Prototype limitations: signal-lock telemetry is deliberately not implemented; user-visible strings are English; Fit is an explicit reset but has no newly-added undo transaction; native properties remain a separate native dialog rather than an embedded inspector. General OBS internals are retained, not a security boundary against arbitrary external plugins or edited scene/config files. No commit or push performed.
