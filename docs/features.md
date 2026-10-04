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
- **Unpair** removes the pairing on both sides. After output stops and the socket closes, the app
  sends `DELETE /desktop/device` with its own device token (Bearer; backend
  pxlview/pixelview-backend-v4#234), which revokes it in the account and drops it from the admin
  list; 200, 401 and 404 all count as removed from the account. Then it deletes the origin's Keychain record
  (verified by a direct `errSecItemNotFound` read-back) and clears local identity. Reconnect is
  disabled before the request, so quitting mid-request still leaves the Mac unpaired locally. The
  status line reports the outcome: removed from the account; Pixelview could not be reached; or no
  readable credential (both of these tell the user to remove the Mac in Pixelview admin). A failed
  local removal reports "Unpair incomplete", keeps identity and keeps Unpair available. Changing
  backend requires a successful Unpair first; a saved pairing keeps reconnecting to its persisted
  origin regardless of the environment variable.
- Unpair from the account (admin **Unpair desktop**, i.e. `SOCKET_DESKTOP_REVOKED`, or a 4401 close
  for a token the backend no longer accepts) stops output, forgets the process-held token, disables
  reconnect and finishes the local Unpair without a prompt: the Keychain record and identity are
  removed and Pair is offered again (with no extra status line). If the Keychain refuses a silent removal, identity is kept and Unpair stays
  available to finish it.
- Verification: offline compiled suites (`test_pairing_ux`, `test_desktop_retry`,
  `test_media_failure_pairing`, `test_desktop`, and `test_pairing_diagnostics` with a real
  QNetworkAccessManager against a loopback server) and the backend route tests. Not yet exercised in
  the built app against a running backend.

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
  `DESKTOP_ERROR {code}` with `subscription_required` or `start_failed`. The Desktop is a plain
  sender that stays online: it needs no project, and knows nothing about pause (pausing a node only
  shows viewers the pause screen). Without a running project nothing listens at the WHIP endpoint,
  so the WHIP POST fails with the ordinary stream-failure dialog. A refused start (or an unusable
  WHIP config) only ends that start: the socket stays open and ready, the node stays Connected and
  Start works again at once ("Pixelview could not start the stream" in the status bar).
  `unknown_message` (an older backend answering e.g. `DESKTOP_STATE`) is ignored and never stops a
  stream. Only `config.whip` is used and SRT is never used. A second publisher is rejected by the
  engine at the WHIP POST; there is no `busy` code.
- `DESKTOP_STOP {}` is sent after the local output has actually stopped; `DESKTOP_STOPPED {}` is not
  waited for. Stop (the button, a remote `stop` or `SOCKET_DESKTOP_STREAM_ENDED`) ends only the
  stream: a ready control socket stays connected, so the Desktop stays online and a remote stop gets
  its `DESKTOP_CONTROL_RESULT`. Only a Stop without a ready socket reconnects it.
- `SOCKET_DESKTOP_STREAM_ENDED {reason}` is sent by the backend just before it takes the engine
  away: `project_deleted` (the running project is deleted in admin) or `engine_stopped` (the engine
  is paused, e.g. the no-viewers cooldown or the inactive-engine cron). A streaming or retrying
  Desktop stops exactly like the Stop button, so the WHIP drop that follows shows no failure dialog;
  the status line says why. The socket stays connected; an idle Desktop ignores it.
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

### Admin remote control

The admin pairing modal (pxlview/pixelview-admin#162) can operate every Sending control of an online
Desktop through the backend pass-through `POST /desktop/devices/{id}/control`
(pxlview/pixelview-backend-v4#236, protocol in that repository's `docs/desktop-remote-control.md`).

- The backend sends `DESKTOP_CONTROL {request_id, command}`; the Desktop answers
  `DESKTOP_CONTROL_RESULT {request_id, ok, error?, state}` on the same socket. Commands are handled
  only on a ready socket and run on the next event-loop turn
  (`frontend/widgets/OBSBasic_PixelviewControl.inc`).
- Commands: `get_state`, `start`, `stop`, `select_device` (a DeckLink device or a
  `test-pattern:<n>` entry from the device list), `set_capture` (a visible boolean or list property
  of the selected DeckLink input or test pattern and one of its enabled values; never the device
  hash or free text),
  `fit`, `set_fps` (the eight sidebar rates), `set_encoder`, `set_bitrate` (whole Mbps, 1-12, the
  quick range), `set_profile`, `set_mute`. Each runs the same native path as the sidebar control
  (`SelectPixelviewDevice`, `SelectPixelviewFPS`, `SavePixelviewEncoding`, `ChangePixelviewAudio`,
  `StartStreaming`/`StopStreaming`), and the local sidebar refreshes to match.
- The configuration lock applies unchanged: while streaming or starting, receiving, unpaired or with
  the native device settings dialog open, everything except `get_state`, `start`, `stop` and
  `set_mute` is refused. Remote `start` also requires a selected capture device, because the local
  "stream a blank screen?" confirmation cannot be answered remotely.
- Failures that show a dialog locally (encoding, FPS, device creation, audio save) are returned as
  the command's `error` instead (`PixelviewWarn`). A media failure after a remote start still shows
  the ordinary local failure dialog on the Mac; the admin sees the Desktop's status line.
- The Desktop pushes its state as `DESKTOP_STATE {state}` whenever it changes: every sidebar
  refresh (including the 2 s device poll), stream transition and audio refresh queues one push
  per event-loop turn, sent only when the content differs from the last push, and a fresh socket
  always gets one after `DESKTOP_READY`. The backend forwards it to the node's admins
  (`SOCKET_DESKTOP_STATE`), which drives the modal and the project-view Desktop status without
  polling. Every state carries `instance` (per launch) and `seq` so admins keep the newest.
- `state` reports stream status (`streaming`, `starting`, `stopping`, `can_start`, `can_stop`,
  retries, the connection status and the Start hint), `locked`/`lock_reason`, the device list and
  selection, the DeckLink properties with their options, encoders, profiles, FPS options and the
  current values, bitrate in kbps with the 1-12 Mbps range, and mute.

### Capture

- DeckLink devices are enumerated every two seconds from the input plugin's own property list (no
  Refresh button, no fabricated devices or mode tables). Selecting a device updates the single
  managed `Pixelview Capture` source; **Device settings...** opens the native DeckLink properties
  dialog (connection, mode, colorspace/range, channel layout, buffering).
- The capture defaults to 10-bit YUV (v210), also for automatic mode detection (upstream defaulted
  to 8-bit, which truncated a 10-bit SDI signal before a 10-bit encode); a pixel format saved
  explicitly in Device settings is kept. SDR limited-range capture is not clamped to 64-940, so
  sub-black and super-white (PLUGE, overshoots) survive into a 10-bit canvas.
- Canvas and output are fixed at 1920x1080 on every video reset. **Fit** resets position, scale,
  bounds, crop and rotation to an inner-fit; polling, hotplug and restored sources never rewrite
  transforms. FPS offers 23.976, 24, 25, 29.97, 30, 50, 59.94 and 60 as exact rationals through the
  real `ResetVideo()` with rollback on failure; it is disabled while output is active.
- Status distinguishes missing plugin, no devices, no selection, disconnected selection and
  "Device selected - Local preview"; device presence is not signal lock.

### Test patterns

- For testing without a capture card, the device list offers, after the DeckLink devices and a
  separator, one "Test pattern: ..." entry per pattern of the bundled `pixelview-test-pattern`
  plugin (the list comes from the plugin's own property, like the DeckLink devices): SMPTE RP 219
  HD bars with PLUGE (-2/0/+2/+4 %), EBU 100/0/75/0 bars, 100 % bars, a 10-bit gray ramp (all 877
  legal luma codes, so an 8-bit stage shows steps), 11 gray steps, white/red/green/blue ramps, a
  16 x 9 crosshatch with circle and centre cross, a circular zone plate reaching Nyquist at the
  side edges, and black.
- Choosing one creates a second managed source, `Pixelview Test Pattern`, in the sender scene and
  shows it instead of `Pixelview Capture`, which stays in the scene hidden with its device and
  settings; choosing a device switches back. Exactly one of the two scene items is visible and that
  one is the capture for Fit, **Settings...**, the audio meter, mute, Listen locally, the
  Start-streaming gate and the remote-control state. A saved test pattern is never replaced by the
  first-run automatic device selection when a card appears. Like every capture control it is
  available only when paired.
- Frames are generated at the canvas size and frame rate as v210, limited-range BT.709 Y'CbCr with
  sub-black and super-white kept, so the pattern takes the same conversion path as a 10-bit
  DeckLink capture (code values are exact RP 219 values). Output stops while the source is not
  shown (for example while receiving).
- **Settings...** on a test pattern sets the pattern, the audio and the overlay. Audio: a sync beep
  (1 kHz at -20 dBFS for one frame, once a second; default), continuous 1 kHz at -20 dBFS or
  -18 dBFS, or none. Overlay (default on): the Mac's time of day as HH:MM:SS plus the frame within
  the second, a bar that fills over each second, and a square that flashes on the first frame of
  each second. The beep is the audio span of exactly that frame with the same timestamp, so the
  A/V offset at the source is zero by construction; the time of day can be compared with the
  receiving side's clock to read glass-to-glass latency.
- Remote control: the patterns appear in `state.capture.devices` with `test-pattern:<n>` ids and are
  chosen with `select_device`; with one selected, `state.capture.properties` and `set_capture` cover
  its pattern, audio and overlay settings. No backend or admin change is involved.

### Encoding defaults

- First initialization prefers Apple VideoToolbox HEVC, then other hardware HEVC, hardware
  H.264/AV1, then x264. Only exact known encoder IDs (VT, NVENC, QSV, AMF, VAAPI) are offered, and
  only when OBS registers them.
- Defaults: 6 Mbps CBR, two-second keyframes for every encoder and codec (HEVC Main, Main10 and
  Main 4:2:2 10 share one encoder configuration; H.264 hardware, VideoToolbox included, and x264
  moved from one second to two on 2026-10-04), HEVC Main
  (H.264 baseline where offered, x264 also ultrafast/zerolatency), Opus audio, `Output/Mode=Advanced`, rescale disabled. Quick bitrate range
  is 1-12 Mbps; the native Advanced dialog keeps out-of-range values.
- NVENC (H.264 and HEVC) defaults to P4, ultra-low-latency tuning, quarter-resolution multipass,
  look-ahead off and adaptive quantization on; all remain editable in Advanced. With
  ultra-low-latency tuning, no B-frames and no look-ahead, `obs-nvenc` returns each packet right
  after its picture (upstream holds three frames for throughput), and CBR uses a one-frame VBV
  (bitrate / fps) that follows live bitrate changes; a `vbvBufferSize=` custom option overrides it.
  The encoder logs its submit-to-packet latency (average and maximum) when it stops. x264 CBR with
  `zerolatency` likewise uses a one-frame VBV unless a custom buffer size is set (macOS included).
  VideoToolbox keeps its native defaults.
- Keyframes always carry their parameter sets: the same load/save policy forces `repeat_headers` on.
  The WHIP service requests it, but Pixelview does not apply service encoder settings
  (`ApplyServiceSettings=false`), and NVENC otherwise sends SPS/PPS only with the first IDR, so
  browsers that joined later (the engine passes H.264/HEVC through) decoded nothing. x264 repeats
  them by default (it now also emits AUDs, as OBS's WHIP setup does); VideoToolbox ignores the key.
  Measured on the T4 (25 fps, 2 s keyframes, 7 s): NVENC HEVC and H.264 carried SPS in 1 of 4
  keyframes before and 4 of 4 after; x264 4 of 4 either way. Verified live on 2026-10-04: NVENC
  HEVC and NVENC H.264 streams from the Windows T4 to the dev backend now play in a browser (both
  were black before the fix).
- B-frames are a permanent policy: `bframes`, `bf` and `bframe_ref_mode` are hidden in Advanced and
  normalized off on every load and save, including x264 `x264opts`, NVENC `frameIntervalP`/UHQ and
  AMF `ffmpeg_opts` overrides.
- The sidebar Profile list labels HEVC Main10 "(recommended)" and Main 4:2:2 10 "(transcoded for
  browsers)", each with a tooltip (the Pixelview Player iOS app and Pixelview Desktop on Apple
  silicon play 4:2:2 natively; browsers get a server transcode). The saved profile value is unchanged.
- HEVC profile maps the canvas format: Main to NV12, Main10 to P010, Main 4:2:2 10 to P216 (limited
  range only). Main/Main10 default to limited range but honor a saved Full setting. Saves are
  in-process transactions: `basic.ini` and `streamEncoder.json` roll back on failure.
- Every profile encodes the same 10-bit 4:2:2 capture; only the stream format differs. The canvas
  is a linear float texture for all SDR outputs (a Pixelview change in `obs_init_textures`: upstream
  rendered NV12 through an 8-bit RGB canvas, which clipped sub-black, super-white and out-of-gamut
  Y'CbCr and rounded twice), and chroma is taken co-sited from the even pixel in
  `libobs/data/format_conversion.effect` (upstream's `[1 2 1]/4` average in linear light softened
  chroma edges by up to ~130 of 1023 codes):
  - Main 4:2:2 10 (P216): luma and chroma exact; the VideoToolbox encoder plugin rounds the 16-bit
    canvas words to the nearest 10-bit code instead of letting VideoToolbox truncate them (which
    cost one code on some samples).
  - Main10 (P010): luma exact; chroma is the mean of each row pair in SDR, and `[1 2 1]/4` around
    the even row for the top-left sited BT.2100 modes, with no horizontal filtering.
  - Main and H.264 (NV12): the 10-bit value rounded once to 8 bits, chroma as for Main10.
  This holds for a 1080-line source filling the canvas 1:1; any scaling resamples.

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
- Windows (source only, see "Windows port"): only `pixelview://` is handled. The installer registers
  it for the user; a development build claims it only when no usable registration exists. A link
  opened while the app runs is handed to that instance over a per-user local socket and the new
  process exits. `https://play.pixelview.io` links are never claimed, so they keep opening in the
  browser.
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
- The release DMG opens a laid-out install window: `Pixelview Desktop.app`, a chevron and the
  `Applications` shortcut on a light background with a "Drag ... to Applications" caption, and the
  `Licenses` folder on a lower band. `cmake/macos/pixelview-dmg.sh` builds it (Finder records the
  layout from `pixelview-dmg-layout.applescript`); the background is
  `cmake/macos/resources/pixelview-dmg-background.tiff` (1x + 2x), regenerated by
  `cmake/macos/pixelview-dmg-background.swift`. Verified by building a preview DMG from a local
  build and opening it in Finder; not yet exercised in a notarized release.

## Receiving

A started receive shows the same live indicators as sending: the red Dock overlay and the active
menu-bar icon, from Start receiving until any stop (button, error, stall, kick or quit). An active
OBS output keeps owning both. Verified by eye on 2026-09-28 in the development build.

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
  H.264 constrained baseline (`42c02a`, packetization-mode 1), HEVC Main, Main10 and Main 4:2:2 10
  (see below), VP9 profiles 0 and 2, plus stereo Opus. Each stream is decoded in its
  own sampling and depth (v210 for 4:2:2, P010 for 10-bit 4:2:0, NV12 for 8-bit) at up to 1920x1080 (30 or 60 fps depending on the negotiated HEVC level) in the
  operator's colour mode, passed as `connect(..., color)`:
  - SDR (default): limited-range BT.709 only. BT.2020 or PQ/HLG input stops with
    `hdr-source-needs-hdr-receive`.
  - HDR PQ or HLG: limited-range BT.2020 P010 or v210, delivered with `VIDEO_TRC_PQ`/`VIDEO_TRC_HLG` and
    the Rec.2100 matrix. The stream's own transfer is used when signalled (HEVC VUI:
    `bt2100-pq`/`bt2100-hlg`). VP9 signals only the BT.2020 gamut (`bt2020-10`), so there the
    operator's PQ/HLG choice labels the frames; wholly unsignalled colour is also trusted. An
    explicit BT.709 source stops with `sdr-source-in-hdr-receive`, the other HDR transfer with
    `hdr-transfer-mismatch`.
  - Full range, unknown combinations and eight-bit HDR stop with `unsupported-colorimetry`.
  The typed reason is reported in `get_status.failure` and the Receiving panel names the fix. The
  engine's WebRTC colour-space RTP header extension (what Chrome uses) is not read by the GStreamer
  receiver; the Desktop relies on the bitstream plus the operator setting.
- Video: decodebin3 -> P010/v210/NV12 policy -> clocked appsink -> `obs_source_output_video2`. Audio: bounded
  queues -> F32 stereo 48 kHz -> clocked appsink -> `obs_source_output_audio`. Both share the
  pipeline clock, and the source runs libobs async-unbuffered so frames are not rebuffered twice.
  OBS timestamps are the sink render time (base + running time + the sink's configured pipeline
  latency), so unbuffered video and timestamped audio line up in OBS. Audio arriving more than
  90 ms past its render time is withheld rather than delivered: libobs raises its global audio
  buffering for stale audio and never lowers it, so one startup burst used to delay receive audio
  (and later sending) for the rest of the session (640-960 ms seen). Production receives on
  2026-09-28 had a median of 14 ms; 4 of 116 windows fell between 50 and 90 ms (short gaps under the
  earlier 50 ms limit, now passed with at most ~90 ms of buffering); everything later was a receive
  start (216-736 ms) or a stall, still withheld. Receive start typically drops 1-2 s of queued audio.
- The bundled `applemedia` plugin is rebuilt from gst-plugins-bad 1.28.3 with a vtdec patch that
  sizes the HEVC output reorder queue from the stream's SPS. Upstream holds 16 frames (~640 ms at
  25 fps) regardless of B-frames and cannot declare it without a caps framerate, so HEVC video
  used to arrive ~600 ms behind audio and ~600 ms later than necessary. H.264 (baseline, no
  reordering) and VP9 were not affected. The same patch adds v210 as a decoder output, chosen only
  for 4:2:2 streams (upstream has no 4:2:2 raw format and would subsample them to P010), and picks
  NV12 or P010 by the stream's bit depth.
- The log records the configured sink latency with each stage's cumulative latency once per
  change (`[pixelview-whep] sink latency`), and the worst video/audio lateness against render time
  plus withheld stale audio for any 5 s window over 200 ms late or with withheld audio
  (`[pixelview-whep] worst lateness`). Healthy windows are not logged: OBS's repeated-line filter
  hid a real stall behind 158 near-identical healthy lines.
- Verified 2026-09-28 against the production backend with an HEVC session and local monitoring
  (no DeckLink): sink latency 120 ms (100 ms jitter buffer), video and audio 5-18 ms late, no
  stale audio withheld, no receive-driven audio buffering, sync judged correct by ear. Before the
  change the same setup measured video ~600 ms late and 896-960 ms of added audio buffering.
  End-to-end glass-to-glass latency was not measured, and DeckLink output was not re-tested.
- An 8-bit stream (HEVC Main, H.264, VP9 profile 0) is decoded as NV12 and widened by the plugin
  to P010 holding exactly code x 4 (16 -> 64, 235 -> 940, 128 -> 512). Decoding it straight to
  P010 let VideoToolbox apply a full-range gain (code x 1023 / 255: white at 943, neutral chroma
  at 514, measured), and handing NV12 to libobs would pass it through an 8-bit RGB texture.
- The patched signaller rejects POST redirects and pins the session `Location` to the accepted
  response origin. The endpoint's `?token=` is also set as the signaller `auth-token`, so session
  PATCH/DELETE carry `Authorization: Bearer <token>` (the engine's `Location` has no token; engine
  DELETE accepts the header from pxlview/pv-engine#58, older engines answer 401 and clean the viewer
  up when the peer connection closes). A bus error, EOS, 15 s without a first frame or 5 s without
  video after frames have flowed ends the attempt; the plugin has no reconnect loop of its own. The
  Receiving panel reconnects an attempt that ended without a typed reason: it stops the session and
  starts a fresh login and WHEP session after 2, 4, 8, then every 10 s
  (`[pixelview-receive] media stopped; reconnecting in N s`), showing
  "Stream interrupted. Reconnecting in N s…". The intent stays set while waiting, so the button
  reads Stop receiving (which cancels) and the mode stays locked; the backoff resets on fresh video.
  Typed refusals (colour mode, HEVC profile), kicks, deleted sessions and control-plane errors never
  retry. A fresh session is required because the engine tears a viewer down on a transient ICE
  disconnect (pxlview/pv-engine#61).
- Verified 2026-09-29 against the local pentest backend and a local pv-engine (HEVC Main 1080p25
  x265 over SRT) with a headless harness running the real `PixelviewReceiver` and the built
  `pixelview-whep` module under the same reconnect rule (the GUI reconnect code itself is covered
  offline by `test_receive_ui.py`): a 5 s engine freeze self-heals without a reconnect; a 5 s
  sender cut, a 30 s sender outage, a 25 s engine freeze and an engine kill/restart each recover
  with fresh sessions (13-50 s, dominated by the engine: viewers that join just before or while its
  input restarts, and viewers present across a sender reconnect, never get media). About one first
  connect in ten fails with a codec-route CORE/NEGOTIATION error and recovers on the retry. The
  real DeckLink card was not exercised through a reconnect (no GUI control available). Endpoints stay in private memory; OBS-log diagnostics are limited to
  timeout/EOS and GStreamer domain/code.

### HEVC 4:2:2 10 reception

- HEVC Main 4:2:2 10 is received on the same route as Main/Main10: the capability probe decodes a
  Main 4:2:2 10 fixture to v210 through the patched `vtdec_hw`, and only then does the offer add
  `profile-id=4` (`interop-constraints=1d0800000000`, at the probed level), which is what makes the
  engine pass a 4:2:2 sender through instead of transcoding it to VP9. The route selector admits
  `main-422-10` on the stock decoder path, VideoToolbox delivers v210 (4:2:2 chroma intact; P010
  would subsample it), and the frames reach OBS as `VIDEO_FORMAT_V210` in SDR, PQ or HLG. Jitter
  buffer, A/V sync, reconnect, preview and the rendered DeckLink output are the ordinary ones.
- The limits are the ordinary ones too: up to 1920x1080, 60 fps at level 4.1, limited range. The
  canvas is linear RGB; the DeckLink output repacks it as v210 (see DeckLink program output), which
  in SDR returns the decoder's code values exactly.
- Where the probe does not decode 4:2:2 (no hardware 4:2:2 decoder, or the probe's known EOS race
  dropped the bit for this app run), profile 4 is not offered and the engine transcodes to VP9 as
  before. A `main-422-10` stream that arrives anyway, or any other non-Main/Main10 HEVC profile, is
  refused before the first access unit with a typed `GST_STREAM_ERROR_WRONG_TYPE`:
  `get_status.failure` reports `unsupported-hevc-main-422-10` or `unsupported-hevc-profile`, and the
  Receiving panel stops with "The sender is streaming HEVC 4:2:2 10-bit, which this Mac cannot
  decode. Please use the HEVC Main or Main10 profile on the sender."

### Jitter buffer

`PixelviewReceive/BufferMs` defaults to 100 ms and is passed explicitly on every `connect`. The
**Buffer: N ms** link in the Receiving panel edits it (0-2000 ms, persisted, applied to the next
connection). It is a jitter-buffer target, not measured end-to-end latency.

### Canvas precision

While receiving, an in-memory canvas transaction switches the main texture to P010 / limited with
Rec.709, or Rec.2100 PQ/HLG when **HDR** is ticked, so ten-bit 4:2:0 precision survives to the GPU
texture (877 distinct levels measured; an SDR NV12 canvas now renders into a float texture as well). The sender's format, colorspace,
range, graphics module and SDR-white/HDR-peak levels are restored when leaving receive mode; the
switch is refused while any output is active, and a failed rollback blocks further mode changes
until restart. Preview and fullscreen remain eight-bit boundaries (OBS tone-maps HDR for them).

### HDR receive setting

The Output heading row of the Receiving panel holds an **HDR** checkbox, a PQ/HLG choice and a
nominal peak (100-10000 nits, default 1000), persisted as `PixelviewReceive/HDR`, `HDRTransfer` and
`HDRNits`. The setting must match the sender and is fixed per connection (disabled while receiving).
In receive mode a change resets the canvas immediately and is refused while the DeckLink output or
any other output is running. The nits value becomes the OBS HDR nominal peak: the HLG reference
peak for decode and re-encode and the DeckLink mastering/MaxCLL/MaxFALL metadata. Backend session
status also exposes `color_profile`/`max_luminance`; the Desktop does not read them yet.

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
  AutoStart) with the keyer UI hidden. AutoStart applies to receiving only: it starts the output
  once received video arrives. Launching (Sending mode) never opens the output, so another
  application such as DaVinci Resolve can keep using the card while this Mac only sends. Reception (including HEVC 4:2:2) uses the rendered
  program output: main texture -> v210 packing on the GPU -> `decklink_output`, with audio from the
  ordinary OBS mix (source gain, mute and mixer routing apply; monitoring is separate).
- The card is always given 10-bit 4:2:2 Y'CbCr (`bmdFormat10BitYUV`, v210, limited range), in SDR
  and HDR, in Receiving and Sending mode. The `DrawV210*` techniques in
  `libobs/data/default.effect` pack one 32-bit v210 word per RGBA8 texel, with chroma co-sited
  from the even pixel and the inverse of the transfer and matrix the v210/P010 source conversions
  use. A 10-bit program that fills a same-size 10-bit canvas 1:1 therefore reaches the card with
  the code values it was decoded or captured with, including sub-black and super-white in SDR; an
  output mode of another size is scaled in linear light first. The sender's profile does not limit
  the output: every SDR canvas is a linear float texture. Preroll and underrun frames are
  v210 black. Only the keyer (UI hidden) still uses 8-bit BGRA.
- **Output range** (Limited, the default, or Full) in the output settings chooses the levels of the
  Y'CbCr put on SDI so that it matches the monitor's setting; SDR SDI carries no range flag. The
  stream and canvas are always limited. Full rescales black/white from 64/940 to 0/1023 as BT.2100
  defines full range, clipped to the SDI codes 4-1019 (so values that were sub-black or
  super-white are clipped), and lands within one code of the ideal mapping; Limited stays exact.
  Together with the capture's range setting this gives one rule: the sender's input range matches
  the grading system, the receiver's output range matches the monitor.
- With an HDR canvas and a device reporting HDR metadata support (and Force SDR off), the v210
  frames carry Rec.2020 HDR metadata: a PQ canvas leaves as PQ (EOTF 2), an HLG canvas as HLG
  (EOTF 3). Without HDR metadata support the HDR canvas is tone-mapped to SDR v210. The start logs
  `[decklink] output video: ...` with the mode chosen. HDR is exact for neutral and ordinarily
  saturated colours; the linear float canvas costs precision in the near-zero channel of very
  saturated ones (tens of codes there, on the order of 0.01 nit).
- `bind_receive(source)` keeps the source identity for the watchdog only, and a running output
  accepts a new (or no) source. The output plays the program canvas, so it keeps running across a
  stalled, ended, stopped or reconnecting receive (repeating the last canvas frame) and is only
  rebound; it drains only when the card output stops, the device is removed or Receiving mode is
  left. Stopping the card with
  frames still outstanding after a stall deadlocked the main thread inside the DeckLink SDK
  (`DisableVideoOutput` → `releaseAllOutstandingFrames`, macOS hang reports 2026-09-28 20:53 and
  22:33).
- `receive_status` returns a `reason`; the UI watchdog (100 ms poll) logs
  `[decklink-output-ui] receive output stopped after N ms: <reason>`, resumes automatically when the
  source is ready again, bounded to three consecutive attempts that fail within ten seconds of
  starting (budget reset on every bind).
- A Start that does not happen (manual or automatic) shows a non-blocking "The DeckLink output did
  not start" warning with the reason as well as the `Start failed: <reason>` log line: nothing
  received yet, no saved settings, device or mode unavailable, the canvas could not follow the mode's
  rate, or the output's own `last_error` (device not connected, mode unavailable, card busy, and in
  Sending mode a frame-rate mismatch naming the canvas rate and the output mode).

