# Pixelview Desktop: build, test and release runbook (macOS, Apple Silicon)

This runbook covers the toolchain, the canonical local Developer ID build and launch, the test suites, the local-only signed/notarized release pipeline with Sparkle updates and Cloudflare R2 publication, and the GPL distribution obligations that gate any public release. The inherited OBS Studio `README.rst`, `CONTRIBUTING.md` and `CODESTYLE.md` stay unchanged for upstream attribution and rebasing (the release contract test pins the `README.rst` hash).

## 1. Toolchain and dependencies

- **Xcode / SDK.** Upstream OBS requires Xcode 26.5 / macOS SDK 26.5 (`cmake/macos/compilerconfig.cmake`). `cmake/macos/pixelview-build.sh` always passes `-DPIXELVIEW_LEGACY_TOOLCHAIN=ON`, lowering the floor to Xcode 15.3 / SDK 14.4 and excluding the Metal renderer (`libobs-metal`, needs Swift 6; OpenGL is used) and `mac-avcapture` (uses `AVCaptureDevice.backgroundReplacementActive`, absent in SDK 14.4). Native `decklink`, `mac-videotoolbox` and `obs-webrtc` (WHIP) stay enabled. `DEVELOPER_DIR` defaults to `/Applications/Xcode.app/Contents/Developer`; global `xcode-select` is untouched.
- **Disabled upstream modules.** Browser/CEF, What's New, the inbound obs-websocket plugin, scripting, virtual camera, AJA, VST, Syphon, VLC. The outbound Desktop control socket uses NSURLSession, not obs-websocket.
- **obs-deps and Qt.** `CMakePresets.json` pins pre-built obs-deps `2026-08-26` and, for `macos-universal`, the Qt6 bundle `2026-05-21` (deliberate Qt compatibility pin) with SHA-256 hashes; CMake downloads and verifies them into `.deps/`, and existing pinned `.deps` are reused on every build.
- **GStreamer runtime (WHEP receiver).** `plugins/pixelview-whep` embeds the official GStreamer `1.28.3` macOS packages plus a source-built, patched `rswebrtc 0.15.2`, pinned by `plugins/pixelview-whep/runtime-lock.json`. The build helper runs these before CMake on every build (no Homebrew/host GStreamer):

  ```sh
  python3 plugins/pixelview-whep/scripts/fetch-gstreamer.py
  python3 plugins/pixelview-whep/scripts/build-rswebrtc.py
  python3 plugins/pixelview-whep/scripts/bundle-runtime.py stage .deps/pixelview-gstreamer
  ```

  SDK: `.deps/gstreamer-upstream-1.28.3/sdk`; staged runtime `.deps/pixelview-gstreamer`, embedded in `pixelview-whep.plugin/Contents/Resources/GStreamer`.
- **Target.** arm64 only, deployment target macOS 14.0, Xcode generator, CMake preset `macos`. Bundle ID `com.pixelview.desktop`; executable `Pixelview Desktop`.
- **Release-only tools.** `git cmake codesign hdiutil security shasum spctl xcrun curl python3`, the AWS CLI (`aws`) for R2, the 1Password CLI (`op`).

## 2. Local development build and launch

### 2.1 Build gate

`cmake/macos/pixelview-build.sh` is the only local build entry point. Unless `PIXELVIEW_RELEASE_BUILD=ON` (release path) or the private recursion marker is set, it `exec`s `cmake/macos/pixelview-signed-development.py` with the same arguments. That helper requires macOS and your normal login `HOME` (it strips `CFFIXED_USER_HOME`); refuses a symlinked `build_macos` or any process running from it; resolves exactly one valid **Developer ID Application** identity for team `MA47F3M8W9` (`release/macos.json`), default SHA-1 `62CC493EC11F0031CF3ED9419671A78F5FD8E584` (Cinecode OU) — `--identity` / `PIXELVIEW_CODESIGN_IDENTITY` may override but must still resolve uniquely for that team, with no ad-hoc or Apple Development fallback; reads default-Keychain metadata only (interactive builds warn if it reports locked and let codesign request native authorization; `--unattended` fails closed; nothing unlocks a Keychain, changes ACLs or approves prompts); requires 6 GiB free disk, `--source-settled` (your promise that all source-editing workers are done) and `--allow-dirty` for a dirty tree.

