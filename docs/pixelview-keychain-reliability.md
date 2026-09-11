# Keychain reliability and application-only configuration isolation

This is a source/offline-regression checkpoint, **not** a rebuilt app, signed
artifact, migration, release or live Keychain acceptance result.

## Failure boundary and preservation

The reported private `HOME` / `CFFIXED_USER_HOME` launches under
`/Users/max/src/pixelview-hardware-25p` returned `SecKeychainCopyDefault = -25307`
and searched only System. Apple's `SecBase.h` identifies -25307 as
`errSecNoDefaultKeychain`; that is not evidence that a saved credential is absent.
Neither condition is used as a final diagnosis or a reason to suppress explicit
authorization. The application now offers normal native macOS authorization on
explicit operator actions. No login Keychain state was changed or re-probed by
these offline tests; live acceptance still requires the operator.

Both native stores keep startup, reconnect, automatic saves and shutdown
noninteractive. Background calls use a try-lock: they fail promptly rather than
waiting behind a native authorization dialog, then scope and restore the legacy
interaction policy. The missing-default preflight applies **only** to background
requests, where a System-only search list must not imply a missing user secret.

Explicit Pair, Unpair and receiver Start credential recovery run on a Qt worker. They
serialize against the same guard but leave macOS's default interactive policy
alone—there is no call that globally enables UI around an asynchronous operation.
They bypass the default-Keychain preflight and let the actual SecItem API return
its result or display native authorization. Nested readback inherits the same
explicit intent. The GUI remains responsive; background operations never run
inside another request's interactive scope. Qt disconnects completion when its
owner dies, and workers retain only value snapshots/the receiver store.

This preserves the existing file-based Keychain domain; no data-protection
Keychain migration, new access group, custom ACL/store, password export or
programmatic Keychain creation/selection/unlock is introduced. Apple's
`kSecUseAuthenticationUI` and LAContext describe per-query authentication control,
but are not a reliable substitute for suppressing legacy trusted-application ACL
dialogs. The receiver LAContext now respects explicit versus background intent.
The legacy suppression remains narrowly scoped to background requests, not all
credential access.

Sender removal previously considered an empty `loadDevice()` result sufficient
verification. That result also represents denied/unavailable reads. Removal now
succeeds only when a direct verification query returns `errSecItemNotFound`.
Receiver removal already had that stricter verification rule. All other statuses
remain failures; failure is never permission to silently unpair/delete or write a
plaintext credential fallback. A successful OS mutation followed by failed
verification remains an **unconfirmed** operation, not a promise of rollback.

There are no dedicated Keychain buttons. The sender retains its durable identity
Offline on an unavailable read. Ordinary Pair with Pixelview retries access to
that existing identity without requesting a new admin code. Its tooltip and
failure guidance explain this behavior. Pair is hidden during ordinary transient
disconnects when a credential is already held in memory; Unpair remains available
subject to native busy guards. New installations still use the admin-code Pair
exchange. Failed/canceled Unpair preserves the stored identity, keeps Unpair
available and disables reconnect; identity is cleared only after confirmed
removal. Remote revocation disables reconnect without deleting the local secret
or requesting an authorization prompt. There is no cleanup workflow.

A successful sender credential read now remains in the connection object's
process-only authorizedToken until explicit Unpair, remote revocation or object destruction (a new
successful Pair replaces it). Reconnect no longer consumes it with std::exchange
and then rereads Keychain on every subsequent socket attempt. Startup/background
read failures remain retryable using existing bounded backoff with native UI
suppressed. They do not change pairing metadata, delete credentials or grant
streaming authority. Server ready/lease checks still apply to every connection.

Integration verification caught a retained process cache after terminal 4401:
reconnect was already disabled, but the revoked credential remained in memory.
The compiled production disconnect callback now clears that cache immediately,
including after a generic transport failure and during pending media/setup drain.
It retains local identity and the secure record for explicit Unpair; revocation
does not initiate a Keychain worker. The regression failed on the cache assertion
before the one-line production fix and passed afterward.

