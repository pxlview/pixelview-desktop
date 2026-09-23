# Pixelview Desktop features

Pixelview Desktop is an OBS Studio fork (base `32.2.1-66-g6b3e55072`, GPL-2.0-or-later) built for
the Pixelview.io platform on macOS / Apple Silicon. This document describes what is implemented in
the current tree, how each part works at a high level, what has actually been verified and how, and
what is known to be missing or deliberately out of scope. It replaces the per-session status notes
that were previously spread across `AGENTS.md` and `docs/`. Where older notes disagree with this document, this
document reflects the newer state; historical build numbers, test counts and dates are intentionally
omitted. Nothing here should be read as release, notarization or hardware-fidelity acceptance unless
the "Verification status" section says so explicitly.

## Sending

### Application shell

- Single window: settings sidebar on the left, the native OBS editable preview on the right. Mode
  tabs switch between **Sending** and **Receiving**; the chosen mode is persisted
  (`PixelviewReceive/Mode`) and restored on the next launch before queued deep links are drained.
- Named sidebar groups (Sending: Pairing / Connection, Capture, Encoding; Receiving: Session
  credentials, Output), one styled primary action per mode, both fitting the 993x658 default window.
  Native Start/Stop Streaming, status bar and Stats window are retained; settings lock while output
  is preparing, connecting, streaming or stopping.
- Upstream docks, first-run wizard, onboarding, OBS auto-update and OBS telemetry are not exposed.
  Configuration lives under `~/Library/Application Support/pixelview/obs-studio/` (or a private root
  via `--app-config-dir`), so a stock OBS installation is never read or overwritten.

### Pairing

- New pairings use `https://api4.pixelview.io`. The Pair dialog asks only for the masked one-time
  admin code. Setting exactly `PIXELVIEW_LOCAL_DEVELOPMENT=1` in the launch environment exposes the
  backend origin field and a Local development checkbox, defaulting to `http://localhost:8000`.
  Any other value keeps production defaults. The switch affects new pairing form defaults only.
- HTTPS/WSS is required except for loopback HTTP with the development flag; no redirects are
  followed, and resource origins are validated.
- `POST /desktop/exchange` returns the device token and non-secret identity (`NodeId`, `DesktopId`,
  origin, development flag) saved in `user.ini`. The token is stored only in the macOS Keychain
  (service `com.pixelview.desktop.device`, account = exact origin, so `localhost` and `127.0.0.1`
  differ; device-only, non-synchronizing). Pairing is complete only after the Keychain save succeeds
  and the first `DESKTOP_READY` matches the exchanged identity. A successful read stays process-held
  for reconnects; startup and reconnect reads never prompt, explicit Pair/Unpair may show the normal
  macOS authorization dialog.
- **Unpair** is local only: it stops output, closes the socket, disables reconnect, deletes the
  origin's Keychain record (verified by a direct `errSecItemNotFound` read-back) and clears local
  identity. It does not revoke the admin-side registration. A failed removal reports "Unpair
  incomplete", keeps identity and keeps Unpair available. Changing backend requires a successful
  Unpair first; a saved pairing keeps reconnecting to its persisted origin regardless of the
  environment variable.
- Remote revocation (`SOCKET_DESKTOP_REVOKED` or a 4401 close) stops output, forgets the process-held
  token and disables reconnect, but keeps the identity and Keychain record for an explicit Unpair.

### Control socket

The backend repository's `/desktop/ws` route is the authority for the wire protocol; the client
behaviour is:

- Connect to `wss://<origin>/desktop/ws?token=<device_token>`; the token authenticates the upgrade
  and no first message is sent. The client keeps a 10 s budget for `DESKTOP_READY {desktop_id,
  node_id}`, which is checked against the saved pairing.
- Envelopes: server to client `{"mutation": NAME, "data": {}}`, client to server
  `{"message": NAME, "data": {}}`.
- Liveness is server-driven: `SOCKET_SEND_PING` every 20 s is answered immediately (even before
  ready) with `PONG_RESPONSE {"streaming": bool, "settings": object|null}`. `settings` carries
  resolution, framerate, encoder, bitrate, profile and an allowlist of non-secret encoder options,
  or `null` if any field would fail server validation. 60 s without a ping is treated as a dead
  socket. There is no client heartbeat, lease, fence or resume.
