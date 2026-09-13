# Desktop heartbeat and reconnect

## Protocols are different

Authoritative Desktop contract: **PR207 checkout** `.desktop-integration/pixelview-backend-v4/docs/desktop-protocol.md` (not the older sibling main checkout). See `pixelview-control-recovery.md` for the working-tree resume fields and verification. Do not send the player/engine `PONG_RESPONSE` envelope on `/desktop/ws`.

| Path | Application heartbeat | Deadline / presence |
| --- | --- | --- |
| Desktop `/desktop/ws` | Client sends `{type:"heartbeat",streaming,settings?}` every **15 seconds**. Server returns `{type:"heartbeat",lease_expires_at}`; it does **not** initiate application PING. | Server presence/lease **45 seconds**, credential/ownership checks at most **5 seconds** while idle. Initial auth **10 seconds**. |
| Player `/wsocket`, engine WS | Backend `SOCKET_SEND_PING` immediately after connection task starts, then every **20 seconds**. Player replies `{message:"PONG_RESPONSE",data:{}}`; engine replies `{mutation:"PONG_RESPONSE",data:null}` (the Go map is nil). | PONG refreshes Redis records to **90 seconds**. This TTL is not a strict per-PING reply timeout. Viewer UI grace is separately **7 seconds**. |
| RFC6455 transport | WebSocket control PING/PONG, handled by NSURLSession/browser/Gorilla rather than Desktop JSON. | Installed backend Uvicorn 0.37.0 configuration defaults are **20-second ping interval / 20-second ping timeout**. `dev.sh` supplies no overrides; deployed proxy/WS implementation settings must be verified separately. |

Sources: backend `services/desktop_service.py:17,214-325`; `services/ws_manager.py:32,39,316-340,522-531,989-1000,1105-1124`; `routes/websocket/connections.py:158-162`; `routes/websocket/engine/handle_msg.py:107-108`; engine `internal/websocket/client.go:233-252`; player `src/stores/store.js:317-321`.

## Verified backend handler distinction

Both backend checkouts were read for the cadence correction:
- `.desktop-integration/pixelview-backend-v4/services/ws_manager.py:33,443–480`
  and `pixelview-backend-v4/services/ws_manager.py:32,316–340` send the JSON
  `SOCKET_SEND_PING` immediately, then sleep 20s. In both checkouts,
  `routes/websocket/connections.py:158–162` routes `PONG_RESPONSE` to
  `pong_received`; viewer presence refresh is 90s at `ws_manager.py:1147–1157`
  (integration) / `990–1000` (sibling). There is no 20s per-JSON-PONG timeout.
- Both installed Uvicorn 0.37.0 environments read back `ws_ping_interval=20.0`
  and `ws_ping_timeout=20.0`. Under their `.venv/lib/python3.11/site-packages/`,
  `uvicorn/protocols/websockets/websockets_impl.py:63` forwards these settings
  to the WebSocket protocol; `ws_handler` at line 229 only marks the handshake
  complete and waits for close. The integration environment's selected `auto`
  protocol is this `websockets_impl.WebSocketProtocol`.
  `websockets/legacy/protocol.py:1218` implements RFC keepalive: sleep interval,
  send control PING, await PONG with timeout, fail on timeout. It sleeps again
  after PONG; native probes remain scheduled from PING-send time, with one
  outstanding PING. Equal configured values do not imply identical phase.
- `dev.sh` and the Dockerfile use Uvicorn without ping overrides. This verifies
  checked-out launch defaults, not the production process/proxy configuration.

Both native macOS transports now share **20s RFC PING / 20s PONG timeout** via
`PixelviewControlPing.hpp` / `PixelviewSocketWatchdog.hpp`, wired by
`PixelviewDesktopMac.mm:54` and `PixelviewReceiverMac.mm:69`. This does not change
15s sender lease heartbeats, the 30s sender ACK budget, 65s receiver application
silence watchdog, or the 7s receiver recovery grace **after detected loss**.

## Native deadlines and recovery

