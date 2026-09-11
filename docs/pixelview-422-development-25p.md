# DEVELOPMENT ONLY — authorized physical 1080p25 Main422 receive preparation

> Historical preparation only. Superseded by direct normal-build enablement; no special app, macro or environment token is required. Use the ordinary `build_macos_native422/frontend/RelWithDebInfo/Pixelview Desktop.app`. See [current status](pixelview-422-implementation-status.md).

**NOT release admission. Historical701ms refusal remains unresolved. Physical
SDI fidelity/cadence/audio and GUI acceptance are not established by this build.**

No existing development advertisement gate existed. Added a compile-time AND
exact environment opt-in. Normal CMake/shipping builds do not define the macro;
even setting the environment cannot enable their profile4. Development builds
without the exact environment likewise keep the ordinary offer unchanged.

- Build definition: `PIXELVIEW_DEVELOPMENT_MAIN422_25P=1`.
- Environment: `PIXELVIEW_DEVELOPMENT_MAIN422_RECEIVE=1080p25-limited709-UNVERIFIED`.
- Uses the real production transceiver/profile-offer hook; requires an existing
  admitted ordinary HEVC capability. Does not claim a native422 capability probe.
- Adds HEVC `level-id=120;profile-id=4;tier-flag=0;tx-mode=SRST;interop-constraints=1d0800000000`.
  Main/Main10 retain their original levels and payload alternatives. SDP level120
  does not itself constrain rate to25: the actual native filter additionally
  refuses classified rates other than25/1 before native activation.
- Existing strict1920x1080 progressive geometry, RTP finite-rate acquisition,
  header/PTL/limited709/native hardware validation and DeckLink owner policies
  remain. No header rewrite, decoder bypass, source extraction or injected test
  code is included in the module. Near-rate quantization limitations remain.

## Prepared artifact

`/Users/max/src/pixelview-hardware-25p/development-artifact/Pixelview Desktop DEVELOPMENT 25p.app`

APFS copy-on-write clone of the existing native422 app; only its WHEP module
recompiled from whole production translation units. Its runtime payload is
unchanged. Module rpaths are loader/executable relative, not the running app.
Ad-hoc signed nested plugin and application; `codesign --verify --deep --strict`
and isolated `--version` passed. This is not notarized or a shipping artifact.
Module SHA256: `acc01ebf0eb0ecca1fa97c35e18d67d891847904ab77c046b9e0e8204e0dd1b9`.
Standalone module is adjacent as `pixelview-whep` (same bytes).

No running GUI, sender/receiver private home, original built app, shared runtime,
physical card, credentials or network authentication was modified by preparation.
Parent owns receiver shutdown/restart and sender configuration; neither current
GUI can be described as enabled from this artifact existing on disk.

## Parent launch after reviewing the gate and clean receiver exit

Do not edit the receiver home while its old process is alive. Preserve its existing
profile and collection. After parent cleanly closes only the intended receiver:

The former private-HOME/special-artifact launch recipe is retired. This document
records historical isolated evidence only. For current tests, compile and launch
only the canonical `build_macos` Developer-ID-signed app with normal HOME and
`--app-config-dir`; see [canonical workflow](pixelview-signed-development.md).
Do not infer current receive policy from this historical opt-in artifact.

Verify the new process executable and environment before receive Start. Preserve
parent's existing additional profile/collection arguments if used. Sender does
not require this receive gate; pairing/UI readback must independently confirm
1920x1080,25/1,P216,limited709,Main42210 and the intended4K Mini input. Receiver
must select the Mini Monitor3G exact1080p25 native output; never infer physical
precision from UI selection alone. Conservative native failure is allowed and
must remain visible, not worked around.

## Verification and reproduction

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
python3 plugins/pixelview-whep/tests/run-development-25p.py
```

Runner compiles tests and a standalone module only; it does not replace the app
clone on subsequent invocations. Test output lives under the isolated artifact
work directory. Logs: `/Users/max/src/pixelview-hardware-25p/development-red.log`
(expected missing-profile assertion before implementation) and
`development-final.log` (passing final run).

Actual webrtcbin production signal/create-offer SDP verifies compile gate on/off
crossed with missing/wrong/exact env, truthful profile4 constraints and unchanged
ordinary HEVC levels. Shipping deterministic empty/stale/envelope regressions pass.
Actual native-filter TU tests reject23.976/24/29.97/30, admit25 through rate
activation, then reject deliberately invalid codec metadata before any VT session.
These seeded-rate tests are policy tests, not physical or actual RTP25 acceptance.
`git diff --check` passed. Parent independent gate review and GUI/card test remain.