### Fullscreen and projector

Fullscreen uses the OBS projector on a chosen display (View menu / sidebar display menu) and the
existing Escape action to close. Stop receiving returns the canvas and outputs to the sending
configuration.

## Log upload

- Stream diagnostics from the app's log (the same level and repeat filter, from the first line of
  the launch) are uploaded to the backend's `POST /desktop/logs`, which forwards them to the
  central Loki as `job="pixelview-desktop"` (backend `docs/desktop-log-ingestion.md`). The Desktop
  never talks to Loki or Grafana directly.
- Only lines on an allowlist are uploaded (`LogShipper::role`): Pixelview's own lines (pairing,
  control socket, remote control, receiving), the encoders with their settings blocks
  (`[VideoToolbox …]`, `[CoreAudio …]`, `[… encoder: '…']`), the stream output (`[obs-webrtc]`,
  streaming start/stop, `Output '…'` frame totals), the failure lines libobs logs bare (`Stream
  output type … failed to start!  Last Error: …`, `Error encoding with encoder`, `creating encoder
  … failed`, skipped frames due to encoding lag, `obs-output '…'`), `video settings reset` /
  `audio settings reset`, DeckLink (`decklink:`, `Decklink API`, `[decklink] output video`, frame
  create/schedule failures, `No active audio`, the missing-driver line), audio-buffering increases
  and limits, the unclean-shutdown marker and the encode/send rows of the shutdown profiler.
  Everything else stays in the local log file only and is neither queued nor spooled: scene and
  source names, the audio monitoring device, the module list, hotkeys, media sources, libav's own
  `[ffmpeg]` messages (they can name a media file) and the rest of the profiler. The match is by
  line prefix, so a source the user renamed to start with an allowlisted prefix (for example
  "Pixelview …") would have its own log lines uploaded; the app's managed sources are not
  affected. Each batch also
  names the app version and build, the macOS version and which Mac it is (`machdep.cpu.brand_string`
  and `hw.model`, e.g. "Apple M1 Pro" / "MacBookPro18,1").
