# Pixelview Desktop release guide

This is the operator checklist for Pixelview Desktop releases. The full technical runbook is in [`docs/pixelview-macos-updates-and-releases.md`](docs/pixelview-macos-updates-and-releases.md).

The inherited OBS `README.rst` stays unchanged for upstream attribution and rebasing. Pixelview release documentation belongs here and under `docs/`.

## Current policy

**Compliance gate is currently blocked.** The signed/notarized DMG flow now requires
versioned corresponding-source, notices and inventory artifacts before any appcast
publication. `release/source-inventory.json` intentionally records unresolved
whole-app licensing/source gaps; a passing media test or `--validate-config` does
not clear them. Complete the [source release runbook](docs/pixelview-source-release.md)
before following the release execution steps below.

- Work continues on `pixelview/minimal-capture` and is pushed to `pxlview/pixelview-desktop`.
- Do **not** create a tag, GitHub release, notarized DMG, R2 upload, or public appcast until the application is accepted for release.
- Merge this branch into the Pixelview release branch only when the application is ready.
- Release only Apple Silicon (`arm64`).
- Use one stable Sparkle feed. There is no beta channel.
- First product version: `0.0.1`, build `1`.
- Keep the exact OBS base tag, `git describe`, and commit separately in `version.json`.
- Signing, notarization, and publishing run locally, not in GitHub Actions.

## Why Apple notarization and Sparkle need different keys

### Apple notarization

Developer ID signing proves who built the app. Apple notarization is a separate upload to Apple, where Apple scans the signed DMG and returns a ticket. The release script staples that ticket to the DMG so Gatekeeper can approve it on another Mac.

Use an **App Store Connect API key** for notarization. Create a team API key in App Store Connect, download its `.p8` private key once, and create this Secure Note in the `pixelview-prod` vault:

```text
Item: pixelview-desktop-notarization
Fields:
  key_id       = the 10-character App Store Connect key ID
  issuer_id    = the App Store Connect issuer UUID
  private_key  = the complete contents of AuthKey_<KEY_ID>.p8 (concealed)
```

The committed 1Password environment files reference those field names, and the notarization file is loaded only for `--prepare`/`--all`. `release/pixelview-macos.sh` uses `op run`, so 1Password asks for biometric/system verification when locked. The private key is written to a mode-0600 temporary file for `notarytool`, then removed. It is never committed or stored in the release output.

### Sparkle update signing

Sparkle signing is independent of Apple. It proves that an update came from Pixelview before an installed copy replaces itself. The app contains only the public key. The private key currently lives in the macOS login Keychain under account `com.pixelview.desktop`.

Back it up in 1Password before the first release:

```text
Item: pixelview-desktop-sparkle-signing
Fields:
  account      = com.pixelview.desktop
  public_key   = k1+OJc59i2HxlsfpR/lS8Yv4iU1RttFDMYzahG/N0lw=
  private_key  = the exported Sparkle private key (concealed)
```

Export it once with Sparkle's pinned `generate_keys` tool, copy the file contents into the concealed field, and securely delete the export. Losing this key prevents installed copies from trusting future updates. Exposing it would allow someone else to sign a malicious update.

The operational Sparkle key remains in Apple Keychain because Sparkle's signing tools use Keychain. The 1Password copy is the encrypted recovery backup, not an environment variable used on every release.

## Other 1Password items

The R2 item already exists:

```text
Vault: pixelview-prod
Item: pixelview-desktop-releases-r2-bucket
Fields:
  endpoint
  access_key_id
  secret_access_key
```

The bucket name is public configuration and is committed as `pixelview-desktop-releases`. R2 credentials are injected only into the release subprocess. The app and Sparkle updater never receive them.

Back up the existing Developer ID identity in 1Password too:

```text
Item: pixelview-desktop-developer-id
Suggested contents:
  Developer ID Application certificate and private key exported as a password-protected .p12 attachment
  p12_password (concealed)
  team_id = MA47F3M8W9
  certificate_sha1 = 62CC493EC11F0031CF3ED9419671A78F5FD8E584
```

`codesign` requires an operational copy in a macOS Keychain. The `.p12` in 1Password is its encrypted backup and is used to restore the identity on a replacement release Mac.

## Local commands

Public metadata can be checked without unlocking 1Password:

```sh
release/pixelview-macos.sh --validate-config
```

Preparation and publishing resolve secret references through 1Password:

```sh
release/pixelview-macos.sh --prepare
release/pixelview-macos.sh --publish
```

Do not run those release commands yet. `--prepare` deliberately requires a clean tagged release commit, and `--publish` additionally requires the canonical remote tag and prepared notarized artifacts.

## Release flow when the app is ready

1. Finish testing on the working branch, commit it, push it, review it, and merge it into the Pixelview release branch.
2. Confirm `version.json` says Pixelview `0.0.1`, build `1`, with the intended OBS base version and commit.
3. Create and push `v0.0.1` to the canonical `pxlview/pixelview-desktop` repository.
4. Create the GitHub release for `v0.0.1`. It can remain a draft until acceptance is complete; binaries are hosted on R2.
5. Run `release/pixelview-macos.sh --prepare` locally. Approve the 1Password verification prompt. This builds, Developer-ID-signs, notarizes, staples, packages, and Sparkle-signs the ARM64 DMG without uploading it.
6. Test the notarized DMG on a clean/quarantined Mac. Confirm Gatekeeper installation, launch, capture, pairing, streaming, audio, and retained configuration.
7. Run `release/pixelview-macos.sh --publish`. Approve the 1Password verification prompt. Immutable artifacts are uploaded and verified first; the stable appcast is replaced last.
8. Perform an older-build-to-`0.0.1` Sparkle replacement and relaunch test before announcing the release. Use a temporary internal test feed/path, not a permanent beta channel. Verify configuration and Keychain state survive replacement.
9. Publish the GitHub release and announce `0.0.1` only after all acceptance checks pass.

No release action in this checklist is part of the current branch commit. The current task only installs the release machinery and documentation.