- `DESKTOP_START {}` yields `DESKTOP_STARTED {config: {whip: {endpoint, bearer_token}}}` or
  `DESKTOP_ERROR {code}` with `active_session_required`, `node_paused`, `subscription_required`,
  `start_failed` (socket stays open; shown as "Pixelview could not start the stream") or
  `unknown_message`. Only `config.whip` is used; a missing WHIP config is an error and SRT is never
  used. A second publisher is rejected by the engine at the WHIP POST; there is no `busy` code.
- `DESKTOP_STOP {}` is sent after the local output has actually stopped; `DESKTOP_STOPPED {}` is not
  waited for.
- Terminal mutations arrive before their close (close code may be 1000): `SOCKET_DESKTOP_REVOKED`
  (see Pairing) and `SOCKET_DESKTOP_REPLACED` (a newer connection from the same device; this socket
  stops reconnecting, a running stream continues, the operator restarts the app to reconnect).
- An upgrade refused with HTTP 401/403 is mapped to 4401/4403 and is terminal; a 4401 close after
  accept is treated like revocation. A 4403 (a refused upgrade or a redirect, for example an edge
  rule) stops reconnecting but keeps the pairing, and while streaming it ends control only: the
  running media is left alone. Every other close (1000 without a terminal mutation, 1001,
  1005, 1006, 1011-1013, TCP loss, server restart) is transient and reconnects with 1-30 s
  exponential backoff.
- Media is independent of the control socket: a control drop while streaming keeps the WHIP output
  running, the reconnected socket needs no new `DESKTOP_START`, and pongs resume reporting
  `streaming: true`. Additionally, both native transports send RFC 6455 ping/pong every 20 s with a
  20 s pong timeout.

### Streaming

- Start Streaming sends `DESKTOP_START` and builds an in-memory `whip_custom` service from the
  returned endpoint and bearer token. Nothing is written to the profile's service settings; existing
  service profiles are preserved. There is no default destination and no SRT fallback.
- Retry policy follows the profile's `Output/Reconnect`, `RetryDelay` and `MaxRetries`. A media
  failure or a control drop before media started drains the output, reconnects the socket and
  requests a fresh start; the counter resets only on an actual media start. Raw libobs WHIP retry is
  disabled so a stale service is never reused. Retryable failures are ICE/peer loss, selected cURL
  transport errors and HTTP 502/503/504; authentication, TLS, SDP, encoder, malformed-response and
  resource-origin failures are terminal and do not retry.
- Stop, Unpair, revocation, identity mismatch, protocol denials and shutdown cancel streaming intent.
  A terminal media failure sends `DESKTOP_STOP` and keeps the control connection and pairing healthy.
- The WHIP client never logs SDP, ICE credentials or resource URLs; redirects do not forward
  credentials and resource DELETE is pinned to the original origin. Error text names the streaming
  destination or encoder, not stream keys or internal output machinery.

### Capture

- DeckLink devices are enumerated every two seconds from the input plugin's own property list (no
  Refresh button, no fabricated devices or mode tables). Selecting a device updates the single
  managed `Pixelview Capture` source; **Device settings...** opens the native DeckLink properties
  dialog (connection, mode, colorspace/range, channel layout, buffering).
- Canvas and output are fixed at 1920x1080 on every video reset. **Fit** resets position, scale,
  bounds, crop and rotation to an inner-fit; polling, hotplug and restored sources never rewrite
  transforms. FPS offers 23.976, 24, 25, 29.97, 30, 50, 59.94 and 60 as exact rationals through the
  real `ResetVideo()` with rollback on failure; it is disabled while output is active.
- Status distinguishes missing plugin, no devices, no selection, disconnected selection and
  "Device selected - Local preview"; device presence is not signal lock.

### Encoding defaults

- First initialization prefers Apple VideoToolbox HEVC, then other hardware HEVC, hardware
  H.264/AV1, then x264. Only exact known encoder IDs (VT, NVENC, QSV, AMF, VAAPI) are offered, and
  only when OBS registers them.