- On by default and disclosed as **Help > Log Files > Share Logs with Pixelview Support**
  (`PixelviewDiagnostics/ShareLogs`). Switching it off stops capture and deletes buffered and
  spooled lines.
- Lines are tagged `send` (encoders, `[obs-webrtc]`, output, streaming start/stop, remote control),
  `receive` (`[pixelview-whep]`, `[pixelview-receive]`, `[decklink-output-ui]`) or `app`. Receive lines carry
  the session and viewer they were captured under; receiver state changes are logged for this.
  The session is attached only once the backend has accepted the receiver login, so a mistyped
  session field (or a password typed into it) is never uploaded, and an ID the backend would not
  accept (it allows 1–128 of `A-Za-z0-9_-`) is left out of the line instead of failing the batch.
- Before anything leaves the Mac, device tokens, bearer values, `token=`/`password=`/`passphrase=`
  style pairs and every URL query string are redacted, the home directory becomes `~` and the
  source name in an audio-buffering or audio-lagging line is removed; the backend redacts again.
- A paired Desktop uploads with its device token and adds the receiver's viewer token when it
  receives from the same backend, so the backend can attribute receive lines to that session; an
  unpaired receiver uploads with its viewer token alone. Without either credential lines stay
  queued. Identity is assigned by the backend from the credential, never from the batch.
