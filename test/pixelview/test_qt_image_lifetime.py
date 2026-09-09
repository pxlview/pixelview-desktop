"""Native macOS Qt ownership regression; no OBS, network or credential access.

PIXELVIEW_TEST_QT_PREFIX may select the former package for a red comparison.
The default follows the platform pin in CMakePresets.json.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform == 'darwin', 'CoreGraphics regression')
class QtImageLifetimeTests(unittest.TestCase):
    def test_native_color_space_and_cursor_lifetime(self):
        presets = json.loads((ROOT / 'CMakePresets.json').read_text())
        dependencies = next(p for p in presets['configurePresets'] if p['name'] == 'dependencies')
        qt = dependencies['vendor']['obsproject.com/obs-studio']['dependencies']['qt6']
        version = qt.get('platformVersions', {}).get('macos-universal', qt['version'])
        prefix = Path(os.environ.get('PIXELVIEW_TEST_QT_PREFIX',
                      str(ROOT / '.deps' / f'obs-deps-qt6-{version}-universal')))
        self.assertTrue((prefix / 'lib/QtGui.framework').exists(), 'Run the macOS build helper first')
        with tempfile.TemporaryDirectory(prefix='pixelview-qt-lifetime-') as temporary:
            executable = str(Path(temporary) / 'qt-image-lifetime')
            guard = str(Path(temporary) / 'ownership-guard.dylib')
            subprocess.run(['xcrun', 'clang++', '-std=c++17', '-dynamiclib',
                            str(ROOT / 'test/pixelview/qt_color_ownership_guard.cpp'),
                            '-framework', 'CoreFoundation', '-framework', 'CoreGraphics',
                            '-o', guard], check=True)
            args = ['xcrun', 'clang++', '-std=c++17', '-Werror',
                    '-include', str(ROOT / 'frontend/utility/PixelviewQtArmCompat.hpp'),
                    str(ROOT / 'test/pixelview/qt_image_lifetime.mm'), '-F' + str(prefix / 'lib'),
                    '-Wl,-rpath,' + str(prefix / 'lib'), '-o', executable]
            for framework in ['QtWidgets', 'QtGui', 'QtCore', 'CoreGraphics', 'CoreFoundation']:
                args += ['-framework', framework]
            subprocess.run(args, check=True)
            environment = dict(os.environ, QT_PLUGIN_PATH=str(prefix / 'plugins'), MallocScribble='1')
            # Instrumented run catches the premature release even when Apple's
            # internal color-space caching happens to conceal the dangling pointer.
            for instrumented in [True, False]:
                env = dict(environment)
                if instrumented:
                    env['DYLD_INSERT_LIBRARIES'] = guard
                else:
                    env.pop('DYLD_INSERT_LIBRARIES', None)
                result = subprocess.run([executable], env=env, capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn('PASS: 6000', result.stdout)


if __name__ == '__main__':
    unittest.main()
