# Application identity and icons

## Verified macOS build 23

The final Xcode-processed bundle has CFBundleName and CFBundleDisplayName = Pixelview Desktop, while executable/bundle ID remain Pixelview/com.pixelview.desktop. Native NSRunningApplication readback confirms Pixelview Desktop. Both the full COPYING resource and root pixelview-app.icns match their source bytes; deep strict signing passes. The app icon uses authentic enlarged play artwork on OBS sidebar gray #1D1F26; transparent macOS template tray states are separate. Native application-icon readback and connection-error alert show the dark asset. The macOS custom menu is Help, avoiding a duplicate native application-name menu. Windows/Linux resources are source-tested, not runtime-tested.


The display name is **Pixelview Desktop**. macOS builds **`Pixelview Desktop.app`**, with executable **`Contents/MacOS/Pixelview Desktop`** and unchanged bundle identifier `com.pixelview.desktop`. Windows/Linux retain the **Pixelview** executable (`Pixelview.exe` on Windows). The filename change does not change the isolated Pixelview configuration paths, Keychain service/account identities, or capture/encoding settings.

## Consumers

- `frontend/OBSApp.cpp`: Qt application display name and desktop-file identity. Non-macOS application/window icons load the embedded `pixelview-app.png` directly, preventing an installed OBS icon theme from overriding the brand. macOS keeps the native bundle icon for the Dock.
- `frontend/widgets/OBSBasic.cpp`: title bar, application menu and Quit action read Pixelview Desktop. The existing authentic sidebar wordmark remains unchanged.
- `frontend/forms/OBSBasic.ui` and `OBSPermissions.ui`: explicit designer window icons use Pixelview rather than overriding the application default with OBS. Changing the dormant permissions form's icon does **not** re-enable startup onboarding; `obs-main.cpp` retains the suppression.
- Stats/projector windows use the same app icon. System-tray creation, activation/deactivation, pause/unpause, tooltip and notifications use Pixelview resources/name. Existing state transitions are unchanged. macOS template icons retain `setIsMask(true)`; idle/active/paused require different alpha silhouettes, not just different colors.
- `frontend/forms/obs.qrc`: embeds dedicated application and six tray PNGs from `frontend/data/images/`. Upstream OBS images remain available for attribution/about content; they are not overwritten or globally renamed.

## Native packaging

### macOS

`cmake/macos/helpers.cmake` sets `OUTPUT_NAME "Pixelview Desktop"` and `PRODUCT_NAME "Pixelview Desktop"`, retaining `PRODUCT_BUNDLE_IDENTIFIER com.pixelview.desktop`. Both `CFBundleDisplayName` and `CFBundleName` are Pixelview Desktop. `frontend/cmake/macos/Info.plist.in` also declares these names and `CFBundleIconFile=pixelview-app.icns`.

The dedicated `frontend/data/images/pixelview-app.icns` is copied to `Contents/Resources/pixelview-app.icns`. The generic recursive data installer explicitly skips that file so it cannot relocate the resource into `Resources/images`. The old `Assets.xcassets` is no longer compiled into the frontend and `ASSETCATALOG_COMPILER_APPICON_NAME` is removed; therefore an upstream asset-catalog icon cannot override the explicit ICNS. Upstream assets remain untouched on disk.

### Windows

`frontend/cmake/windows/obs.rc.in` references the dedicated `pixelview-app.ico` using its configured absolute source path. FileDescription/ProductName are Pixelview Desktop; InternalName is Pixelview and OriginalFilename is Pixelview.exe. LegalCopyright remains the upstream value. The application manifest description is branded without changing requested privileges. The common frontend CMake output name is Pixelview on every platform.

This covers the application executable, not a new distribution/updater product: the separate upstream updater and installer/CI infrastructure retain upstream names and must not be used to distribute or update Pixelview without a separate packaging audit. Pixelview's existing automatic-update suppression remains unchanged.

### Linux/BSD

Both install `com.pixelview.desktop.desktop`, matching Qt's desktop-file name, with `Name=Pixelview Desktop`, `Exec=Pixelview`, `StartupWMClass=Pixelview`, and `Icon=com.pixelview.desktop`. The provided 1024-square application PNG installs as `icons/hicolor/1024x1024/apps/com.pixelview.desktop.png`. Dedicated AppStream metadata describes the preview fork and retains GPL/OBS attribution rather than publishing stock OBS product metadata under a different app name. Existing upstream launcher/artwork files remain in source but are not installed by these frontend rules.

## Verification

Run `python3 test/pixelview/test_app_branding.py -v` from the repository for the branding checks. On macOS, the dependency-free Xcode mini-build uses the production branding properties, plist template and plugin-embedding block. It verifies the real `Pixelview Desktop.app` filename, processed plist, executable, generated target paths, embedded plugin path, and installation into a separate prefix; the installed fixture executable must exit successfully. These checks pass alongside the release and build-signing regression suites. They do not build or sign the full application, or establish rendered icon quality or hardware behavior.

Before release, rebuild/reconfigure the current source and check the final bundle's plist, ICNS placement, code signature, Dock hover/app menu/title and every tray state. Old Xcode outputs can retain obsolete Assets.car/AppIcon resources; use a clean application product when verifying. Windows and Linux/BSD native execution/packaging must be checked on their platforms. Do not mistake cached Dock/launcher artwork from an old running build for source verification.