- Batches go out every 10 s, at most 500 lines / 192 KiB and one launch per batch. An error line
  (after 2 s) or a full batch goes out earlier, but never less than 5 s after the previous upload,
  so a steady error source stays at 720 requests an hour against the backend's 1800. The answer
  removes exactly the lines that were in the batch, also when older lines aged out or overflowed
  while the request was in flight. 429/503 honour `Retry-After`; transport and 5xx
  failures back off from 10 s to 5 min; 401/403 pause until the credential changes; a backend
  without the endpoint (404/405) is retried hourly. Upload outcomes are logged on transitions only.
- Unsent lines (up to 5000) are spooled to `obs-studio/pixelview-log-spool.ndjson` at most every
  5 s, promptly after an error line and at close, and are uploaded by the next launch. Lines older
  than 50 minutes are discarded on both sides: Loki rejects them on the shared streams, so an
  offline or long-closed Desktop loses the older part of its spool.
- Not included: crash reports, full log files on demand, debug-level lines and metrics. The
  profiler rows are written at quit, so they only arrive if the app is started again within 50
  minutes.

## Shutdown and recovery

- The main window carries `WA_DeleteOnClose`, so `OBSBasic::closeEvent` calls
  `PixelviewShutdownReady()` first. The helper stops receiving, cancels streaming intent and reports
  whether stop, socket, output or setup state is still settling; while it is, the close is ignored
  and retried every 100 ms. The wait is bounded by `PIXELVIEW_SHUTDOWN_WAIT_MS` (10 s); after it, or
  on `aboutToQuit`, an active stream output is force-stopped and normal scene teardown runs.
  `kill -TERM` goes through the same path.
