# Blackmagic audio and compact controls

## Delivered behavior (macOS build 27)

Audio controls sit directly below the native video preview, left aligned:

- The existing OBS `VolumeMeter` displays Blackmagic source audio. This is source-level telemetry, not proof of remote reception or physical audibility.
- **Mute audio** is a master mute: native source output mute is enabled and native local monitoring is turned off. The meter is hidden behind an inactive **Audio muted** indicator, without manufacturing zero meter samples.
- **Listen locally** is unchecked and disabled while master-muted. Unmuting restores stream audio but leaves local monitoring off until explicitly enabled again. Turning local listening off does not mute the stream.
- Local output selection uses native OBS monitoring devices, including **System default**. It is profile-wide. Device availability/acceptance alone does not establish audible playback.
- Source volume, audio track routing, capture properties and native Opus encoding defaults are preserved.

Settings are locked during native preparation, connection, streaming and stopping, including DeckLink properties, device selection, Fit/framing, encoder/FPS/bitrate/profile/Advanced and the local output-device selector. Operational master mute and local listening remain usable during streaming, with listening subject to master mute. Stats remains available. Start/Stop is the original OBS control, bottom-anchored in the sidebar; native Preparing may temporarily disable it.

The app play mark was reduced by 15% in each dimension (longest dimension 820 to 697 pixels on the 1024-square dark icon). The wordmark and tray-state artwork were not changed.

## State and cleanup

Both native audio flags are saved and read back by exact source UUID. Transactions restore both prior flags on ordinary persistence failure. Legacy/external muted-plus-monitoring states are normalized to monitoring off; failed normalization persistence remains fail-closed at runtime and retries without repeated warnings. The canonical native monitoring boolean is saved alongside compatibility fields. The native API is 32.2.2; version-33 migration tests are explicit fixtures, not a claim that untouched 32.2.2 scenes previously lost monitoring.

The self-deleting native meter is observed with `QPointer`, and audio callbacks/widgets are cleared before scene teardown processes deferred deletion. The build-24 shutdown crash was reproduced and corrected; later real quit/restarts completed cleanly with zero reported memory leaks.

## Real verification

- Full regression suite: 63 tests; additional real-libobs source serialization harness: 213 checks.
- Full macOS application build and deep strict code-signature verification.
- Real incoming Blackmagic audio encoded through a loopback-only SRT receiver as HD HEVC plus 48 kHz stereo Opus. No external server was used.
- Build 27, unmuted with listening on: 19.48 seconds decoded, RMS approximately -10.66 dBFS; CoreAudio reported this process producing output on MacBook Pro Speakers.
- Build 27, master muted: 18.34 seconds decoded, every sample zero; native source flags were muted=true / monitoring=false and CoreAudio reported output stopped.
- Master mute was also toggled while streaming. The final five seconds of decoded audio were all zero and local output stopped.
- Inactive meter presentation was inspected in the native app. Unmute did not restart local listening.
- Native output-device selection and restart persistence were exercised with named speakers and System default. Master-muted restart preserved mute with local playback off, without a crash/recovery prompt.
- Configuration locks, bottom transport and under-preview controls were exercised at a 900-pixel-wide window. Native Stop and connection-failure recovery restored configuration controls. Native Qt and compiled guard tests additionally cover pending preparation and layout constraints.
- Source capture settings, source transforms, volume, mixers and encoder JSON were compared with the pre-audio baseline.

Local-output activity was measured through read-only CoreAudio process properties, not a microphone recording or a physical-speaker audition. Windows/Linux hardware, physical audio-device hotplug, live OS-default switching, and a real Pixelview server were not exercised.

Temporary SRT service configuration was removed after clean shutdown, and the receiver was stopped. The delivered application is idle, master-unmuted, local listening off, using System default. No commits or publishing were performed.
