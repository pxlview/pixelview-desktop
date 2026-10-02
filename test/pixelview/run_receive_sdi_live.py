#!/usr/bin/env python3
"""Live receive to SDI without the application window (hardware and a backend needed).

Builds receive_sdi_live.mm against the built app and runs it: the real
PixelviewReceiver logs in to the backend, the packaged pixelview-whep module
receives the session, the picture fills a libobs canvas 1:1 and leaves through
the built DeckLink output as 10-bit 4:2:2 Y'CbCr or 10-bit 4:4:4 RGB, rendered
with the header the DeckLink output UI uses. Measure the card's output with
plugins/decklink/tests/run-e2e-sdi-tool.py --capture on another device.

The session id and password are read from a JSON file ({"session_id": ...,
"password": ...}) named by --credentials and passed on stdin; they are never
printed. This does not exercise the application's own windows.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get('PIXELVIEW_TEST_BUILD', ROOT / 'build_macos'))
APP = BUILD / 'frontend/RelWithDebInfo/Pixelview Desktop.app/Contents'
FW = APP / 'Frameworks'
# Headers of the Qt the app bundles (6.10); the newer deps set is 6.11 and does not load against it.
QT = ROOT / '.deps/obs-deps-qt6-2026-05-21-universal'
DEPS = ROOT / '.deps/obs-deps-2026-08-26-universal'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--credentials', type=Path, required=True)
    parser.add_argument('--output', required=True, help='DeckLink output device (name substring)')
    parser.add_argument('--mode', default='1080p25')
    parser.add_argument('--format', choices=('yuv422', 'rgb444'), default='yuv422')
    parser.add_argument('--range', choices=('limited', 'full'), default='limited')
    parser.add_argument('--seconds', type=int, default=60)
    parser.add_argument('--origin', default='http://localhost:8000')
    args = parser.parse_args()
    credentials = json.loads(args.credentials.read_text())
    # The canvas follows the output mode's frame rate, as the application's receive mode does.
    rate = {'23.98': (24000, 1001), '24': (24, 1), '25': (25, 1), '29.97': (30000, 1001), '30': (30, 1)}[args.mode.split('p')[-1]]
    with tempfile.TemporaryDirectory(prefix='pixelview-receive-sdi-') as tmp:
        tmp = Path(tmp)
        exe = tmp / 'receive-sdi'
        subprocess.run(['xcrun', 'clang++', '-std=c++17', '-fobjc-arc', '-O1', '-Wno-deprecated-declarations', '-include', 'arm_acle.h', '-I' + str(ROOT), '-I' + str(ROOT / 'libobs'),
                        '-I' + str(BUILD / 'config'), '-I' + str(BUILD / 'libobs'), '-I' + str(DEPS / 'include'),
                        '-I' + str(ROOT / 'plugins/decklink-output-ui'), '-F' + str(QT / 'lib'), '-F' + str(FW),
                        '-framework', 'QtCore', '-framework', 'Foundation', '-framework', 'libobs',
                        '-Wl,-rpath,' + str(FW), str(ROOT / 'frontend/utility/PixelviewReceiver.cpp'),
                        str(ROOT / 'frontend/utility/PixelviewReceiverMac.mm'), str(Path(__file__).with_name('receive_sdi_live.mm')),
                        '-o', str(exe)], check=True, env=dict(os.environ, DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer'))
        env = {k: v for k, v in os.environ.items() if not k.startswith(('GST_', 'DYLD_'))}
        env.update(HOME=str(tmp), CFFIXED_USER_HOME=str(tmp))
        decklink = APP / 'PlugIns/decklink.plugin/Contents'
        result = subprocess.run([str(exe), str(APP / 'PlugIns/pixelview-whep.plugin/Contents/MacOS/pixelview-whep'),
                                 str(FW / 'libobs.framework/Versions/A/Resources'), str(FW / 'libobs-opengl.dylib'),
                                 str(decklink / 'MacOS/decklink'), str(decklink / 'Resources'), args.output, args.mode,
                                 args.format, args.range, str(args.seconds), args.origin, str(rate[0]), str(rate[1])],
                                input=json.dumps({k: str(credentials[k]) for k in ('session_id', 'password')}) + '\n',
                                text=True, env=env)
        raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