- Before that gate, the close also waits while a dialog of the main window is still inside
  `exec()` (`PixelviewDialogLoopsClosed()`; `exec()` keeps `WA_ShowModal` set until it returns).
  On macOS a message box runs the native `NSAlert` loop, which also delivers the window's deferred
  delete, so accepting the close there tore down and freed the window, and the stack-allocated box
  with it, under the still-running method (quitting with a stream-failure box open aborted in
  `~OBSBasic`). A visible dialog is closed like Esc, so a question answers No/Cancel, never
  `NoButton`; a box that quit already hid but whose native loop still runs ("Dialog is not top
  level modal window") has that loop stopped with the button the close chose (`abortModal` when
  none). The close retries every 100 ms; `closeWindow` repeats the check except on `aboutToQuit`.
- An unclean-shutdown sentinel (`.sentinel/run_*`) produces the branded one-button Continue prompt on
  the next launch; it offers neither safe mode nor crash upload and blocks until dismissed.
- Reconnect summary: sender control socket 1-30 s backoff, media unaffected by control drops,
  stream retries per profile reconnect settings; receiver control 1-30 s re-authentication with 7 s
  media grace; receive media reconnects with a fresh session (2-10 s backoff) while a rendered
  DeckLink output keeps playing; a DeckLink receive output resumes up to three times after a named
  stop. Relaunch
  reconnects pairing but never invents streaming or receiving intent.
- Qt is pinned to the OBS 6.10.3 package on macOS because 6.11.1 crashes in `QImage::toCGImage`
  during scene activation.

## Windows port (in progress, development build only)

The Windows x64 development build compiles and runs (2026-10-03, see "Verified live"). Apart from
what that section lists, treat everything below as source-level work.

- **Control socket and receiver transport.** `PixelviewWebSocket.cpp` is an RFC 6455 client on
  `QSslSocket` (text frames only, no extensions/redirects/cookies, size limits, RFC ping/pong
  liveness with the same `ControlPing` policy as macOS). `PixelviewDesktopQt.cpp` and
  `PixelviewReceiverQt.cpp` map closes exactly like the macOS files (401 → 4401, 403/3xx/TLS →
  4403 for the control socket; 401/403/3xx → 1008 and TLS → 4403/495 for the receiver). The
  receiver logs in with `platform: WINDOWS`.
- **Credentials.** Windows Credential Manager generic credentials (`CRED_PERSIST_LOCAL_MACHINE`,
  never roaming): `com.pixelview.desktop.device:<origin>` and
  `com.pixelview.desktop.receiver:latest-session`, with the same verify-after-write/delete rules
  and log categories as the Keychain. There are no prompts. Blobs are limited to 2,560 bytes, so an
  oversized receive password is not saved.
- **Isolation.** The single-instance mutex is `PixelviewDesktopCore`, settings live under
  `%APPDATA%\pixelview\obs-studio`, and third-party plugins are loaded only from that per-user
  tree (never from `C:\ProgramData\obs-studio`). The app switches to its own `bin\64bit`
  directory at startup so link launches resolve data correctly.
- **Updates.** The OBS Windows updater, What's New and their obsproject.com endpoints are not
  compiled (`OBS_WINDOWS_UPDATER` is never defined). Release builds use WinSparkle 0.9.4 with the
  Pixelview appcast and an EdDSA key (`frontend/cmake/feature-winsparkle.cmake`,
  `PixelviewWinSparkle.cpp`). Development builds have no updater.
- **Encoders.** Hardware HEVC is preferred in the order NVENC, AMF, then QSV; x264 is the fallback.
  The Windows encoder log lines are on the log-upload allowlist, and Windows home paths are
  redacted in both separator forms.
- **Receiving is not available on Windows.** `pixelview-whep` is macOS-only (GStreamer plus
  VideoToolbox). The Receive tab is disabled when the module is absent, and Start explains why.
- **Not ported yet:** Linux; the WHEP receiver; Windows-specific capture checks; the D3D11 compile
  of the Pixelview canvas/format-conversion shaders (written and checked only as GLSL/Metal so far).

## Verification status

### Compiled and offline tests (`test/pixelview`, `plugins/*/tests`)

The Python drivers compile production source slices (offscreen Qt, Objective-C++ transports against
loopback servers, libobs harnesses). `python3 -m unittest discover -s test/pixelview` runs 230
tests; `test_icon_assets` (needs Pillow) and `test_pixelview_sources` (corresponding-source inventory
gate) fail in the current environment regardless of changes.

- Control socket and pairing: `test_desktop.py` (`desktop_native.cpp` policy table, real exchange,
  Keychain record, 4401 close), `test_desktop_control.py` (`desktop_control*.mm`: NSURLSession
  against a scripted server for ready, ping/pong, start, TCP loss while streaming, revoked/replaced,
  60 s silence), `test_control_socket.py` (RFC 6455 keepalive, refused upgrade),
  `test_desktop_retry.py`, `test_media_failure_pairing.py`, `test_whip_retry.py`,
  `test_pairing_ux.py`, `test_pairing_defaults.py`, `test_backend_selection.py`, `test_stream_lock.py`,
  `test_streaming_ui.py`, `test_remote_control.py` (admin remote-control source contracts),
  `test_log_shipper.py` (`log_shipper.cpp`: capture bound and switch, redaction, roles, batching,
  retry/pause outcomes, spool across launches; plus the log-handler, menu and receiver wiring).
  `desktop_backend_smoke.mm` is an opt-in live tool.
- Keychain: `test_keychain_reliability.py`, `test_keychain_noninteractive.py`,
  `test_keychain_async.py` (mocked Security APIs), `test_receive_keychain_restart.py` and
  `test_receive_ui.py` (real isolated test-only Keychain service, removed afterwards),
  `test_config_isolation.py`.
- Receiver: `test_receiver.py` (`receiver_native.cpp`, `receiver_transport.mm`),
  `test_receiver_login_errors.py`, `receiver_expiry.cpp`, `test_receive_ui.py`,
  `test_mode_persistence.py`, `test_deep_links.py`, `test_canvas_fullscreen.py`,
  `test_receive_preview_zoom.py`, `test_receive_precision_canvas.py`, `run_receive_precision_probe.py`, `run_video_fidelity_probe.py`
  (v210 through the real libobs canvas to the encoder input and to the DeckLink v210 render)
  (real libobs/OpenGL level count). `receiver_live.py` and `receiver_media_live.py` are opt-in live
  tools.
- Test patterns (`plugins/pixelview-test-pattern/tests/run-generator.py`, generator only, no
  libobs): RP 219 code values and bar/PLUGE layout, every pattern legal at four sizes, v210 packing
  round trip with co-sited chroma, overlay bounds (inside the 75 % bars, chroma aligned, nothing
  outside its rectangle), tone levels, phase continuity and beep gating. `test_capture_policy.cpp`
  (ids, status) and `test_capture_startup.py` (list order, separator, selection, no automatic
  replacement of a saved pattern) cover the sidebar.
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
  `run-audio-route.py`, `run-whep-loopback.py` (synthetic loopback WHEP
  through a real engine build: all five 4:2:0 video alternatives plus HTTP 406 negative, HEVC
  Main10 and VP9 profile 2 PQ/HLG, HEVC Main 4:2:2 10 in SDR/PQ/HLG, and the colour-mode refusals).
- DeckLink (`plugins/decklink/tests`): `run-receive.py` (complete output owner with a refusing fake
  SDK, ASan/UBSan with `PV_DECKLINK_SANITIZE=1`: start rollback and restart, device exclusion, OBS
  mixed audio with source gain/mute, 600 ms frame gap and stalled receive stay healthy, live rebind,
  named reasons for an inactive card and a removed device), `test_decklink_output_ui.py` (compiled
  Qt watchdog including resume budget).
