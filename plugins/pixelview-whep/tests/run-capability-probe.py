#!/usr/bin/env python3
"""Offline native receiver capability tests against an existing isolated runtime."""
import argparse
import hashlib
import json
import re
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
SDK = REPO / '.deps/gstreamer-upstream-1.28.3/sdk'


def verify_fixtures():
    metadata = json.loads((ROOT / 'tests/capability-fixtures.json').read_text())
    header = (ROOT / 'capability-fixtures.h').read_text()
    assert len(metadata['fixtures']) == 5
    for row in metadata['fixtures']:
        stream = row['ffprobe']['streams'][0]
        assert (stream['width'], stream['height'], stream['nb_read_frames']) == (1920, 1080, '3'), row['name']
        timing = row['encoded_timing']
        if stream['codec_name'] == 'h264':
            assert timing['frame_mbs_only_flag'] == 1
            assert timing['time_scale'] == 120 * timing['num_units_in_tick']
        elif stream['codec_name'] == 'hevc':
            assert timing['general_tier_flag'] == 0
            assert timing['vui_time_scale'] == 60 * timing['vui_num_units_in_tick']
        else:
            assert stream['r_frame_rate'] == '60/1'
        assert stream['level'] == {'h264': 42, 'hevc': 123, 'vp9': -99}[stream['codec_name']]
        arrays = re.findall(r'static const guint8 fixture_' + row['name'] + r'_\d+\[\] = \{(.*?)\};', header, re.S)
        packets = [bytes(int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})', a)) for a in arrays]
        assert len(packets) == 3
        assert [hashlib.sha256(p).hexdigest() for p in packets] == row['packet_sha256']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, default=ROOT / '.test-build/decoder-profile/runtime')
    parser.add_argument('--repeat-real', type=int, default=1, help='Fresh-process repetitions to expose EOS/drain races')
    args = parser.parse_args()
    assert args.repeat_real > 0
    verify_fixtures()
    stage = args.runtime.resolve()
    work = ROOT / '.test-build/capability-probe'
    work.mkdir(parents=True, exist_ok=True)
    assert (ROOT / 'capability-probe.c').exists(), 'Missing runtime decode capability implementation'
    env = {k: v for k, v in os.environ.items() if not k.startswith(('GST_', 'DYLD_'))}
    env.update(GST_PLUGIN_SYSTEM_PATH_1_0='', GST_PLUGIN_PATH_1_0=str(stage/'lib/gstreamer-1.0'),
               GST_PLUGIN_SCANNER=str(stage/'libexec/gst-plugin-scanner'), GST_REGISTRY=str(work/'registry.bin'))
    includes = ['-I'+str(SDK/p) for p in ('include/gstreamer-1.0', 'include/glib-2.0', 'lib/glib-2.0/include')]
    libs = ['-l'+n for n in ('gstapp-1.0.0', 'gstvideo-1.0.0', 'gstbase-1.0.0', 'gstreamer-1.0.0', 'gobject-2.0.0', 'glib-2.0.0')]
    exe = work/'capability-probe'
    subprocess.run(['clang', '-arch', 'arm64', '-Wall', '-Wextra', '-Werror', '-DPIXELVIEW_CAPABILITY_TESTING',
                    *includes, str(ROOT/'tests/capability-probe.c'), str(ROOT/'capability-probe.c'),
                    '-o', str(exe), '-L'+str(stage/'lib'), '-Wl,-rpath,'+str(stage/'lib'), *libs], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(exe)], check=True, capture_output=True)
    # Also compile the shipping variant: no test-only controls may be exported.
    obj = work/'capability-probe-production.o'
    subprocess.run(['clang', '-arch', 'arm64', '-Wall', '-Wextra', '-Werror', *includes,
                    '-c', str(ROOT/'capability-probe.c'), '-o', str(obj)], check=True)
    assert 'pixelview_capability_probe_test_' not in subprocess.check_output(['nm', str(obj)], text=True)
    # Observe the actual VT session through the existing audit's test-only hook.
    hook = work/'decoder-session-observer.dylib'
    subprocess.run(['clang', '-arch', 'arm64', '-Wall', '-Wextra', '-Werror', *includes,
                    '-DDECODER_INTERPOSE_ONLY', '-dynamiclib', str(ROOT/'tests/decoder-profiles.c'),
                    '-o', str(hook), '-framework', 'VideoToolbox', '-framework', 'CoreMedia',
                    '-framework', 'CoreFoundation'], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(hook)], check=True, capture_output=True)
    for mode in ('real', 'absent', 'malformed', 'malformed-main10', 'cancel', 'timeout', 'wrong-rate', 'wrong-timing'):
        result = subprocess.run([str(exe), mode], env=env, text=True, capture_output=True, timeout=15)
        print(mode, result.returncode, result.stdout.strip(), result.stderr.strip())
        assert result.returncode == 0, result
        assert not result.stderr, result.stderr
    for iteration in range(1, args.repeat_real):
        repeated = subprocess.run([str(exe), 'real'], env=env, text=True, capture_output=True, timeout=15)
        assert repeated.returncode == 0 and not repeated.stderr, (iteration, repeated)
    print('real repetitions passed:', args.repeat_real)
    observed = subprocess.run([str(exe), 'real'], env={**env, 'DYLD_INSERT_LIBRARIES': str(hook)},
                              text=True, capture_output=True, timeout=15)
    assert observed.returncode == 0, observed
    sessions = observed.stderr.splitlines()
    assert len(sessions) == 5 and all(line == 'HW_SESSION create=0 query=0 hardware=true' for line in sessions), observed
    print('hardware-observed', observed.stdout.strip(), 'hardware_sessions=5')


if __name__ == '__main__':
    main()
