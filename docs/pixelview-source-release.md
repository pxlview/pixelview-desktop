# Corresponding-source release artifacts

**Current state: release blocked, not legally cleared.** `release/source-inventory.json`
is a deliberately incomplete, review-blocked inventory. Do not flip `status` merely
to get a build through. Engineering validation cannot establish license compatibility,
source completeness, patent clearance or vendor redistribution permission.

## Artifact contract

The existing local signed/notarized DMG → R2 → Sparkle flow now additionally requires:

- `Pixelview-Desktop-VERSION-BUILD-sources.tar.gz`
- `Pixelview-Desktop-VERSION-BUILD-NOTICES.txt`
- `Pixelview-Desktop-VERSION-BUILD-source-inventory.json`

`release-manifest.json.compliance` has exactly `sources`, `notices`, and `inventory`.
Each record contains `name`, SHA-256 `sha256`, byte `size`, and its immutable
`https://downloads.pixelview.io/desktop/macos/releases/VERSION-BUILD/NAME` URL.
The resolved inventory records the exact project commit/tag and SHA-256 of the
tracked input inventory, together with all reviewed input records.

Preparation validates/builds compliance inputs before accessing signing or notary
credentials. Source tarballs remain outside Sparkle's application-archive scan.
Publication regenerates expected bytes from the clean tagged source and pinned
source cache, rather than trusting staged hashes. All seven immutable objects
(DMG, checksum, release notes, release manifest, and these three artifacts) receive
the existing conflict preflight, conditional PUT, and public byte comparison before
the stable appcast may advance. Existing signing, notarization, canonical tag,
monotonic build, release locking, and ETag safeguards remain in force.

## Inventory schema 1

The inventory must be a **tracked file at the exact clean product tag**:

- `review.status`: `approved` only after responsible review; `review.blockers`: empty.
- `review.evidence`: `{path, sha256, size}` of a tracked review document. Record
  reviewer, scope, actual dynamic/static/asset closure, chosen distribution terms,
  required source/rebuild/relink/replacement materials, tests and exceptions there.
- `runtime_bindings`: nonempty list of tracked `{path, sha256, size}` records binding
  the reviewed build/runtime pins (including CMake dependency pins and receiver lock).
- `components`: unique `id`, exact `version`, license expression, nonempty `sources`,
  `notices`, `recipes`, and an explicit `patches` list (empty only if unmodified).
- `sources` entries: `{name, url, sha256, size}`. URLs must be public credential-free
  HTTPS, no query/fragment; pin exact upstream versions, not moving branch URLs.
  Source bytes live only in a dedicated external cache under their SHA-256 basename.
- `notices`, `recipes`, `patches`: tracked `{path, sha256, size}` records, never paths
  into raw `.deps`, private `.runtime`, home directories, or mutable SDK trees.
- Optional `excluded_submodules`: exact `{path, commit, reason}` entries only for
  reviewed modules not involved in this target build. Required modules must be
  vendored into the tagged project or otherwise integrated with complete build
  instructions; omission is not automatic. Current upstream browser/websocket and
  Windows capture gitlinks need explicit platform/build-scope review.

The source archive contains the complete tracked project tree under `project/`,
explicit source archives under `dependencies/COMPONENT/`, plus notices and resolved
inventory. Safe relative project symlinks are preserved; escaping links are refused.
Dependency inputs must currently be inspectable tar-family archives (including
`.crate`), with safe regular files/directories, no links/devices, duplicate entries,
more than 200,000 entries or more than 8 GiB expanded bytes per archive. Unsupported
archive layouts fail closed and need reviewed repackaging/support, not an override.
No archive is blindly extracted. No ignored/untracked files or runtime binaries are
harvested. Metadata and gzip timestamps are normalized for repeatable output.

## Offline license interface

The builder writes these additional staging files:

- `license/third-party-notices.txt`: exactly the published notice artifact bytes.
- `license/source-manifest.json`: schema 1, `version`, `source_commit`, `source_tag`,
  `status`, `source_url`, `candidate_source_url`, `publication_verified`, and the
  three artifact records in `artifacts`.

Preparation exports `PIXELVIEW_LICENSE_DATA_DIR` pointing at that **external temporary
license directory**. The build helper forwards `-DPIXELVIEW_LICENSE_DATA_DIR=...`;
the frontend CMake integration installs its two files under
`Contents/Resources/license/` before signing. Do not overwrite tracked frontend data
to prepare a release. The signed app's two files are compared byte-for-byte with
regenerated inputs, including when validating a mounted prepared DMG.

Before publication, the source manifest honestly says
`status: release-candidate-unpublished`, `publication_verified: false`, and keeps
`source_url` empty. `candidate_source_url` is the exact required future immutable
location. Do not display it as an already verified public download. This signed
prepublication metadata cannot be rewritten after uploading without invalidating
the app signature; successful publication instead establishes availability through
the public release manifest and R2 readback. Development UI assets must continue to
say development/unpublished, not substitute the next release tag.

## Offline preparation/checking (no signing or uploading)

Once **all** review/material requirements are satisfied and the operator has
separately created the intended clean tag, the source-only command is:

```sh
python3 cmake/macos/pixelview_sources.py \
  --root "$PWD" --tag v0.0.1 \
  --cache "$HOME/Library/Caches/pixelview-sources" \
  --output /tmp/pixelview-source-check \
  --release-id 0.0.1-1 \
  --base-url https://downloads.pixelview.io/desktop/macos
```

It does not download, sign, notarize, upload, commit or create tags. Release scripts
use the same builder; `PIXELVIEW_SOURCE_CACHE` overrides the external cache location.
Keep this cache available for publication revalidation. Missing input/review/source
is an error, not a notice-only fallback. `--validate-config` remains a credential-free
public metadata check, **not** a compliance/release readiness check.

## Collected and unresolved

Actually fetched the public `gst-plugin-webrtc-0.15.2.crate`: **630608 bytes**, SHA-256
`58b0f7af06bd2e98c71e8ad76a27a4045727b88af769a89d6a7f0fd5903047b0`, matching the
receiver runtime lock. Its MPL-2.0 license is byte-identical to the already retained
`plugins/pixelview-whep/licenses/rswebrtc/LICENSE-MPL-2.0`. The seed inventory binds
that source, actual patch, Cargo lock, build/acquisition recipes and upstream guide.
The cached public source is not committed or bundled from `.deps`.

Still blocked: the audited FFmpeg binary reports GPL version 3 or later; Qt 6.11.1
module/third-party notices and corresponding sources are incomplete; the official
GStreamer/Cerbero transitive source/recipe set, gst-plugins-good notice gap, Rust
static transitive sources/notices, OBS dependency-bundle sources, Sparkle, fonts,
codecs and vendor SDK obligations require completion. The full binary closure,
GPLv3-compatible distribution approach, modifications, installation/replacement
information, source rebuild and anonymous public availability need responsible
review. See the inventory's explicit blockers and the distribution checklist.

## Verification obtained

Source tests exercise real Git object archives, exact temporary test tags, missing,
tampered, symlinked and unsafe inputs, incomplete review/material lists, dirty or
untagged trees, artifact metadata, and deterministic reconstruction. An optional
local test runs the real CLI with the downloaded rswebrtc archive and actual project
patch/recipe/license/lock inputs in a **test-only Git tree**; it does not approve the
whole application. Production checkout source preparation was actually attempted
and correctly refused its dirty tree. No whole-app source bundle, production tag,
DMG, notarization, release, R2 upload or appcast publication was made by this work.