- Sender WHIP/DeckLink/VideoToolbox modules and the GStreamer runtime pass deep/strict code-signature
  verification in the canonical Developer ID local build.

### Verified live

- Test patterns (2026-10-04, signed local build, isolated `--app-config-dir`, unpaired, an
  UltraStudio Recorder 3G attached without signal): with a saved scene holding a visible
  `Pixelview Test Pattern` and a hidden `Pixelview Capture`, the app restored "Test pattern: SMPTE
  color bars" in the device list and the attached card did not replace it; the preview showed the
  RP 219 bars with the running time-of-day overlay, the meter showed the once-a-second beep, and
  crosshatch rendered likewise. Quit through SIGTERM was clean (no remaining sources, empty
  sentinel, no crash report); the plugin's destroy freed all of its own allocations before libobs
  counted leaks. The shutdown "Number of memory leaks: 1" seen in some runs also occurs with the
  original DeckLink-only scene (7 of 8 runs), so it is not from the test pattern; its cause is not
  identified. Not verified: choosing a pattern from the dropdown or **Settings...** in the running
  app (both need pairing; covered only by `test_capture_startup.py`), streaming a pattern over WHIP
  to a receiver, the beep/flash alignment as received, and selecting a pattern from the admin.
- Windows x64 development build (2026-10-03, `windows-port`, Windows Server 2025 EC2 VM without a
  GPU driver or DeckLink card, so D3D11 ran on the Microsoft Basic Render Driver and x264 was the
  only video encoder): `cmake/windows/pixelview-build.py` built `Pixelview.exe`; the app started
  cleanly and, with `PIXELVIEW_LOCAL_DEVELOPMENT=1`, paired through the Pair dialog against the local
  backend in pentest mode (pairing exchange, Credential Manager save under
  `com.pixelview.desktop.device:http://localhost:8000`, control socket `DESKTOP_READY`; the backend
  registered the desktop for node 707880). Not run on Windows: the test suites, capture, streaming
  (no engine), Unpair, revocation, links, the installer and release.
- NVENC latency on Windows (2026-10-04, `windows-port`, EC2 g4dn Tesla T4, driver 616.92): a libobs
  probe fed the 1080p30 test pattern into `obs_nvenc_hevc_tex` and OBS's null output for 15 s per
  setting, with no skipped frames. Submit-to-packet: upstream defaults (P5, high quality, three-frame
  output delay) 70.7 ms average / 76.6 ms max; Pixelview defaults (P4, ultra-low latency,
  quarter-res, AQ, zero delay) 15.0 / 21.3 ms; P7 17.0 / 24.6 ms; P4 with look-ahead (13-frame delay)
  404 / 412 ms. x264 logged the one-frame buffer (200 kbit at 6 Mbps) and kept a custom buffer size.
  Not measured: transmission and receive, real camera content, other GPUs, and NVENC on a live
  stream.
- Control socket against the local backend (`dev.sh --k8s`, backend commit ab4aa77) with the rebuilt
  signed bundle: pairing exchange and `DESKTOP_READY`; pongs recorded in Redis presence with parsed
  settings; backend reload (close 1005) followed by automatic reconnect; admin revocation via
  `SOCKET_DESKTOP_REVOKED` and DB-only revocation via 4401, both disabling pairing with the revoked
  message; Unpair and re-pairing; `DESKTOP_START`/`STARTED` (WHIP granted) and `DESKTOP_STOP`/`STOPPED`
  via the smoke tool. (This run predates account removal on Unpair and the automatic local cleanup
  after revocation; those have not been run against a live backend yet.)
- Admin remote control (2026-09-24, local backend on pxlview/pixelview-backend-v4#236 without an
  engine, rebuilt signed bundle in an isolated `--app-config-dir` paired to dev node 707880, no
  DeckLink device attached): `get_state` round trips of ~120 ms; `set_fps`, `set_bitrate`,
  `set_profile` and `set_encoder` applied and appeared in the Mac's sidebar, also when driven from
  the admin modal (pxlview/pixelview-admin#162); out-of-range and unknown values, a missing
  device, and `set_capture`/`set_mute`/`fit` without a source returned `ok: false` with the reason
  and no dialog on the Mac. A remote `start` refused a settings change while starting; before the
  capture-device gate existed it went through `DESKTOP_START` to the WHIP POST (404, no engine).
  With the gate, `start` without a device is refused ("Choose a capture device to stream.") and
  `can_start` is false. SIGTERM afterwards shut down cleanly.
- State push and DeckLink commands (2026-09-24, same setup with an UltraStudio 4K Mini attached):
  a remote change reached the admin store as a `SOCKET_DESKTOP_STATE` push ~80 ms later; a mute
  toggled on the Mac itself was pushed with no command involved; quitting the app pushed
  `online: false` and relaunching pushed it back online. `set_capture` toggled `buffering` and
  changed `color_range` and restored both, an unoffered `mode_id` and `device_hash` were refused,
  `fit`, `select_device` and remote unmute applied, and `can_start` became true with the device.
- Receiver control: live loopback login/registration with viewer-record read-back in Redis and exact
  removal after stop; live missing-session 401 mapping.
- Receiving media (earlier build, 50 ms jitter era): local backend + engine + synthetic H.264/Opus
  publisher through normal ingress, 135 s uninterrupted video/audio in the GUI, mode round trip,
  fullscreen with advancing timecode, Listen/Mute behaviour. Synthetic loopback WHEP decode for
  H.264, HEVC Main/Main10 and VP9 0/2 on the current plugin source.
- HDR receive decode (offline loopback, not a live sender): x265 Main10 PQ (with HDR10 SEI) and
  HLG, and libvpx VP9 profile 2 BT.2020, through the real `pixelview_whep_source`, VideoToolbox and a
  loopback server built from the engine's WHEP negotiation (`pv-engine`
  `fix/whep-receiver-compatibility`; the harness does not build against the current hotfix branch).
  Frames arrived as P010 with the expected transfer and 876 codes; SDR-in-HDR, HDR-in-SDR and
  PQ-as-HLG stopped with their typed reasons. One VP9 run failed earlier, at SDP negotiation (the
  offer lacked profile 2), and passed on three reruns.
- HEVC 4:2:2 receive decode (offline loopback 2026-10-01):
  x265 Main 4:2:2 10 in SDR, PQ and HLG through the real `pixelview_whep_source`, the patched
  `vtdec_hw` and a loopback server built from the WHEP negotiation of `pv-engine` `main` (`d04faf6`),
  which selected `hevc-10bit-422` passthrough for the Desktop offer. Frames reached OBS as
  `VIDEO_FORMAT_V210` with 831-840 luma codes, and a fixture whose Cb alternates on every row kept
  that alternation on all 127 row pairs (impossible after 4:2:0); PQ-in-SDR stopped with its typed
  reason. The same run passed the fourteen earlier cases, now against engine `main`. Separately, a
  1080p25 x265 Main 4:2:2 10 clip decoded by the patched `vtdec_hw` matched FFmpeg's software
  decode in all but 4 of 1,382,400 v210 words. On the M1 Pro test Mac the decode cost about
  1.3 ms per 1080p frame.
- HEVC 4:2:2 receive live (2026-10-01, local pentest backend + local pv-engine `main`, x265 Main
  4:2:2 10 1080p25 over SRT, headless harness running the real `PixelviewReceiver` and the
  `pixelview-whep` module from the signed development build): the engine detected `hevc-10bit-422`
  and served it to the Desktop viewer as passthrough; every frame the module handed to OBS in two
  runs (60 s and 30 s, no reconnects) was 1920x1080 `VIDEO_FORMAT_V210`, and a sender whose Cb
  alternates per row kept it on all 540 sampled row pairs. Sink latency 160 ms, video at most
  17 ms late. A Main10 clip sent the same way still arrived as P010. Not verified: the app window
  itself (preview/GPU conversion of v210; screen control was declined), DeckLink output of a 4:2:2
  receive, a Pixelview Desktop sender (VideoToolbox 4:2:2 rather than x265), HDR 4:2:2 live,
  production, runs longer than a minute and other Mac models.
