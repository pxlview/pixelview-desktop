# Player → Desktop link contract

Coordinated fallback: `pixelview://play/<sessionID>?token=<original encoded token>`.
Universal Link: `https://play.pixelview.io/<sessionID>?token=<original encoded token>`.
Missing token is allowed: prefill session and clear the form and stored password, including links for the same session ID. Session and password are replaced atomically, without intermediate field-change notifications, and saved without requiring Start. Operator session-ID edits clear the old password; programmatic link prefill does not discard the new one. This is **prefill only**, never start/login. The masked form password remains across Start/Stop/retry or login failure and is restored across application launches using macOS Keychain. Passwords never enter plaintext configuration, scene data or logs.

Public bundle ID: `com.pixelview.desktop`. Public Apple team: `MA47F3M8W9`
(from `release/macos.env`); AASA appID: `MA47F3M8W9.com.pixelview.desktop`.

The player router is `/:sessionID`, not a UUID-only route. Preserve case and exact decoded
session text. One nonempty path segment; no trailing slash, dot segments, encoded slash,
backslash, controls or whitespace-only/edge-whitespace ID. Bound UTF-8 ID to 256 bytes.
Token uses the actual player's UTF-8 base64url encoding, with optional correct padding;
legacy standard base64 is accepted (percent-encode `+` in web query strings). Do not
replace spaces with plus, decode twice, or transport a plaintext password. Desktop rejects
invalid UTF-8, control characters, malformed/noncanonical base64, duplicate query keys,
userinfo, explicit ports, fragments and other origins. Extra unique player query options
are ignored. URL bound 8192 characters, decoded password bound 4096 UTF-8 bytes.

Idle links select Receiving and fill the existing masked password/session controls.
Busy sending/receiving/preparing/stopping/recovery/pairing or shutdown rejects without
changing mode/credentials or interrupting output. Cold delivery has one memory-only
pending slot (latest link wins) until UI startup completes. Raw links and encoded tokens
are never logged or persisted; no automatic retry of busy links. Accepted link prefill
saves the session ID/name in user configuration and its decoded password only in Keychain.

## Receive credential persistence

The latest session ID and edited receiver name persist immediately on operator edits;
the hostname is the default name only when no name was saved. Password edits persist on
editingFinished, Start, accepted link delivery, and clean shutdown with an unfinished edit.
Stopping receive retains the masked form. Shutdown clears process copies without deleting
the saved password and never autostarts reception on the next launch.

The dedicated generic-password service is `com.pixelview.desktop.receiver`, account
`latest-session`, separate from sender/device pairing. It is non-synchronizable and
device-only. A single Keychain record contains the exact session ID, backend origin,
non-secret revision and decoded password. Configuration contains only ID/name/revision.
Load requires all bindings to match, so another session/backend never inherits a password.
Each replacement writes a new configuration revision before updating the Keychain item;
a failed same-session replacement cannot restore the previous revision's password.
The configuration file and Keychain are not a cross-store transaction: interrupted or
failed saves fail closed to a missing credential, rather than mixing credential pairs.

Keychain save/read/delete operations are read back, do not prompt, and have no plaintext
fallback. Storage failures use a separate visible warning, preserving authentication/media
errors and editable current input. Unlock Keychain and edit the password or click Start to
retry. If configuration itself cannot be saved, previous settings may remain; a best-effort
Keychain removal prevents stale credentials being reused when possible. Both stores failing
cannot guarantee removal of a previously saved item.

## macOS signing opt-in

`cmake/macos/pixelview-build.sh` passes these environment options (also available as
CMake cache options):

- `PIXELVIEW_ENABLE_UNIVERSAL_LINKS=ON`
- `PIXELVIEW_ASSOCIATED_DOMAINS_PROFILE=/absolute/path/to/authorized.provisionprofile`

Keep the existing explicit Developer ID identity and `PIXELVIEW_CODESIGN_TEAM=MA47F3M8W9`.
Default is OFF, including ad-hoc builds; these still support the custom scheme. ON fails
closed without a signed identity, the intended team, and a CMS-decoded, unexpired macOS
profile authorizing the exact app ID and associated domain capability. The profile is
embedded as `Contents/embedded.provisionprofile` before signing; the target uses the
separate associated-domain entitlements including application/team identifiers. Profiles
are operator-provided and never committed. The release helper inherits these options;
no R2 or publishing configuration is changed. After signing, inspect the actual bundle
entitlements/profile and perform clean-Mac HTTPS acceptance. Apple signing/provisioning
and website AASA deployment remain operator work, not completed by the local tests.

## Implementation / verification

- Qt `QFileOpenEvent` handles custom URL events without replacing Qt's AppleEvent handler.
- The existing OBS NSApplication `setDelegate:` hook preserves its delegate and OBS/CEF
  event dispatch. A per-instance runtime subclass adds NSUserActivity continuation and
  forwards unrelated activities to the original implementation. AppKit's cached optional
  selectors are refreshed; later delegate installations are handled too.
- The app-owned inbox is installed immediately after QApplication construction, attached
  to the real receive UI, and drained only after `OBSInit()` completes. It owns one parsed
  request, never a URL/activity history. UI destruction shuts it down.
- `python3 -m unittest discover -s test/pixelview -p test_deep_links.py` compiles the actual
  Qt parser, inbox, UI prefill and Objective-C++ activity bridge. It also checks bundle
  wiring, profile authorization and real tiny CMake ad-hoc/opt-in behavior.
- `test_receive_ui.py` compiles the full production receive include with real Qt/libobs,
  tests link prefill through the inbox, every native busy flag, normal masked controls,
  no login/autostart, edit/link persistence before Start, and actual Keychain reload in a
  separate process. Its unique test-only Keychain service is removed and read back absent
  even after test failure; sender pairing is never accessed. Missing/corrupt items,
  session/origin mismatch, injected storage denial, actual config-save failure, login-error
  preservation and plaintext/log non-disclosure are covered. Hardware/network
  boundaries are offline fixtures, not claims of full application or Apple OS dispatch QA.
- Standalone tests may emit expected offscreen Qt/font and graphics-context fixture
  warnings. The complete app builds successfully and passes deep/strict signature
  verification in the earlier link acceptance run (156 tests). The newer persistence change
  is verified by the compiled native offscreen harness; full-app build/relaunch acceptance
  remains a separate gate.
- Real LaunchServices cold and warm custom-scheme delivery passed in an isolated,
  ad-hoc-signed app copy with a unique test bundle ID and private HOME. A nonshipping
  inspector confirmed exact session and decoded Unicode password equality, masked
  password mode, Receiving selected, and Start receiving still idle. The warm link
  reached the same process. No production credentials were used. Test evidence is
  `runtime/deep-link-qa/verified.json` in the sibling `pixelview-whep-spike` checkout.
- The web player is unchanged: the proposed player button and association asset
  were reverted at the user's request. Existing HTTPS session/token links remain
  the input contract. Production HTTPS dispatch requires separately serving the
  matching AASA at the domain's `/.well-known/apple-app-site-association`, plus
  signed/provisioned release acceptance; neither is deployed or verified here.

## macOS deployment status

Custom-scheme Info.plist registration does not require Associated Domains. No global
LaunchServices registration is performed by this implementation. HTTPS dispatch requires
Apple Associated Domains authorization, a correctly signed/provisioned app, and a matching
AASA served by play.pixelview.io. Adding local code does **not** deploy or verify that
association. Browser same-domain navigation/user preferences may keep HTTPS in the browser;
custom-scheme links remain an alternative, but no player button is added here. Never put links containing real tokens in
shell arguments, screenshots or test logs.
