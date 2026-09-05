import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class ConfigIsolation(unittest.TestCase):
    def test_config_root_isolated_including_null_and_portable_paths(self):
        header = ROOT / 'frontend/utility/PixelviewConfig.hpp'
        self.assertTrue(header.exists(), 'Pixelview configuration namespace is missing')
        code = r'''
#include "frontend/utility/PixelviewConfig.hpp"
#include <cassert>
int main() {
 assert(pixelview::configName(nullptr) == "pixelview");
 assert(pixelview::configName("") == "pixelview");
 assert(pixelview::configName("obs-studio/global.ini") == "pixelview/obs-studio/global.ini");
 assert(pixelview::configName("obs-studio/basic/profiles") == "pixelview/obs-studio/basic/profiles");
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / 'test.cpp'
            binary = pathlib.Path(directory) / 'test'
            source.write_text(code)
            subprocess.run(['c++', '-std=c++17', '-I', str(ROOT), str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
        app = (ROOT / 'frontend/OBSApp.cpp').read_text()
        for function in ('int GetAppConfigPath(', 'char *GetAppConfigPathPtr('):
            body = app.split(function, 1)[1].split('\n}', 1)[0]
            self.assertIn('pixelview::configName(name)', body)


if __name__ == '__main__':
    unittest.main()