- 10-bit 4:2:2 chain, offline (2026-10-01, M1 Pro, no Blackmagic hardware attached):
  `run_video_fidelity_probe.py` feeds a 1920x1080 v210 source through a real libobs scene. In SDR
  on a P216 or P010 canvas, every sample of five patterns (luma ramp, chroma gradients, colour
  bars, random legal noise, all codes 4-1019) came back identical both at the encoder input (P216
  rounded to 10 bits) and in the DeckLink v210 render; before the co-sited chroma change the same
  probe showed chroma errors up to 131 codes on bar edges and 35 on gradients, and sub-black and
  super-white clipped. PQ and HLG were exact for the ramp, gradients and bars at 30% saturation
  (HLG ramp within 2 codes); random noise and the full code sweep were not (documented float
  canvas limit). `plugins/mac-videotoolbox/tests/run-422-rounding.py` on the hardware HEVC 4:2:2
  encoder at 40 Mbit/s: clean 10-bit codes decoded exact on 128 flat patches, input 12/64 of a
  code low lost one code on 11 of them, and the same input rounded as `encoder.c` now does decoded
  exact. `plugins/decklink/tests/run-receive.py` (also sanitized) covers v210 preroll black, row
  size and the pixel format handed to a fake SDK. After the signed build, a live local receive
  (as above) again delivered only V210 frames, now unclamped (range 0-1), and the app launched and
  quit cleanly with the new effects. Not verified: any real SDI input or output (capture at
  10-bit, the output UI's render path in the running app, v210 with HDR metadata on a card, a
  monitor's picture), a Pixelview Desktop sender to a Pixelview Desktop receiver, and whether
  **Fit** places every capture mode exactly 1:1.
- All profiles, offline (2026-10-01, same Mac, libobs from the signed build): `run_video_fidelity_probe.py`
  with a v210 capture source in SDR gave, at the encoder input, Main 4:2:2 10 (P216) exact; Main10
  (P010) luma exact and chroma exact wherever both rows of a pair agree (within one code of their
  mean where they do not), before the change 131 codes off on bar edges; Main (NV12) within one
  8-bit step of the 10-bit value on every pattern including codes 4-1019, before the change up to 6
  steps off on saturated gradients and 54 on sub-black/super-white. The DeckLink v210 render was
  exact on all three canvases. A P010 (4:2:0 receive) source reached the DeckLink render exact in
  luma and in chroma wherever the rows of a pair agree. PQ and HLG as before (ordinary colours
  exact). Receive: x265 Main decoded by the patched `vtdec_hw` came out NV12, Main10 P010, Main
  4:2:2 10 v210; all 18 loopback cases pass with the 8-bit ones (H.264, HEVC Main, VP9 0) audited
  as P010 with every sample a multiple of four. Not verified: as above (no SDI hardware).
- SDI hardware loop (2026-10-02, UltraStudio Monitor 3G cabled to UltraStudio Recorder 3G, 1080p25,
  `plugins/decklink/tests/run-sdi-loop.py` against the signed build's `decklink` plugin and
  libobs): a v210 pattern through the canvas, the `DrawV210` render and `decklink_output`
  (`10-bit 4:2:2 YUV SDR`) was captured back by `decklink-input` at its default 10-bit format
  identical on every sample of all five patterns, codes 4-1019 included; no dropped frames in
  14 s. With a PQ and an HLG canvas the card accepted `... with HDR metadata` and ordinary colours
  came back exact (HLG ramp within 2 codes; extreme colours as in the offline probe); the Recorder
  did not report HDR metadata on the captured frames, so the signalling itself is unconfirmed. In
  the capture direction, the pattern played to the card and captured into a P216, P010 and NV12
  canvas reached the encoder input exactly as the offline probe measured (P216 exact, P010 luma
  exact, NV12 within one 8-bit step). The harness replicates the output UI's render with the
  shared `decklink-v210-render.hpp`; the app's own UI path, a monitor's picture and a Desktop
  sender to Desktop receiver session were still not exercised.
- Output range switch (2026-10-02, same loop and offline probe, signed build): with Output range
  Full the captured SDI was within one code of the BT.2100 full-range mapping on every sample of
  all five patterns (luma 91-100% exact, chroma 94-100%, neutral chroma exact), the log reported
  `10-bit 4:2:2 YUV SDR, full range`, and limited stayed bit-exact. The setting itself was applied
  through the output's settings in the harness; the dropdown in the app's Output settings and a
  monitor set to full were not looked at.
- End to end through two running apps (2026-10-02, signed development build, local pentest backend
  and local pv-engine `main`): an UltraStudio 4K Mini played a 10-bit 4:2:2 test picture into an
  UltraStudio Recorder 3G; a sender instance captured it and streamed over WHIP, configured and
  started through the backend's Desktop control route (frame rate, capture mode and range, profile,
  12 Mbit/s); a receiver instance received the session and played it out of an UltraStudio Monitor
  3G into the 4K Mini, where `plugins/decklink/tests/run-e2e-sdi-tool.py` averaged 50 captured
  frames. Twelve cases, each profile with the source and the output in limited or full range:
  - Main 4:2:2 10 and Main10: flat patches (black, white, greys, primaries, skin) within 1 code in
    luma and 2 in chroma of the source expressed in the output range, neutral chroma exactly 512,
    609-618 of the ramp's 632 ten-bit steps present. Main 4:2:2 10 kept the row-alternating chroma
    at full amplitude; Main10 averaged it away, as 4:2:0 must.
  - Main: within 3 codes (8-bit steps are 4 codes), black and white exact with a limited source,
    159 ramp steps.
  - Limited in and out carried sub-black (40) and super-white (980) through unchanged in all three
    profiles. With a full-range source, black and white sit at the SDI limits 4 and 1019 and come
    out at 67 and 936 on a limited output and within 1 code of 4 and 1019 on a full output.
  The log reported `10-bit 4:2:2 YUV SDR, limited range` and `... full range`, and the Output range
  dropdown showed the saved value. Not verified: HDR and other frame rates end to end, a monitor's
  picture, production, the receiver's session fields typed by hand (a deep link filled them), and
  the output dialog's Start/Stop button, which did not respond to the accessibility press used for
  the test (the receiver was restarted to change the output range). Repeated 2026-10-02 on the
  signed build of commit ef1809a37 (the pushed state): the same twelve cases with the same
  results, followed by a ten-minute Main 4:2:2 10 limited-range run at 12 Mbit/s with 22,881
  frames out of the card, no lagged, skipped or dropped frames on either side, video lateness 5
  ms, and both instances quitting cleanly. Operator check the same day with independent
  instruments: DaVinci Resolve on this Mac played a picture of a person out of the UltraStudio
  Monitor 3G into the 4K Mini, the sender captured it and the receiver played out of the 4K Mini
  into an UltraStudio Recorder 3G on a second Mac, where Resolve's live capture was compared by
  eye and on the scopes against a still of the same footage. Video levels in and out, Full levels
  in and out, and a full-range source to a limited- range output all matched the still, with the
  loss of fine detail expected at 12 Mbit/s. Not recorded to a file and not measured numerically.
- HDR PQ receive live (2026-09-23, local backend + engine, rebuilt signed bundle with
  `PIXELVIEW_LOCAL_DEVELOPMENT=1`): OBS 32.2 sending HEVC Main 4:2:2 10 Rec.2100 PQ over WHIP; the
  engine transcoded to VP9 profile 2 tagged BT.2020/PQ/limited (the Desktop does not offer 4:2:2);
  the Desktop received in HDR PQ at 1000 nits and the UltraStudio 4K Mini reported HDR metadata
  support, logging `10-bit RGB PQ with HDR metadata`. Runs of ~3 min and ~3.5 min showed 4.3% and
  15.3% lagged frames while the engine transcoded on the same Mac; the cause is not isolated. The
  SDI picture was not inspected on a scope.
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
- Log upload to production (2026-10-02): the signed development build from local master, paired
  to `api4` with the normal settings root, uploaded through the deployed `POST /desktop/logs` and
  the lines were read back from the AMS Loki as `job="pixelview-desktop"`. Startup lines (video and
  audio settings, DeckLink API and capture, control socket) arrived as `app` and three WHIP sends
  (VideoToolbox settings block, Opus encoder, `[obs-webrtc]` states and connect time, streaming
  start/stop, output frame totals) as `send`, each stamped with the desktop ID, node, version and
  build, macOS version, `cpu` and `hardware_model`; no scene, source or module lines, and the
  audio-buffering source name removed. The spool was cleared after upload. The run also showed
  that multi-line messages (the encoder, video and audio settings blocks, so bitrate, keyframe
  interval and profile) arrived as their first line only: the capture ran after the log-file
  writer, which cuts the message at every newline in place. The capture now runs first; a rebuilt
  development build then delivered the video and audio settings blocks and the VideoToolbox
  settings block of a WHIP send (bitrate, keyframe interval 2 s, profile, all sixteen lines) to
  the AMS Loki whole.