- Defaults: 6 Mbps CBR, one-second keyframes, HEVC Main (H.264 baseline where offered, x264 also
  ultrafast/zerolatency), Opus audio, `Output/Mode=Advanced`, rescale disabled. Quick bitrate range
  is 1-12 Mbps; the native Advanced dialog keeps out-of-range values.
- B-frames are a permanent policy: `bframes`, `bf` and `bframe_ref_mode` are hidden in Advanced and
  normalized off on every load and save, including x264 `x264opts`, NVENC `frameIntervalP`/UHQ and
  AMF `ffmpeg_opts` overrides.
- HEVC profile maps the canvas format: Main to NV12, Main10 to P010, Main 4:2:2 10 to P216 (limited
  range only). Main/Main10 default to limited range but honor a saved Full setting. Saves are
  in-process transactions: `basic.ini` and `streamEncoder.json` roll back on failure.

### Audio

- The OBS `VolumeMeter` under the preview shows the capture source's audio. **Mute audio** is a
  master mute (source mute plus local monitoring off; the meter is replaced by an "Audio muted"
  indicator). **Listen locally** enables native monitoring to a selectable output device (System
  default included) and is disabled while muted; unmuting does not re-enable listening.
- Both flags are saved per source UUID and normalized on load; monitoring failures fail closed.
  Volume, track routing and Opus defaults are the OBS ones.

### Deep links

- `pixelview://play/<sessionID>?token=<base64url password>` and
  `https://play.pixelview.io/<sessionID>?token=...` prefill the Receiving form (session ID plus
  decoded password), switch to Receiving when idle, and never start a session. A missing token
  clears the stored password for that session. Busy states reject the link without side effects; one
  pending link is held in memory during startup.
- The custom scheme is registered in `Info.plist`. Universal Links require
  `PIXELVIEW_ENABLE_UNIVERSAL_LINKS=ON` plus an operator-provided Associated Domains profile at
  signing time, and a matching AASA served from `play.pixelview.io`; neither is deployed.

### Branding, licensing and updates

- Display name **Pixelview Desktop**, bundle `Pixelview Desktop.app`, bundle ID
  `com.pixelview.desktop`, dedicated ICNS/tray assets and the authentic `pv-home` wordmark. The
  macOS ICNS follows Apple's icon grid (824 px rounded plate on the 1024 px canvas with a soft
  shadow) so it matches other apps in the Dock and Cmd+Tab; the PNG/ICO stay full-bleed squares.
  All are regenerated by `test/pixelview/generate_brand_icons.py`. Windows and Linux resources are
  branded in source but only source-tested.
- **Help -> License information** opens an offline viewer with the license, third-party notices and
  source/build info pages fed from `Contents/Resources/license/`; it never contacts the network.
  Development builds show development/unpublished placeholders.
- Sparkle 2 is compiled in only when CMake receives the exact production appcast URL and the
  committed public key (`frontend/cmake/feature-sparkle.cmake`); local and development builds have
  no updater. Release builds check at startup and every 24 hours, expose
  **Help -> Check for Updates...**, and never install silently.

## Receiving

### Session credentials

- The Receiving panel takes a session ID, masked password (Show/Hide) and receiver name (hostname
  by default). Session ID and name persist in user configuration; the password persists only in the
  Keychain (service `com.pixelview.desktop.receiver`, account `latest-session`, device-only), bound
  to the exact session ID, backend origin and a configuration revision so another session or backend
  never inherits it. Storage failures are reported separately from login errors and never fall back
  to plaintext.
- The receiver backend origin comes from the same `PixelviewBackend.hpp` helper as pairing
  (`https://api4.pixelview.io`, or `http://localhost:8000` with `PIXELVIEW_LOCAL_DEVELOPMENT=1`);
  it never inherits the saved sending origin or token. Receiving does not require pairing.
- Login denials map to fixed messages (wrong ID/password or deleted session; viewer limit reached;
  session ended). Server error bodies and URLs never appear in the UI.

### Session end

A host removal (`SOCKET_USER_KICKED`) stops receiving with "Removed from this session by its host."
A deleted session (`SOCKET_SESSION_DELETED`, also what the expiry cron sends) stops receiving with
"This session has ended. It was deleted or expired on Pixelview." and the backend refuses the next
login with "Session archived". Both were verified live against the local backend.

