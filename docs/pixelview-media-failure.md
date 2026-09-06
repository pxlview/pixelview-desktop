# Media failure versus Desktop connectivity

## Failure observed

A local WHIP start attempt returned HTTP 404. Desktop displayed the upstream OBS stream-key error, then `Native output stopped. Start manually.` and `Node 707880 · Offline`; Start became unavailable.

The native log records HTTP 404 at 17:53:33 and 17:56:37 on 2026-09-06. A subsequent read-only local probe found no listener on engine TCP 8080, and `GET http://127.0.0.1:8000/ingress/707880/health` returned HTTP 404 with `Engine not available`. The backend maps unavailable non-handoff engine requests to 404, so this status alone does not establish invalid ingest credentials.

## Root cause

The frontend sent native media failure through `Desktop::fail`, the control-plane failure path. That cleared authenticated readiness and invoked the halt callback, stopping heartbeats and closing the control socket after native cleanup. A terminal media failure did not schedule an idle reconnect. The durable pairing identity remained saved, but the control connection was dropped and Start required readiness that could not recover on its own.

Media setup/output failure must not invalidate durable pairing. Terminal media failure must stop and drain output, release its stream lease, and retain a healthy control connection. Start must remain unavailable until native cleanup and lease-stop acknowledgement complete. Revocation, control disconnection, malformed authority, and acknowledgement expiry must still invalidate streaming authority.

User-facing errors should describe the streaming destination or encoder, not internal “native output” machinery or manually configured stream keys.

## Verification scope

The full Pixelview regression suite passed 99 tests. The new compiled frontend/controller regression covers terminal WHIP rejection and synchronous setup failure, retained connection/heartbeat, native/setup drain, stale callbacks, one lease release, main/tray Start gating through stop acknowledgement, and a missing-stop-ack timeout despite healthy heartbeats. Real control failure/revocation recovery tests remain green. Independent final code review found no blocking security or logic issues.

A fresh native build completed successfully in `build_macos_media_failure/frontend/RelWithDebInfo/Pixelview.app`, build 49. Deep/strict ad-hoc signature verification passed, the bundle identifier is `com.pixelview.desktop`, and the harmless `--version` invocation returned `OBS Studio - 32.1.0`. Logs: `/tmp/pixelview-media-failure-clean-build.log` and `/tmp/pixelview-media-failure-tests.log`. The old development build cache referenced a removed SDK; the fresh directory avoided its stale header-search configuration.

A subsequent Developer ID build 50 completed in `build_macos_developer_id/frontend/RelWithDebInfo/Pixelview.app`, passed deep/strict signature verification, and was relaunched locally. Build log: `/tmp/pixelview-desktop-restart-build.log`. The local engine was separately started with `scripts/run-mac.sh --mode=server`; its health returned HTTP 200 and its backend WebSocket received configuration. These are not customer-release/notarization or end-to-end media-delivery assertions. Engine availability remains a separate runtime dependency from Desktop pairing.
