#!/usr/bin/env python3
"""Hardware helper for end-to-end tests through two running Pixelview Desktop apps.

Builds the SDI pattern tool against the built app and runs it. It plays a
10-bit 4:2:2 test picture (flat patches, a fine luma ramp, row-alternating
chroma, colour bars) out of one DeckLink device and/or captures on another and
reports, averaged over 50 frames, how the captured levels compare with the
picture. Use one process per device (the plugin gives a device one owner).

  --play NAME [--source-range limited|full]   play the picture on output NAME
  --capture NAME --source-range R --expect R  capture on input NAME; R is the
                                              range the generator sent and the
                                              range the output under test is set to
Typical loop: generator output -> sender's capture card; receiver's output
card -> analyser input. Drive the sender with the backend's Desktop control
route and start receiving in the receiver app.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
APP = Path(os.environ.get('PIXELVIEW_TEST_BUILD', ROOT / 'build_macos')) / 'frontend/RelWithDebInfo/Pixelview Desktop.app'
FW = APP / 'Contents/Frameworks'
PLUGIN = APP / 'Contents/PlugIns/decklink.plugin/Contents'
DEPS = ROOT / '.deps/obs-deps-2026-08-26-universal'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--play')
    parser.add_argument('--capture')
    parser.add_argument('--source-range', choices=('limited', 'full'), default='limited')
    parser.add_argument('--expect', choices=('limited', 'full'), default='limited')
    parser.add_argument('--mode', default='1080p25')
    parser.add_argument('--seconds', type=int, default=20)
    parser.add_argument('--skip-frames', type=int, default=125)
    args = parser.parse_args()
    assert args.play or args.capture
    with tempfile.TemporaryDirectory(prefix='pixelview-e2e-sdi-') as tmp:
        tmp = Path(tmp)
        include = ['-I' + str(p) for p in (ROOT / 'libobs', ROOT / 'build_macos/config', ROOT / 'build_macos/libobs',
                                           DEPS / 'include', HERE)]
        link = ['-F' + str(FW), '-framework', 'libobs']
        subprocess.run(['xcrun', 'clang++', '-std=c++17', '-O1', *include, str(HERE / 'e2e-sdi-tool.mm'), *link,
                        '-framework', 'Foundation', '-Wl,-rpath,' + str(FW), '-o', str(tmp / 'tool')], check=True)
        subprocess.run(['xcrun', 'clang', '-O1', '-dynamiclib', *include, str(HERE / 'e2e-observer.c'), *link,
                        '-o', str(tmp / 'observer.dylib')], check=True)
        env = dict(os.environ, HOME=str(tmp), CFFIXED_USER_HOME=str(tmp), SECONDS=str(args.seconds),
                   DYLD_INSERT_LIBRARIES=str(tmp / 'observer.dylib'), E2E_SOURCE=args.source_range,
                   E2E_EXPECT=args.expect, E2E_SKIP=str(args.skip_frames), PLAY_RANGE=args.source_range)
        if args.play:
            env['PLAY'] = args.play
        if args.capture:
            env['CAPTURE'] = args.capture
        subprocess.run([str(tmp / 'tool'), str(FW / 'libobs.framework/Versions/A/Resources'),
                        str(FW / 'libobs-opengl.dylib'), str(PLUGIN / 'MacOS/decklink'), str(PLUGIN / 'Resources'),
                        args.mode], env=env, check=True)


if __name__ == '__main__':
    main()
