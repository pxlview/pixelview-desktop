#!/usr/bin/env python3
"""Offline 4:4:4 capture/receive fidelity probe against the existing libobs build.

A 10-bit RGB source (an RGB 4:4:4 SDI capture) and a P416 source (a received
HEVC Main 4:4:4 10 stream) go through the real libobs canvas and must reach the
P416 encoder input and the DeckLink RGB 4:4:4 render with the code values they
came in with: exactly where source and target are the same colour family, and
within one code where the BT.709 matrix converts between R'G'B' and Y'CbCr.
Needs a libobs built from the current tree. The built framework is copied to a
temporary directory and overlaid with the repository's current effect files, as
in run_video_fidelity_probe.py.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get('PIXELVIEW_TEST_BUILD', ROOT / 'build_macos'))
FW = BUILD / 'libobs/RelWithDebInfo'
DEPS = ROOT / '.deps/obs-deps-2026-08-26-universal'
GRAPHICS = BUILD / 'libobs-opengl/RelWithDebInfo/libobs-opengl.dylib'
# (source format, DeckLink output range)
CASES = [('r10l',), ('p416',), ('r10l', 'full'), ('p416', 'full')]


def main():
    env = dict(os.environ, DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
    with tempfile.TemporaryDirectory(prefix='pixelview-fidelity-444-') as tmp:
        tmp = Path(tmp)
        env.update(HOME=str(tmp), CFFIXED_USER_HOME=str(tmp))
        framework = tmp / 'libobs.framework'
        shutil.copytree(FW / 'libobs.framework', framework, symlinks=True)
        resources = framework / 'Versions/A/Resources'
        for effect in (ROOT / 'libobs/data').glob('*.effect'):
            shutil.copy2(effect, resources / effect.name)
        subprocess.run(['codesign', '--force', '--sign', '-', str(framework)], check=True, capture_output=True)
        exe = tmp / 'probe'
        subprocess.run(['xcrun', 'clang++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-Wno-deprecated-declarations', '-Wno-missing-field-initializers',
                        '-I' + str(ROOT / 'libobs'), '-I' + str(BUILD / 'config'), '-I' + str(BUILD / 'libobs'),
                        '-I' + str(DEPS / 'include'), '-I' + str(ROOT / 'plugins/decklink-output-ui'),
                        str(Path(__file__).with_name('video_fidelity_444_probe.mm')),
                        '-F' + str(tmp), '-framework', 'libobs', '-framework', 'Foundation',
                        '-Wl,-rpath,' + str(tmp), '-Wl,-rpath,' + str(DEPS / 'lib'), '-o', str(exe)],
                       check=True, env=env)
        failed = []
        for case in CASES:
            result = subprocess.run([str(exe), str(ROOT / 'libobs/data'), str(GRAPHICS), *case],
                                    env=env, text=True, capture_output=True, timeout=120)
            lines = [line for line in result.stdout.splitlines() if line.startswith(('RESULT', 'PASS', 'FAIL'))]
            print('\n'.join(lines), flush=True)
            if result.returncode != 0:
                failed.append((case, result.returncode))
        assert not failed, failed
        print('PASS 4:4:4 capture/receive fidelity: RGB and Y\'CbCr 4:4:4 exact through the canvas, '
              'matrix conversions within one code')


if __name__ == '__main__':
    main()
