#!/usr/bin/env python3
"""Hardware helper for end-to-end tests through two running Pixelview Desktop apps.

Builds the SDI pattern tool against the built app and runs it. It plays a
10-bit 4:2:2 test picture (flat patches, a fine luma ramp, row-alternating
chroma, colour bars) out of one DeckLink device and/or captures on another and
reports, averaged over 50 frames, how the captured levels compare with the
picture. With --format rgb444 the picture is 10-bit RGB 4:4:4 instead, with a
field whose colour changes on every pixel (lost by any chroma subsampling) and
bar edges on odd pixels. Use one process per device (the plugin gives a device
one owner).

  --format yuv422|rgb444                      which picture and SDI format (both ends)
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
# PIXELVIEW_TEST_DECKLINK_PLUGIN: a decklink.plugin/Contents built on its own (quick hardware iterations).
PLUGIN = Path(os.environ.get('PIXELVIEW_TEST_DECKLINK_PLUGIN', APP / 'Contents/PlugIns/decklink.plugin/Contents'))
DEPS = ROOT / '.deps/obs-deps-2026-08-26-universal'


RATES = {'23.98': (24000, 1001), '24': (24, 1), '25': (25, 1), '29.97': (30000, 1001), '30': (30, 1)}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--play')
    parser.add_argument('--capture')
    parser.add_argument('--source-range', choices=('limited', 'full'), default='limited')
    parser.add_argument('--expect', choices=('limited', 'full'), default='limited')
    parser.add_argument('--format', choices=('yuv422', 'rgb444'), default='yuv422',
                        help='rgb444: the 4:4:4 picture as 10-bit RGB on SDI, played and captured without subsampling')
    parser.add_argument('--mode', default='1080p25')
    parser.add_argument('--seconds', type=int, default=20)
    parser.add_argument('--skip-frames', type=int, default=125)
    args = parser.parse_args()
    assert args.play or args.capture
    # The tool's canvas and frame queue run at the SDI mode's rate (Auto capture keeps 25).
    rate = RATES.get(args.mode.split('p')[-1], (25, 1))
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
                   E2E_EXPECT=args.expect, E2E_SKIP=str(args.skip_frames), PLAY_RANGE=args.source_range,
                   E2E_FORMAT='rgb' if args.format == 'rgb444' else 'yuv',
                   E2E_FPS_NUM=str(rate[0]), E2E_FPS_DEN=str(rate[1]))
        if args.play:
            env['PLAY'] = args.play
        if args.capture:
            env['CAPTURE'] = args.capture
        subprocess.run([str(tmp / 'tool'), str(FW / 'libobs.framework/Versions/A/Resources'),
                        str(FW / 'libobs-opengl.dylib'), str(PLUGIN / 'MacOS/decklink'), str(PLUGIN / 'Resources'),
                        args.mode], env=env, check=True)


if __name__ == '__main__':
    main()
