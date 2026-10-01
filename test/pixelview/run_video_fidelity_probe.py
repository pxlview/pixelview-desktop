#!/usr/bin/env python3
"""Offline capture/receive fidelity probe against the existing libobs build.

A v210 (or, for a 4:2:0 receive, P010) source through the real libobs canvas
must reach each profile's encoder input (P216, P010, NV12) and the DeckLink
v210 render with the code values it came in with, as far as the target format
can hold them. Needs a libobs built from the current tree (the canvas texture
format is chosen in C). The built framework is copied to a temporary directory and overlaid with
the repository's current effect files, so shader edits are tested without
rebuilding or touching the build tree.
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
# (colour mode, canvas format, chroma saturation, source format). The canvas is
# the sender's profile: P216 Main 4:2:2 10, P010 Main10, NV12 Main. Source v210
# is the SDI capture or a 4:2:2 receive; P010 is a 4:2:0 receive. HDR runs at
# reduced saturation: ordinary colours must be exact, extreme ones are
# documented loss.
CASES = [('sdr', 'P216', '1', 'v210'), ('sdr', 'P010', '1', 'v210'), ('sdr', 'NV12', '1', 'v210'),
         ('sdr', 'P010', '1', 'P010'),
         ('pq', 'P216', '0.3', 'v210'), ('pq', 'P010', '0.3', 'v210'), ('hlg', 'P010', '0.3', 'v210'),
         ('pq', 'P010', '0.3', 'P010')]


def main():
    env = dict(os.environ, DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
    with tempfile.TemporaryDirectory(prefix='pixelview-fidelity-') as tmp:
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
                        str(Path(__file__).with_name('video_fidelity_probe.mm')),
                        '-F' + str(tmp), '-framework', 'libobs', '-framework', 'Foundation',
                        '-Wl,-rpath,' + str(tmp), '-Wl,-rpath,' + str(DEPS / 'lib'), '-o', str(exe)],
                       check=True, env=env)
        failed = []
        only = os.environ.get('PV_FIDELITY_CASE')
        for colour, canvas, saturation, source in CASES:
            if only and only != f'{colour}-{source}-{canvas}':
                continue
            result = subprocess.run([str(exe), str(ROOT / 'libobs/data'), str(GRAPHICS), colour, canvas, saturation, source],
                                    env=env, text=True, capture_output=True, timeout=120)
            lines = [line for line in result.stdout.splitlines() if line.startswith(('RESULT', 'PASS', 'FAIL'))]
            print('\n'.join(lines), flush=True)
            if result.returncode != 0:
                failed.append((colour, source, canvas, result.returncode))
        assert not failed, failed
        print('PASS capture/receive fidelity: SDR exact to each profile\'s encoder input and to the DeckLink v210 '
              'render; HDR exact for ordinary colours')


if __name__ == '__main__':
    main()
