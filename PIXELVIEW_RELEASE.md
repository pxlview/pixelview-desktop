# Pixelview Desktop release checklist

Operator checklist for a public macOS release. The full procedure, gates and
commands are in [`docs/build-and-release.md`](docs/build-and-release.md); the
inherited OBS `README.rst` stays unchanged for upstream attribution.

1. Land the release commit on `master`: `version.json` carries a new product
   version and a build number above every published build, the OBS base
   fields match `git describe`, and `docs/releases/<version>.md` plus
   `<version>.html` exist. The tree must be clean.
2. Corresponding source is the public repository at the release tag; the
   release packager also ships a sources tarball and NOTICES next to the DMG.
   Review the open items in `release/source-inventory.json` when convenient;
   they do not gate the release.
3. Create and push `v0.0.1` (an annotated tag at the release commit; use the
   real version), then run `release/pixelview-macos.sh --validate-config`.
4. Run `release/pixelview-macos.sh --prepare`: build, sign, notarize, staple,
   Gatekeeper-assess and generate the signed appcast under `dist/macos/`.
   Nothing is uploaded.
5. Push the source and the tag. Create the GitHub release for the tag so
   the exact source exists publicly before the update becomes visible.
6. Run `release/pixelview-macos.sh --publish` to upload the immutable assets
   and, last, the appcast to R2.
7. Acceptance on a clean/quarantined Mac: download the DMG through the public
   domain, confirm Gatekeeper accepts it, install and launch, verify capture,
   pairing, streaming and receiving, then verify
   Sparkle replacement and relaunch from an older build against a staging
   feed, with configuration and Keychain state preserved.
8. The website download link is the stable `https://downloads.pixelview.io/desktop/macos/latest/Pixelview-Desktop-arm64.dmg`; publish re-points it after the appcast.
