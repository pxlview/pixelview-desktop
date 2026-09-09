# Pixelview Desktop macOS updates and releases

## Implemented release model

Pixelview Desktop uses Sparkle 2 on macOS, with a stable Apple Silicon feed at:

```text
https://downloads.pixelview.io/desktop/macos/appcast-arm64.xml
```

The first product version is `0.0.1`, build `1`. `version.json` is the product-version source of truth and separately pins the OBS base version and commit. The app bundle embeds all four values plus its exact Pixelview source commit/tag in `Info.plist`.

Development builds keep Sparkle disabled. A release build enables Sparkle only when CMake receives the exact production feed and a non-empty Pixelview public key. Release configuration fails if the target is not `arm64`, if the feed differs, or if an OBS update URL is supplied.

The app:

- checks the Pixelview feed at startup and every 24 hours;
- exposes **Help → Check for Updates…**;
- asks the user before installation and does not silently install updates;
- requires signed Sparkle metadata and verifies the update before extraction;
- contains no OBS branch lookup, Rosetta feed override, or `X-OBS2-GUID` update telemetry;
- does not upload logs or crash reports to OBS, and does not fetch OBS patron or OAuth-logo resources.

There is no automatic product analytics or crash telemetry in this release path. Sparkle contacts only the configured Pixelview appcast/download host for update checks.

The Sparkle public key is intentionally committed in `release/macos.json`. Its operational private key is in the macOS login Keychain under account `com.pixelview.desktop`, with an encrypted recovery copy kept in 1Password. Apple Developer ID signing and Sparkle Ed25519 signing are separate trust layers.

## Keep release tooling in the public repository

A separate private release repository is not needed. Keeping `pixelview-release.sh`, `version.json`, the public Sparkle key, and the runbook beside the source prevents release logic from drifting away from the code it packages. None of these files contains a secret.

Keep these outside Git:

- Developer ID private key;
- notarytool credentials;
- Sparkle private key and its offline backup;
- R2 access key and secret;
- generated `dist/` artifacts and `.runtime/` tool cache.

Public GitHub CI is test-only. It cannot sign, notarize, upload artifacts, publish releases, or access R2. The inherited OBS release, Steam, Windows-signing, appcast, and scheduled workflows have been removed.

## One-time Cloudflare R2 setup

Create the following in the Cloudflare dashboard:

1. An R2 bucket, suggested name: `pixelview-desktop-releases`.
2. A production custom domain on that bucket: `downloads.pixelview.io`.
3. An R2 API token with **Object Read & Write** permission limited to that bucket. Record its access-key ID and secret once; do not put them in the repository.
4. Confirm a test object is publicly readable through `https://downloads.pixelview.io/...`. Do not use the development `r2.dev` hostname for releases.

The release script writes this layout:

```text
desktop/macos/
  appcast-arm64.xml
  releases/0.0.1-1/
    Pixelview-Desktop-0.0.1-build1-arm64.dmg
    Pixelview-Desktop-0.0.1-build1-arm64.dmg.sha256
    Pixelview-Desktop-0.0.1-build1-arm64.html
    release-manifest.json
```

Version-and-build-qualified objects get `Cache-Control: public,max-age=31536000,immutable`. Before any write, the script checks every intended R2 key: an existing byte-identical object is accepted as a retry, while an existing different object aborts the whole upload before the first write. Immutable PUTs use `If-None-Match: *`, and appcast replacement uses the current R2 ETag with `If-Match`, so a racing release cannot overwrite another publisher. A local lock prevents two release processes from sharing the same staging tree. The appcast is uploaded last with `Cache-Control: no-cache, max-age=0, must-revalidate`. The script reads all immutable objects back through the public domain byte-for-byte before publishing the appcast, then reads the public appcast back byte-for-byte.

The R2 credentials already live in `pixelview-prod/pixelview-desktop-releases-r2-bucket`. The committed `release/macos-r2.1password.env` contains only these secret references and is injected only for `--publish` or `--all`:

```text
op://pixelview-prod/pixelview-desktop-releases-r2-bucket/endpoint
op://pixelview-prod/pixelview-desktop-releases-r2-bucket/access_key_id
op://pixelview-prod/pixelview-desktop-releases-r2-bucket/secret_access_key
```

Run the release through `release/pixelview-macos.sh`; it invokes `op run`, which asks for biometric/system verification when 1Password is locked. `--prepare` receives only notarization credentials, `--publish` receives only R2 credentials, and `--all` receives both. The app never receives either credential set.

CORS is not required for Sparkle or direct browser downloads. Add a narrow `GET`/`HEAD` CORS policy only if JavaScript on another origin must fetch these objects.

## One-time Apple notarization setup

The installed signing certificate must be a **Developer ID Application** certificate for the Cinecode team. A drag-to-Applications DMG does not need a Developer ID Installer certificate.

Notarization means uploading the signed DMG to Apple for malware scanning and receiving a ticket that the script staples to the DMG. It is separate from Developer ID signing. Use an App Store Connect team API key and create this Secure Note manually in 1Password:

```text
Vault: pixelview-prod
Item: pixelview-desktop-notarization
Fields:
  key_id       = 10-character App Store Connect API key ID
  issuer_id    = App Store Connect issuer UUID
  private_key  = complete AuthKey_<KEY_ID>.p8 contents (concealed)
```