### Viewer flow

`pixelview::PixelviewReceiver` (QtCore + NSURLSession) implements the web player's viewer flow:

1. `POST /login/player` with session ID, password, name and `client_type=pixelview-desktop`;
   the response supplies the WHEP `stream_url` and a `client_token`.
2. Open `wss://<origin>/wsocket?token=<client_token>`; send `ADD_VIEWER_WEB` with a random stable
   viewer ID. Only `SOCKET_ADD_VIEWER_WEB` with `status=success` releases the media endpoint.
3. Answer `SOCKET_SEND_PING` with an empty `PONG_RESPONSE`; ignore chat/viewer-list mutations.
   65 s without a server ping is treated as loss.
4. Transient loss (transport, HTTP 408/429/502/503/504) re-authenticates with 1-30 s backoff;
   already-delivered media stays up for at most 7 s of control loss. Kick, session deletion, auth or
   TLS denial stop media and require a manual Start. Authorization is renewed automatically at 22 h
   against a 23 h hard limit.

The receiver never uses `/desktop/ws`; the earlier `register_receiver` design in
An earlier backend handoff proposing receiver registration over the control socket was never implemented.

### WHEP native decode

- `pixelview_whep_source` (`plugins/pixelview-whep`) embeds a pinned GStreamer 1.28.3 runtime
  (source-built patched rswebrtc 0.15.2, libnice, VideoToolbox decoders, libopus) in the plugin
  bundle. No host GStreamer is used.
- On each connection the plugin probes the VideoToolbox decoders and offers only verified profiles:
  H.264 constrained baseline (`42c02a`, packetization-mode 1), HEVC Main and Main10, VP9 profiles 0
  and 2, plus stereo Opus. The raw policy is P010 limited-range BT.709 SDR at up to 1920x1080 (30 or
  60 fps depending on the negotiated HEVC level). Full-range, PQ/HLG, non-709 and unknown
  colorimetry are rejected before delivery.
- Video: decodebin3 -> P010 policy -> clocked appsink -> `obs_source_output_video2`. Audio: bounded
  queues -> F32 stereo 48 kHz -> clocked appsink -> `obs_source_output_audio`. Both share the
  pipeline clock, and the source runs libobs async-unbuffered so frames are not rebuffered twice.
  Eight-bit sources are upconverted to P010 without gaining precision.
- The patched signaller rejects POST redirects and pins the session `Location` to the accepted
  response origin. The endpoint's `?token=` is also set as the signaller `auth-token`, so session
  PATCH/DELETE carry `Authorization: Bearer <token>` (the engine's `Location` has no token; engine
  DELETE accepts the header from pxlview/pv-engine#58, older engines answer 401 and clean the viewer
  up when the peer connection closes). A bus error, EOS or 15 s without video ends the attempt; the
  plugin has no reconnect loop of its own. Endpoints stay in private memory; OBS-log diagnostics are limited to
  timeout/EOS and GStreamer domain/code.

### HEVC 4:2:2 10 refusal

- `plugins/pixelview-whep/main422-25p.h` is the single switch and returns FALSE. The offer omits
  profile 4, and the parsed-CAPS route selector refuses `main-422-10` (or any other non-Main/Main10
  HEVC profile) before the first access unit with a typed `GST_STREAM_ERROR_WRONG_TYPE`.
  `get_status.failure` reports `unsupported-hevc-main-422-10` or `unsupported-hevc-profile`, and the
  Receiving panel stops with "The sender is streaming HEVC 4:2:2 10-bit, which cannot be received.
  Please use the HEVC Main or Main10 profile on the sender."
- The native VideoToolbox x422 -> v210 decode path, its DeckLink A/V scheduler and offline suites
  remain in the tree; flipping the switch to TRUE restores the earlier strict 1080p25 path, which
  was never certified (an intermittent 701 ms first-frame failure is unresolved).

### Jitter buffer

`PixelviewReceive/BufferMs` defaults to 100 ms and is passed explicitly on every `connect`. The
**Buffer: N ms** link in the Receiving panel edits it (0-2000 ms, persisted, applied to the next
connection). It is a jitter-buffer target, not measured end-to-end latency.

