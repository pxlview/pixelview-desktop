#!/usr/bin/env python3
"""Hardware SDI loop test. Needs a DeckLink output device cabled to a DeckLink
input device (device names containing "Monitor" and "Recorder", 1080p25) and
the built app in build_macos. Not part of the offline suites.

Output direction: a 10-bit 4:2:2 pattern through the real libobs canvas, the
DrawV210 render and the built decklink_output must be captured back identical
in SDR (every code 4-1019); PQ and HLG must be identical for ordinary colours.
Capture direction: the same pattern fed to the card must reach each profile's
encoder input (P216, P010, NV12 canvas output) as the offline fidelity probe
expects.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
APP = Path(os.environ.get('PIXELVIEW_TEST_BUILD', ROOT / 'build_macos')) / 'frontend/RelWithDebInfo/Pixelview Desktop.app'
FW = APP / 'Contents/Frameworks'
PLUGIN = APP / 'Contents/PlugIns/decklink.plugin/Contents'
DEPS = ROOT / '.deps/obs-deps-2026-08-26-universal'
RESULT = re.compile(r'region="([^"]+)" luma_exact=([0-9.]+)% worst=(\d+) chroma_exact=([0-9.]+)% worst=(\d+)')


def main():
    with tempfile.TemporaryDirectory(prefix='pixelview-sdi-loop-') as tmp:
        tmp = Path(tmp)
        include = ['-I' + str(p) for p in (ROOT / 'libobs', ROOT / 'build_macos/config', ROOT / 'build_macos/libobs',
                                           DEPS / 'include', ROOT / 'plugins/decklink-output-ui', HERE)]
        link = ['-F' + str(FW), '-framework', 'libobs']
        subprocess.run(['xcrun', 'clang++', '-std=c++17', '-O1', *include, str(HERE / 'sdi-loop.mm'), *link,
                        '-framework', 'Foundation', '-Wl,-rpath,' + str(FW), '-o', str(tmp / 'loop')], check=True)
        subprocess.run(['xcrun', 'clang', '-dynamiclib', *include, str(HERE / 'sdi-loop-observer.c'), *link,
                        '-o', str(tmp / 'observer.dylib')], check=True)
        base = dict(os.environ, HOME=str(tmp), CFFIXED_USER_HOME=str(tmp))
        args = [str(tmp / 'loop'), str(FW / 'libobs.framework/Versions/A/Resources'), str(FW / 'libobs-opengl.dylib'),
                str(PLUGIN / 'MacOS/decklink'), str(PLUGIN / 'Resources'), '1080p25']

        def run(colour, env, marker):
            out = subprocess.run(args + [colour], env={**base, **env}, text=True, capture_output=True, timeout=120)
            text = out.stdout + out.stderr
            assert 'output start: 1' in text, text[-2000:]
            rows = {}
            for line in text.splitlines():
                if marker in line and (marker != 'CAPTURED' or 'frame=105 ' in line):
                    m = RESULT.search(line)
                    if m:
                        rows[m[1]] = (float(m[2]), int(m[3]), float(m[4]), int(m[5]))
                        print(colour, env.get('CANVAS', 'output'), line[line.index('region='):], flush=True)
            assert len(rows) == 5, 'no stable capture: is the output cabled to the input?\n' + text[-1500:]
            return rows

        observer = {'DYLD_INSERT_LIBRARIES': str(tmp / 'observer.dylib')}
        rows = run('sdr', observer, 'CAPTURED')
        assert all(r[1] == 0 and r[3] == 0 for r in rows.values()), rows
        for colour in ('pq', 'hlg'):
            rows = run(colour, {**observer, 'SAT': '0.3'}, 'CAPTURED')
            assert rows['chroma gradients'][1] == 0 and rows['chroma gradients'][3] == 0, rows
            assert rows['colour bars'][1] == 0 and rows['colour bars'][3] == 0, rows
            assert rows['luma ramp'][1] <= (2 if colour == 'hlg' else 0) and rows['luma ramp'][3] == 0, rows
        for canvas, limit in (('P216', 0), ('P010', 0), ('NV12', 1)):
            rows = run('sdr', {'CANVAS': canvas}, 'ENCODER-INPUT')
            for region, r in rows.items():
                if region == 'random noise' and canvas != 'P216':
                    assert r[1] <= limit and r[3] <= 1, (canvas, region, r)  # 4:2:0 shares chroma between rows
                else:
                    assert r[1] <= limit and r[3] <= limit, (canvas, region, r)
    print('PASS SDI loop: DeckLink output and capture carry 10-bit 4:2:2 exactly in SDR; HDR exact for ordinary colours')


if __name__ == '__main__':
    main()
