# Developer ID signing

Development signing uses `pixelview-build.sh`. Customer releases must use the fail-closed `release/pixelview-macos.sh` 1Password entrypoint documented in [Pixelview Desktop macOS updates and releases](pixelview-macos-updates-and-releases.md); that path additionally requires an arm64 Release build, Pixelview Sparkle metadata, Apple notarization, stapling, Gatekeeper assessment, provenance, and a signed appcast.

The local build helper supports explicit opt-in signing through upstream OBS/Xcode settings:

```sh
PIXELVIEW_CODESIGN_IDENTITY='Developer ID Application: Your Organization (TEAMID1234)' \
PIXELVIEW_CODESIGN_TEAM='TEAMID1234' \
bash cmake/macos/pixelview-build.sh
```

A 40-character certificate SHA-1 fingerprint is also accepted as the identity. These inputs select an installed signing identity; no private keys or machine-specific identities are stored in source. Xcode resolves the actual certificate; the helper validates input shape, not the certificate's class or team membership. Verify the resulting authority and team before distribution.

Signed builds default to `build_macos_developer_id/frontend/RelWithDebInfo/Pixelview.app`, separate from the running development app in `build_macos`. `PIXELVIEW_BUILD_DIR` overrides the build directory for either mode; choose a new directory and never target a running app's build directory. `DEVELOPER_DIR` selects Xcode for this invocation only. With no Pixelview signing identity/team, the helper explicitly selects ad-hoc signing and clears inherited upstream signing/provisioning inputs. A team without an identity is rejected instead of triggering automatic Apple Development signing.

## Native signing path

`cmake/macos/xcode.cmake` selects manual signing from `OBS_CODESIGN_IDENTITY` and `OBS_CODESIGN_TEAM`. In RelWithDebInfo it enables hardened runtime, secure timestamps and disables injected debugging entitlements. `cmake/macos/helpers.cmake` signs embedded frameworks/plugins on copy and arranges dependencies before the outer application's final signature. The existing frontend entitlements are unchanged. The helper does not call `codesign --deep` to sign, access Keychain item data, alter ACLs, disable Gatekeeper or perform notarization.

If macOS requests private-key authorization, the user must handle the OS prompt personally. Do not automate its approval or change Keychain access controls as a workaround.

Verify a completed artifact before launching or distributing:

```sh
app=build_macos_developer_id/frontend/RelWithDebInfo/Pixelview.app
codesign --verify --deep --strict --verbose=2 "$app"
codesign -d --verbose=4 "$app"
codesign -d -r- "$app"
codesign -d --entitlements :- "$app"
```

Require Developer ID Application authority, the expected TeamIdentifier, `runtime` flags, a secure timestamp, and a designated requirement with an Apple anchor and team certificate condition rather than an ad-hoc cdhash-only requirement. Inspect every embedded Mach-O/framework/plugin too. Passing code-signature validation does not establish notarization or Gatekeeper acceptance.

## Verified local build

The full Pixelview suite passes **129 tests**, including the Xcode-processed branding/plist check, native SDK/deployment regression, release/updater contracts, and signing-helper tests (plus existing disposable Keychain/loopback fixtures, not the real pairing account). `bash -n` and `git diff --check` passed.

The Xcode 26.6 / SDK 26.5 compilation blocker was reproduced with the existing `test-spatial-aq.c` and `-mmacosx-version-min=13.0`. The legacy `MAC_OS_X_VERSION_MAX_ALLOWED` resolves to 140000 at that target, whereas `__MAC_OS_X_VERSION_MAX_ALLOWED` correctly reflects SDK 26.5. The narrow `vt-compat.h` fix includes `Availability.h` and uses the SDK maximum for both declaration branches. It preserves the runtime macOS 15 availability gate and old-SDK NULL-safe `dlsym` fallback.

The new regression failed on duplicate enums before the fix and passed afterward against SDK 26.5 and the installed Command Line Tools SDK 14.2. SDK 14.4 is not installed; its pre-macOS-15 fallback is preserved, but a fresh SDK 14.4 execution is not claimed. The test enables `-Werror=unguarded-availability` and checks the actual framework export at runtime.

The isolated Developer ID build completed. The resulting app passed deep/strict code-signature verification and reports Developer ID Application authority, the expected team, hardened runtime, a secure timestamp and an Apple/team designated requirement. The signed app was launched and its capture preview rendered. Notarization and Gatekeeper release acceptance remain unverified; signing is not notarization.

For first-time private-key authorization, use a single signing request before a multi-target build, or set `BUILD_JOBS=1` to avoid stacked prompts. After an interrupted signing run, inspect for generated `.cstemp` leftovers and remove only those stale temporary files when no signing process is active before retrying. Never remove credentials or weaken Keychain access controls to fix a build.

Ignored local evidence: `.runtime/developer-id-build-report.json`, `.runtime/developer-id-build.log`, `.runtime/developer-id-tests.log`, `.runtime/developer-id-review.log`, `.runtime/vt-sdk-test-red.log`, `.runtime/vt-sdk-test-green.log`, `.runtime/vt-sdk-test-legacy-green.log`, `.runtime/sdk-macros.txt`.
