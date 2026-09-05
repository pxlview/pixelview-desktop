# Pixelview capture and encoding prototype — local verification

## Native streaming, statistics and licensing (build 23)

- The sidebar now hosts the actual native OBS Start/Stop Streaming button, including its preparing/connecting/stopping states and existing validation/error dialogs. No stream starts automatically.
- The existing OBS bottom status bar shows CPU/FPS and, while streaming, time, bitrate, network-dropped frames and connection indicators. Show stats opens the original detailed Stats window (render misses, encoding skips, output frame drops, memory and disk). Frame-drop metrics are not packet-level loss telemetry.
- FPS and encoding changes are blocked while native stream setup is pending as well as while output/video is active.
- macOS Help → License information opens a dedicated offline viewer with OBS attribution and full unchanged COPYING. It does not instantiate the network-fetching upstream About dialog. The equivalent action remains in the Pixelview Desktop menu on other platforms.
- Build 23 was compiled, deeply/strictly signature-verified and exercised on this Mac. All 45 tests and independent focused review passed. A real GUI-started loopback-only SRT stream carried 1920×1080 HEVC Main (no B-frames) and 48 kHz stereo Opus; Stop Streaming ended the receiver and restored editable controls. A closed loopback receiver produced the native Failed to connect to server popup. Missing configuration warning, detailed Stats reuse/reopen and offline license reopen were also exercised.
- The temporary SRT service was removed and the original no-destination state restored. Capture sources/settings/transforms and stock OBS configuration hashes were preserved. Native save added scene resolution metadata 1920×1080; no source refit occurred.
- Authentication/backend integration, external-server publishing, network impairment/reconnect/delay scenarios and Windows/Linux runtime remain unverified. Release-wide third-party licensing/source-distribution review is still a release gate; see [distribution checklist](pixelview-distribution-license.md).


Scope: local capture preview and native encoding preferences in an OBS fork. Authentication, backend WebSocket, WHIP publishing and receiver/DeckLink playout are deliberately not implemented.

## Initial capture/FPS verification on this Mac

- Native arm64 build completed with `BUILD SUCCEEDED`; bundle passes `codesign --verify --deep --strict`. Initial capture/FPS bundle build number: 13; superseded by the encoding build below. Independent code reviews passed for device handling and the FPS addition.
- Pixelview launches directly into one editable preview and Blackmagic controls, without OBS scenes/mixer/streaming docks or the setup wizard.
- Bundle ID `com.pixelview.desktop`; settings live under `~/Library/Application Support/pixelview/obs-studio/`. The stock OBS installation is not replaced.
- The actual connected **UltraStudio Recorder 3G** appears in the native DeckLink device list. Selecting it creates one managed `Pixelview Capture` source and displays the input picture.
- A separately compiled read-only DeckLink SDK probe reports signal lock and `bmdModeHD1080p24` while Pixelview captures.
- Native Device settings exposes video/audio connection, mode, colorspace/range, channel layout, buffering and other applicable DeckLink properties. Some controls require scrolling, as in upstream OBS.
- Changed video connection from SDI to HDMI through the UI, saved it, reselected the same device, reopened settings and confirmed HDMI was retained. Restored SDI and verified the picture returned.
- Runtime logs and saved profile confirm **1920 × 1080 base and output**, with OpenGL rendering. Startup now saves the HD dimensions rather than leaving stale upstream output defaults on disk.
- Exercised every toolbar FPS option: **23.976, 24, 25, 29.97, 30, 50, 59.94, 60**. For each, the native video-reset log and saved profile matched the exact rational rate; both resolutions remained HD. This controls OBS composition/output timing, not the device input mode.
- Forced a real profile-save failure by temporarily creating a directory at the otherwise absent `basic.ini.tmp` path. Changing 60 to 59.94 reset video, failed to save, restored runtime/profile/selector to 60, and displayed the native recovery warning. Removed the QA obstruction, dismissed the warning and successfully selected 23.976.
- Clean quit/relaunch retained **23.976 (24000/1001)** in the selector, native runtime and profile. Capture source identity, device settings and scene transforms matched the pre-FPS baseline; stock OBS configuration hashes were unchanged. Pixelview is left open at this rate to match the supplied “24 NTSC” screenshot.
- Resized the source using the preview corner handle, then dragged its position. After Refresh and a clean quit/relaunch, the source retained position `(173, 81)` and bounds `(1621, 911)`; the preview confirmed the restored layout.
- **Fit** restored position `(0, 0)`, bounds `(1920, 1080)`, proportional inner-fit mode and zero crop.
- Missing saved-device test: temporarily substituted a nonexistent device identifier in the isolated scene file. The app launched without crashing, showed a disconnected-device message, and did not silently select the available device. This is a saved-device fixture test, **not a physical unplug/replug test**.
- Non-HD geometry test: temporarily used an explicitly synthetic **640 × 480 SMPTE-bars image fixture** under the same managed scene item. Native proportional HD bounds rendered it centered with pillarboxing. This verifies geometry, **not non-HD capture from the hardware**.
- Restored the real DeckLink scene after fixture tests. Pixelview is left on the actual UltraStudio input.
- Ten Python configuration/source-integration tests and the C++ capture-policy test executable pass. FPS tests compile the production rate table and transaction helper, covering active-output rejection, reset/save failures and failed rollback. A compiled regression executes the toolbar option-validation block with simulated native lists to verify multichannel-to-stereo fallback without muting explicitly supported audio choices. Source-contract tests are not end-to-end UI tests; the above checks were performed separately against the compiled application.

