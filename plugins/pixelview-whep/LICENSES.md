# Runtime licensing and distribution status

This is an engineering dependency inventory, **not legal release clearance**.

## Examined original texts

- obs-gstreamer source headers (commit `f5b075e9039b1c5ef8462b248e5a365fe36bc13f`) explicitly grant GPL version 2 or later. Raw appsink/OBS mapping informed this independently narrowed source; attribution retained. Pixelview/OBS corresponding source remains GPL-governed.
- Installed GStreamer 1.28.3 `LICENSE` is the complete GNU LGPL 2.1 text. Curated plugin metadata identifies the C plugins as LGPL. Do not infer licenses of arbitrary plugins from the GStreamer core license.
- rswebrtc 0.15.2's actual `LICENSE-MPL-2.0` and package `Cargo.toml` identify MPL-2.0. Sections 3.1–3.4 govern source/executable/larger-work distribution and preservation of notices. Compatibility with GPL via secondary licenses is conditional, **not automatic**. Preserve MPL notices and source availability. Inspect Exhibit-B exclusions and the complete statically linked Rust crate closure before release.
- libnice 0.1.23 `COPYING` explicitly says `LGPL-2.1-or-later OR MPL-1.1`; this build chooses the LGPL alternative. Its GStreamer plugin is compiled unchanged from the hash-verified upstream release by the included script; preserve COPYING, COPYING.LGPL and COPYING.MPL.

## Deliberately curated codecs

H.264/HEVC use the LGPL applemedia GStreamer bridge to Apple's OS-provided VideoToolbox; no Apple codec binary is redistributed. Opus uses GStreamer's opus plugin plus libopus. No gst-libav, FFmpeg codec closure, x264, x265, FDK AAC, proprietary codec library, or unrelated Homebrew plugin directories are copied by this packager. AppleMedia also exposes encoders/capture factories in its single library; this receiver never uses them. Codec patent/product rights are separate from source-code licensing and remain a legal question.

## Current official-distribution payload

The current build uses the pinned official GStreamer1.28.3 runtime/development packages, extracted locally without running installer scripts. See `scripts/UPSTREAM.md` for verified versions and provenance. The staged runtime has53 arm64 Mach-O files. It no longer uses the Homebrew GnuTLS/nettle/GMP/X11 dependency chain; OpenSSL and MoltenVK are present. The upstream libnice plugin is supplied by the SDK rather than built against host libraries.

The official SDK's available notices and version inventory are retained, including notices for some SDK components not shipped. The missing separate gst-plugins-good notice directory and transitive Rust source/notices remain review items. Do not infer whole-app compliance from this curated runtime: the OBS dependency FFmpeg is GPLv3-or-later and Qt and other application dependencies have separate obligations.

`release/source-inventory.json` is the authoritative explicit release-blocker list. `cmake/macos/pixelview_sources.py` and the release validator refuse incomplete/unreviewed source inventories. The License dialog has GPL2/GPL3, third-party-notice and source/build pages; a development fallback is not a claim of complete notices or published corresponding source. See `docs/pixelview-source-release.md`.

## Historical Homebrew closure findings (not the current payload)

The actual dylib closure includes GnuTLS/libnice networking, GLib/libintl/PCRE2/orc, libsrtp/libopus, libusrsctp, OpenSSL3, and applemedia→gstgl→X11 libraries. Inspection of original installed texts found LGPLv3 in nettle, GMP, libidn2 and libunistring; OpenSSL3's LICENSE.txt is Apache-2.0; libopus/libsrtp texts have BSD-style redistribution/notice clauses. Thus a **GPLv2-only distribution compatibility claim would be wrong**. Pixelview/OBS and this source have version-2-or-later grants, but selecting GPLv3 for a combined distribution and verifying every component's grants/exceptions is part of release legal review. The inventory preserves both LGPL and GPL texts where packages ship them; a package-level COPYING is not necessarily the license of the particular library (for example gettext/libintl).

Homebrew glib omitted its license file: included original LGPL-2.1-or-later text from `https://raw.githubusercontent.com/GNOME/glib/2.88.1/LICENSES/LGPL-2.1-or-later.txt`. Homebrew gettext includes GPL3 COPYING but libintl has its own LGPL2.1 text, included from `https://raw.githubusercontent.com/autotools-mirror/gettext/v1.0/gettext-runtime/intl/COPYING.LIB`. PCRE2's COPYING points to LICENCE.md; the packager includes both. Existing Homebrew `sbom.spdx.json` files and formula/receipts are copied rather than inventing transitive licenses.

## Pixelview rswebrtc origin patch

The packaged rswebrtc0.15.2 is built from the crates.io release, SHA256
`58b0f7af06bd2e98c71e8ad76a27a4045727b88af769a89d6a7f0fd5903047b0`, with
`patches/rswebrtc-0.15.2-same-origin.patch`. The changed MPL-covered file is
`src/whep_signaller/client.rs`; original copyright/license headers remain.
Pixelview changes reject endpoint redirects, constrain resource Location to
the accepted response origin without userinfo, and disable DELETE redirects
(PATCH already uses the no-redirect client). Upstream default Cargo features
are preserved; AWS/LiveKit extras are not enabled. Rust1.94.0 and Cargo.lock
pin the Rust build inputs; binary hashes and source/patch provenance are
recorded rather than claiming an unmodified Homebrew binary.

`build-rswebrtc.py` produces a separate output directory containing the exact
source crate, patch, Cargo manifest/lock, MPL text and binary provenance.
The packager includes these materials in `licenses/rswebrtc`. Apply the patch
to the extracted source and follow the supplied build script to replace the
module; the normal runtime packager rewrites load commands and ad-hoc signs.
No source-hosting/legal release clearance is implied: all statically linked
crate notices and corresponding dependency source availability still require
the transitive review below before distribution.

## Generated artifacts and obligations

The bundle includes `licenses/` containing original available package license/notice texts, exact Homebrew formula source and installation receipts, rswebrtc MPL license/Cargo manifests/lock, and libnice texts. `sbom.json` records the exact recursive Mach-O closure, originating packages/versions, input SHA256s, and an explicit Rust-transitive-review-pending marker. `runtime-lock.json` fails builds on unreviewed binary input changes. This inventory is a custom provenance JSON, not a claim of complete SPDX/CycloneDX compliance. The source-built rswebrtc module includes statically linked crates not visible to otool; **Cargo.lock alone is not a complete third-party notice bundle**.

Before distributing: archive exact corresponding sources (including Homebrew patches/formulas, Rust crates and build inputs), publish the sources or a legally sufficient offer, retain notices, complete transitive Rust license/feature analysis, and document a working replacement/relink/rebuild procedure for LGPL libraries. Dynamic linking and source availability must not be defeated by an EULA or code-signing restrictions; users must be able to rebuild/ad-hoc sign their modified copy. The included native/build/packaging source and formula provenance help that process but do not fulfill source hosting alone. Full Developer ID/notarization and clean-Mac acceptance are separate release gates.

Authoritative guidance consulted by parent:
- https://gstreamer.freedesktop.org/documentation/frequently-asked-questions/licensing.html
- https://www.mozilla.org/en-US/MPL/2.0/FAQ/ (Q14, conditional compatibility)
