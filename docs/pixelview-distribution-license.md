# Pixelview distribution licensing checklist

This is an engineering release checklist, not legal advice or a legal-compliance certification. It describes inspected source and official guidance, not a completed audit of a signed release. No publication or public-source availability was verified.

## License and branding

The local `COPYING` contains GNU GPL version 2. More importantly, `README.rst:22–23` expressly says **version 2 (or any later version)**, and source headers such as `libobs/obs-view.c:1–7` grant that option. Thus the OBS code's grant is **GPL-2.0-or-later**, not merely GPL-2.0-only. The sample notice in COPYING's appendix alone would not establish the program's license; the README and actual source grants do. GPLv2 §9 explains the later-version option.[1] OBS's official help page confirms GPLv2 and permits reuse subject to the license.[3]

Preserve the existing grant and upstream copyright notices. This checklist uses GPLv2 obligations as the baseline, **not** a declaration that every possible dependency combination may ship under v2: inspect the licenses of the components actually shipped, and reassess if distribution must use GPLv3 terms.[1][2]

Pixelview can be an independently branded modified distribution, with source under the applicable GPL terms; charging for copies is permitted, but restricting recipients' GPL rights is not (§§1, 2, 6).[1] Use original Pixelview branding and clearly say it is based on OBS Studio rather than suggesting an official OBS release or endorsement. OBS's official site identifies “OBS,” “OBS Studio,” “Open Broadcaster Software,” and the OBS Studio logo as registered trademarks of Wizards of OBS LLC.[4] GPL copyright permission should not be treated as trademark clearance; brand clearance remains a separate release review.

## Binary release requirements

