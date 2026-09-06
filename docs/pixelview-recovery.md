# Recovery UI and streaming reconnect verification

## Startup and separator

The Pairing / Connection block now ends with a native `QFrame::HLine`, matching the native separator below the capture controls. The automatic unclean-shutdown dialog identifies Pixelview Desktop in its message and has one **Continue** button. Continue (including Escape/window dismissal) uses normal mode. This startup prompt does not offer upstream safe mode or crash-upload choices. Explicit developer CLI options and `CrashHandler` marker/evidence handling are unchanged.

`test/pixelview/test_recovery_ui.py` compiles and executes the real extracted Qt dialog function, checks the branded message, one default Continue button, absence of upload checkbox, and normal-mode/no-upload return both with and without a crash log. Separator placement is a source integration contract, not a rendered screenshot assertion. macOS QMessageBox intentionally ignores window titles; branding is therefore verified in visible message text as well as the source title declaration.

TDD evidence: `/tmp/pixelview-ui-red.log` (missing separator), `/tmp/pixelview-ui-green.log`; `/tmp/pixelview-crash-red.log` (old dialog), `/tmp/pixelview-crash-green.log` (two tests pass). The offscreen Qt platform prints nonfatal font-alias/size-hint notices. GUI startup with a synthetic marker and rendered separator inspection are parent-owned acceptance.

## Streaming reconnect

Session-only stream intent is retained after transient control disconnect, authentication/acknowledgement timeout, or native media disconnected/connect-failed completion. Recovery uses the native profile's `Output/Reconnect`, `RetryDelay`, and `MaxRetries` values, with the configured fixed delay and a consecutive-attempt counter reset only by actual media start. It drains the old output/setup, authenticates a new control connection, requests a fresh lease/config, and only then resumes native WHIP. Raw libobs WHIP retry remains disabled because it would restart with the cached service, bypassing the authority gate.

The WHIP producer distinguishes retryable peer/ICE failure, selected cURL transport failures (resolve/connect/timeout/send/receive/empty transport), and HTTP 502/503/504 from terminal authentication, TLS, malformed response/configuration, SDP, encoder, and resource-origin failures. The latter never enter the retry controller. `test_whip_retry.py` compiles extracted producer branches and feeds their native result into the production controller dispatcher.

Stop/ForceStop cancels even between attempts or during lease/setup preparation. Unpair, revocation, identity mismatch, protocol/config errors, busy/lifecycle denials, and shutdown also cancel intent. Setup continuations have an attempt generation and one-shot claim; stop cleanup has a separate generation and waits for native completion plus the setup future. Reconnecting is displayed through the existing native statusbar and control, with counter/countdown and an actionable Stop. Authentication alone and native stream-delay preparation are not reported as successful media recovery. Ordinary relaunch may reconnect pairing/control but never invents streaming intent.

`test/pixelview/test_desktop_retry.py` compiles real production policy with fake monotonic time and extracts real frontend methods/continuations against offline boundaries. It exercises fresh lease gates, bounded retries, Stop/Unpair/terminal cancellation, late denial/revocation, pending setup, duplicate/stale cleanup and setup, and synchronous lifecycle changes before native start. The boundary harness is not a real output-thread timing or GUI/network test. TDD transcript: `/tmp/pixelview-reconnect-tdd.log`. Full-suite evidence is recorded below after native packaging.

The user-requested session-only resume policy supersedes older no-auto-resume wording in protected `AGENTS.md`; that file was intentionally not edited. Paired credentials and running backend/engine state were not changed. The authenticated WHIP same-origin security guard is unchanged.

## Native package and review evidence

Build **38** completed with `BUILD SUCCEEDED` using `bash cmake/macos/pixelview-build.sh`. Bundle: `build_macos/frontend/RelWithDebInfo/Pixelview.app`, arm64, identifier `com.pixelview.desktop`. Its development ad-hoc signature passed `codesign --verify --deep --strict`; this is not a notarized customer release. The harmless executable `--version` returned `OBS Studio - 32.1.0` (upstream version output is unchanged).

Final full-suite command: `uv run --with pillow python -m unittest discover -s test/pixelview -p 'test_*.py' -v` — **79 tests passed, no skips**, exit 0. Log: `/tmp/pixelview-recovery-all-tests.log`. This unrestricted verification includes the existing loopback/isolated-Keychain fixtures and offscreen Qt dialog tests; it does not access the retained paired credential or substitute for backend/media acceptance. The earlier fix-worker report used restricted skips, not this final run.

Build log: `/tmp/pixelview-recovery-build.log`; read-back signing/bundle/version checks: `/tmp/pixelview-recovery-build-verification.json`. Initial independent review `/tmp/pixelview-recovery-review.json` caught transient-producer classification and cancellation presentation issues; their test-first fixes are in `/tmp/pixelview-recovery-fix-tdd.log`. The subsequent review `/tmp/pixelview-recovery-review-final.json` caught accepted shutdown during an existing recovery drain. `/tmp/pixelview-recovery-shutdown-tdd.log` records its real close-gate RED/GREEN regression, including blocked setup and late callbacks. Shutdown now latches immediately, stops timers and cancels intent without resetting native stop completion again; final teardown can still finish after drain.

Final independent static review: `/tmp/pixelview-recovery-review-approved.json` — **passed**, no security concerns or logic errors. The nonblocking native producer-to-Qt callback-ordering and real media/GUI follow-ups remain explicitly outside the offline harness claim. Its documentation-evidence suggestion is addressed by the package/full-suite evidence above.

Native GUI, real media interruption/resume, Stop during a backend outage, and the synthetic crash-marker/rendered separator checks remain parent-owned acceptance. No such GUI/network recovery success is inferred from these compiled boundary tests.