Apple provides the `.p8` private key only when the API key is created. Download it, copy it to the concealed `private_key` field, verify the 1Password item, and securely remove the downloaded file. The wrapper injects these fields; the release script writes the private key to a mode-0600 temporary file for `notarytool` and removes it on exit. A persistent notarytool Keychain profile is no longer required for the 1Password path. Direct invocation can still use `PIXELVIEW_NOTARY_PROFILE` as a fallback.

## Sparkle key ownership and backup

The release public key is:

```text
k1+OJc59i2HxlsfpR/lS8Yv4iU1RttFDMYzahG/N0lw=
```

It was generated for Keychain account `com.pixelview.desktop`. The release script downloads Sparkle 2.9.2 from the official Sparkle release and verifies both the pinned archive SHA-256 and the three exact extracted tool hashes before execution. It also confirms that the Keychain key matches the committed public key.

Before the first public release, export one backup of the Sparkle private key using Sparkle's `generate_keys --account com.pixelview.desktop -x ...` command. Create `pixelview-prod/pixelview-desktop-sparkle-signing` with concealed field `private_key`, plus `account` and `public_key` fields, copy the export into it, then securely remove the local export. Losing this key prevents existing installations from trusting future updates; publishing it would let an attacker sign updates.

The release tools consume the operational key from macOS Keychain. The 1Password item is the encrypted recovery source for another release Mac; it is deliberately not injected as a routine environment variable.

## Release preparation

For each release:

1. Update `version.json`. For every public release, choose a new semantic product version and increase the positive build number beyond every published build. Never reuse a published marketing version; a replacement release gets both a new version and a new build number.
2. When rebasing on OBS, update `obs_base_version`, `obs_base_describe`, and `obs_base_commit` to the exact upstream base. `obs_base_describe` must equal `git describe --tags --long OBS_BASE_COMMIT`; the release script verifies it. Do not infer the base from the Pixelview product version.
3. Add `docs/releases/VERSION.html` and matching GitHub release notes.
4. Commit all release changes. The tree must be clean.
5. Create an annotated tag at the release commit:

```sh
git tag -a v0.0.1 -m 'Pixelview Desktop 0.0.1'
```

6. Validate public metadata:

```sh
release/pixelview-macos.sh --validate-config
```

7. Build, sign, notarize, staple, assess with Gatekeeper, and generate the signed first appcast locally:

```sh
release/pixelview-macos.sh --prepare
```

The script fails closed if the tag is not at `HEAD`, the tree is dirty, the identity/profile/key is missing, signing is wrong, notarization/stapling/Gatekeeper fails, Sparkle is missing, an OBS update endpoint is embedded, the prepared manifest/checksum/appcast disagrees with the mounted signed app, the appcast signatures fail verification, or the build number does not advance the authenticated current feed. For the first `0.0.1` build, the public appcast URL must resolve and return HTTP 404; a DNS/TLS/network error is not treated as an empty feed.

Prepared files are under `dist/macos/`. Nothing is uploaded by `--prepare`.

## Publishing order

Push the exact source before making the update visible:

```sh
git push origin HEAD
git push origin v0.0.1
```

Create the GitHub release for `v0.0.1` so the exact source tag and release page exist. Binary downloads may remain on R2; attaching the checksum and manifest to GitHub is optional.

Then publish R2 assets:

```sh
release/pixelview-macos.sh --publish
```

`--publish` verifies the prepared checksum/manifest, Developer ID signature and configured Apple team, notarization ticket, mounted app provenance, Sparkle feed/enclosure signatures, monotonic build number, and remote Git tag before touching R2. It then conflict-checks, uploads, and publicly verifies the immutable assets before uploading `appcast-arm64.xml` last. `--all` combines preparation and publishing, but the source tag still has to be pushed before its publish phase, so the separate two-command flow is clearer for a small team.

After publishing, update the product docs/download button to:

```text
https://downloads.pixelview.io/desktop/macos/releases/0.0.1-1/Pixelview-Desktop-0.0.1-build1-arm64.dmg
```

## Required final acceptance test

Before calling `0.0.1` public:

1. Download the DMG through the public custom domain on a clean/quarantined Mac.
2. Confirm Gatekeeper accepts it, drag `Pixelview Desktop.app` to Applications, and launch it.
3. Verify capture, pairing, WHIP streaming, audio monitoring, and retained configuration.
4. For the update cycle, publish an internal older build and a higher-build replacement to a staging feed, then verify **Check for Updates…** downloads, replaces, relaunches, and preserves Pixelview configuration and Keychain state.
5. Confirm the appcast and DMG expose no secret and the manifest commit/tag matches GitHub.

## What remains outside automation

The repository cannot create the Cloudflare bucket/domain/token, Apple app-specific password or API key, GitHub tag/release, or clean-Mac acceptance environment. Those are deliberate operator-owned steps. Signing and publishing stay local until release frequency or team size justifies moving them into protected CI.

## References

- https://sparkle-project.org/documentation
- https://sparkle-project.org/documentation/publishing
- https://developer.apple.com/documentation/security/customizing-the-notarization-workflow
- https://developer.apple.com/documentation/technotes/tn3147-migrating-to-the-latest-notarization-tool
- https://developers.cloudflare.com/r2/buckets/public-buckets
- https://developers.cloudflare.com/r2/examples/rclone