- **Complete corresponding source:** provide the preferred editable source corresponding to each binary, including Pixelview changes, all required covered modules, interface definitions, and scripts controlling compilation and installation (§3). A link to upstream OBS or a moving fork default branch is not a substitute for the source used for that release.[1]
- **Recommended download arrangement:** attach a complete source archive to the same release/download location as each binary, with equivalent access (§3's final paragraph). Record the release tag/commit, build configuration, patches, dependency versions and source provenance, and include required submodule contents rather than assuming GitHub's automatically generated archive contains them. Build a clean checkout/archive to validate completeness; identical binary hashes are not asserted here.[1]
- **Other delivery channels:** accompany physical/offline binary delivery with source under §3(a), or a genuine written offer under §3(b), valid at least three years, to supply any third party complete machine-readable corresponding source for no more than the cost of physically distributing it. A casual “source on request” sentence is not a fulfilled offer. The narrow §3(c) pass-through alternative is limited to noncommercial redistribution of binaries received with a qualifying offer; do not use it as a default for this modified fork.[1]
- **Notices and license copy:** keep copyright, license and no-warranty notices intact; give recipients a copy of the GPL with the program (§1, incorporated by §3). Keep upstream `COPYING` and `AUTHORS`; add Pixelview attribution separately, not by replacing OBS authors with the fork name. Include an unmodified full `COPYING` in release materials and an accessible offline copy in the app package.[1]
- **Identify modifications:** modified files must carry prominent notices stating that they were changed and the date of change (§2(a)). Add these without deleting upstream headers. A release changelog is useful supplementary identification; do not assume a general About notice or Git history alone substitutes for notices in the modified files.[1]
- **Recipient rights:** the distributed derivative as a whole must be licensed under the applicable GPL conditions; do not add an EULA, NDA, no-commercial-use rule or no-redistribution condition that contradicts those rights (§§2(b), 6).[1]

The configured origin, inspected with credentials/userinfo and query/fragment excluded from output, is `https://github.com/pxlview/pixelview-desktop.git`. The corresponding project-page candidate is `https://github.com/pxlview/pixelview-desktop`. **A configured remote proves neither that the repository is public nor that it contains the release source.** Before calling it a source-download link in shipped notices, check anonymous access and the exact release archive/commit, including required dependencies.

## Native UI integration — no frontend changes made by this review

**No particular menu entry or label is mandated by GPLv2.** Section 2(c) has a conditional interactive-startup announcement requirement and an explicit exception when the upstream interactive program does not normally display such an announcement. Do not turn that into a blanket claim that every GUI requires a License menu, or that an About menu alone discharges all obligations. Keep an easy offline license route as release practice, and review the actual startup behavior if relying on the exception.[1]

Existing implementation, inspected in this checkout:

- `frontend/widgets/OBSBasic_MainControls.cpp:680–690`: `OBSBasic::on_actionShowAbout_triggered()` closes any existing About window, creates `new OBSAbout(this)`, calls `show()`, and sets `Qt::WA_DeleteOnClose`. Existing action name: `actionShowAbout`.
- `frontend/dialogs/OBSAbout.cpp:50–63`: clickable `ui->license`, labeled via `QTStr("About.License")`, connects `ClickableLabel::clicked` to `OBSAbout::ShowLicense()`.
- `frontend/dialogs/OBSAbout.hpp:15–18`: `ShowLicense()` is a **private Qt slot**, not an ordinary public method. To open directly on License without changing its visibility, a caller may use `QMetaObject::invokeMethod(dialog, "ShowLicense", Qt::DirectConnection)` on the GUI thread; alternatively expose a deliberate public selection API in the UI worker's own change. This invocation suggestion is source-based, not runtime-tested here.
- `ShowLicense()` (`OBSAbout.cpp:154–171`) resolves **`GetDataFilePath("license/gplv2.txt", path)`**, reads with `os_quick_read_utf8_file`, and displays plain text in `ui->textBrowser`. Its failure text points to upstream COPYING; it does not fetch a replacement license automatically.
- Source resource: **`frontend/data/license/gplv2.txt`**. `cmake/macos/helpers.cmake:354–375` recursively packages frontend data, preserving relative subdirectories under `Resources`; expected app destination: **`Pixelview.app/Contents/Resources/license/gplv2.txt`** (bundle name depends on the built target). `target_install_resources` is called at line 312. `AUTHORS` is separately added at line 152 and resolves on macOS as **`Contents/Resources/AUTHORS`**.
- Windows and Linux CPack configs explicitly point `CPACK_RESOURCE_FILE_LICENSE` to `frontend/data/license/gplv2.txt`.
- **Packaging gap to review:** the current UI resource is a reflowed GPLv2 text ending at §12; root `COPYING` additionally includes “How to Apply These Terms to Your New Programs.” They are not byte-identical. For release, ship the full unchanged root COPYING as well rather than claiming the UI resource is an exact full copy. No upstream license file was renamed or edited here.
- **UI race:** `OBSAbout` starts an OBS Patreon JSON request when its cache is empty; the result is connected to `ShowAbout()`. A late response can overwrite a directly selected License page. The UI worker should guard page selection or use a dedicated offline license viewer if adding a direct License action. The native dialog also contains upstream donation/contribution links and branding; it is not already a Pixelview-specific notice dialog.

Suggested separate notice wording (not installed by this review):

> Pixelview is a modified distribution based on OBS Studio. The OBS-derived program is free software under GNU GPL version 2 or, at your option, any later version, subject to the included component licenses. It is provided without warranty to the extent permitted by law. See the included COPYING and third-party notices. Corresponding source for this release: [insert verified release-specific source location]. Pixelview is not an official OBS Project release.

## Dependencies, drivers and remaining release gates

### Whole-app audit and implemented release gate

The dependency audit reports that the actual bundled FFmpeg build is **GPL version
3 or later**, while Qt **6.11.1** notices/source materials are incomplete. Therefore
the GPLv2 discussion above is not approval to distribute the current combination
under GPLv2-only terms. Resolve the actual combined distribution terms, provide the
applicable full license texts, and complete exact-version source/build/replacement
materials and third-party notices before release.

The local release flow now fails closed on the review-blocked
`release/source-inventory.json`. It requires versioned source/notices/inventory
artifacts, binds their SHA-256/size/immutable URLs in the release manifest and signed
app license metadata, and publishes/verifies all of them through the existing R2
conditional-write flow before advancing the appcast. See
[corresponding-source release artifacts](pixelview-source-release.md) for schema,
offline UI interface, collected rswebrtc source evidence, tests and unresolved gaps.
This is working enforcement plumbing, not a completed whole-app license clearance.

Inventory the **actual final app/installer**, not just this repository: bundled frameworks, dynamic/static libraries, plugins, codecs, browser engine if enabled, assets/fonts, installers and updater. Collect each component's exact-version license/copyright notices and source obligations. Qt, FFmpeg, CEF and other dependencies must be reviewed according to the versions, build options and licenses actually present; the OBS GPL text is not an exhaustive third-party notice bundle. GPLv2 distinguishes genuinely separate aggregation from a combined derivative, and FSF guidance discusses license compatibility and linking.[1][2]

Do not assume every proprietary hardware driver or SDK is prohibited, or that every such component is automatically exempt. GPLv2 §3 has a limited operating-system-component source exception, including an explicit limitation when the component accompanies the executable; FSF guidance illustrates why shipping a library can differ from using the installed system copy.[1][2] Independently check vendor redistribution rights, whether a driver is separately installed versus bundled, how it links/interacts, and any applicable exception or permission. No proprietary-driver inventory or blanket OBS-specific linking exception was established in this review. Codec/patent and platform-distribution terms may need specialist review independently of the GPL.

Before shipping: anonymously retrieve the release's source, test its build, inspect the final package for full license and third-party notices, verify the offline license UI and attribution, check modification notices, and audit enabled dependencies/SDKs. None of those release-wide checks is certified complete by this document.

## Sources

[1] https://www.gnu.org/licenses/old-licenses/gpl-2.0.html
[2] https://www.gnu.org/licenses/gpl-faq.html
[3] https://obsproject.com/help
[4] https://obsproject.com
