#!/usr/bin/env python3
"""Offline native production decoder, generated CC0 fixtures, no card/GUI/auth."""
import array
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / 'plugins/pixelview-whep'
WORK = PLUGIN / '.test-build/native-422'
SDK = ROOT / '.deps/gstreamer-upstream-1.28.3/sdk'
WORK.mkdir(parents=True, exist_ok=True)
for stale in ['acceptance.json', 'results.json']:
    (WORK / stale).unlink(missing_ok=True)
STAGE = WORK / 'runtime'
if not STAGE.exists():
    shutil.copytree(ROOT / '.deps/pixelview-gstreamer', STAGE)
ENV = {k: v for k, v in os.environ.items() if not k.startswith(('GST_', 'DYLD_'))}
ENV.update(HOME=str(WORK), CFFIXED_USER_HOME=str(WORK),
           DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer',
           GST_PLUGIN_SYSTEM_PATH_1_0='', GST_PLUGIN_PATH_1_0=str(STAGE / 'lib/gstreamer-1.0'),
           GST_PLUGIN_SCANNER=str(STAGE / 'libexec/gst-plugin-scanner'),
           GST_REGISTRY=str(WORK / 'registry.bin'))
commands = []
def run(cmd, **kw):
    if os.environ.get('PV_NATIVE422_SANITIZE')=='1' and cmd[:2]==['xcrun','clang'] and '-dynamiclib' not in cmd:
        cmd=[*cmd,'-fsanitize=address,undefined','-fno-omit-frame-pointer']
    commands.append([str(x) for x in cmd])
    (WORK / 'commands.json').write_text(json.dumps(commands, indent=2))
    extra_env = kw.pop('extra_env', {})
    return subprocess.run(cmd, check=True, env={**ENV, **extra_env}, timeout=90, **kw)
inc = ['-I' + str(SDK / p) for p in ['include/gstreamer-1.0', 'include/glib-2.0', 'lib/glib-2.0/include']]
libs = ['-L' + str(STAGE / 'lib'), '-Wl,-rpath,' + str(STAGE / 'lib')]
libs += ['-l' + x + '-1.0.0' for x in ['gstapp', 'gstvideo', 'gstbase', 'gstreamer', 'gstcodecparsers', 'gstrtp']]
libs += ['-lglib-2.0.0', '-lgobject-2.0.0']
feed_test = WORK / 'source-feed'
run(['xcrun', 'clang', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
     str(PLUGIN / 'tests/source-feed.c'), '-o', str(feed_test)])
run([str(feed_test)])
bounds = WORK / 'native-422-bounds'
run(['xcrun', 'clang', '-Wall', '-Wextra', '-Werror', *inc,
     str(PLUGIN / 'tests/native-422-bounds.m'), *libs,
     '-framework', 'Foundation', '-framework', 'VideoToolbox', '-framework', 'CoreMedia',
     '-framework', 'CoreVideo', '-o', str(bounds)])
for boundary in ['codec', 'au']:
    run([str(bounds), boundary])
exe = WORK / 'native-422'
run(['xcrun', 'clang', '-Wall', '-Wextra', '-Werror', '-DGST_USE_UNSTABLE_API', *inc,
     str(PLUGIN / 'tests/native-422.c'), str(PLUGIN / 'native-422.m'), *libs,
     '-framework', 'Foundation', '-framework', 'VideoToolbox', '-framework', 'CoreMedia',
     '-framework', 'CoreVideo', '-o', str(exe)])
results = []
observer = WORK / 'native-422-observer.dylib'
run(['xcrun', 'clang', '-Wall', '-Wextra', '-Werror', '-dynamiclib',
     str(PLUGIN / 'tests/native-422-observer.c'), '-framework', 'CoreVideo', '-lcompression', '-o', str(observer)])
