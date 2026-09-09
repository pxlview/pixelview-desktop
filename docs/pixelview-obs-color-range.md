# OBS and Pixelview: limited versus full range

## Customer-facing explanation

With the default limited output setting, OBS can stream correctly from either full- or limited-range capture, provided the DeckLink source range matches the actual incoming video signal. The source setting describes the input; the global OBS output setting describes the encoded output. Limited-range video can display proper black and white; it is not inherently washed out or restricted to SDR. Incorrect interpretation, clipping, or double conversion causes problems.

Main and Main10 normally stream limited range, but saved Full settings can change that. The tested Apple VideoToolbox Main42210/P216 path only accepts limited range. Pixelview receiving is intentionally limited-only for now.

This is a documentation snapshot of the investigation at commit `39bf2c5e08a09ec0f1bbbb7babbfeba46161d50c`. Product policy is distinguished below from implementation at that checkpoint. Local raw evidence and harness paths are provenance references, not files included in this repository; the findings and measured matrix are preserved here independently.

## Product decision and full signal flow

**Pixelview Desktop receiving is limited-range only for now. Ten-bit 4:2:2 fidelity is required at DeckLink output, NOT at the Mac preview/fullscreen.** Validate incoming range; reject full-range or unsupported/ambiguous metadata with a clear error rather than silently relabeling samples. This is the intended contract, not proof that all publishers already enforce it.

| Stage / setting | Meaning | Intended Pixelview behavior |
|---|---|---|
| Editing application's video output | Actual signal delivered to the capture card; may differ from timeline/project range | Full or limited is acceptable if correctly identified |
| OBS DeckLink source range | Interpretation of captured samples and input conversion matrix | Match actual incoming signal; keep this choice available |
| OBS global output range | Range used by output RGB-to-YUV conversion and encoder input | Limited for the Pixelview sender contract |
| Default OBS NV12 / Rec.709 / Partial | Eight-bit420 format, color standard, and limited range are three separate settings | Normal fresh/default Main stream is limited |
| HEVC Main / NV12 | Eight-bit420 encoding | Defaults limited; Full is technically supported if global range changed |
| HEVC Main10 / P010 | Ten-bit420 encoding | Defaults limited; Full is technically supported if global range changed |
| HEVC Main42210 / P216 with Apple VT | Ten-bit422 encoding | Limited supported; Full explicitly rejected |
| WHIP ingest / engine passthrough | HEVC SPS determines actual depth/chroma; passthrough is not a range conversion | Preserve encoded stream; no new WHIP parser required |
| Safari / Chrome playback | Decode signaled range and convert for display | Limited is appropriate; no manual player range expansion |
| Pixelview Desktop receive | Validate actual incoming range and color metadata | Limited-only; initially BT709 SDR for native422 output |
| DeckLink playout | Native ten-bit422 YUV/v210, separate from RGB preview | Preserve limited-range decoded codes without extra range conversion |
| Mac preview / fullscreen | Operator monitoring only | May remain eight-bit or be disabled without degrading DeckLink |

Correctly interpreted full-range capture can be converted to limited-range encoded output through OBS. Limited does not mean grey blacks, reduced gamut, or necessarily SDR. A range mismatch can crush/clip or wash out levels; converting full to limited is not mathematically code-identical. HDR transfer/primaries/metadata are separate and outside this initial SDR422 implementation.

**Current implementation caveat:** the committed Pixelview sender defaults to Partial but still preserves saved Full settings for Main/Main10. Do not claim limited is enforced there yet. Main42210/P216 already rejects Full. Keep receiver validation even when publishers are expected to be OBS/Pixelview.

Implementation handoff: [Native422 implementation handoff](pixelview-422-implementation-handoff.md).

## Outcome
The always-limited hypothesis is false. The existing compiled Apple hardware HEVC encoder emits full-range SPS VUI for full-range NV12 and P010, both with CPU frames and with a real libobs OpenGL canvas. P216 full range is rejected, not silently normalized.

Host: Apple M1 Pro, macOS 26.6.2 (25G83). Repository `/Users/max/src/pixelview-desktop`, branch `pixelview/minimal-capture`, HEAD `39bf2c5e08a09ec0f1bbbb7babbfeba46161d50c`; clean before and after. Loaded current `build_macos` mac-videotoolbox plugin and `Pixelview Desktop.app` embedded libobs/OpenGL read-only. Runtime libobs API 537001986 equals header API 537001986. Binary SHA256 provenance is in `binary-hashes.json`; no production rebuild was performed, so exact source-to-binary reproducibility is not asserted.

## Measured range matrix
Each row was tested at 1920x1080, 30fps, BT709, CBR6000kbps, one-second keyframes, no B-frames, spatial AQ Auto (resolves off). Actual input CV fourcc comes from a test-only interposition at VTCompressionSessionEncodeFrame, not a predicted settings mapping. Native logs report hardware sessions.

