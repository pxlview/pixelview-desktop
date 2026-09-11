# Canonical local Developer ID build and launch (not a release)

Always build and launch this same bundle:

```text
/Users/max/src/pixelview-desktop/build_macos/frontend/RelWithDebInfo/Pixelview Desktop.app
```

Do not use alternate signed-development, native422, cloned or special development
apps for normal testing. Old artifact paths in historical evidence are not launch
recommendations. Do not overwrite a running bundle; stop it yourself first.

## Repeatable commands

Run in your normal logged-in Terminal (`HOME=/Users/max`). No private HOME:

```sh
cd /Users/max/src/pixelview-desktop
unset CFFIXED_USER_HOME
# Metadata only; no signing, build, GUI or credential access:
bash cmake/macos/pixelview-build.sh --check-only
# Only once all source-editing workers have finished and the app is stopped:
BUILD_JOBS=1 bash cmake/macos/pixelview-build.sh --allow-dirty --source-settled
# After successful compilation and signature verification; does not open GUI:
python3 cmake/macos/pixelview-launch.py --check-only
# Launch when authorized (not performed by the build helper):
python3 cmake/macos/pixelview-launch.py
```

The ordinary shell helper dispatches `pixelview-signed-development.py` for local
builds. It explicitly selects Cinecode OU's installed Developer ID Application
certificate `62CC493EC11F0031CF3ED9419671A78F5FD8E584`, team `MA47F3M8W9` from
`release/macos.json`. This is public certificate metadata, not a private key.
Explicit `--identity`/`PIXELVIEW_CODESIGN_IDENTITY` overrides must resolve uniquely
to a valid Developer ID Application certificate for that team. There is no ad-hoc
or Apple Development fallback. The launcher deliberately requires the pinned
identity's verification report for this repeatable local workflow.

Local output/configuration overrides cannot redirect this wrapper away from
`build_macos`/`RelWithDebInfo`. Normal HOME is required, and child environments
remove `CFFIXED_USER_HOME`. Upstream CMake and non-macOS workflows are unchanged.
The private `PIXELVIEW_LOCAL_SIGNING_VERIFIED` recursion marker is not an operator
option; do not set it to bypass preflight. No bundle cloning or deletion occurs.

The helper checks running executable paths before staging/building, refuses
symlinked output, and requires explicit dirty-tree/source-worker acknowledgements.
Do not start this app or another build while it is building. It hashes tracked and
nonignored untracked source before/after compilation and verification, rejecting
source drift. Existing pinned `.deps` are reused and validated; missing inputs may
be downloaded/rebuilt. The 6 GiB free-space floor is not a capacity guarantee.

## Native signing authorization

Read-only `SecKeychainCopyDefault` / `SecKeychainGetStatus` metadata can report a
locked default Keychain even while `security find-identity` lists a valid identity.
**Interactive local builds warn, then let actual Xcode/codesign request native
authorization.** They do not preemptively abort merely because this metadata says
locked. `BUILD_JOBS=1` limits simultaneous requests. The operator alone handles
any native security prompt; a prompt is not guaranteed, and signing can still fail.

For an explicit fail-closed unattended gate:

```sh
bash cmake/macos/pixelview-build.sh --check-only --unattended
```

`--unattended` refuses locked/unavailable metadata before staging; an unlocked
status still does not prove private-key authorization. Use check-only for
unattended readiness checks. No helper unlocks a Keychain, supplies a key password,
exports keys, changes ACLs/partition lists, approves dialogs, or retries with
ad-hoc signing. Signing failure stops the pipeline and prevents acceptance.

Native Xcode signing keeps sign-on-copy dependencies, hardened runtime, timestamps
and existing entitlements. Verification uses `codesign --deep --strict` only for
verification (never signing), checks Apple/team/bundle designated requirement,
every nested Mach-O signer, no debug entitlement, root entitlements and arm64,
and runs only harmless `--version`. The report is
`build_macos/signed-development-verification.json`; it binds source digest, HEAD,
actual signatures and designated requirement. The launcher requires this report
and rechecks signatures/code metadata before executing the fixed bundle.

## Two settings roots, one app and normal Keychain

After the new source is compiled and launching is authorized, run in separate
normal-user terminals:

```sh
python3 cmake/macos/pixelview-launch.py --app-config-dir /Users/max/src/pixelview-hardware-25p/sender-config
python3 cmake/macos/pixelview-launch.py --app-config-dir /Users/max/src/pixelview-hardware-25p/receiver-config
```

The launcher adds `--multi` with an explicit config root. Both execute the same
canonical app, with normal HOME and unset CFFIXED_USER_HOME. The option isolates
application files, not Keychain identities; shared pairing/receiver credential
records and mutation limits are described in [Keychain reliability](pixelview-keychain-reliability.md).
Never run two writers against the same config root.

Optional one-time **nonsecret audio/video settings only** migration, before either
instance uses the destination (destination must not exist):

```sh
python3 cmake/macos/pixelview-launch.py \
  --migrate-from '/Users/max/src/pixelview-hardware-25p/receiver/Library/Application Support/pixelview' \
  --app-config-dir /Users/max/src/pixelview-hardware-25p/receiver-config
```

Use the corresponding sender source for sender settings. This command exits
without launching. It retains originals untouched, writes a filtered backup
`migration-settings-backup.json`, and copies only allowlisted basic.ini video
format/rate/color and audio format settings into new profiles. It never copies
whole configuration files, scenes, service/stream settings, pairing fields,
passwords, Keychain files or credentials. Re-select scenes, hardware and profile
manually; this is deliberately not a full profile clone. Existing destinations
fail closed rather than overwrite. No actual user-settings migration is implied
by installing or testing this script.

Developer ID's stable designated requirement does not retroactively grant access
to earlier ad-hoc CDHash-based item ACLs. Native user-approved access or re-pairing
may still be necessary. Never delete credentials to hide an access failure.

## Release boundary and tests

`pixelview-release.sh` and the release entrypoint retain their clean-tree/exact-tag,
fresh release directory, explicit signing, notarization and publication gates.
This local path disables Sparkle, Associated Domains and release mode; it never
notarizes, publishes, commits, pushes, installs or restarts an app. Do not treat a
dirty incremental local build as release provenance or GUI/hardware acceptance.

```sh
python3 cmake/macos/test_signed_development.py
python3 -m unittest discover -s test/pixelview -p test_build_signing.py
python3 -m unittest discover -s test/pixelview -p test_pixelview_release.py
bash -n cmake/macos/pixelview-build.sh cmake/macos/pixelview-release.sh
bash cmake/macos/pixelview-release.sh --validate-config
```

Offline tests use synthetic metadata and temporary settings, not real credentials
or simulated claims of a successfully signed app. Final native compilation waits
for all source workers to settle; only its real verification report establishes
that the canonical bundle has been rebuilt and signed.
