# Native receiver authorization controller (macOS)

`pixelview::PixelviewReceiver` (`frontend/utility/PixelviewReceiver.hpp`) is a QtCore QObject with callbacks, not a media player. It has no dependency on publishing pairing, lease state, Keychain, OBS services or scenes. Construction is idle: only an explicit `start(sessionId, password, name)` creates receiving intent.

## Frontend integration

Compile `PixelviewReceiver.cpp` and `PixelviewReceiverMac.mm` with QtCore and Foundation; compile the Objective-C++ file with ARC (`-fobjc-arc`). No QtWebSockets or QtNetwork dependency is added. The transport is separately owned and does not modify `PixelviewDesktopMac.mm`.

```cpp
receiver = new pixelview::PixelviewReceiver(this);
// Production default: https://api4.pixelview.io
// Explicit development only, independent of publish pairing:
// receiver->setOrigin(QUrl("http://127.0.0.1:8000"), true);
receiver->onEndpoint = [this](const QString &url) {
    // Call source connect(endpoint=url) in memory only; omit latency for native GStreamer defaults.
};
receiver->onStopped = [this] {
    // Disconnect native receive source synchronously; do not stop publishing.
};
receiver->onChanged = [this] {
    // Refresh state/status. Ready means authorized, not decoded frames.
};
// UI supplies QSysInfo::machineHostName() as initial editable name.
// receiver->start(sessionIdText, passwordText, nameText);
// receiver->stop(); before destroying receive source/frontend.
```

Public API: `setOrigin(QUrl, bool development=false)` returns false for unsafe origins or while receiving; `start(QString,QString,QString)`, `stop()`, `state()`, `status()`. States: `Idle`, `Authenticating`, `Registering`, `Ready`, `Reconnecting`, `Error`. Callbacks: `onEndpoint(const QString&)`, `onStopped()`, `onChanged()`. Construct/use on the Qt main thread. Callbacks may call stop, but must not delete the controller synchronously. Destructor cancels silently; frontend must stop before source teardown. Password/name/session are not stored across launches. Do not persist password fields or callback URLs, including source settings, scene JSON, crash diagnostics, command-line arguments or logs.

## Protocol and credential boundaries

Authoritative inspected files in backend-v4: `routes/login/player.py`, `routes/websocket/connections.py`, `routes/websocket/add_viewer.py`, `services/ws_manager.py`, `constants/urls.py`. Player reference: `src/api/playerSession.js`, `src/stores/store.js`.

1. POST `/login/player` with session_id/password/name, `client_type="pixelview-desktop"`, explicit WEB device, MAC platform, GSTREAMER browser and mobile=false. Parse `player=WHEP`, `client_token`, `stream_url`. No destination reconstruction from session ID/password/node ID.
2. Open backend `/wsocket?token=<client_token>` using WSS (WS for explicitly enabled loopback HTTP).
3. On open, send ADD_VIEWER_WEB with name, randomly generated stable viewer_id, initial_load=false and `client_type="pixelview-desktop"`. Only `SOCKET_ADD_VIEWER_WEB` with `data.status=success` releases the media endpoint. Backend propagation on this actual legacy path is required; shared `/desktop/ws` registration support alone does not implement identity here.
4. Append viewer_id while retaining server-selected path and existing query/token. Reject existing viewer_id to avoid duplicate-parameter authority ambiguity. A new manually started receive gets a new ID; automatic reconnect retains the ID but reauthenticates and uses a fresh returned URL.
5. Answer `SOCKET_SEND_PING` with `PONG_RESPONSE` and empty data. Ignore unrelated valid mutation messages (chat, viewer list, drawing).
6. Stop/recovery closes websocket normally. Backend unregisters its live viewer on disconnect; there is **no REMOVE_VIEWER client mutation**. Backend may retain a short disconnected-viewer display grace. Media source teardown is separately required to delete its WHEP resource.

Server URLs can legitimately be on a different backend/engine origin: v3 uses the ingress server URL, v2 selectors can use a stream-server hostname. Accept authenticated HTTPS server-returned endpoints without a hardcoded hostname whitelist. HTTP requires explicit development and literal localhost/127.0.0.1/::1; userinfo and fragments are rejected. Backend origins additionally forbid non-root path/query. Never follow login/WS redirects. The media plugin must separately reject credential-bearing redirects and constrain resource Location/DELETE to the server-selected WHEP endpoint's scheme/host/effective port, **not** the login API origin.

Native NSURLSession is ephemeral, with no cache/cookies/credential storage and default TLS verification. Login responses and WS messages are capped at256KiB. Requests have a15-second request timeout. The shared NSURLSession resource lifetime is24hours so it does not terminate a healthy WebSocket before the controller's23-hour authorization limit. Independent controller deadlines remain login30seconds, registration15seconds and missing server heartbeat65seconds (backend ping interval20seconds). Every asynchronous event captures an attempt generation and QPointer. Late events after cancel/restart/destruction cannot deliver endpoints. A normal websocket close is given up to2seconds to flush, then invalidated.