It fingerprints every tracked and non-ignored untracked file (name, mode, bytes) before the build, after it and after verification. **Editing any repository file during a build, including tests or docs, fails the run with "Source changed during ..." and rejects the artifact.** Draft edits outside the repository while a build runs.

The shell helper then runs with a fixed environment (identity SHA-1, team, `PIXELVIEW_BUILD_DIR=build_macos`, `RelWithDebInfo`, empty Sparkle URL/key, release OFF, universal links OFF, empty `PIXELVIEW_SOURCE_TAG`). Verification covers `codesign --verify --deep --strict`, bundle ID, no `SUFeedURL`, the Apple/team/Developer ID designated requirement, hardened runtime, every nested Mach-O signed by the same authority with no `get-task-allow`, root entitlements equal to `frontend/cmake/macos/entitlements.plist`, arm64, and `--version` containing `Pixelview Desktop`. The report is `build_macos/signed-development-verification.json`; a failed or interrupted run deletes any previous report.

```sh
cd /Users/max/src/pixelview-desktop
unset CFFIXED_USER_HOME
bash cmake/macos/pixelview-build.sh --check-only               # metadata preflight; no build/signing
bash cmake/macos/pixelview-build.sh --check-only --unattended  # fail closed on locked Keychain metadata
BUILD_JOBS=1 bash cmake/macos/pixelview-build.sh --allow-dirty --source-settled
```