### Canvas precision

While receiving, an in-memory canvas transaction switches the main texture to P010 / Rec.709 /
limited so ten-bit 4:2:0 precision survives to the GPU texture (877 distinct levels measured versus
256 on NV12). The sender's format, colorspace, range and graphics module are restored when leaving
receive mode; the switch is refused while any output is active, and a failed rollback blocks further
mode changes until restart. Preview, fullscreen and the rendered DeckLink output remain eight-bit
boundaries.

The receive canvas frame rate is independent of the Sending FPS (which is locked while unpaired):
the DeckLink output mode is the single receive frame-rate setting. Before a receive Start the output
UI reads the selected mode's exact rate (`mode_frame_rate`) and asks the frontend
(`pixelview_receive_frame_rate`) to reset only the receive canvas to it; the rate is remembered in
`PixelviewReceive/FPSNum|FPSDen` so the next switch to Receiving opens at the card's rate. The
sender's `Video/FPS*` profile values are never written and are restored with the rest of the sender
video state when leaving receive mode. Until an output mode has been started the receive canvas
follows the sender rate. The reset is refused, with an on-screen reason, while another output is active.

### DeckLink program output

- The Receiving panel's Output group holds the native DeckLink output settings (device, mode,
  AutoStart) with the keyer UI hidden. Ordinary Main/Main10 reception uses the stock OBS rendered
  program output: main texture -> BGRA staging -> `decklink_output`, with audio from the ordinary
  OBS mix (source gain, mute and mixer routing apply; monitoring is separate). Output is 8-bit BGRA,
  not ten-bit 4:2:2 SDI.
- `bind_receive(source, native=false)` keeps the source identity for the watchdog only. The health
  check stays healthy while the source is `playing` and has not turned native 4:2:2; leaving
  `playing` (the source's own 15 s stale/EOS/error detection), source removal, card removal or a
  native scheduling failure still drains the output. There is no per-frame freshness cutoff.
- `receive_status` returns a `reason`; the UI watchdog (100 ms poll) logs
  `[decklink-output-ui] receive output stopped after N ms: <reason>`, resumes automatically when the
  source is ready again, bounded to three consecutive attempts that fail within ten seconds of
  starting (budget reset on every bind).
- A Start that does not happen (manual or automatic) shows a non-blocking "The DeckLink output did
  not start" warning with the reason as well as the `Start failed: <reason>` log line: nothing
  received yet, no saved settings, device or mode unavailable, the canvas could not follow the mode's
  rate, or the output's own `last_error` (device not connected, mode unavailable, card busy, and in
  Sending mode a frame-rate mismatch naming the canvas rate and the output mode). Only the
  launch-time AutoStart stays log-only when it is merely early (no video yet).

### Fullscreen and projector

Fullscreen uses the OBS projector on a chosen display (View menu / sidebar display menu) and the
existing Escape action to close. Stop receiving returns the canvas and outputs to the sending
configuration.

## Shutdown and recovery

- The main window carries `WA_DeleteOnClose`, so `OBSBasic::closeEvent` calls
  `PixelviewShutdownReady()` first. The helper stops receiving, cancels streaming intent and reports
  whether stop, socket, output or setup state is still settling; while it is, the close is ignored
  and retried every 100 ms. The wait is bounded by `PIXELVIEW_SHUTDOWN_WAIT_MS` (10 s); after it, or
  on `aboutToQuit`, an active stream output is force-stopped and normal scene teardown runs.
  `kill -TERM` goes through the same path.
- An unclean-shutdown sentinel (`.sentinel/run_*`) produces the branded one-button Continue prompt on
  the next launch; it offers neither safe mode nor crash upload and blocks until dismissed.
- Reconnect summary: sender control socket 1-30 s backoff, media unaffected by control drops,
  stream retries per profile reconnect settings; receiver control 1-30 s re-authentication with 7 s
  media grace; DeckLink receive output resumes up to three times after a named stop. Relaunch
  reconnects pairing but never invents streaming or receiving intent.
- Qt is pinned to the OBS 6.10.3 package on macOS because 6.11.1 crashes in `QImage::toCGImage`
  during scene activation.