The receiver's ordinary Start receiving reloads unknown saved state without
rotating its revision, or retries saving explicit pending edits, on a worker.
Canceled/failed reads retain the unknown-state latch and never log in with an
empty replacement. Failed saves can still use the current explicitly entered
password for this session. Metadata saves and shutdown preserve inaccessible
records. Receiver reconnect never invokes this user-action authorization path.
Successful recovery starts only the user-requested receive operation; startup
and deep links never auto-start reception.

Diagnostics on standard error contain a fixed operation name, fixed category
and numeric OSStatus only. No account, origin, credential, query dictionary or
arbitrary OS/backend error text is printed. Capture stderr when investigating a
launch. `interaction-required` (-25308) is **not** a diagnosis of a locked login
Keychain: legacy trusted-application access can also require interaction.
`authorization-denied` (-25293), `no-default-keychain` (-25307),
`keychain-unavailable`, and generic `security-error` remain distinct. UI warnings
continue to avoid falsely declaring an inaccessible record removed.

## Launch contract for the application owner

OBS's `--portable` parsing is conditional on `ALLOW_PORTABLE_MODE` (Windows or
`ENABLE_PORTABLE_CONFIG`). Its path is a build-relative `CONFIG_PATH`, not an
arbitrary per-instance directory selector. Do not depend on it for independent
macOS sender/receiver roots or rewrite HOME to make it work.

The new Pixelview argument is:

```text
--app-config-dir <absolute-path>
```

It overrides **both** `GetAppConfigPath` and `GetAppConfigPathPtr`, including
null/empty name calls. The supplied directory is the Pixelview root itself;
OBS files are below `<absolute-path>/obs-studio/`, not another `pixelview/` child.
Without the option, existing `~/Library/Application Support/pixelview/obs-studio`
behavior is unchanged. Relative/missing paths fail before application startup.
An explicit root takes precedence over portable mode; it is retained in normal
argument-based restarts. The option does not sandbox external plugins, media
paths, macOS preferences/caches or Keychain. Use a private operator-owned
configuration directory, and never two writers on the same root.

After source changes are settled, a new binary is built/verified by the signing
owner, and that owner authorizes GUI launches, use the **same verified app**:

```sh
# Run from the normal logged-in user's Terminal, not a private-HOME wrapper.
# HOME must be /Users/max; CFFIXED_USER_HOME must be unset.
unset CFFIXED_USER_HOME
APP='/Users/max/src/pixelview-desktop/build_macos/frontend/RelWithDebInfo/Pixelview Desktop.app'
"$APP/Contents/MacOS/Pixelview Desktop" --multi \
  --app-config-dir '/Users/max/src/pixelview-hardware-25p/sender-config'
# In a second normal-user Terminal, if concurrent receive is authorized:
"$APP/Contents/MacOS/Pixelview Desktop" --multi \
  --app-config-dir '/Users/max/src/pixelview-hardware-25p/receiver-config'
```

These are recipes only; no app was launched/restarted and no bundle overwritten
by this patch. The existing unrebuilt binary does not implement the option.
The intended signed-development artifact/helper is documented in
[pixelview-signed-development.md](pixelview-signed-development.md).

### Separate settings are not separate secure identities

The bundle ID remains `com.pixelview.desktop`; sender service remains
`com.pixelview.desktop.device`, account = existing normalized backend origin.
Receiver service remains `com.pixelview.desktop.receiver`, account =
`latest-session`. No config-root suffix, test suffix or new access group is added
to shipping identity. Sender/receiver settings can be separate, but these secure
records are deliberately shared by the same signed app under the same macOS
user. Unpair changes the origin's shared device record, not just one config
folder. Receiver credential replacement changes the one latest receiver record;
revision mismatches prevent another root from resurrecting a stale password.

Do not use this as a two-independent-senders recipe: two roots using one origin
share the same device credential and do not create separate backend identities
or publish leases. Avoid concurrent pairing/receiver-password mutations across
instances. The process-local mutex is not a cross-process transaction lock.

