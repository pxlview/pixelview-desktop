# Control-only WebSocket recovery (working tree)

## Protocol dependency and actual routes

Sender retention requires the **updated backend PR207 lease-resume contract**, not merely a retrying WebSocket. Initial authenticated `/desktop/ws` readiness must include boolean `resumed:false`. Older backends omit this field; Desktop keeps their existing fail-closed stop/drain/fresh-start behavior on control loss. No server lease is bypassed.

The actual receiver is independent of sender pairing: `PixelviewReceiver.cpp` performs `/login/player` → `/wsocket?token=...` → `ADD_VIEWER_WEB`. PR207 also offers optional `register_receiver` on `/desktop/ws`, but this frontend does **not** use that route. Unpaired receiving remains supported; a shared-socket migration is not included.

Exact sender resume fields agreed with the backend workstream:

- Initial `ready`: existing identity, `heartbeat_interval:15`, `lease_seconds:45`, plus `resumed:false` (capability indicator).
- Reconnect auth: `{type:"auth",device_token:<process-held pairing token>,resume_fence:<last fence>}`. No cached ingest credentials are sent as control authentication.
- Successful resume `ready`: `resumed:true`, a strictly greater positive integral `fence` (backend rotates it atomically), and positive numeric `lease_expires_at`. The backend transfers only the same device's live matching lease and does not extend its expiry during transfer. Failed resume is terminal (4409); no silent new start.
- Desktop retains its **original monotonic deadline** on resumed readiness, updates the fence, then immediately sends an ordinary application heartbeat. Only its ACK renews the local request-time deadline. A lost resume reply may lead to a fence mismatch on the next attempt; this intentionally stops rather than guessing ownership.

Exact receiver identity fields:

- `/login/player` JSON adds `client_type:"pixelview-desktop"` alongside unchanged `device_type:"WEB"`, `platform:"MAC"`, `browser:"GSTREAMER"`, `mobile:false`, name/session/password.
- `ADD_VIEWER_WEB.data` adds `client_type:"pixelview-desktop"` alongside name, stable random `viewer_id`, `initial_load:false`.
- Automatic retry retains the same viewer ID, but performs fresh login/token validation and re-registration. Only successful registration can initially deliver a WHEP endpoint. A reconnect returning the identical endpoint does not call media connect/disconnect again. A changed authenticated endpoint is replaced only after registration succeeds.
- Backend/admin support for **this legacy registration path** is required to display the Desktop identity and enforce same-viewer lifecycle rules across retries. Sending the field alone does not establish server acceptance or deployed admin display. Shared `/desktop/ws` receiver support alone is insufficient.

## Timeouts and safety

Both macOS transports use supported `NSURLSessionWebSocketTask.sendPingWithPongReceiveHandler`. A shared small policy sends RFC6455 PING every **5 seconds**, permits one outstanding PING, and fails after **5 seconds without its PONG**. The main-thread watchdog polls every **250 ms**; generic failure delivery waits **100 ms** to let authoritative close/HTTP/TLS delegates win. A two-second delayed PONG is tolerated. Typical silent-loss detection is within roughly 10 seconds plus scheduling, not an exact real-time bound.

A PONG never refreshes application authorization. Sending still requires the application heartbeat every **15 seconds**, a **30-second monotonic request/ACK budget**, and the server's **45-second lease**. Sender control-only recovery retries after **1 second** without waiting for healthy media to drain; authentication remains bounded to **10 seconds** and the existing local lease deadline always wins. Only already-started, leased output on a resume-capable backend can continue. Initial/pending media setup retains the old drain/reacquire path. Native media loss retains the separate OBS retry/lease flow.

Receiving permits at most **7 seconds after detected control loss**, clamped to the original media authorization's remaining time, to reauthenticate/re-register while keeping the existing media source. Neither deadline resets on failed attempts or a fresh HTTP login alone. The inspected `.desktop-integration/pixelview-backend-v4/routes/login/player.py` issues viewer tokens with `expires=int(time.time()) + 86000`; no response/schema change is needed. Desktop uses a conservative **23-hour absolute monotonic deadline anchored before each login request**, promoting that candidate only on successful, still-valid registration. Recovery must complete before both the old authorization deadline and the seven-second grace. Checks before readiness reject late ACKs even when watchdog callbacks are queued. Automatic renewal starts at **22 hours from the successful authority's login-request start**, through the existing control reconnect/login/registration path. Media remains retained for at most seven seconds and never beyond the old 23-hour deadline. A separate hard-expiry watchdog remains armed across retries; only successful registration replaces its deadline and schedules the next proactive renewal. There is no scheduled manual Start every 23 hours: manual Start is required only if renewal/recovery fails terminally, grace elapses, or hard expiry is reached. Receiver retries remain exponential **1–30 seconds** (initial login/registration can retry while no media exists). Login/registration budgets remain **30/15 seconds**, server application-ping silence **65 seconds**. A changed endpoint, expired grace, or terminal authority event tears media down. This is not a promise to survive a shared network/RTP/media outage or a hard real-time shutdown guarantee while the Qt thread is stalled.

