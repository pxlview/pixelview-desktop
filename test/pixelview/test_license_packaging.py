"""Build a disposable tiny macOS Qt-free bundle using the production resource registration."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / 'frontend/cmake/pixelview-license.cmake'


class LicensePackaging(unittest.TestCase):
    def test_external_license_assets_are_bundled_and_missing_input_fails(self):
        self.assertTrue(MODULE.is_file(), 'Production registration for external generated license assets is missing')
        self.assertIn('include(cmake/pixelview-license.cmake)', (ROOT / 'frontend/CMakeLists.txt').read_text())
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            assets = root / 'generated'; assets.mkdir()
            (assets / 'third-party-notices.txt').write_text('partial test-only inventory\n')
            (assets / 'source-manifest.json').write_text('{"source_status":"unpublished-test-fixture"}\n')
            (root / 'main.c').write_text('int main(void) { return 0; }\n')
            (root / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.28)
project(license_resource_test C)
set(OS_MACOS TRUE)
add_executable(obs-studio MACOSX_BUNDLE main.c)
include("{MODULE}")
''')
            env = {**os.environ}
            env.pop('SDKROOT', None)
            subprocess.run(['cmake', '-S', str(root), '-B', str(root / 'build'),
                            '-DPIXELVIEW_LICENSE_DATA_DIR=' + str(assets)], check=True, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            subprocess.run(['cmake', '--build', str(root / 'build')], check=True, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            bundled = root / 'build/obs-studio.app/Contents/Resources/license'
            for name in ('third-party-notices.txt', 'source-manifest.json'):
                self.assertEqual((assets / name).read_bytes(), (bundled / name).read_bytes())
            (assets / 'source-manifest.json').unlink()
            result = subprocess.run(['cmake', '-S', str(root), '-B', str(root / 'bad'),
                                     '-DPIXELVIEW_LICENSE_DATA_DIR=' + str(assets)], env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('source-manifest.json', result.stdout)
            subprocess.run(['cmake', '-S', str(root), '-B', str(root / 'dev')], check=True, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            subprocess.run(['cmake', '--build', str(root / 'dev')], check=True, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            self.assertFalse((root / 'dev/obs-studio.app/Contents/Resources/license').exists())


if __name__ == '__main__':
    unittest.main()