## Developer ID access model and safe migration

Apple TN2206 says the creating app is trusted with its item and its identity is
tracked with its designated requirement (DR), except custom ACL tracking.
TN3127 describes Developer ID requirements involving the Apple anchor,
identifier, certificate type and team. **Same team alone is insufficient.**
Keep `com.pixelview.desktop`, the configured Developer ID Application identity
and compatible DR across rebuilds. No broad trusted-application list, custom
team-wide ACL, new entitlement or global partition-list change is needed for
this same-application model.

Coordination with the signing worker is through its actual helper and report:
`cmake/macos/pixelview-signed-development.py` checks the root identifier,
Apple anchor, team OU and Developer ID Application certificate marker, verifies
deep/strict signatures and nested signers, and records the actual DR. This source
contract has been inspected; **a successful newly signed artifact and live
same-DR Keychain access have not been established by the storage tests**. The
signing owner must supply its real verification report before acceptance.

Earlier ad-hoc builds may have item ACLs tied to a different code identity;
re-signing with Developer ID does not retroactively make those ACLs match.
Preserve those records. The operator may need to authorize access using macOS
Keychain Access or explicitly Unpair and Pair with a fresh admin code after
resolving access. Do not auto-delete/migrate items, unlock the login Keychain,
approve prompts, export secrets, or weaken access to make a regression green.
An app rename alone must not change bundle/service/account identities.

For legacy private-HOME **configuration files**, after both owned instances are
stopped, the operator may copy the desired `pixelview` directory's contents to a
new app-config root (back up first; retain original). Do not copy `Library/Keychains`
or infer that copied pairing metadata proves credential access. The new root
should have `obs-studio` directly beneath it. Keep unknown/durable pairing
metadata intact and verify normal-user secure access separately.

## Source investigation for invisible normal UX

The previous sender refresh offered an authorization button for **every**
`paired && !ready` state, even an ordinary network outage. Its connection path
consumed the one-shot authorization token, then synchronously reread the secure
record on later reconnects. Those two source paths explain the spurious button
and how a transient disconnect could become credential-unavailable despite an
already authorized process. Compiled Qt regression tests reproduced both before
fixing them. This is not proof of the OSStatus behind a particular live outage.

Read-only inspection found the canonical existing bundle signed with Developer ID
Application for team MA47F3M8W9 and a DR including com.pixelview.desktop, Apple
anchor and Developer ID certificate constraints. It was not rebuilt or resigned.
Seven same-day normal app log files contained none of the fixed Keychain category
or control-failure strings; Keychain diagnostics go to stderr, not those logs.
No running exact-name Pixelview Desktop process was available to inspect stderr.
Consequently the actual live failure cannot be attributed to locked Keychain,
ACL denial, missing default or revocation from those logs. No live credential
queries, unlocks, ACL changes or authorization prompts were performed.

macOS authorization is not bypassable: a locked/missing login Keychain, denied
legacy-item ACL or an item created under an incompatible prior designated
requirement may still require the owner to authorize access or restore normal
Keychain availability. A stable compatible signature and an already authorized
item permit the ordinary prompt-free path. A native Cancel is not proof that the
item is missing or that unpairing succeeded. Migrating to the data-protection
Keychain requires deliberate entitlement/provisioning and existing-item migration;
flipping a SecItem query flag would hide existing file-based records, not fix them.

## Authoritative references

