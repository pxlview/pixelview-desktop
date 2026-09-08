# Offline license assets and generated build metadata

- `COPYING` is the byte-identical root OBS `COPYING`, including the appendix.
  SHA-256: `8177f97513213526df2cf6184d8ff986c675afb514d4e68a404010521b880643`.
- `AUTHORS` is the unchanged root OBS `AUTHORS`; Pixelview attribution is separate in the dialog.
- `gplv3.txt` was downloaded directly from the Free Software Foundation's official
  <https://www.gnu.org/licenses/gpl-3.0.txt> over HTTPS. It is the full, unmodified
  GPL version 3 (including the application appendix), 35,149 bytes.
  SHA-256: `3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986`.
- The existing upstream `gplv2.txt` is retained unchanged.

The combined build contains GPL-3.0-or-later FFmpeg. Applying the compatible
later-version option to distribution of the combined work does not rewrite
all upstream file grants. Individual notices and obligations still apply.

## Build-generated interface

Generate these files outside the checkout before CMake configuration:

```
<PIXELVIEW_LICENSE_DATA_DIR>/third-party-notices.txt
<PIXELVIEW_LICENSE_DATA_DIR>/source-manifest.json
```

Configure with `-DPIXELVIEW_LICENSE_DATA_DIR=/absolute/generated/directory`.
The production frontend installs these unchanged as `license/third-party-notices.txt`
and `license/source-manifest.json` (macOS `Contents/Resources/license`) before
bundle signing. An explicit directory with a missing or empty file fails configuration.
An unset directory is supported for development, with honest unavailable UI.
Use a fresh release build directory so stale bundled metadata cannot survive.

The dialog is a plain-text, offline viewer: it renders the complete notices and
manifest without HTML processing, network requests, truncating the component
inventory, or synthesizing source URLs. It also shows the configured Pixelview
version and exact OBS base description. The manifest producer owns schema and
validation; recommended explicit fields are `schema_version`, `version`,
`source_commit`, `source_url`, and `source_status`. Release build numbers, source
hashes, dependency/build provenance, audit scope and unresolved obligations must
remain explicit in the producer's schema. The exact immutable source URL must
come from the release configuration/build metadata, not a moving branch URL.
A URL alone is not evidence of source publication.

Official GStreamer license texts plus an SDK inventory are not a complete
whole-application dependency/license audit. Development builds must not claim
complete compliance or published corresponding source. A production release
still requires the exact shipped transitive inventory, notices, source/build
materials and any replacement/relinking obligations to be verified before
publication. This dialog does not establish those facts.

## Verification

`python3 -m unittest discover -s test/pixelview -p '*license*.py' -v`
compiles the actual dialog function against pinned Qt, checks selectable
read-only full licenses and generated inventories in shown offline tabs, and
builds a disposable small bundle with the production external-resource CMake
module. These are isolated UI/packaging checks, not a full application build,
signing check, release audit, or hardware acceptance run.