| Requested profile / OBS format | OBS range | Actual CV fourcc | SPS video_full_range_flag | ffprobe range / pixel format | CPU packets=decoded frames | GPU packets=decoded frames |
|---|---|---|---|---|---|---|
| Main / NV12 | PARTIAL | 420v | 0 | tv / yuv420p | 119 | 149 |
| Main / NV12 | FULL | 420f | 1 | pc / yuvj420p | 119 | 148 |
| Main10 / P010 | PARTIAL | x420 | 0 | tv / yuv420p10le | 119 | 148 |
| Main10 / P010 | FULL | xf20 | 1 | pc / yuv420p10le | 119 | 148 |
| Main42210 / P216 | PARTIAL | sv22 | 0 | tv / yuv422p10le (profile Rext) | 118 | 146 |
| Main42210 / P216 | FULL | no frame submitted | no bitstream | rejected | 0 | 0 |

Both rejected cases return: `Full range color is not supported by 16-bit Apple VT encoders. Select limited range color in Settings -> Advanced.` Successful cases have no encoder error. '16-bit' refers to the input-format path, not an assertion that Main10 P010 full range is unsupported.

GPU case: real obs_reset_video(gpu_conversion=true), synchronous animated custom source in private scene on main canvas, encoder attached to obs_get_video(); no CPU injection. It uses real conversion/readback and VT encode, not a claim of zero-copy. P216 retains preferred-NV12 hint with scaling explicitly disabled, yet emits 422 ten-bit. Inspector independently decodes two GPU frames per successful case and verifies spatial variation and time-varying content. CPU input uses video_output_open and synthetic planes; full/partial cases deliberately reuse the same code pattern, so this is signaling/input-contract evidence, not a range-expansion fidelity measurement. Bounded stop may leave asynchronous frames unflushed.

## Source audit (paths relative to repository)
- `plugins/decklink/decklink-device-instance.cpp:383-387`: capture setting populates currentFrame.range and range-specific input color matrix/min/max; line249 submits obs_source_output_video2. This controls interpretation of incoming capture, not the global encoder output range. No DeckLink plugin or hardware was loaded in this audit.
- `frontend/widgets/OBSBasic.cpp:892`: default ColorRange is Partial; default is not an enforced override.
- `frontend/settings/OBSBasicSettings.cpp:1024-1028,2598,2659,3316`: upstream settings expose, load, and save Partial/Full. This file has no diff from local upstream base 6b3e55072.
- `frontend/widgets/OBSBasic.cpp:2187-2204`: reads ColorRange and maps Full to VIDEO_RANGE_FULL; otherwise PARTIAL, independently of capture range.
- `libobs/obs.c:590-602`: canvas output conversion matrix derives from output format/colorspace/range. `libobs/obs-video.c:360-363` uses that matrix for conversion.
- `plugins/mac-videotoolbox/encoder.c:668-707,712-735`: reads attached video_output_info range, maps NV12/P010 to video/full CV formats, explicitly rejects full P216. Lines472-480 supply pixel format to CV attributes; line1145 copies rows, not an unconditional full-to-limited conversion. No get_video_info override. Diff against 6b3e55072 only changes spatial-AQ compatibility, not range logic.
- `frontend/utility/PixelviewEncoding.hpp:112-121` and `frontend/widgets/OBSBasic_PixelviewEncoding.inc:321`: fork rejects Main42210+Full and maps profiles to NV12/P010/P216; does not force all profiles to Partial. `docs/pixelview-encoding.md:44` explicitly says existing colorspace/range preserved. Receiver-specific limited policy in OBSBasic_PixelviewReceive.inc is not a permanent publisher policy.

Therefore correct full/limited capture interpretation can normalize through an OBS RGB canvas into limited output **when output range is configured limited**. Stock OBS can instead configure full; current fork preserves that possibility for Main/Main10. A receiver should honor decoded bitstream range, or require and validate an explicit limited-only publisher contract. Neither publisher identity nor DeckLink source setting proves that contract. These runtime findings apply to the tested Apple VideoToolbox path, not every encoder/platform or every future OBS release. Physical DeckLink conversion and precise code fidelity remain untested.

## Reproduce
```sh
cd /Users/max/src
bash /Users/max/src/obs-vt-range-audit/run.sh > /Users/max/src/obs-vt-range-audit/build.log 2>&1
python3 /Users/max/src/obs-vt-range-audit/inspect.py > /Users/max/src/obs-vt-range-audit/inspect.log
```
Both executed successfully. run.sh has complete native compiler/link/environment commands and uses a private HOME/CFFIXED_USER_HOME. Existing gpu.mm/cpu.cpp are self-contained; prepare.py records original adaptation provenance from read-only `/private/tmp/pixelview-{gpu,encode}-smoke` but is not needed to rerun. The inherited CPU probe also emits two bonus Apple H264 range cases; those are not part of the HEVC assertions.

Per stream (example):
```sh
/opt/homebrew/bin/ffprobe -v error -count_frames -show_streams -of json /Users/max/src/obs-vt-range-audit/gpu/gpu-main-full.hevc
/opt/homebrew/bin/ffmpeg -v verbose -i /Users/max/src/obs-vt-range-audit/gpu/gpu-main-full.hevc -c:v copy -bsf:v trace_headers -f null -
```
`commands.json` records every exact ffprobe/ffmpeg argv. `summary.json` verifies all twelve HEVC cases and retains byte/packet/frame counts, actual CV formats, errors and VUI flags. Each cpu/gpu directory retains runtime.log, actual HEVC packets in Annex-B files, packet CSVs, full ffprobe JSON and full trace_headers logs. New artifacts only under this directory. No capture/output hardware, GUI/user applications, user configuration, authentication, network media, app build/staging/signing or commits.
