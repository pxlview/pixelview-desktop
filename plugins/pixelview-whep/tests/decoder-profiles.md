# Actual receiving decoder profile probe (M1 Pro)

## Outcome

Tested macOS 26.6.2 (25G83), Apple M1 Pro, with the curated official GStreamer
1.28.3 runtime copied from `.deps/pixelview-gstreamer`. This is a compressed-file
**receiving/decoding** test, not encoder capability detection or factory inspection.

Both `vtdec` and `vtdec_hw` gave the same support results:

| Compressed fixture, verified by ffprobe and parser | Unconstrained system-memory output | Explicit higher-depth output | Actual VT session hardware property |
| --- | --- | --- | --- |
| HEVC Main, 8-bit 4:2:0 | NV12, 8-bit 4:2:0 | P010_10LE; AYUV64 (conversion/expansion, not added source precision) | true |
| HEVC Main 10, 10-bit 4:2:0 | **NV12, losing 10-bit output** | P010_10LE, 10-bit 4:2:0; AYUV64 | true |
| HEVC Main 4:2:2 10 (ffprobe Rext), 10-bit 4:2:2 | **NV12, losing depth and chroma resolution** | P010_10LE (still 4:2:0); AYUV64 (16-bit-component 4:4:4 representation) | true |
| VP9 profile 0, 8-bit 4:2:0 | NV12 | P010_10LE (expansion only) | true |
| VP9 profile 1, 8-bit 4:2:2 and 4:4:4 | **Rejected: not-negotiated, zero decoded frames** | Also rejected | No session created |
| VP9 profile 2, 10-bit 4:2:0 | **NV12, losing 10-bit output** | P010_10LE, 10-bit 4:2:0 | true |
| VP9 profile 3, 10-bit 4:2:2 and 4:4:4 | **Rejected: not-negotiated, zero decoded frames** | Also rejected | No session created |

The full recorded matrix has **9 compressed fixtures and 46 runs**: 26 successful
decodes, 16 VP9 profile 1/3 negotiation failures, and 4 explicit native 4:2:2 raw
format link failures. Asking HEVC Main42210 for `I422_10LE` or `Y210` failed for
both decoders. There is no `videoconvert` in any tested graph.

Do not turn the Main42210 success into a claim of native 4:2:2 raw output.
AYUV64 is packed alpha/Y/U/V with 16-bit components and 4:4:4 sampling: VT must
convert the compressed 10-bit 4:2:2 representation. This test does **not** prove
that VT's internal conversion avoids intermediate chroma/depth loss. P010
explicitly subsamples 4:2:2 to 4:2:0. A grading-quality 4:2:2 path still needs
sample-level fidelity validation and downstream format integration.

P010 first-frame luma contained more than 256 distinct values for the 10-bit
fixtures (HEVC Main10: 805; Main42210: 801; VP9 profile2: 717 in the recorded run).
The 8-bit controls had 213 and 187. This establishes more-than-8-bit sample
variation, but is not a reference-decoder error/PSNR comparison. Low two bits
alone are **not** a precision test: VT also fills them when expanding 8-bit.

## Hardware evidence and color metadata

A test-only injected, ad-hoc signed dylib interposes the **actual**
`VTDecompressionSessionCreate` called by the unmodified applemedia plugin. It
immediately reads `kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder`
from that session with `VTSessionCopyProperty`. Every successful run recorded:

```text
HW_SESSION create=0 query=0 hardware=true
```

This is stronger than the factory's Hardware class, `VTIsHardwareDecodeSupported`,
`vtdec_hw`'s name, or a hardware-required encoder flag. The hook does not replace
session options, change the callback or supply synthetic decoder output.
`DYLD_INSERT_LIBRARIES` is restricted to this test subprocess, not the application.
An executable-only interpose did not observe dlopened plugin calls; the separate
injected dylib did. Call the original VT symbol directly from the interpose image;
`dlsym(RTLD_NEXT, ...)` recursed/crashed in the exploratory version. The runner
now rejects signal exits and requires observed hardware-query evidence on success.