for case, range_, transfer, matrix in [('limited', 'tv', 'bt709', 'bt709'),
                                      ('full', 'pc', 'bt709', 'bt709'),
                                      ('hdr', 'tv', 'smpte2084', 'bt709'),
                                      ('unknown', 'tv', 'unknown', 'unknown')]:
    raw = WORK / (case + '.yuv')
    values = array.array('H')
    for f in range(3):
        values.extend(64 + (x + f * 7) % 877 for y in range(64) for x in range(1024))
        for c in range(2):
            def chroma(x, y):
                if 32 <= y < 48:
                    # One-sample chroma impulses on neutral, reversed each row.
                    return (960 if y % 2 else 64) if x % 32 == f + c else 512
                ramp = (x * (13 + c * 4) + f * 23) % 897
                return 64 + (ramp if y % 2 else 896 - ramp)
            values.extend(chroma(x, y) for y in range(64) for x in range(512))
    raw.write_bytes(values.tobytes())
    fixture = WORK / (case + '.h265')
    tags = ['-color_range', range_, '-color_primaries', 'bt709', '-color_trc', transfer, '-colorspace', matrix]
    run(['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pixel_format', 'yuv422p10le',
         '-video_size', '1024x64', '-framerate', '30000/1001', *tags, '-i', str(raw),
         '-frames:v', '3', '-c:v', 'libx265', '-preset', 'ultrafast', '-x265-params',
         f'lossless=1:log-level=error:pools=1:frame-threads=1:bframes=0:colorprim=bt709:transfer={transfer}:colormatrix={matrix}:range={"full" if range_ == "pc" else "limited"}',
         *tags, str(fixture)], capture_output=True)
    ref = WORK / (case + '-reference.yuv')
    run(['ffmpeg', '-v', 'error', '-y', '-c:v', 'hevc', '-i', str(fixture), '-pix_fmt', 'yuv422p10le', '-f', 'rawvideo', str(ref)])
    assert ref.read_bytes() == raw.read_bytes(), 'lossless independent reference differs'
    packed = WORK / (case + '.v210')
    native_dump = WORK / (case + '-native.yuv')
    native_dump.unlink(missing_ok=True)
    observe_env = {'DYLD_INSERT_LIBRARIES': str(observer), 'PV_NATIVE422_X422_DUMP': str(native_dump), 'PV_NATIVE422_OBSERVER_EXECUTABLE':str(exe)} if case == 'limited' else {}
    run([str(exe), str(fixture), str(packed), 'accept' if case == 'limited' else 'reject'], extra_env=observe_env)
    if case == 'limited':
        assert native_dump.read_bytes() == raw.read_bytes(), 'native x422 differs before production packing'
        unpacked = WORK / 'v210-reference.yuv'
        run(['ffmpeg', '-v', 'error', '-y', '-f', 'v210', '-video_size', '1024x64',
             '-i', str(packed), '-pix_fmt', 'yuv422p10le', '-f', 'rawvideo', str(unpacked)])
        assert unpacked.read_bytes() == raw.read_bytes(), 'final v210 differs from independent HEVC reference'
    results.append(dict(case=case, accepted=case == 'limited', reference_exact=True))
    (WORK / 'results.json').write_text(json.dumps(results, indent=2))
filter_exe = WORK / 'native-422-filter'
for negative in ['malformed', 'missing-pts', 'resolution', 'profile']:
    run([str(exe), str(WORK / 'limited.h265'), str(WORK / (negative + '.v210')), negative])
control = WORK / 'negative420.h265'
run(['ffmpeg', '-v', 'error', '-y', '-i', str(WORK / 'limited.h265'),
     '-pix_fmt', 'yuv420p10le', '-c:v', 'libx265', '-preset', 'ultrafast', '-x265-params',
     'lossless=1:log-level=error:pools=1:frame-threads=1:bframes=0:colorprim=bt709:transfer=bt709:colormatrix=bt709',
     str(control)], capture_output=True)
run([str(exe), str(control), str(WORK / 'negative420.v210'), 'reject'])
run(['xcrun', 'clang', '-Wall', '-Wextra', '-Werror', *inc,
     str(PLUGIN / 'tests/native-422-filter.c'), str(PLUGIN / 'native-422-filter.c'),
     str(PLUGIN / 'native-422.m'), *libs, '-framework', 'Foundation',
     '-framework', 'VideoToolbox', '-framework', 'CoreMedia', '-framework', 'CoreVideo', '-o', str(filter_exe)])
