# Working in this repository

Pixelview Desktop is a fork of OBS Studio that ships a focused macOS app for
the Pixelview.io streaming platform. Read `PIXELVIEW.md` for what the product
is and how the tree is organised, `docs/features.md` for what is implemented
and verified today, and `docs/build-and-release.md` for building, testing,
signing and releasing.

## Ground rules

- This is a separate GPL fork. Never open a pull request, push a branch or
  target work against `obsproject/obs-studio` (the `upstream` remote is for
  rebasing only). Keep upstream notices, licensing files and the
  corresponding-source material intact; product changes must not remove
  upstream attribution.
- Pixelview keeps its own application identity and settings root
  (`~/Library/Application Support/pixelview/obs-studio`). Do not write to or
  read from a stock OBS installation.
- Do not invent backend endpoints or reuse Uplink/node passwords. The Desktop
  control socket and receiver flows are documented in `docs/features.md`;
  the backend repository is the authority for the wire protocol.
- Local builds are Developer ID signed through the helper in `cmake/macos`.
  Release preparation, notarization and publication are deliberate operator
  steps with their own gates; never run them as a side effect of other work.
- Secrets (device tokens, notarization and R2 credentials, the Sparkle
  private key) never go into the repository, logs or test fixtures.

## Documentation discipline

Keep the documentation set small and current instead of adding status files:

- `PIXELVIEW.md` — product description, repository structure, company and
  licensing information.
- `docs/features.md` — implemented behaviour, verification status and known
  limitations. Update it when a delivered feature, its verification status
  or a limitation changes.
- `docs/build-and-release.md` — toolchain, build, launch, tests and the
  release runbook. Update it when a command or gate changes.
- `docs/releases/` — one note per public release.

Do not add per-change audit, RCA, handoff or "status" documents; put the
durable outcome into the files above and the details into the commit
message. Do not describe planned work as implemented, and do not upgrade
verification claims beyond what was actually run.

## Verification expectations

Offline suites live under `test/pixelview` and `plugins/*/tests`; the app can
be built and launched locally against the development backend. When a change
touches native media, the control socket, pairing or the DeckLink paths, run
the affected suites and, where hardware or a backend is available, exercise
the real path before claiming it works. State plainly what was not run.
