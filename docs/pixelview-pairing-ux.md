# Pairing UI

The sidebar offers **Pair with Pixelview** or **Unpair**. There is no separate
cleanup button or recovery workflow.

## Production and local-development pairing

New pairings use `https://api4.pixelview.io` by default. The pairing dialog asks
only for the one-time admin pairing code (masked); it shows no backend URL field,
URL prompt or local-development checkbox.

For local development, explicitly opt in at **runtime** when launching the same
app (after the updated source has been built separately):

```sh
cd /Users/max/src/pixelview-desktop
PIXELVIEW_LOCAL_DEVELOPMENT=1 python3 cmake/macos/pixelview-launch.py
```

Only the exact value `1` enables the development form; unset, empty, `0`, `true`,
and other values use production. The launcher inherits this environment variable.
Quit the existing process before launching with a different environment; setting
it during a build or in another terminal does not change a running app.

The development form defaults to `http://localhost:8000` (the existing backend
port documented in [the backend smoke command](pixelview-build.md#desktop-protocol-build-and-harness-build-35)),
with **Local development** checked. Both the origin and checkbox remain editable;
for a backend advertising `127.0.0.1`, override to `http://127.0.0.1:8000` so the
media resource has the same origin. Unchecking the box requires HTTPS. HTTP is
still allowed only on loopback; TLS trust and resource-origin validation are not
bypassed.

This switch changes **new pairing form defaults only**. Saved pairings reconnect
using their original origin and stored development flag, with the same Keychain
account and authorization flow, even if the launch environment changes. To switch
an existing pairing between environments, first explicitly Unpair successfully,
then Pair with a fresh admin code. No credential migration, deletion or replacement
is triggered by the environment variable. Form overrides are saved only through
the existing successful pairing flow; cancellation changes nothing.

- A completed pairing requires secure device-credential storage, an authenticated
  `ready` message matching the exchanged identity, and a successful identity save.
  A failed secure save does not report pairing success. Check Keychain access and
  use a new admin pairing code when retrying Pair.
- Unpair stops connection activity and disables reconnection. If credential removal
  or identity persistence fails, it reports **Unpair incomplete** and retains the
  **Unpair** action. Pair stays hidden/disabled until Unpair succeeds. Correct the
  reported access/save problem and click Unpair again; no automatic credential
  overwrite or backend-origin change is introduced.
- A credential that cannot be read is not treated as proof that pairing was removed:
  the saved node stays Offline and its identity is preserved. Unpair is an explicit
  local removal request, not revocation of the admin registration.
- Native output/setup, stopping, shutdown, exchange and lease/recovery guards remain
  in force. Internal removal and persistence checks are unchanged.

## Verification and limits

`test_pairing_defaults.py` compiles and executes the production Qt dialog and
submit path offscreen. Production and development behavior each failed before
implementation and then passed. It covers exact opt-in values, masked token-only
production UI, localhost:8000 defaults, editable overrides, remote-HTTP rejection
by the real origin validator, cancellation and the existing origin-change guard.
Its four cases pass on the committed tree (2026-09-13). The dialog change was
compiled into the locally signed development bundles used for the DeckLink
receive verification that day, but the production and development pairing forms
were not exercised interactively there; this is not an integrated pairing
acceptance claim.

`test/pixelview/test_pairing_ux.py` compiles the production Qt widgets, refresh,
Unpair transition, pair completion, authenticated-ready callback and connection
method against offline storage/config/transport boundaries. It covers readable
layout, Pair/Unpair visibility and labels, busy guards, removal/save failure and
retry, secure-save failure/success, durable-ready success/failure, and preservation
of an inaccessible credential. `test_keychain_noninteractive.py` compiles the real
storage functions with mocked Security APIs; it does not access any Keychain.

These are offline regressions, not live Keychain acceptance. The previously
reported private-HOME environment with no default Keychain is not repaired by this
UI change. No Keychain operations, application configuration changes, app restart,
live hardware test, full app build or running-bundle update was performed for this
patch. A normal app rebuild/deployment remains a separate coordinated step.