- Clean shutdown via SIGTERM on the signed bundle: log ends with `Shutting down`, zero leaks, sentinel
  removed, no crash report. With the stream-failure box still open after a WHIP 404 against the
  local backend (pentest mode, no engine), SIGTERM logged the dialog wait, then shut down with zero
  leaks, no sentinel and no crash report (previously SIGABRT in `~OBSBasic`); that run did not log
  "Cannot hide", so the native-loop stop path is covered only by the offscreen harness.
  Custom-scheme deep links delivered cold and warm to an isolated app copy.

### Not verified

- The receive canvas following the DeckLink output mode's frame rate, and the on-screen Start
  failure warning, on real DeckLink hardware (offline harness and compile only).
- Physical SDI picture inspection of the receiver's DeckLink output (cadence, colour, long-run A/V
  sync); the stall that used to trip the old 500 ms watchdog was not reproduced on demand.
- HDR receive end to end: an OBS PQ/HLG sender through the engine, the Desktop HDR canvas and the
  DeckLink HDR picture (EOTF, metadata, levels) on a scope or HDR monitor; HEVC Main10 passthrough
  and HLG live; whether the UltraStudio Monitor 3G reports HDR metadata support; the HLG R10L
  shader on hardware; the source of the lagged frames seen with the HDR canvas.
- Media start from the app to the local engine after the control-socket rewrite (no capture input
  attached); the WHIP grant was exercised only by the smoke tool.
- A real Cloudflare/backend drop during an active WHIP stream on a production origin; only a local
  backend reload was exercised.
- Admin remote control: a remote start that reaches an engine and streams, remote stop of a live
  stream, and the relay and state push across several backend workers (FakeRedis test only).
- Log upload: receive lines with a session verified by the viewer token, an unpaired receiver
  uploading with its viewer token alone, the menu switch on the running app, the failure lines of a
  stream that does not start, spooled lines uploaded by a later launch, and the backend budgets
  (429) were exercised offline only. The Desktop Logs dashboard and alert rules have not been
  checked against these live lines.
- Production notarization, Gatekeeper, R2 publication, appcast and the Sparkle update cycle;
  the interactive Pair forms on the rebuilt bundle (offscreen tests only);
  clicking the red close button (SIGTERM shares the path).
- Long hardware soak, Internet loss/recovery, glass-to-glass latency, 4K/interlaced/HDR inputs, hot
  unplug, multiple capture devices, NVENC/QSV/AMF/VAAPI hardware, Windows/Linux runtime.
- Windows: no compile, launch, pairing, streaming, DeckLink, Credential Manager, link hand-off,
  installer, Authenticode or WinSparkle run yet. Offline only: the portable WebSocket/control
  socket/receiver transport (`test_websocket_qt.py`, run on macOS against the same Qt code), log
  redaction and encoder order, and the build/release tooling logic (`test_windows_release.py`).
- A live HEVC 4:2:2 10 sender against the refusal path (offline suites only). The operator's Main10
  glitch report has not been reproduced or root-caused; the stock-path simplification and the
  100 ms buffer are mitigations.

## Known limitations and intentionally out of scope

- Admin remote control covers the Sending controls only: no remote receiving, pairing, Advanced
  encoder options, local monitoring or DeckLink output settings.
- No remote playout, engine provisioning, billing or engine-enforced fencing; a stream only reaches
  viewers when a project (engine) is running in admin and the subscription is active. Admin's `streaming` flag is the client's own
  report, not independent media observation.
- SRT fallback is intentionally unavailable; nodes without a WHIP config report an error.
- Credential and transport implementations exist for macOS only (Keychain, NSURLSession); Windows and
  Linux have source-level branding only and no pairing, receiving or updater.
- Receiving accepts limited-range BT.709 SDR, or limited-range BT.2020 PQ/HLG when the operator
  ticks HDR and picks the matching transfer; full range, other colour, eight-bit HDR, HEVC profiles
  beyond Main/Main10/Main 4:2:2 10 and VP9 profiles 1/3 are refused. HDR is not detected
  automatically, and the HDR metadata on the DeckLink output is derived from the nits setting, not
  from the sender's own mastering metadata. Fullscreen and preview are 8-bit.
- Sample-exact conversion needs 1920x1080 with the picture filling the canvas 1:1 and a 1080-line
  output mode; scaling resamples. Main10 and Main streams are 4:2:0 (and Main 8-bit) by definition,
  so on the receiver the DeckLink output's 4:2:2 chroma is interpolated vertically from them. HDR is not bit-exact for very saturated colours (float canvas precision).
  The lossy HEVC encode in between is of course not bit-exact either.
- The sender enforces limited range only for Main 4:2:2 10 (P216); Main/Main10 honour a saved Full
  setting.
- Capability probing caches a reduced decoder mask for the process if the pinned applemedia decoder
  sends EOS before its last probe frame (about one probe in twelve in a 2026-10-01 repetition, a
  different profile each time); an isolated decoder patch was evaluated and rejected. A run that
  loses the Main 4:2:2 10 bit receives a 4:2:2 sender as the engine's VP9 transcode until restart.
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
  `OBSBasic_PixelviewEncoding.inc`, `OBSBasic_PixelviewAudio.inc`, `OBSBasic_PixelviewDeepLinks.inc`,
  `OBSBasic_PixelviewLogs.inc` (log upload transport and switch).
  Capture shell, FPS, mode persistence and the close gate are in `OBSBasic.cpp`.
- `frontend/utility/Pixelview*.{hpp,cpp,mm}` - `PixelviewDesktop.hpp` (control policy),
  `PixelviewDesktopConnection.hpp` / `PixelviewDesktopMac.mm` (exchange, socket, Keychain),
  `PixelviewControlPing.hpp`, `PixelviewSocketWatchdog.hpp`, `PixelviewBackend.hpp` (origin
  defaults), `PixelviewReceiver*` (viewer flow), `PixelviewReceiveCredentialStore*`,
  `Pixelview*Keychain*.hpp`, `PixelviewDeepLink*`, `PixelviewEncoding.hpp`, `PixelviewFPS.hpp`,
  `PixelviewCapturePolicy.hpp`, `PixelviewConfig.hpp`, `PixelviewAudio.hpp`, `PixelviewSparkle.*`,
  `PixelviewLogShipper.hpp` (log capture and upload policy).
- `plugins/pixelview-whep` - WHEP source, capability probe, profile offer, video-format policy,
  `codec-route.c` (codec/profile route selector), `scripts/` (runtime staging, rswebrtc build,
  SBOM), `patches/`, `tests/`.
- `plugins/decklink` - `decklink-output-receive.inc` (receive bind, health, `receive_status`);
  `plugins/decklink-output-ui/decklink-receive-ui.inc`
  (watchdog, resume budget, AutoStart, Start refusal logging).
- `plugins/mac-videotoolbox` - SDK compatibility header, spatial-AQ handling and the 10-bit rounding
  of 4:2:2 canvas frames for the sender (`tests/run-422-rounding.py`, hardware encoder plus ffmpeg).
- `test/pixelview` - Python drivers and the C++/Objective-C++ harness sources they compile.
- `release/` - `pixelview-macos.sh` (1Password-wrapped prepare/publish), `macos.json` (team, Sparkle
  public key), `source-inventory.json`; `version.json` at the root is the product-version source.
- `cmake/macos` - `pixelview-build.sh` / `pixelview-signed-development.py` (canonical Developer ID
  local build), `pixelview-launch.py`, `pixelview-release.sh`, `pixelview_release_validate.py`,
  `pixelview_sources.py`, `xcode.cmake`, `helpers.cmake`.