## Verification status

### Compiled and offline tests (`test/pixelview`, `plugins/*/tests`)

The Python drivers compile production source slices (offscreen Qt, Objective-C++ transports against
loopback servers, libobs harnesses). `python3 -m unittest discover -s test/pixelview` runs 211
tests; `test_icon_assets` (needs Pillow) and `test_pixelview_sources` (corresponding-source inventory
gate) fail in the current environment regardless of changes.

- Control socket and pairing: `test_desktop.py` (`desktop_native.cpp` policy table, real exchange,
  Keychain record, 4401 close), `test_desktop_control.py` (`desktop_control*.mm`: NSURLSession
  against a scripted server for ready, ping/pong, start, TCP loss while streaming, revoked/replaced,
  60 s silence), `test_control_socket.py` (RFC 6455 keepalive, refused upgrade),
  `test_desktop_retry.py`, `test_media_failure_pairing.py`, `test_whip_retry.py`,
  `test_pairing_ux.py`, `test_pairing_defaults.py`, `test_backend_selection.py`, `test_stream_lock.py`,
  `test_streaming_ui.py`. `desktop_backend_smoke.mm` is an opt-in live tool.
- Keychain: `test_keychain_reliability.py`, `test_keychain_noninteractive.py`,
  `test_keychain_async.py` (mocked Security APIs), `test_receive_keychain_restart.py` and
  `test_receive_ui.py` (real isolated test-only Keychain service, removed afterwards),
  `test_config_isolation.py`.
- Receiver: `test_receiver.py` (`receiver_native.cpp`, `receiver_transport.mm`),
  `test_receiver_login_errors.py`, `receiver_expiry.cpp`, `test_receive_ui.py`,
  `test_mode_persistence.py`, `test_deep_links.py`, `test_canvas_fullscreen.py`,
  `test_receive_preview_zoom.py`, `test_receive_precision_canvas.py`, `run_receive_precision_probe.py`
  (real libobs/OpenGL level count). `receiver_live.py` and `receiver_media_live.py` are opt-in live
  tools.
- Capture, encoding, audio, UI: `test_capture_policy.cpp`, `test_capture_*.py`, `test_fps.py`,
  `test_encoding.py`, `test_first_launch_defaults.py`, `test_unavailable_encoder.py`,
  `test_nvenc_policy.py`, `test_amf_policy.py`, `test_vt_sdk_compat.py`, `test_audio*.py`,
  `run_audio_serialization_native.py`, `test_sidebar_polish.py`, `test_ui_contract.py`,
  `test_recovery_ui.py`, `test_qt_image_lifetime.py`.
- Branding and release: `test_app_branding.py`, `test_license_*.py`, `test_pixelview_license.py`,
  `test_build_signing.py`, `test_pixelview_release.py`, `cmake/macos/test_signed_development.py`.
- WHEP plugin (`plugins/pixelview-whep/tests`): `test_packaging.py`, `run-native.py`,
  `run-codecs.py`, `run-production-offer.py`, `run-profile-offer.py`, `run-capability-probe.py`,
  `run-decoder-profiles.py`, `run-video-precision.py`, `run-ordinary-route.py`,
  `run-preview-dispatch.py`, `run-audio-route.py`, `run-whep-loopback.py` (synthetic loopback WHEP
  through a real engine build: all five video alternatives plus HTTP 406 negative), `run-main422-25p.py`
  and the `run-native-422*.py` suites (native branch admitted explicitly per filter).
- DeckLink (`plugins/decklink/tests`): `run-receive.py` (complete output owner under ASan/UBSan with
  a refusing fake SDK: rendered path takes OBS mixed audio, 600 ms frame gap stays healthy, named
  state/native reasons, native v210/PCM exactness), `test_decklink_output_ui.py` (compiled Qt watchdog
  including resume budget).
- Sender WHIP/DeckLink/VideoToolbox modules and the GStreamer runtime pass deep/strict code-signature
  verification in the canonical Developer ID local build.

### Verified live

