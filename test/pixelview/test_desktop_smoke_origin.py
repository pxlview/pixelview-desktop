"""Compile actual smoke origin/auth/cleanup statements offline; never touch Keychain."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class DesktopSmokeOrigin(unittest.TestCase):
    def test_saved_account_is_loaded_and_cleaned_for_both_origin_spellings(self):
        smoke = (ROOT / 'test/pixelview/desktop_backend_smoke.mm').read_text()
        connection = (ROOT / 'frontend/utility/PixelviewDesktopConnection.hpp').read_text()
        origin = smoke[smoke.index(' QUrl origin('):smoke.index(' std::string input;')]
        pair = smoke[smoke.index(' client.pair('):smoke.index(' QTimer::singleShot(')]
        auth = smoke[smoke.index('  QUrl ws='):smoke.index('\n };', smoke.index('  QUrl ws='))]
        cleanup = smoke[smoke.index(' if(!keep &&'):smoke.index('\n}', smoke.index(' if(!keep &&'))]
        # Execute the real pair normalization, without its HTTP or credential I/O.
        normalize = connection[connection.index('  url.setPath("");'):connection.index('  exchanging=true;')]
        source = r'''
#include "frontend/utility/PixelviewDesktop.hpp"
#include <QtCore/QMap>
#include <iostream>
#include <string>
namespace pixelview {
QMap<QString, QString> devices;
QString saved, loaded, removed;
QString loadDevice(const QString &account) { loaded=account; return devices.value(account); }
bool removeDevice(const QString &account) { removed=account; devices.remove(account); return true; }
}
struct Client {
 QUrl origin; bool development=false;
 void pair(QUrl url,bool dev,QString code) {
  (void)code;
// NORMALIZE
  pixelview::saved=origin.toString();
  pixelview::devices.insert(pixelview::saved,"fixture-only");
 }
 void openSocket(QUrl,QString token) { authenticated=token=="fixture-only"; }
 bool authenticated=false;
};
Client client;
int run(char **argv,bool keep) {
// ORIGIN
 std::string input="fixture-only";
// PAIR
// AUTH
 int result=0;
// CLEANUP
}
int main() {
 for (const char *text : {"https://fixture.invalid", "https://fixture.invalid/"}) {
  for (bool keep : {false,true}) {
   pixelview::devices.clear(); pixelview::saved.clear(); pixelview::loaded.clear(); pixelview::removed.clear();
   char *argv[]={nullptr,const_cast<char *>(text)};
   if(run(argv,keep)!=0) return 10;
   if(pixelview::saved!="https://fixture.invalid") return 11;
   if(pixelview::loaded!=pixelview::saved || !client.authenticated) {
    std::cerr << "authentication account differs from saved account: " << text << '\n'; return 12;
   }
   if(!keep && (pixelview::removed!=pixelview::saved || !pixelview::devices.isEmpty())) {
    std::cerr << "cleanup did not remove saved account: " << text << '\n'; return 13;
   }
   if(keep && (!pixelview::removed.isEmpty() || !pixelview::devices.contains(pixelview::saved))) return 14;
  }
 }
 // Invalid paths must still be rejected BEFORE normalization and all credential I/O.
 for (const char *text : {"https://fixture.invalid/path", "https://fixture.invalid/?q=x",
                          "https://fixture.invalid/#x", "https://user@fixture.invalid/",
                          "http://fixture.invalid/"}) {
  pixelview::saved.clear(); pixelview::loaded.clear(); pixelview::removed.clear();
  char *argv[]={nullptr,const_cast<char *>(text)};
  if(run(argv,false)!=2 || !pixelview::saved.isEmpty() || !pixelview::loaded.isEmpty() || !pixelview::removed.isEmpty()) return 15;
 }
}
'''
        for marker, statements in [('NORMALIZE', normalize), ('ORIGIN', origin),
                                   ('PAIR', pair), ('AUTH', auth), ('CLEANUP', cleanup)]:
            source = source.replace('// ' + marker, statements)
        qt = ROOT / '.deps/obs-deps-qt6-2026-08-26-universal'
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / 'smoke-origin.cpp'
            src.write_text(source)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fPIC',
                            '-I' + str(ROOT), '-F' + str(qt / 'lib'), '-framework', 'QtCore',
                            '-Wl,-rpath,' + str(qt / 'lib'), str(src), '-o', tmp + '/test'], check=True)
            result = subprocess.run([tmp + '/test'], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
