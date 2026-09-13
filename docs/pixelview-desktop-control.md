# Pixelview Desktop control socket

The Desktop control socket is an ordinary connection on the backend's shared
ConnectionManager, using the same envelope as the web player and iOS app.
There is no lease, fence, resume or client heartbeat: the pv-engine rejects a
second publisher itself, and media is independent of the control socket.

## Wire protocol

- Connect: `wss://<backend>/desktop/ws?token=<device_token>`. The paired
  device token (from `POST /desktop/exchange`, unchanged) authenticates the
  upgrade; no first message is sent. For an invalid or revoked token the
  server accepts and immediately closes with 4401 and no prior message, which the client treats exactly like `SOCKET_DESKTOP_REVOKED`; an
  upgrade rejected outright with HTTP 401/403 is mapped to 4401/4403 and is
  terminal as well. The client keeps a 10-second budget for `DESKTOP_READY`
  after opening.
- Envelope: server → client `{"mutation": NAME, "data": {...}}`; client →
  server `{"message": NAME, "data": {...}}`.
- `DESKTOP_READY {desktop_id, node_id}`: sent once after connect. The client
  verifies the identity against its saved pairing and, if Start was pressed
  before the socket was ready and no stream is running, requests a start.
- `SOCKET_SEND_PING {}` every 20 s: the client answers immediately with
  `PONG_RESPONSE {"streaming": bool, "settings": object|null}`. `settings` has
  the shape `resolution{width,height}`, `framerate`, `encoder`, `bitrate`,
  `encoder_profile`, `encoder_settings`, or `null` when any field would fail
  the server's validation (an invalid settings object is read by the server as
  a bare pong, which would hide an active stream). The pong is the only
  liveness and status report; presence expires server-side 90 s after the last
  pong. The client treats 60 s without a server ping as a dead socket.
- `DESKTOP_START {}` → `DESKTOP_STARTED {config:{whip:{endpoint,bearer_token}|null, srt:{...}}}`
  or `DESKTOP_ERROR {code}` with `active_session_required`, `node_paused`,
  `subscription_required`, `start_failed` (a server-side start failure; the
  socket stays open) or `unknown_message`. Only `config.whip` is used;
  a missing WHIP config is an explicit error and SRT is never used. If another
  source is already publishing, the WHIP POST itself fails and the native
  output reports it; there is no `busy` code.
- `DESKTOP_STOP {}` → `DESKTOP_STOPPED {}`. Stopping is local; the client
  sends the stop after its output has actually stopped and does not wait for
  the acknowledgement.
- Terminal closes are announced by a mutation before the close, whose code may
  then be 1000: `SOCKET_DESKTOP_REVOKED` (admin revoked the device: the
  client stops output, forgets the process-held token, disables reconnect and
  keeps its identity for an explicit Unpair) and `SOCKET_DESKTOP_REPLACED`
  (the same device opened a newer connection: this socket does not reconnect;
  a running stream continues and the operator restarts the app to reconnect).
- Every other close (network loss, server restart, 1001/1006/1011/1012/1013,
  even 1000 without a preceding terminal mutation) is transient. The client
  reconnects with the existing 1–30 s exponential backoff while idle. While
  streaming, the media keeps running, the reconnect follows the same backoff
  and the next `DESKTOP_READY` needs no new `DESKTOP_START`; pongs simply
  resume reporting `streaming: true`.

## Client policy (macOS)

`pixelview::Desktop` (`frontend/utility/PixelviewDesktop.hpp`) holds the
policy with injected time and callbacks; `DesktopConnection` and
`PixelviewDesktopMac.mm` own the pairing HTTP exchange, the NSURLSession
socket and the Keychain record; `OBSBasic_PixelviewDesktop.inc` wires the
timers, the in-memory `whip_custom` service and the sidebar. Streaming retry
keeps the profile's `Output/Reconnect`, `RetryDelay` and `MaxRetries`
semantics: a media failure or a control drop before media started drains the
output, reconnects and requests a fresh start; Stop, Unpair, revocation,
identity mismatch, protocol denials and shutdown cancel intent. RFC6455
ping/pong at 20 s/20 s remains a transport-level liveness check shared with
the receiver path.

The receiver path is unchanged: `POST /login/player` then
`wss://<origin>/wsocket?token=...` with `client_type` `pixelview-desktop`
(`docs/pixelview-receiver-controller.md`). Receiver registration never uses
the control socket.

## Tests

- `python3 -m unittest discover -s test/pixelview -p test_desktop.py`: real
  transport, pairing exchange, Keychain record and a loopback 4401 close;
  `desktop_native.cpp` is the policy truth table.
- `python3 -m unittest discover -s test/pixelview -p test_desktop_retry.py`
  and `test_media_failure_pairing.py`: compiled production frontend methods
  against offline boundaries (control loss while streaming, revoked/replaced
  mutations, ping silence, retry budget, drains and the close gate).
- `python3 -m unittest discover -s test/pixelview -p test_desktop_control.py`:
  the real NSURLSession transport against a scripted loopback server (token
  upgrade, ready, ping/pong, start, TCP loss while streaming, reconnect without
  a new start, revoked and replaced mutations, 60 s ping silence).
- `python3 -m unittest discover -s test/pixelview -p test_control_socket.py`:
  RFC6455 keepalive on both transports and terminal upgrade rejection.
- `desktop_backend_smoke.mm` (opt-in, live backend): pair, connect, answer the
  first ping, request and release a start.