- Control socket against the local backend (`dev.sh --k8s`, backend commit ab4aa77) with the rebuilt
  signed bundle: pairing exchange and `DESKTOP_READY`; pongs recorded in Redis presence with parsed
  settings; backend reload (close 1005) followed by automatic reconnect; admin revocation via
  `SOCKET_DESKTOP_REVOKED` and DB-only revocation via 4401, both disabling pairing with the revoked
  message; Unpair and re-pairing; `DESKTOP_START`/`STARTED` (WHIP granted) and `DESKTOP_STOP`/`STOPPED`
  via the smoke tool.
- Receiver control: live loopback login/registration with viewer-record read-back in Redis and exact
  removal after stop; live missing-session 401 mapping.
- Receiving media (earlier build, 50 ms jitter era): local backend + engine + synthetic H.264/Opus
  publisher through normal ingress, 135 s uninterrupted video/audio in the GUI, mode round trip,
  fullscreen with advancing timecode, Listen/Mute behaviour. Synthetic loopback WHEP decode for
  H.264, HEVC Main/Main10 and VP9 0/2 on the current plugin source.
- WHEP session DELETE with `Authorization: Bearer` (2026-09-23, local engine with
  pxlview/pv-engine#58): Stop receiving closed the viewer immediately (`viewer_left` reason
  `client_closed`, encoder and track removed before the peer connection closed) instead of the
  earlier `401 missing token`.
- DeckLink receive output: 1080p24 Main10 to an UltraStudio Monitor 3G from the rebuilt signed
  bundle; a deliberate 15 s stream cut logged the named stop and armed resume; the restarted run
  stayed up 28 minutes. AutoStart was exercised once.
- Keychain: the sender device record was saved, read, deleted (Unpair) and re-created (re-pair) by
  the application on this Mac during the live control-socket run; receiver-record restart
  persistence is covered by the isolated-service tests. Earlier `-25307`/`-25293` failures were seen
  only under private-HOME launches and were not attributed to a specific cause.
- Sender capture/encoding on an UltraStudio Recorder 3G (historical, before the WHIP path): device
  selection, DeckLink settings round trip, all FPS options, Fit, save-failure rollback; ffprobe of
  all three Apple HEVC profiles and H.264 (no B-frames, 1 s keyframes); loopback SRT smoke with
  mute/listen measured through CoreAudio.
- Clean shutdown via SIGTERM on the signed bundle: log ends with `Shutting down`, zero leaks, sentinel
  removed, no crash report. Custom-scheme deep links delivered cold and warm to an isolated app copy.

### Not verified

- The receive canvas following the DeckLink output mode's frame rate, and the on-screen Start
  failure warning, on real DeckLink hardware (offline harness and compile only).
- Physical SDI picture inspection of the receiver's DeckLink output (cadence, colour, long-run A/V
  sync); the stall that used to trip the old 500 ms watchdog was not reproduced on demand.
- Media start from the app to the local engine after the control-socket rewrite (no capture input
  attached); the WHIP grant was exercised only by the smoke tool.
- A real Cloudflare/backend drop during an active WHIP stream on a production origin; only a local
  backend reload was exercised.
- Production notarization, Gatekeeper, R2 publication, appcast and the Sparkle update cycle;
  clean-Mac acceptance; the interactive Pair forms on the rebuilt bundle (offscreen tests only);
  clicking the red close button (SIGTERM shares the path).
- Long hardware soak, Internet loss/recovery, glass-to-glass latency, 4K/interlaced/HDR inputs, hot
  unplug, multiple capture devices, NVENC/QSV/AMF/VAAPI hardware, Windows/Linux runtime.
- A live HEVC 4:2:2 10 sender against the refusal path (offline suites only); the native 4:2:2 path
  behind the switch is uncertified. The operator's Main10 glitch report has not been reproduced or
  root-caused; the stock-path simplification and the 100 ms buffer are mitigations.

## Known limitations and intentionally out of scope

- No remote playout, engine provisioning, billing or engine-enforced fencing; an active project,
  engine and subscription must already exist in admin. Admin's `streaming` flag is the client's own
  report, not independent media observation.
- SRT fallback is intentionally unavailable; nodes without a WHIP config report an error.
- Credential and transport implementations exist for macOS only (Keychain, NSURLSession); Windows and
  Linux have source-level branding only and no pairing, receiving or updater.
