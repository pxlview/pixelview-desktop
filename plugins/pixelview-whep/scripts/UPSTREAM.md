# Official GStreamer runtime acquisition

This replaces the machine-specific Homebrew runtime with public, pinned upstream
GStreamer **1.28.3** macOS universal packages. No installer is executed and nothing
is written to `/Library/Frameworks` or another global prefix.

## Build and verification

From the repository root, with Python 3.12+, Apple command-line developer tools,
`pkg-config`, and the Rust **1.94.0** aarch64-apple-darwin toolchain available:

```sh
python3 plugins/pixelview-whep/scripts/fetch-gstreamer.py
python3 plugins/pixelview-whep/scripts/build-rswebrtc.py
python3 plugins/pixelview-whep/scripts/bundle-runtime.py stage .deps/pixelview-gstreamer-upstream
python3 plugins/pixelview-whep/scripts/build-rswebrtc.py --test
python3 plugins/pixelview-whep/tests/test_packaging.py -v
python3 plugins/pixelview-whep/tests/run-upstream.py
```

`run-upstream.py` reuses an existing `build_macos` libobs, but compiles the actual
receiver module and native codec harness itself. It does not run a full-app Xcode
build. `--obs-build` selects a different existing libobs build. Its default runtime
is `.deps/pixelview-gstreamer-upstream`; `--runtime` selects another staged copy.
It copies that runtime into an isolated test directory to avoid duplicate
GStreamer GTypes or mutating the runtime while another process is linking it.

The runner also compiles the live production-pipeline probe. Opt-in `--live` reads
one authorized WHEP URL from stdin, never command-line arguments; only media
counters are printed. Raw diagnostics stay in mode-0600
`.deps/upstream-native/live-private.log`. Live validation requires the matching
viewer authorization/keeper and an active engine; do not infer live delivery from
codec roundtrips. Do not run it when network state changes are prohibited.

`cmake/macos/pixelview-build.sh` performs acquisition, patched-module build and
normal `.deps/pixelview-gstreamer` staging in that order for development and
release builds. Initial upstream validation deliberately does **not** overwrite
that normal stage. Do not stage/acquire and compile against the same mutable
directory concurrently.

## Trust and reproducibility

- URLs and SHA-256 hashes in `runtime-lock.json` come from the official HTTPS
  `.pkg.sha256sum` sidecars. Both full downloads were actually checked.
- The upstream packages are not Apple Installer-signed. Upstream also publishes
  detached `.asc` signatures; these were **not independently OpenPGP-verified**.
  A pinned digest authenticated through the official HTTPS origin is not a claim
  of Apple signing, notarization, or independently verified release-key trust.
- `pkgutil --expand-full` extracts payloads; package scripts are never executed.
  SDK pkg-config prefixes are relocated into the repository-local SDK.
- Acquisition verifies even cached archives and rebuilds the SDK from them.
  Partial downloads cannot become accepted archives. SDK publication moves the
  previous tree aside before cleanup so Finder-created `.DS_Store` files cannot
  destroy an otherwise valid replacement.
- Every curated universal input has a portable SDK-relative binary hash. Staging
  rejects changed inputs rather than falling back to a host installation.
  `--write-lock` is an explicit review operation, never a normal build step.
- Runtime files are thinned to arm64, dependency references are rewritten to
  loader-relative paths, unmanaged rpaths are removed, and each Mach-O is signed
  ad hoc for local validation. Every file's architecture, minOS, signature and
  dependency closure are checked. The product target remains macOS 14.0.
- The SDK's unpatched `libgstrswebrtc` is never shipped. rswebrtc **0.15.2** is
  rebuilt from the pinned crates.io source with the unchanged same-origin patch,
  Rust toolchain, default features and Cargo lock. Output lives separately in
  `.deps/rswebrtc-upstream-patched/output`. Provenance includes the SDK package
  identities and staging checks its source, patch, lock and output digests.

## Payload and license-audit handoff

Verified closure: **53 arm64 Mach-O files**. Required WHEP/ICE/VideoToolbox/Opus
factories are checked with the bundled scanner and inspection binary, without
host plugin discovery. No libav, x264 or x265 plugin is shipped. `libgstnice` is
provided by upstream, not compiled against a host libnice installation.

Versions from the official SDK `share/versions.txt` include:

| Component | Version |
|---|---|
| GStreamer core/base/good/bad | 1.28.3 |
| Patched rswebrtc source | 0.15.2 |
| GLib | 2.82.4 |
| proxy-libintl | 0.5 |
| PCRE2 | 10.42 |
| libffi | 3.2.9999.5 |
| zlib | 1.3.1 |
| ORC | 0.4.42 |
| libnice | 0.1.23 |
| OpenSSL | 3.5.0 |
| libsrtp | 2.8.0 |
| Opus | 1.5.2 |
| MoltenVK | 1.3.283.0 |

MoltenVK is pulled by applemedia's Vulkan library dependency. The prior
GnuTLS/nettle/GMP/p11-kit/libidn2/libunistring/X11 chain is absent. Upstream bad's
license directory explicitly includes `ext_sctp_usrsctp_LICENSE.md` for its
statically included SCTP source.

The runtime `sbom.json` identifies the actual binary closure. For audit, the
packager retains the official SDK's **full notice set and version inventory**,
which include unshipped components; those files are not a claim that every SDK
codec is bundled. The official SDK has no separate `gst-plugins-good-1.0` notice
directory. Exact corresponding sources, this notice gap, Rust transitive static
notices and distribution obligations remain the separate license audit's scope.
Do not reuse old Homebrew package versions/receipts or treat successful runtime
validation as legal clearance.

Actual verification obtained: all 53 files require macOS 11.0 or 14.0; real module
loading and jitter=50 readback pass; H264 and HEVC each deliver 30 decoded native
frames; Opus delivers 28800 audio frames; all 9 Rust tests, including the
cross-origin/redirect security tests, pass. Live service reception and full-app
integration are separate acceptance steps, not asserted by this script document.