## Encoding verification — build 17

- Final native arm64 build succeeded (`/tmp/pixelview-encoding-build-final.log`); the complete bundle passed deep/strict code-signature verification and was launched. The final independent review `/tmp/pixelview-encoding-review-approved.json` passed with no security or logic findings. No commit or push.
- Settings occupy the left column; the native editable preview remains on the right. The actual encoder menu on this Mac contains x264, Apple VT H264 Hardware Encoder and Apple VT HEVC Hardware Encoder. Apple HEVC was selected automatically on first initialization.
- Native saved defaults were checked through all three encoder choices: 6000 kbps, CBR, one-second keyframes; Apple hardware B-frames off. H.264 uses baseline where offered; x264 also uses ultrafast/zerolatency. Opus is persisted as `AdvOut/AudioEncoder=ffmpeg_opus`, with OBS advanced audio defaults and no custom audio control.
- Main→NV12, Main10→P010 and Main 4:2:2 10→P216 were exercised through the real UI and checked against both saved configuration and actual video-reset logs. The profile popup was left open across refresh ticks before selection. FPS stayed 24000/1001 and base/output stayed 1920×1080.
- Quick bitrate 1, 12 and 6 Mbps wrote 1000, 12000 and 6000 kbps. Native Advanced edits to 15000 kbps and a two-second keyframe interval survived Refresh, a HEVC profile change and clean quit/relaunch. The out-of-quick-range value remained explicitly shown as actual kbps, not overwritten by the clamped quick display. Editing x264's native bitrate to 7000 then pressing Cancel left the saved 6000 unchanged.
- A real profile-save failure was induced with a temporary directory at the absent `basic.ini.tmp`. The attempted Main10 change rolled back the encoder JSON, global video format and selector to Main/NV12; the native warning said previous preferences were restored. The obstruction was removed. Partial JSON rename and failed-recovery branches are separately covered by fault-injected compiled production-save tests, not a claimed physical disk-full test.
- Seeded a pre-existing 640×360 rescale configuration before first startup. Initialization explicitly saved the actual `AdvOut/RescaleFilter=0` (OBS_SCALE_DISABLE), not just the obsolete boolean. The inactive resolution fixture was removed after testing.
- Native CPU-input libobs smoke produced real 1920×1080 Apple H.264 Baseline, x264 Constrained Baseline, and all three Apple HEVC profiles. ffprobe confirmed no B-frames and one-second keyframe spacing at the test's 30 fps. Opus produced valid 48-kHz stereo packets. See `/tmp/pixelview-encode-smoke-report.md` and its adjacent harness/artifacts.
- A separate real OpenGL renderer→libobs→Apple HEVC test rendered animated synthetic bars at 1920×1080, rather than injecting CPU video frames. Main emitted yuv420p, Main10 yuv420p10le, and Main42210 yuv422p10le (ffprobe profile Rext). All had no B-frames and one-second keyframes. P216 retained 4:2:2 10-bit despite the upstream preferred-NV12 hint when GPU scaling was disabled. Parent reran the bitstream/content inspector successfully. See `/tmp/pixelview-gpu-smoke-report.md`.
- Spatial AQ remained native Automatic. The actual Apple VT encoder resolves Automatic to disabled under CBR; this is upstream behavior, not a forced On setting.
- All **16 Python tests** passed, including compiled policy, refresh/default and extracted-production transaction harnesses, plus the separate compiled C++ capture policy test. Fault injection/mocks are distinguished from the separately performed GUI and real encoded-packet checks above.
- After final rebuild, profile switching was exercised again and the app was returned to **Apple hardware HEVC, Main, 6 Mbps, CBR, one-second keyframes, B-frames off**, at 23.976 fps. Capture settings/framing matched the pre-encoding baseline, and stock OBS configuration hashes were unchanged. The final app is left open.

## Remaining limits