- `OBSBasic_PixelviewDesktop.inc` starts its 15-second heartbeat timer only after valid authenticated readiness; its watchdog runs every 100 ms. The initial connection/auth deadline is 10 seconds.
- `PixelviewDesktop.hpp` permits only one outstanding heartbeat. Initial readiness gives a 30-second local deadline; a heartbeat ACK moves it to **the monotonic send time + 30 seconds**, not receive time + 30 seconds. Healthy RFC6455 PONGs cannot extend that authority. Stop acknowledgement also has an independent 30-second bound.
- `PixelviewDesktopMac.mm` sets the NSURLSession request timeout to 10 seconds. It does **not** set the receiver's former 30-second resource timeout. Native configuration readback on this system gives resource timeout **604800 seconds**. The real transport stayed connected for over 65 seconds with 15-second application heartbeats, both before and after the cancellation fix. Do not transplant the receiver timeout fix here without a failing reproduction.
- A lost TCP connection cannot survive a server restart. The application opens a **new authenticated socket** using its process-held device credential. With updated PR207, `auth.resume_fence` can atomically transfer a matching live lease; resumed readiness rotates the fence and does not extend the local deadline. An immediate application heartbeat renews authority. Older readiness without boolean `resumed` keeps the old stop/drain/fresh-lease behavior.
- Idle control reconnect uses 1, 2, 4, 8, 16, then 30-second capped backoff, reset on authenticated ready. Streaming intent uses the native Output/Reconnect, RetryDelay and MaxRetries settings (policy defaults: 2 seconds, 20 retries), not this exponential idle backoff. Both wait for pending native output/setup to drain. Successful actual output resets the streaming retry count.
- Streaming intent exists only in memory. Manual Stop, shutdown, Unpair, revocation, identity mismatch and terminal protocol/lifecycle denial cancel it. Fresh application construction does not auto-start.
- An **abrupt backend crash with an outstanding lease** requires the updated backend's explicit atomic resume contract, not an immediate new `start`. Healthy sender control recovery retries after one second without draining media, but only until the original monotonic deadline and only after resume capability was negotiated. Failed resume/4409 remains terminal. Real backend-plus-media crash acceptance is not established by native loopback fixtures; never weaken ownership or turn terminal denial into a retry.

## Cancellation fix

A delayed `didOpenWithProtocol` callback could send authentication even after `closeSocket()` detached the owner. A compiled regression invoking the real late delegate failed on this behavior before the fix. Delegate lifecycle and receive-loop decisions now run on Cocoa main; detached attempts cannot authenticate or re-arm receive, and cancellation clears pending auth. Each socket owns a separate delegate; nulling its owner before cancellation is the attempt fence. An old delegate does not adopt the new socket's owner.

The historical cancellation fix addressed a demonstrated race, **not a proven explanation for the operator's intermittent disconnects**. The later control-recovery change additionally adds native PING/PONG detection, terminal HTTP/TLS classification and negotiated resume. Application heartbeat budgets, origin checks and secure-storage implementation remain unchanged.

## Pairing versus Keychain availability

The integrated frontend has no dedicated Keychain button. Pair recovers an inaccessible existing sender identity without a new admin code; it is hidden during ordinary disconnected states when the credential is held in process memory. Connect reuses `authorizedToken` across transport reconnects and retries unavailable background reads with prompt-free bounded backoff. Explicit Unpair and terminal 4401 clear the process cache; revocation disables reconnect while preserving local identity and the secure record for explicit removal. A network failure is not evidence that pairing expired: the device bearer is nonexpiring and revocable. See `pixelview-keychain-reliability.md` for the 18 focused offline UI/storage tests.

## Offline verification

Run from this checkout:

```sh
python3 test/pixelview/test_desktop_heartbeat.py -v
PYTHONPATH=test/pixelview python3 -m unittest test_desktop_retry test_media_failure_pairing test_whip_retry test_recovery_ui -v
```

The heartbeat suite compiles the actual native transport and Desktop policy, excludes the secure-storage implementation entirely, and uses synthetic loopback credentials only. It proves:

- 65-second healthy socket, four 15-second heartbeat ACKs, forced TCP/server disappearance, fresh authentication with unchanged synthetic identity, another heartbeat ACK, and terminal 4401;
- native control PONG frames without application ACK still cause the 30-second application watchdog to expire, retaining identity and no lease/streaming intent;
- late open after cancellation sends no authentication.

The loopback server is an explicit protocol fixture, **not** the production backend or DynamoDB. The reconnect orchestration in that harness is synthetic; the separate compiled policy test covers native retry/intent/lease decisions. No app, backend or engine was restarted, no stream started, and no real Keychain read or write was required.

Integration verification repaired the extracted frontend fixture's missing `exchanging` member and supplied a state-recording `config_set_bool`/`config_get_bool` boundary with the production API types. Obsolete automatic-Unpair expectations were replaced with stronger assertions: terminal revocation cancels intent, clears only the process cache, disables reconnect, preserves identity and never invokes removal. This exposed and fixed a missing cache invalidation on 4401. The final runs passed 18 focused UI/credential tests, 12 drain/media-failure/retry/recovery tests, and all 3 native heartbeat tests (121.212 seconds). Full app build/signed GUI, real DynamoDB crash recovery, durable live Keychain recovery and live media resume remain separate verification; no app/backend restart or user credential mutation was performed.