HEVC input VUI explicitly carries limited-range BT.709 matrix, transfer and
primaries, verified with ffprobe; parser and raw appsink caps preserve `bt709`.
VP9 fixtures carry limited-range BT.709 colorspace, and parser/raw caps say
`bt709`; ffprobe does not report independent VP9 primaries/transfer values.
Do not equate those inferred GStreamer values with separately signaled metadata.
No PQ/HLG, mastering display, content-light metadata, HDR display, ICC transform,
full-range, OBS grading, WHEP/SDP, or DeckLink output is tested here.

All successful cases in the retained results produced all 12 frames. Some earlier
repetitions produced 11/12; the cause is not isolated. This is a short capability
probe, **not** a no-frame-loss, complete-drain, real-time throughput or endurance
certification. Actual per-run counts remain in the JSON. The malformed HEVC
negative control produced zero frames and a parser error; failures are not counted
as supported simply because an element exists.

VP9 profiles 1/3 fail at the GStreamer negotiation boundary before a VT session
is created. This establishes lack of support through these bundled elements,
not a universal claim that Apple's underlying API cannot decode those profiles.
VP9 12-bit variants were not tested.

## Reproduce

From the repository root, with the already extracted pinned SDK and existing
`ffmpeg`/`ffprobe` providing libx265 and libvpx-vp9:

```sh
python3 plugins/pixelview-whep/tests/run-decoder-profiles.py
```

Optional `--runtime PATH` and `--work PATH` choose the read-only source runtime
and the disposable test directory. The default work area is
`plugins/pixelview-whep/.test-build/decoder-profile`. The runner replaces **only**
its private `runtime/` copy, signs and strict-verifies its 53 code files, compiles
against official SDK headers and links to that single copied runtime. It resets
GST/DYLD variables, isolates the registry/plugin/scanner paths, and never loads
Homebrew GStreamer. Homebrew ffmpeg only generates synthetic public-tool
`testsrc2` fixtures; all measured VT decoding happens in the bundled runtime.

The probe follows the existing `run-upstream.py`/`run-codecs.py` isolation approach,
but deliberately has no libobs, production source include, live endpoint,
credentials, capture device, GUI, global install or application staging.

Graphs are `filesrc ! h265parse ! vtdec[_hw] ! video/x-raw[,format=...] ! appsink`
and `appsrc ! vp9parse ! vtdec[_hw] ! video/x-raw[,format=...] ! appsink`.
The runner extracts IVF frame packets in the native harness, preserving their
compressed contents; no IVF demux plugin is added to the curated runtime.

The work area retains generated compressed fixtures, their exact encoder commands
and SHA256s, ffprobe output, first raw frames, binary, observer dylib, factory
inspection logs and `results.json`. The durable review copies are:

- `decoder-profiles.c`: receive harness and observer build variant.
- `run-decoder-profiles.py`: generator, isolation, compilation and matrix runner.
- `decoder-profile-results.json`: recorded measured matrix and negative control.
- `decoder-software-inventory.json`: read-only pinned SDK software inventory.

## Software fallback investigation — not packaged

The curated runtime has **no** `vp9dec`, `avdec_vp9` or `avdec_h265` factory.
The official pinned SDK does contain `libgstvpx.dylib` and `libgstlibav.dylib`.
The inventory records exact hashes, arm64 linkage and relevant static strings:

- `libgstvpx.dylib` contains `vp9dec` and raw-format strings including I420_10LE,
  I422_10LE, Y444_10LE and 12-bit equivalents. Its dependency includes
  `libvpx.9.dylib` and `libgsttag-1.0.0.dylib` plus GStreamer/GLib libraries.
- `libgstlibav.dylib` contains dynamically named `avdec_%s` registration and
  depends on libavcodec61, libavfilter10, libavformat61, libavutil59 and GStreamer
  libraries. Presence is **not** proof of a registered/working specific decoder.

No software plugin was copied into either the curated runtime or app, and no
runtime lock, production pipeline, `pixelview-whep.c` or `runtime.h` was changed.
These are candidate fallback dependencies, not a software-decode acceptance
result. Parent coordination is required before closure/licensing review, isolated
software decoding, curated packaging and fallback selection changes.