Stop/shutdown/Unpair, auth/TLS errors, identity mismatch, malformed protocol, lease loss/expiry, revocation, kick and session deletion remain terminal. Receiver handles `SOCKET_USER_KICKED` and `SOCKET_SESSION_DELETED` immediately, not only their following close. Only close codes 0/1001/1006/1011/1012/1013 are retryable. Detached native delegates and receiver attempt generations cannot mutate newer attempts. Transient sender cancellation uses task cancellation rather than an explicit normal close that would release a resumable server lease.

## Verification and remaining gates

Final parent verification after automatic-renewal correction: **25 tests passed
in 148.000 seconds**, covering native socket liveness, receiver recovery/expiry/
renewal, sender policy/frontend retry, media-failure separation, WHIP retry,
recovery UI and pairing completion. `git diff --check` passed. The parent also
reviewed the independent expiry watchdog and request-anchored renewal path.
Backend PR207 independently passed **380 tests, 3 skipped** with local DynamoDB
and Redis enabled; Admin passed 6 Node and 37 Vitest tests plus build/typecheck/
lint. These separate suites do not establish live end-to-end media continuity.

Receiver authorization-expiry follow-up: `python3 test/pixelview/test_receiver.py` passed **3 tests in 62.811 seconds**, including compiled native controller and NSURLSession loopback tests. RED→GREEN assertions caught unclamped recovery, late registration after repeated fresh HTTP logins, and expired initial registration. The proactive-renewal correction additionally passed all **3 tests in 63.071 seconds**. Its new proactive renewal assertion failed against the forced-expiry implementation, then passed with automatic renewal. Mutation checks rejected cancellation of the independent watchdog and bypass of old-authority readiness validation. The `receiver_expiry.cpp` harness compiles production controller logic with the 23-hour deadline, 22-hour refresh and retry delay scaled down; it exercises timer-driven expiry and deliberately withholds Qt timer dispatch before a late ACK. This is boundary/controller evidence, not a 23-hour soak or live media certification. No backend files/schema or AGENTS files were changed for this fix.

RED/GREEN reproductions covered immediate receiver media teardown, absent wire identity, ignored kick/session-end messages, terminal closes incorrectly retrying, sender force-stop during control loss, missing immediate resume heartbeat, invalid lease ACK acceptance, late receiver re-registration beyond grace, missing native PING detection, and HTTP upgrade rejection incorrectly classified as transient.

Compiled tests execute the real receiver controller, complete Objective-C++ transports, production Desktop policy, extracted production frontend disconnect/watchdog/readiness/drain handlers, and the complete production reconnect method against refusing/offline boundaries. Native loopback tests verify both transports' actual RFC6455 PINGs, two-second PONG delay, unanswered PING timeout, HTTP403 terminal rejection, and sender `resume_fence` wire payload. Existing 65-second heartbeat/authenticated-reconnect/4401 and PONG-without-application-ACK tests remain passing. Receiver controller tests prove a real two-second retry interval produces one endpoint delivery and zero stops, same viewer ID, bounded expiry, and stale callback fencing.

Run:

```sh
PYTHONPATH=test/pixelview python3 -m unittest test_control_socket test_receiver test_desktop_retry test_desktop_heartbeat
PYTHONPATH=test/pixelview python3 -m unittest test_pairing_ux test_keychain_async test_keychain_reliability test_keychain_noninteractive test_receiver_login_errors test_media_failure_pairing test_whip_retry test_recovery_ui
```

The combined offline/loopback run completed **45 tests in 292.026 seconds: OK, one opt-in live-backend test skipped**. Its final log is `/tmp/pixelview-desktop-control-tests.log`; despite the process manager prematurely returning `exit_code:null`, macOS subsequently reported zombie `XSTAT=0` and the log contained the final unittest summary. After a final RED/GREEN fix ensuring native media failure/output drain cancels pending control-only recovery, all **14 sender policy/frontend/media-failure regression tests passed** again (22.770 seconds). Existing offscreen Qt font/platform warnings and deliberately simulated Keychain-denial diagnostics are present; no unexpected test failure remains.

Loopback servers are explicit protocol fixtures, not DynamoDB/backend integration or proof of media delivery. No active application/stream was stopped, restarted or replaced. The canonical app was running, so no signed app build/install was attempted. Whole native transport/controller translation units were compiled into scratch harnesses. Full frontend app build/signing, deployed backend interoperability, admin viewer identity readback, and live WHIP/WHEP/DeckLink continuity during a backend-only two-second outage remain integration acceptance gates.
