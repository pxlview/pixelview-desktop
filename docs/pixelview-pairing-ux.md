# Pairing UI

The sidebar offers **Pair with Pixelview** or **Unpair**. There is no separate
cleanup button or recovery workflow.

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