`BUILD_JOBS` sets `cmake --build -j` (default 8); use `1` for first-time private-key authorization so prompts do not stack (10–15 min after a header change). Output is always `build_macos/frontend/RelWithDebInfo/Pixelview Desktop.app`; `PIXELVIEW_BUILD_DIR`/`PIXELVIEW_BUILD_CONFIG` cannot redirect a local build. The build never installs to `/Applications`, notarizes, publishes, commits or restarts an app. When CMake options change, rerun the full helper (Xcode's `ZERO_CHECK` does not reliably regenerate the plugin target list). Do not set `PIXELVIEW_LOCAL_SIGNING_VERIFIED=1` to bypass the gate. After an interrupted signing run, remove only stale `.cstemp` files while no signing process is active; never delete credentials or weaken Keychain ACLs.

### 2.2 Launch

`cmake/macos/pixelview-launch.py` requires the verification report for the canonical app, pinned identity and team, re-verifies the bundle against it, and `execve`s `Contents/MacOS/Pixelview Desktop` with normal HOME and no `CFFIXED_USER_HOME`. Wait a moment after the build exits; the report is written last.

```sh
python3 cmake/macos/pixelview-launch.py --check-only   # verify and print the command only
python3 cmake/macos/pixelview-launch.py                # new pairings use https://api4.pixelview.io
PIXELVIEW_LOCAL_DEVELOPMENT=1 python3 cmake/macos/pixelview-launch.py   # new pairings use http://localhost:8000
python3 cmake/macos/pixelview-launch.py --app-config-dir /Users/max/src/pixelview-hardware-25p/sender-config
python3 cmake/macos/pixelview-launch.py --app-config-dir /Users/max/src/pixelview-hardware-25p/receiver-config
```

- Only the exact value `PIXELVIEW_LOCAL_DEVELOPMENT=1` selects localhost; saved pairings are never retargeted by the environment — Unpair first. Keychain accounts are keyed by exact origin (`localhost` ≠ `127.0.0.1`).
- `--app-config-dir` (absolute path) adds `--multi --app-config-dir <root>`; it isolates application files, not Keychain identities. Never run two writers against one root. `--migrate-from '<old>/Library/Application Support/pixelview' --app-config-dir <new-root>` copies only allowlisted video/audio `basic.ini` settings into a new root and exits without launching.

### 2.3 Settings, logs, sentinel

- Settings root: `~/Library/Application Support/pixelview/obs-studio/` (stock OBS configuration is never read or replaced).
- Logs: `~/Library/Application Support/pixelview/obs-studio/logs/`. A clean shutdown ends with `==== Shutting down`, `Freeing OBS context data` without "source(s) were remaining", and `Number of memory leaks`.
- Unclean-shutdown sentinel: `~/Library/Application Support/pixelview/obs-studio/.sentinel/` (`run_*` files). Stale zero-byte entries block the next launch on a modal "did not shut down properly" dialog before any log output; delete them first. A blocked instance ignores SIGTERM and needs `pkill -9`.
- Crash reports: `~/Library/Logs/DiagnosticReports/Pixelview Desktop-*.ips`. `kill -TERM <pid>` exercises the normal close path.

Manual inspection: `codesign --verify --deep --strict --verbose=2 "$app"`, `codesign -d --verbose=4 "$app"`, `codesign -d -r- "$app"`, `codesign -d --entitlements :- "$app"`. Expect Developer ID Application authority, `TeamIdentifier=MA47F3M8W9`, `runtime` flags, a secure timestamp and an Apple-anchored designated requirement. A valid signature is not notarization or Gatekeeper acceptance.

## 3. Tests

### 3.1 Pixelview Python suite

```sh
python3 -m unittest discover -s test/pixelview
python3 -m unittest discover -s test/pixelview -p test_receive_ui.py   # one module; dotted form fails at import
```

About 7 minutes. Two failures are environmental: `test_icon_assets` (needs PIL; `uv run --with pillow python -m unittest ...` provides it) and `test_pixelview_sources` (corresponding-source size gate). Signing/release helpers also have focused checks:

```sh
python3 cmake/macos/test_signed_development.py
python3 -m unittest discover -s test/pixelview -p test_build_signing.py
python3 -m unittest discover -s test/pixelview -p test_pixelview_release.py
bash -n cmake/macos/pixelview-build.sh cmake/macos/pixelview-release.sh
bash cmake/macos/pixelview-release.sh --validate-config
```

### 3.2 Plugin offline suites

```sh
python3 plugins/decklink/tests/run-receive.py          # ~3 min; needs build_macos_native422 frameworks
python3 plugins/pixelview-whep/tests/test_packaging.py
python3 plugins/pixelview-whep/tests/run-native.py
python3 plugins/pixelview-whep/tests/run-codecs.py
python3 plugins/pixelview-whep/scripts/bundle-runtime.py verify .deps/pixelview-gstreamer
python3 plugins/pixelview-whep/scripts/build-rswebrtc.py --test --unpatched   # expected failure
python3 plugins/pixelview-whep/scripts/build-rswebrtc.py --test              # expected success
```

Route/filter changes: `plugins/pixelview-whep/tests/run-ordinary-route.py` (fast) and `run-native-422.py` (~2 min, `-Werror`). The DeckLink feed fixture decodes a Main 4:2:2 clip, so `run-receive.py` fails while `plugins/pixelview-whep/main422-25p.h` returns FALSE; use a scratch copy of the runner adding `-DPIXELVIEW_MAIN422_25P_H -include <header returning TRUE>`.

### 3.3 Live control-plane smoke tool (no media)

```sh
clang++ -std=c++17 -fobjc-arc -fPIC -I. \
  -F.deps/obs-deps-qt6-2026-08-26-universal/lib \
  -framework QtCore -framework QtNetwork -framework Foundation -framework Security \
  -Wl,-rpath,"$PWD/.deps/obs-deps-qt6-2026-08-26-universal/lib" \
  frontend/utility/PixelviewDesktopMac.mm test/pixelview/desktop_backend_smoke.mm \
  -o /tmp/pixelview-desktop-backend-smoke
/tmp/pixelview-desktop-backend-smoke http://localhost:8000 < /private/path/to/one-time-code
```

Use a disposable node device and a mode-0600 input file. The harness deletes the origin's Keychain credential afterward unless `--keep-device` is given, so target `127.0.0.1` rather than the app's `localhost` origin. Not a TLS or media-delivery test.

### 3.4 CI

`.github/workflows/ci.yaml` (`contents: read`, `fetch-depth: 0` so `git describe` can check the pinned OBS commit) runs only `python3 test/pixelview/test_pixelview_release.py -v`, `python3 test/pixelview/test_pixelview_license.py -v`, and `bash -n` on `cmake/macos/pixelview-build.sh` and `cmake/macos/pixelview-release.sh`. CI cannot sign, notarize, upload or publish; the contract test asserts `ci.yaml` is the only workflow.

## 4. Release

Local-only, arm64-only, one stable Sparkle feed (`https://downloads.pixelview.io/desktop/macos/appcast-arm64.xml`), no beta channel. Create no tag, GitHub release, notarized DMG, R2 upload or public appcast before the app is accepted and the compliance gate (4.7) is cleared.

### 4.1 `version.json`

Current: `pixelview_version` `0.0.1`, `pixelview_build_number` `1`, `obs_base_version` `32.2.1`, `obs_base_describe` `32.2.1-66-g6b3e55072`, `obs_base_commit` `6b3e550729f125b6c5b3767df88c08f5aef9d264`.

- Every public release gets a new semantic product version **and** a build number above every published build. The validator rejects a republished marketing version even with a new build, and any non-increasing build.
- The OBS base is pinned independently of the product version. When rebasing, set all three `obs_base_*` fields to the exact upstream base; `obs_base_describe` must equal `git describe --tags --long <obs_base_commit>` and the commit must be an ancestor of the release commit (both checked).
- The tag is always `v<pixelview_version>` and must point at `HEAD`. `cmake/common/pixelview-version.cmake` feeds these values into `CFBundleShortVersionString`/`CFBundleVersion` and the `PixelviewSourceCommit`, `PixelviewSourceTag` and `PixelviewOBSBase*` plist keys.

### 4.2 Release notes and tag

1. Add `docs/releases/<version>.html` (script default; `--release-notes F` overrides) and `docs/releases/<version>.md` for the GitHub release text.
2. Commit everything; `git status --porcelain` must be empty.
3. `git tag -a v0.0.1 -m 'Pixelview Desktop 0.0.1'`

### 4.3 Credentials and key ownership

`release/pixelview-macos.sh` wraps `cmake/macos/pixelview-release.sh` with `op run`, injecting only what the phase needs (1Password prompts for biometric/system verification when locked); the app never receives them.

| File | Injected for | Content |
|---|---|---|
| `release/macos.env` | every phase | `PIXELVIEW_CODESIGN_IDENTITY="Developer ID Application: Cinecode OU (MA47F3M8W9)"`, `PIXELVIEW_CODESIGN_TEAM=MA47F3M8W9`, `PIXELVIEW_R2_BUCKET=pixelview-desktop-releases` |
| `release/macos-notary.1password.env` | `--prepare`, `--all` | `op://pixelview-desktop/pixelview-desktop-notarization/{key_id,issuer_id,private_key}` (App Store Connect API key; `.p8` goes to a mode-0600 temp file for `notarytool`, removed on exit) |
| `release/macos-r2.1password.env` | `--publish`, `--all` | `op://pixelview-desktop/pixelview-desktop-releases-r2-bucket/{endpoint,access_key_id,secret_access_key}` |

- **Developer ID.** Certificate and private key must be in a macOS Keychain; the script resolves exactly one Developer ID Application certificate for the team by fingerprint and verifies the signed app's leaf SHA-1. Back it up in 1Password as `pixelview-desktop-developer-id` (password-protected `.p12`, `p12_password`, `team_id`, `certificate_sha1`).
- **Sparkle.** Public key `k1+OJc59i2HxlsfpR/lS8Yv4iU1RttFDMYzahG/N0lw=` is committed in `release/macos.json`. The operational Ed25519 private key lives in the login Keychain under account `com.pixelview.desktop`; the script checks `generate_keys --account com.pixelview.desktop -p` matches. Before the first public release, export one backup with `generate_keys --account com.pixelview.desktop -x ...` into 1Password item `pixelview-desktop-sparkle-signing` (`account`, `public_key`, concealed `private_key`) and securely remove the export. Losing the key orphans every installed copy; leaking it lets anyone sign updates. It is a recovery copy, never a routine environment variable.
- Sparkle `2.9.2` tools are cached in `.runtime/release-tools/` and verified against the archive and per-tool SHA-256 pins in `release/macos.json`. Direct `cmake/macos/pixelview-release.sh` invocation may use `PIXELVIEW_NOTARY_PROFILE=<notarytool keychain profile>` instead of the API key.
- Never commit: Developer ID key, notarytool credentials, Sparkle private key, R2 keys, `dist/`, `.runtime/`.

### 4.4 Phases

```sh
release/pixelview-macos.sh --validate-config   # public metadata; no credentials
release/pixelview-macos.sh --prepare           # build, sign, notarize, staple, appcast; no upload
release/pixelview-macos.sh --publish           # upload prepared assets to R2, appcast, then latest/
release/pixelview-macos.sh --publish-latest    # only re-point latest/ at the prepared, already public release
release/pixelview-macos.sh --all               # prepare then publish; tag must already be pushed
```

`--validate-config` prints `Pixelview Desktop <version> (build <n>)`, OBS base and target, and checks `version.json` shapes, the `git describe` pin and the fixed `release/macos.json` values (arm64, appcast/download URLs, `source_repository` `https://github.com/pxlview/pixelview-desktop`, key/checksum formats, `r2_prefix` `desktop/macos`). It is not a readiness check.

`--prepare`, in order: take `dist/macos/.release.lock`; require a clean tree, `v<version>` at `HEAD` and the OBS base as an ancestor; regenerate the compliance stage (4.7) **before** any credential; resolve the identity, require the notes file, verify Sparkle tools and the Keychain Sparkle key, check notarytool credentials; delete `dist/macos/releases/<version>-<build>`, the staged appcast and `build_macos_release_<version>_<build>`; run `pixelview-build.sh` with `PIXELVIEW_RELEASE_BUILD=ON`, `PIXELVIEW_BUILD_CONFIG=Release`, the production appcast URL/public key, `PIXELVIEW_SOURCE_TAG=v<version>` and `PIXELVIEW_LICENSE_DATA_DIR` (Sparkle is compiled in only here); re-sign Sparkle's ad-hoc helpers (`Downloader.xpc`, `Installer.xpc`, `Autoupdate`, `Updater.app`, then the framework and the app) with the Developer ID and a secure timestamp, since Apple's notary rejects them as shipped; verify the app (deep/strict signature, bundle ID, all version/provenance plist keys, `SUFeedURL`, `SUPublicEDKey`, `Sparkle.framework`, arm64-only, team, certificate fingerprint, installed license assets byte-identical to regenerated ones, no embedded `obsproject.com/(osx_update|update_studio)`); stage the DMG (`Pixelview Desktop.app` and the `Applications` symlink at the top level; `COPYING`, `AUTHORS`, `THIRD-PARTY-NOTICES.txt` and `RELEASE.txt` inside a `Licenses` folder) and build it with `cmake/macos/pixelview-dmg.sh` (background, Finder-recorded install window layout, UDZO) as `Pixelview-Desktop-<version>-build<n>-arm64.dmg` — Finder does the layout, so the terminal needs Automation permission for Finder and the release volume name must not already be mounted; sign with timestamp; `notarytool submit --wait` (`notarization.json`; any status other than `Accepted` aborts and saves `notarization-log.json` with Apple's findings); staple, validate, `spctl --assess --type open`; copy the notes to `...-arm64.html`; run `generate_appcast --maximum-versions 10 --maximum-deltas 0` into `dist/macos/appcast-arm64.xml`; write `.sha256`, copy the three compliance artifacts, write `release-manifest.json`; then re-validate everything (`pixelview_release_validate.py`, DMG signature, stapler, Gatekeeper, appcast/enclosure/notes Ed25519 signatures, build progression, mounted app).

Build progression: the public appcast must return HTTP 200 with a valid signature and a lower build, or HTTP 404 (only `0.0.1` build `1` may initialize the feed). Any other status or a network error aborts.

### 4.5 R2 layout and immutability

`PIXELVIEW_R2_ENDPOINT` must match `https://<32 hex>.r2.cloudflarestorage.com`. Bucket `pixelview-desktop-releases`, served at `https://downloads.pixelview.io`:

```text
desktop/macos/
  appcast-arm64.xml                              Cache-Control: no-cache, max-age=0, must-revalidate
  latest/Pixelview-Desktop-arm64.dmg             mutable server-side copy of the current release DMG (no-cache)
  latest/latest.json                             version, build, tag, commit, sha256, size, immutable URL (no-cache)
  releases/<version>-<build>/                    Cache-Control: public,max-age=31536000,immutable
    Pixelview-Desktop-<version>-build<n>-arm64.dmg
    Pixelview-Desktop-<version>-build<n>-arm64.dmg.sha256
    Pixelview-Desktop-<version>-build<n>-arm64.html
    release-manifest.json
    Pixelview-Desktop-<version>-<build>-sources.tar.gz
    Pixelview-Desktop-<version>-<build>-NOTICES.txt
    Pixelview-Desktop-<version>-<build>-source-inventory.json
```

`--publish` re-verifies the Sparkle key and the whole prepared release, checks that `refs/tags/v<version>` on the canonical GitHub repository equals `HEAD`, establishes the appcast precondition (`If-Match: <ETag>` after validating the current feed, or `If-None-Match: *` on 404), then HEADs every immutable key: byte-identical existing objects are accepted as a retry; any differing object aborts before the first write. Immutable objects are PUT with `If-None-Match: *` (SigV4 `curl`), read back through the public domain and compared byte-for-byte; only then is `appcast-arm64.xml` PUT and read back. Never overwrite or delete a published `releases/<version>-<build>/` object.

### 4.6 Publishing order and acceptance

1. Push source and tag, then create the GitHub release for the tag (draft until acceptance; binaries stay on R2):

   ```sh
   git push origin HEAD
   git push origin v0.0.1
   ```

2. `release/pixelview-macos.sh --prepare`; approve the 1Password prompt.
3. Clean/quarantined-Mac acceptance with the prepared DMG: Gatekeeper accepts it; drag to Applications; launch; verify capture, pairing, WHIP streaming, audio monitoring and retained configuration.
4. `release/pixelview-macos.sh --publish`; approve the 1Password prompt.
5. Download the DMG through the public domain on a clean Mac and repeat step 3.
6. Update cycle: publish an older internal build and a higher-build replacement on a temporary staging feed (not a permanent beta channel); verify **Help → Check for Updates…** downloads, replaces, relaunches and preserves configuration and Keychain state.
7. Confirm the appcast and DMG expose no secret and the manifest commit/tag matches GitHub. Publish the GitHub release and announce only after every check passes; the website download button can point permanently at `https://downloads.pixelview.io/desktop/macos/latest/Pixelview-Desktop-arm64.dmg` (updated by publish, after the appcast; `--publish-latest` re-points it for a prepared release that is already public), and `latest/latest.json` gives the version, checksum and immutable URL for display.

The repository cannot create the R2 bucket/domain/token, the App Store Connect API key, the GitHub tag/release or the acceptance Mac; those are operator steps.

### 4.7 Corresponding source

The project is public at `https://github.com/pxlview/pixelview-desktop` and every release is an annotated tag there; that is the primary corresponding-source offer, and the in-app License dialog names it. In addition, preparation and publication run the source packager, which ships a sources tarball, a NOTICES file and the resolved inventory next to each DMG. The offline form is:

```sh
python3 cmake/macos/pixelview_sources.py \
  --root "$PWD" --tag v0.0.1 \
  --cache "$HOME/Library/Caches/pixelview-sources" \
  --output /tmp/pixelview-source-check \
  --release-id 0.0.1-1 \
  --base-url https://downloads.pixelview.io/desktop/macos
```

(`PIXELVIEW_SOURCE_CACHE` overrides the cache; keep it for publication revalidation.) It reads the tracked `release/source-inventory.json` at the exact clean tag, packs the complete project tree plus every inventoried component source archive into the sources tarball, writes the NOTICES file and resolved inventory, and writes `license/third-party-notices.txt` and `license/source-manifest.json`, which the build installs under `Contents/Resources/license/` before signing. `release-manifest.json.compliance` records `sources`, `notices` and `inventory` (name, SHA-256, size, immutable URL). Missing, tampered, symlinked or unsafe inputs, or a dirty/untagged tree, fail the packager.

`release/source-inventory.json` also carries a `review` block. It is informational: it records open points about third-party component materials (FFmpeg GPLv3 terms, Qt and GStreamer module sources and recipes, Cargo dependency notices, Sparkle/Blackmagic SDK obligations) for follow-up and never gates a build or a publish.

## 5. Distribution licensing obligations

- OBS code is **GPL-2.0-or-later** (`README.rst`, source headers); the bundled FFmpeg is GPL-3.0-or-later, so the shipped combination's terms must be settled before release. Preserve the upstream grant, `COPYING` and `AUTHORS`; add Pixelview attribution separately.
- **Corresponding source** (GPLv2 §3): every binary release ships the complete preferred editable source actually used (Pixelview changes, required modules and submodule contents, interface definitions, build/install scripts, dependency versions, patches, provenance). A link to upstream OBS or a moving branch is not sufficient. The pipeline publishes the sources tarball beside the DMG under the same immutable URL and records it in the manifest and the in-app `source-manifest.json`; offline delivery needs a §3(a) copy or a genuine three-year §3(b) written offer.
- **Notices**: ship the unmodified root `COPYING` (the in-app `frontend/data/license/gplv2.txt` is a reflowed copy ending at §12, not a substitute), `AUTHORS`, and complete third-party notices for every bundled framework, library, plugin, codec, font, updater and SDK at its exact version (`THIRD-PARTY-NOTICES.txt` in the DMG, `third-party-notices.txt` in the app).
- **Modification notices** (§2(a)): modified files carry prominent change notices with dates without deleting upstream headers; a changelog only supplements them. **Recipient rights** (§§2(b), 6): no EULA, NDA, non-commercial or no-redistribution term may restrict GPL rights; charging for copies is fine.
- **Branding**: "OBS", "OBS Studio", "Open Broadcaster Software" and the logo are registered trademarks of Wizards of OBS LLC. Use Pixelview branding and state that the app is a modified distribution based on OBS Studio, not an official OBS Project release; GPL permission is not trademark clearance.
- **Proprietary SDKs/drivers** (DeckLink and similar): check vendor redistribution rights and the narrow §3 system-component exception per component; nothing is assumed exempt or prohibited. Codec/patent terms may need specialist review.
- Before shipping: anonymously retrieve and rebuild the release source, inspect the final DMG for full license and notice files, verify the offline license UI (Help → About → License) and attribution, and audit enabled dependencies. This is an engineering checklist, not legal advice.
