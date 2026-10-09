# Pixelview Desktop release checklist

Every release ships on both platforms: macOS (Apple Silicon) and Windows
(x64), with the same version, build number, tag and release notes. Each
platform is built and signed on its own machine and publishes to its own feed
in the same R2 bucket (`desktop/macos/` and `desktop/windows/` under
`https://downloads.pixelview.io`). Full procedures, gates and credentials are
in [`docs/build-and-release.md`](docs/build-and-release.md) (section 4 for
macOS, 6 for Windows); the inherited OBS `README.rst` stays unchanged for
upstream attribution.

There is **one Git tag** per release (`v<version>`, step 3). The GitHub
release in step 6 is not a second tag: it is the release page GitHub shows
for that existing tag.

1. **Release commit.** On `master`, raise `pixelview_version` and
   `pixelview_build_number` in `version.json` (the build number must be above
   every build published on either platform), check that the OBS base fields
   match `git describe`, and add `docs/releases/<version>.md` and
   `<version>.html`. The notes cover both platforms; mark a change that
   applies to only one with "On the Mac:" or "On Windows:". Commit, push
   `master`; the tree must be clean.
2. **Validate on both machines.** `release/pixelview-macos.sh
   --validate-config` on the Mac and `powershell -ExecutionPolicy Bypass -File
   release\pixelview-windows.ps1 --validate-config` on Windows.
3. **Create and push the tag**, once, from either machine:
   `git tag -a v<version> -m 'Pixelview Desktop <version>'` and
   `git push origin v<version>`. Never move or reuse a pushed tag; if a fix is
   needed afterwards, it is a new version on both platforms.
4. **macOS** (on the Mac, from `git fetch --tags && git checkout v<version>`):
   `release/pixelview-macos.sh --prepare` builds, signs, notarizes, staples,
   Gatekeeper-assesses and stages the signed appcast under `dist/macos/`
   without uploading; `release/pixelview-macos.sh --e2e` then installs, pairs,
   streams and unpairs that DMG on fresh Namespace VMs (macOS 14, 15, 26 and
   27), and every image must pass; then `release/pixelview-macos.sh --publish`
   uploads the immutable assets and, last, the appcast, and refuses without
   that passing report. Approve the 1Password prompt for each phase.
5. **Windows** (on the Windows machine, from `git fetch --tags && git checkout
   v<version>`): `az login` as the Artifact Signing signer, then
   `powershell -ExecutionPolicy Bypass -File release\pixelview-windows.ps1 --prepare`
   builds, Authenticode-signs every binary and the installer, EdDSA-signs the
   installer and stages the appcast under `dist/windows/` without uploading;
   then the same command with `--publish` uploads the immutable assets, the
   appcast and `latest/`. Approve the 1Password prompt for each phase.
   Steps 4 and 5 are independent and may run in either order.
6. **Create the GitHub release** for the tag once both platforms are
   published: `gh release create v<version> --notes-file
   docs/releases/<version>.md`.
7. **Acceptance.**
   - On a clean/quarantined Mac: download the DMG through the public domain,
     confirm Gatekeeper accepts it, install and launch, verify capture,
     pairing, streaming and receiving, and verify
     Sparkle replacement and relaunch from the previous release with
     configuration and Keychain state preserved.
   - On Windows: download the installer through the public domain, confirm
     SmartScreen shows the signed publisher, install and launch, verify
     capture, pairing and streaming, and verify that Help > Check for
     Updates offers this release to the previous one and that the update
     installs, relaunches and keeps configuration and Credential Manager
     state.
8. **Website links** stay fixed and are re-pointed by each `--publish`:
   `https://downloads.pixelview.io/desktop/macos/latest/Pixelview-Desktop-arm64.dmg`
   and `https://downloads.pixelview.io/desktop/windows/latest/Pixelview-Desktop-x64-setup.exe`.