- [Apple TN3137: On Mac keychain APIs and implementations](https://developer.apple.com/documentation/technotes/tn3137-on-mac-keychains): default file-based SecItem domain and shim limitations.
- [Apple SecKeychainSetUserInteractionAllowed](https://developer.apple.com/documentation/security/seckeychainsetuserinteractionallowed(_:)): UI allowed by default; restore any scoped suppression.
- [Apple kSecUseAuthenticationUI](https://developer.apple.com/documentation/security/ksecuseauthenticationui): supported query key and default Allow semantics.
- [Chromium legacy-Keychain compatibility guard](https://chromium.googlesource.com/chromium/src/+/cc1a44a0893dbb937eadcfea9c4bdd1691262e7c/crypto/apple/scoped_keychain_user_interaction_allowed.cc): documents the file-based per-query suppression limitation (FB16959400); this is implementation corroboration, not an Apple API guarantee.

- [Apple TN2206: macOS Code Signing In Depth](https://developer.apple.com/library/archive/technotes/tn2206/_index.html), Keychain Access Controls and designated requirements.
- [Apple TN3127: Inside Code Signing: Requirements](https://developer.apple.com/documentation/technotes/tn3127-inside-code-signing-requirements), Developer ID and Xcode DRs.
- Apple macOS Security SDK `SecKeychain.h`: `SecKeychainCopyDefault` retrieves a retained default reference; `SecKeychainSetUserInteractionAllowed` scopes optional interaction. These legacy APIs are deprecated, retained here to preserve the existing file-based item domain rather than silently migrate credentials.
- Apple macOS Security SDK `SecBase.h`: OSStatus definitions, including -25307 and -25308. [errSecNoSuchKeychain](https://developer.apple.com/documentation/security/errsecnosuchkeychain) is distinct from no default.

## Offline verification

The invisible-UX revision passed 18 focused tests (compiled real Qt pairing
handlers, full receive include/controller with memory storage and offline
transport, real native storage with mocked Security boundaries, asynchronous
prompt suppression, cancellation and owner destruction). RED runs first caught
the dedicated buttons, consumed reconnect credential and nonretryable background
read. Run only the named offline receiver methods, not the entire receive suite:

```sh
cd test/pixelview
python3 -m unittest test_pairing_ux test_keychain_async test_keychain_reliability \
  test_keychain_noninteractive \
  test_receive_ui.ReceiveUI.test_unavailable_credential_preserved_until_explicit_replacement \
  test_receive_ui.ReceiveUI.test_offline_native_receive_lifecycle \
  test_receive_ui.ReceiveUI.test_native422_preview_telemetry \
  test_receive_ui.ReceiveUI.test_native_receive_credentials_lifecycle_and_shared_outputs \
  test_receive_ui.ReceiveUI.test_shared_monitor_device_receive_and_native_busy_guards \
  test_receive_ui.ReceiveUI.test_actual_mode_widgets_and_busy_lock
```

Qt offscreen/font warnings and fault-injected safe Keychain categories are
expected. No full application build, live secure-store access, signed rebuild
retention, real network disconnect or physical hardware acceptance is claimed.

`test_keychain_reliability.py` compiles the actual sender storage block and entire
receiver store against in-memory/refusing Security boundaries: save/read/remove,
readback denial after delete, locked/interaction-required, missing default,
missing Keychain and authorization denial. A recovered boundary recovers the
original secret after failed mutations. It checks diagnostics for categories
and credential/origin non-disclosure. It also exercises explicit interactive
save/read/delete, user cancellation, denied mutations, retry and bypassing the
background-only missing-default preflight. `test_keychain_noninteractive.py`
checks policy restoration and no credential calls after policy setup failure.
`test_keychain_async.py` compiles the real Qt worker and guard, simulates a pending
native prompt, verifies advancing GUI timer ticks and prompt-free background
failure, then destroys a completion owner while its task is pending. The compiled
pairing UI tests execute asynchronous save/unpair completion. The explicitly
offline receiver suite checks authorization failure/retry, preserved revision and
old-secret recovery, explicit replacement, and the production layout.
`test_config_isolation.py` compiles both actual config helper bodies and checks
null/empty paths, two distinct roots, spaces, default behavior, invalid roots,
fail-closed buffer truncation and unchanged HOME. Pairing UX and explicitly offline receiver lifecycle tests cover
the existing persistence/retry UI boundary; do not run live Keychain fixtures
while the OS is blocked. These tests do not establish live ACL access, GUI
startup, actual app restart persistence, signing, media or hardware fidelity.