run([str(filter_exe), str(WORK / 'limited.h265'), str(WORK / 'filter.v210')])
assert (WORK / 'filter.v210').read_bytes() == (WORK / 'limited.v210').read_bytes() * 3, 'all nine restart frames must match, not just the final three'
FW = ROOT / 'build_macos/libobs/RelWithDebInfo'
DEPS = sorted((ROOT / '.deps').glob('obs-deps-*/include/simde'))[-1].parents[1]
obs_inc = ['-I' + str(p) for p in [ROOT / 'libobs', ROOT / 'build_macos/config', DEPS / 'include']]
obs_libs = ['-F' + str(FW), '-framework', 'libobs', '-Wl,-rpath,' + str(FW),
            '-Wl,-rpath,' + str(DEPS / 'lib'), '-lgstaudio-1.0.0']
integration = WORK / 'native-422-integration'
import native422_build
run(['xcrun', 'clang', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', *inc, *obs_inc,
     str(PLUGIN / 'tests/native-422-integration.c'),
     *[str(PLUGIN / p) for p in ['video-format.c', 'profile-offer.c', 'capability-probe.c']],
     *native422_build.sources(PLUGIN), '-framework', 'Foundation', '-framework', 'VideoToolbox',
     '-framework', 'CoreMedia', '-framework', 'CoreVideo', *libs, *obs_libs, '-o', str(integration)])
run([str(integration), str(WORK / 'limited.h265'), str(WORK / 'integration.v210')])
assert (WORK / 'integration.v210').read_bytes() == (WORK / 'limited.v210').read_bytes()
layout_exe = WORK / 'native-422-layout'
run(['xcrun', 'clang', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
     '-fno-omit-frame-pointer', '-g', *inc, str(PLUGIN / 'tests/native-422-layout.m'),
     *libs, '-framework', 'Foundation', '-framework', 'VideoToolbox', '-framework', 'CoreMedia',
     '-framework', 'CoreVideo', '-o', str(layout_exe)])
run([str(layout_exe), str(WORK)])
for width in [2, 4, 6, 8, 46, 48, 50, 94, 96, 98, 128, 720, 1024, 1280, 1920]:
    unpacked = WORK / f'layout-{width}-unpacked.yuv'
    run(['ffmpeg', '-v', 'error', '-y', '-f', 'v210', '-video_size', f'{width}x8',
         '-i', str(WORK / f'layout-{width}.v210'), '-pix_fmt', 'yuv422p10le', '-f', 'rawvideo', str(unpacked)])
    assert unpacked.read_bytes() == (WORK / f'layout-{width}.yuv').read_bytes()
run(['python3', str(PLUGIN / 'tests/run-native-422-diagnostic.py')])
run(['python3', str(PLUGIN / 'tests/run-preview-dispatch.py')])
acceptance = dict(offline_native_suite_passed=True, main422_advertised=False,
                  native_x422_exact=True, final_v210_exact=True, cases=results,
                  oversized_fragmented_inputs_unmerged=True, restart_frames_compared=9,
                  source_feed_version=1, source_feed_queue_sanitizers=True,
                  source_audio_timestamp_and_mute_tested=True,
                  rejected_after_first_frame=['malformed', 'missing-pts', 'resolution', 'profile'],
                  negative420_rejected=True, layout_widths=[2, 4, 6, 8, 46, 48, 50, 94, 96, 98, 128, 720, 1024, 1280, 1920],
                  layout_asan_ubsan_passed=True, source_bound_filter_passed=True,
                  main422_whep_rtp_tested=False, decklink_scheduler_implemented=False,
                  physical_card_tested=False)
(WORK / 'acceptance.json').write_text(json.dumps(acceptance, indent=2))
files = [PLUGIN / p for p in ['pixelview-whep.c', 'native-422.m', 'native-422.h', 'native-422-filter.c', 'native-422-filter.h', 'source-feed.h', 'source-feed-queue.h']]
files += [WORK / p for p in ['limited.h265', 'limited.yuv', 'limited-native.yuv', 'limited.v210', 'integration.v210']]
(WORK / 'hashes.json').write_text(json.dumps({str(p.relative_to(PLUGIN)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}, indent=2))
print(json.dumps(acceptance, indent=2))