- Receiving is limited-range BT.709 SDR only; HDR, full range, non-709 colour, HEVC 4:2:2 (see above)
  and VP9 profiles 1/3 are refused. DeckLink receive output is 8-bit BGRA; fullscreen is 8-bit. The
  sender can still select HEVC Main 4:2:2 10, which the Pixelview receiver refuses.
- The sender enforces limited range only for Main 4:2:2 10 (P216); Main/Main10 honour a saved Full
  setting.
- Capability probing caches a reduced decoder mask for the process if the pinned applemedia decoder
  sends EOS before its last probe frame; an isolated decoder patch was evaluated and rejected.
- Signal-lock and frozen-frame telemetry are not implemented; device presence only.
- Deep-link HTTPS dispatch needs Associated Domains and a served AASA; only the custom scheme works
  today. Fit has no undo; UI strings are English only.
- Two settings roots on one Mac share the same Keychain identities (one device record per origin, one
  receiver record); this is not a two-sender recipe.
- Corresponding source is offered through the public repository at each release tag and the
  sources tarball published next to each DMG; `release/source-inventory.json` keeps informational
  open review items about third-party component materials and does not gate a release.
- Minimum macOS 14 (from the bundled runtime); arm64 only; Xcode 26 / SDK 26.5 normal toolchain
  (`PIXELVIEW_LEGACY_TOOLCHAIN=ON` permits Xcode 15.3 without Metal and AVFoundation capture).
- Configuration files and the Keychain are not a cross-store transaction; interrupted saves fail
  closed to a missing credential.

## Where things live

- `frontend/widgets/OBSBasic_PixelviewDesktop.inc` - pairing UI, control-socket timers, in-memory
  WHIP service, streaming retry; `OBSBasic_PixelviewReceive.inc` - Receiving panel, viewer
  controller wiring, jitter setting, canvas precision transaction, DeckLink output binding;
  `OBSBasic_PixelviewEncoding.inc`, `OBSBasic_PixelviewAudio.inc`, `OBSBasic_PixelviewDeepLinks.inc`.
  Capture shell, FPS, mode persistence and the close gate are in `OBSBasic.cpp`.
- `frontend/utility/Pixelview*.{hpp,cpp,mm}` - `PixelviewDesktop.hpp` (control policy),
  `PixelviewDesktopConnection.hpp` / `PixelviewDesktopMac.mm` (exchange, socket, Keychain),
  `PixelviewControlPing.hpp`, `PixelviewSocketWatchdog.hpp`, `PixelviewBackend.hpp` (origin
  defaults), `PixelviewReceiver*` (viewer flow), `PixelviewReceiveCredentialStore*`,
  `Pixelview*Keychain*.hpp`, `PixelviewDeepLink*`, `PixelviewEncoding.hpp`, `PixelviewFPS.hpp`,
  `PixelviewCapturePolicy.hpp`, `PixelviewConfig.hpp`, `PixelviewAudio.hpp`, `PixelviewSparkle.*`.
- `plugins/pixelview-whep` - WHEP source, capability probe, profile offer, video-format policy,
  `main422-25p.h` switch, native 4:2:2 filter, `scripts/` (runtime staging, rswebrtc build, SBOM),
  `patches/`, `tests/`.
- `plugins/decklink` - `decklink-output-receive.inc`, `decklink-receive.hpp` (receive bind, health,
  `receive_status`, native v210 scheduler); `plugins/decklink-output-ui/decklink-receive-ui.inc`
  (watchdog, resume budget, AutoStart, Start refusal logging).
- `plugins/mac-videotoolbox` - SDK compatibility header and spatial-AQ handling for the sender.
- `test/pixelview` - Python drivers and the C++/Objective-C++ harness sources they compile.
- `release/` - `pixelview-macos.sh` (1Password-wrapped prepare/publish), `macos.json` (team, Sparkle
  public key), `source-inventory.json`; `version.json` at the root is the product-version source.
- `cmake/macos` - `pixelview-build.sh` / `pixelview-signed-development.py` (canonical Developer ID
  local build), `pixelview-launch.py`, `pixelview-release.sh`, `pixelview_release_validate.py`,
  `pixelview_sources.py`, `xcode.cmake`, `helpers.cmake`.
