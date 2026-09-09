# Native receiver login error parity

## Audited contracts

Read-only sources: `pixelview-player/src/composables/auth/useLogin.js`,
`src/api/playerSession.js`, `src/content/auth.content.js`, `src/router/router.js`,
`src/views/LoginView.vue`, `src/stores/store.js`; and
`pixelview-backend-v4/routes/login/player.py`, `services/db.py`.

The backend's player login has three explicit denials, **all HTTP 401**:

| JSON detail | Meaning / native status |
| --- | --- |
| `Unauthorized` | Wrong password, invalid invite/group-link authorization, or absent/hard-deleted session. Native: `Wrong session ID or password. Please try again. If the session was deleted, ask your host for a new link.` |
| `Viewers limit reached` | Stream full. Native matches the webplayer: `Maximum viewers limit reached. Contact your host for more information.` The current backend comparison is strictly `len(viewer_ids) > viewers_limit`; this task does not change it. |
| `Session archived` | Authenticated archived session, including archived-and-soft-deleted sessions. Native: `The stream has ended. Thanks for watching! Ask your host for a new session link.` |

**There is no distinct session-not-found HTTP status/detail.** `authorized()` returns
no session for a missing database row and login raises `401 Unauthorized`, exactly
as for incorrect credentials. Native must not falsely claim it identified deletion.
`Session archived` is checked after password authorization; `deleted` is consulted
inside the archived branch, not a standalone login check. Database hard deletion
removes the row; soft deletion sets a flag. No user record was deleted for testing.

The web API wrapper allowlists just these three details. `useLogin` additionally
handles undefined response data with an unexpected-response message and handles
unknown/missing error detail by setting `unknownSession`; that flag is neither
returned by the composable nor rendered by LoginView. The router's `NotFound`
page (`404 - Page Not Found` / `The page you are looking for does not exist.`)
is for unmatched URL paths, not failed session lookup. Native deliberately provides
an actionable generic unavailable message rather than reproducing the silent
unknown-error path or mislabeling an HTTP route 404 as a missing session.

Native retains malformed/unsupported-success validation with retry/contact-host
advice, fixed allowlisted error strings, the 262144-byte body limit, redirect
rejection, secret cleanup and no endpoint delivery on denial. Unknown strings,
non-string detail, nested validation inputs and error bodies never enter status.
Known details with an unexpected HTTP status do not masquerade as a known denial.
Existing temporary HTTP recovery (408/429/502/503/504 and network failure) stays
unchanged. Token expiration is a separate control-channel event with the existing
`Receiver authorization expired or rejected. Start again to sign in.` message;
there is no invented expired-session login detail. Web-local kicked-session storage
and malformed-link routing are not backend login error branches.

## Verification

```sh
python3 test/pixelview/test_receiver_login_errors.py -v
python3 test/pixelview/test_receiver.py -v
# Explicit opt-in; an already-running loopback backend only:
PIXELVIEW_TEST_LIVE_LOGIN_ORIGIN=http://127.0.0.1:8000 \
  python3 test/pixelview/test_receiver_login_errors.py -v
```

The error harness compiles the actual `PixelviewReceiver.cpp` and
`PixelviewReceiverMac.mm`, sends real NSURLSession HTTP requests to an ephemeral
loopback server and checks exact production status, terminal/retry state, no media
endpoint, and one request for each denial. Each changed behavior was first observed
failing before its production mapping was added. It covers all three backend
denials, unknown/missing detail, malformed/empty/unsupported successful responses,
HTTP validation/server/route/redirect errors, oversized body and temporary retry.
The existing compiled controller tests additionally assert both token mutations
and terminal authorization close codes. The existing transport regression includes
registration, PONG, normal close, redirect/auth/body rejection and a 35-second soak.

Verified locally: the login-error suite passed **8 tests with live opt-in**;
`test_receiver.py` passed **2 tests** (controller plus native transport).
The live test used a fresh `missing-<UUID>` session ID and a synthetic password,
through the compiled production controller and native transport. Its observer
asserted the exact raw response, then printed only this known safe result:

```text
PASS: Wrong session ID or password. Please try again. If the session was deleted, ask your host for a new link.
LIVE missing-session HTTP 401 {"detail":"Unauthorized"}
```

This proves real nonexistent-session behavior, not a distinct not-found response.
Full/archived states are verified using actual backend-schema loopback fixtures,
not live account mutation. No player/backend edits, service restart, credential
lookup, GUI launch/kill, commit, full application build or real user deletion was
performed. Native rendered UI and live full/archived account acceptance remain
separate checks owned by the parent task.
