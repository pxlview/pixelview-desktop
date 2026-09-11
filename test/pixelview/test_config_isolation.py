import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class ConfigIsolation(unittest.TestCase):
    def test_explicit_root_routes_both_native_helpers_without_home_changes(self):
        app = (ROOT / 'frontend/OBSApp.cpp').read_text()
        functions = '\n'.join(signature + app.split(signature, 1)[1].split('\n}', 1)[0] + '\n}'
                              for signature in ('int GetAppConfigPath(', 'char *GetAppConfigPathPtr('))
        code = r'''
#include "frontend/utility/PixelviewConfig.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#define ALLOW_PORTABLE_MODE 0
int os_get_config_path(char *p,size_t n,const char *s) { return snprintf(p,n,"normal/%s",s); }
char *os_get_config_path_ptr(const char *s) { return strdup((std::string("normal/")+s).c_str()); }
char *bstrdup(const char *s) { return strdup(s); }
''' + functions + r'''
int main() {
 auto home=std::string(getenv("HOME"));
 assert(!pixelview::setConfigRoot("relative"));
 assert(!pixelview::setConfigRoot(""));
 assert(pixelview::setConfigRoot("/tmp/Pixelview sender/"));
 for(const char *name : {static_cast<const char *>(nullptr),"","obs-studio/logs","obs-studio/basic/profiles"}) {
  auto expected=std::string("/tmp/Pixelview sender")+(name && *name ? std::string("/")+name : "");
  char path[512]; assert(GetAppConfigPath(path,sizeof(path),name)==expected.size());
  assert(path==expected); auto ptr=GetAppConfigPathPtr(name); assert(ptr==expected); free(ptr);
 }
 char small[4]; assert(GetAppConfigPath(small,sizeof(small),nullptr)<0);
 assert(std::string(getenv("HOME"))==home);
 assert(pixelview::setConfigRoot("/tmp/receiver"));
 auto ptr=GetAppConfigPathPtr(nullptr); assert(std::string(ptr)=="/tmp/receiver"); free(ptr);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / 'test.cpp'
            binary = pathlib.Path(directory) / 'test'
            source.write_text(code)
            subprocess.run(['c++', '-std=c++17', '-I', str(ROOT), str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
        main = (ROOT / 'frontend/obs-main.cpp').read_text()
        self.assertIn('"--app-config-dir"', main)
        self.assertIn('pixelview::setConfigRoot(argv[i])', main)

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