Transient transport/HTTP408/429/502/503/504 loss triggers exponential1–30second reauthentication. An already-delivered media endpoint remains active for at most seven seconds after detected control loss, clamped to its original authorization deadline; retries never reset either monotonic budget. Successful re-registration with the same URL does not redeliver/rebuild media, while a changed authenticated URL is replaced after registration. Grace expiry, auth/TLS/protocol denial, kick or session deletion stop media, clear controller credentials and require manual Start; no background restart on launch. Both native control transports send RFC6455 PING every five seconds with a five-second pong timeout, separately from application heartbeat deadlines. Each login request starts a conservative absolute23-hour candidate deadline, shorter than the inspected backend's86000-second viewer token lifetime. Only completed valid registration promotes it; HTTP success alone cannot extend retained media authority. Automatic renewal starts at22hours from that login-request start through the existing control reconnect path, retaining media for no more than seven seconds and the old authority's remaining budget. The independent23-hour hard-expiry watchdog is not canceled by recovery or renewed by HTTP success. Successful registration schedules the next automatic renewal; there is no forced manual Start every23hours. Manual Start is required if automatic recovery fails terminally, grace expires, or the hard authorization deadline is reached. Readiness checks both old and candidate authority before accepting a late ACK, independently of queued timer delivery. Status messages are fixed/sanitized; server error bodies and URLs never appear in UI status. See `pixelview-control-recovery.md` for exact wire fields, safety rules and remaining integration gates.

Controller credentials remain in memory for the active intent only; stop/failure still clears that controller copy. Independently, the frontend retains its masked password field across Start/Stop/new Start and login failure so an operator can retry or correct it. Session/password fields are disabled while busy and re-enabled afterward. Operator session-ID edits clear the form password; deep links replace both fields atomically without starting. Native close/shutdown clears the form copy, while the latest password is stored separately in macOS Keychain and restored only for its matching session, backend origin and saved revision. Session ID and receiver name are saved in normal settings as edited; passwords never enter those settings or logs. Keychain failures are reported separately without a plaintext fallback. QString/NSURLSession memory is not claimed to provide cryptographic zeroization. The media source can independently fail even when control remains Ready; frontend must display source status separately and retain an operational Stop.

## Verification

Run `python3 test/pixelview/test_receiver.py`. It compiles the real C++ controller against pinned QtCore and the actual Objective-C++ transport against Foundation. Coverage includes successful login/register/heartbeat, server-selected URLs and query preservation, independent origins, malformed login/control data, auth denial, duplicate viewer IDs, late completions, stop/cancel, same-ID reconnect with fresh URL, callback cancellation, timers, and native HTTP/WS loopback registration/PONG/normal close. Native failure fixtures cover HTTP and websocket redirect rejection, HTTP401, malformed JSON, websocket upgrade403 and oversized login bodies. No OBS media, DeckLink or publishers are started.

Optional live smoke (requires operator-authorized existing local backend/engine/Redis and private JSON containing session_id/password):

```
python3 test/pixelview/receiver_live.py /private/path/session.json
```

This compiles the actual transport/controller, passes credentials through stdin, verifies its random viewer ID against read-only Redis, keeps the connection for25seconds and checks its exact live viewer record is removed after normal stop. It does not connect media, alter backend/player code, start/stop servers, or touch an existing receiver/publisher. Player login itself has the backend's normal resume-node side effect; run only against an already-running authorized local session.

Verified locally: compiled regression suites pass; real existing loopback backend login and registration readback succeeded, connection remained Ready for25seconds, and exact viewer live-record removal was confirmed after stop. Native receiving media/UI/SDI acceptance is separate and is not established by these authorization tests.

## Remembered mode and close ordering

`SelectPixelviewMode` persists `PixelviewReceive/Mode` (`sending`/`receiving`) after a committed switch and saves the user configuration. `InitPixelview` restores a remembered `receiving` mode synchronously once the receive UI and deep-link inbox are attached (before the periodic device refresh starts), and before `deepLinkInbox().ready()` drains queued session links, so a link still wins. `StopPixelviewReceive`, `RefreshPixelviewModes` and `closeWindow` never write the key: leaving receive mode during shutdown is not a mode choice.

`OBSBasic::closeEvent` calls `PixelviewShutdownReady()` before accepting the close. The helper stops receiving, marks shutdown pending, fails the lease once, and reports whether stop/lease/socket/output/setup state is still settling. While it is, the close event is ignored and retried every 100 ms (window visible, controls showing the stopping state), because the main window carries `WA_DeleteOnClose` and an accepted close would destroy it before any deferred teardown. The wait is bounded by `PIXELVIEW_SHUTDOWN_WAIT_MS`; after it, or on `aboutToQuit` (forced), the helper force-stops an active stream output and lets `closeWindow` run its normal scene teardown. Source contracts live in `test/pixelview/test_mode_persistence.py`; the native harness asserts the persisted mode.