- NVIDIA/Intel/AMD/VAAPI hardware was not available on this Mac; backend recognition is tested against actual upstream identifiers, not physical encoding on those GPUs. Unknown third-party hardware IDs are excluded rather than guessed. Switching encoder intentionally initializes its defaults; current-encoder Advanced overrides persist across refresh/profile/bitrate edits and restart, but are not cached per encoder.
- The local synthetic encoding tests are short. They do not validate sustained bandwidth, production muxing/end-of-stream flush, color accuracy/HDR, audio/video synchronization or remote publishing. P216 is a 16-bit-container input format; the verified HEVC result is 10-bit. Higher-bit-depth encoding does not restore detail already captured in 8-bit; DeckLink capture settings remain independent.

- Active-output rejection and a failed video-reset rollback are covered by automated policy tests, not injected into a live publishing session. Real save-failure recovery was exercised as described above.
- Physical hot unplug/replug and switching between multiple capture devices were not exercised; only one device is connected.
- Actual non-HD, 4K, interlaced and HDR hardware signals were not available for testing. Input pixel-aspect-ratio/interlace/color fidelity needs dedicated validation before customer deployment.
- The status line deliberately says “Device selected • Local preview,” not “Live.” It reports device presence; independent signal-loss/frozen-frame telemetry is not implemented. Hover for troubleshooting.
- This is a locally ad-hoc-signed prototype, not a notarized distribution build. It retains upstream OBS icon/resources and native properties-dialog styling.
- The compatibility build uses installed Xcode 15.3 / SDK 14.4, with Metal and AVFoundation camera capture excluded. Native VideoToolbox encoding is now enabled using a narrowly scoped SDK compatibility header. A current supported Xcode toolchain remains recommended for distribution. See [build notes](pixelview-build.md).

## Reproduce

```sh
bash cmake/macos/pixelview-build.sh
python3 -m unittest discover -s test/pixelview -v
c++ -std=c++17 test/pixelview/test_capture_policy.cpp -o /tmp/pixelview-policy
/tmp/pixelview-policy
open build_macos/frontend/RelWithDebInfo/Pixelview.app
```

Stock OBS was closed while idle (not streaming or recording) to release its capture device. No streaming credentials or stock source settings were changed.

## UI polish verification — build 19

- Authentic `pxlview/pv-home` wordmark from commit `8092872f34e5db3f043a9fc100bdecde7690a740`, `src/assets/media/logo-light.png`; original bytes matched and the UI derivative only removes transparent margins. Embedded logo plus Desktop visually verified.
- Compact grouped sidebar and native preview retained. Settings/Fit share a row; FPS/Mbps share a row; redundant headings, normal encoding diagnostics and Refresh button removed. Native device and encoder labels are left-aligned. The two-second timer remains; FPS selections still apply immediately rather than selecting the source rate automatically.
- Native minimum-width QA: 900px-wide window (628px outer height including macOS titlebar), no clipped controls, all eight controls measured 36px high, FPS/bitrate top edges match, single footer remains readable and left-aligned. Original 1252×976 outer window geometry restored after testing.
- Actual startup fixture `bframes=true, bf=3` became `bframes=false, bf=0` in canonical `streamEncoder.json`. Advanced has no Use B-frames control. Native CBR→ABR callback exposed Limit bitrate while keeping B-frame controls hidden; Cancel retained CBR. Final build's Advanced dialog was checked again.
- Real UI FPS edits saved and applied runtime 30/1 then 24000/1001 without Refresh; quick bitrate edits saved 7000 then 6000kbps. Final restart retained Apple HEVC Main, 6000kbps CBR, one-second keyframes, B-frames off, NV12, HD canvas/output, 24000/1001 FPS and native Opus defaults. After restoring window geometry and clean exit, the complete scene JSON matched the pre-task baseline, including capture settings/transforms.
- Final **27 unittest tests**, strict-warning standalone capture-policy executable, `git diff --check` and deep/strict signature verification passed. Full Qt/moc application build succeeded in `/tmp/pixelview-ui-polish-build-final.log`; bundle version 19. Independent final review passed in `/tmp/pixelview-ui-polish-review-approved.json`, with five source hashes checked against disk.
- Separate real libobs CPU encoding test used deliberately B-frame-enabled input settings, then the exact final production normalization. Apple HEVC Main emitted 119 decoded frames, Apple H.264 High 119, x264 High/veryfast/empty tune 95; every stream was HD, contained only I/P frames, and had PTS=DTS. Using High avoids baseline masking a failed B-frame policy. The final source snapshots and raw outputs passed `/tmp/pixelview-bframes-smoke/inspect_results.py`. Bounded stop does not imply all 120 submitted frames were drained.
- Evidence: `/tmp/pixelview-ui-polish-runtime-qa.json`, `/tmp/pixelview-bframes-smoke-report.md`, `/private/tmp/pixelview-polish-final-minimum.png`, `/private/tmp/pixelview-polish-final-advanced.png`. The app remains a local preview/preferences prototype, not a publishing implementation. No new physical hotplug, live-stream, NVENC/AMF hardware, or large-system-font certification is claimed.
