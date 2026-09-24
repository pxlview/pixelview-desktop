"""Pairing failures are diagnosable from the log without ever logging the code."""
import http.server
import pathlib
import subprocess
import tempfile
import threading
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
QT = ROOT / '.deps/obs-deps-qt6-2026-08-26-universal/lib'


class PairingDiagnostics(unittest.TestCase):
    def test_backend_detail_never_echoes_the_code_and_messages_name_the_cause(self):
        code = r'''#include "frontend/utility/PixelviewDesktopConnection.hpp"
#include <cassert>
using pixelview::DesktopConnection;
int main() {
 // FastAPI echoes the submitted value under "input"; only loc/msg may survive.
 const auto validation=QJsonDocument::fromJson(R"({"detail":[{"type":"string_too_short","loc":["body","pairing_token"],
  "msg":"String should have at least 6 characters","input":"K7QM3"}]})").object();
 const QString detail=DesktopConnection::exchangeDetail(validation);
 assert(detail=="body.pairing_token: String should have at least 6 characters");
 assert(!detail.contains("K7QM3"));
 assert(DesktopConnection::exchangeDetail(QJsonDocument::fromJson(R"({"detail":"Invalid or expired pairing token"})").object())=="Invalid or expired pairing token");
 assert(DesktopConnection::exchangeDetail({}).isEmpty());
 assert(DesktopConnection::exchangeDetail({{"detail",QString(500,'x')}}).size()==200);
 assert(DesktopConnection::exchangeMessage(0,"TLS initialization failed").contains("TLS initialization failed"));
 assert(DesktopConnection::exchangeMessage(401,"x").contains("invalid, expired or already used"));
 assert(DesktopConnection::exchangeMessage(422,"x").contains("invalid, expired or already used"));
 assert(DesktopConnection::exchangeMessage(429,"x").contains("too many attempts"));
 assert(DesktopConnection::exchangeMessage(503,"x").contains("HTTP 503"));
}'''
        with tempfile.TemporaryDirectory() as tmp:
            src = pathlib.Path(tmp) / 'diagnostics.cpp'
            src.write_text(code)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT), '-F' + str(QT), '-framework', 'QtCore',
                            '-framework', 'QtNetwork', '-Wl,-rpath,' + str(QT), str(src), '-o', tmp + '/test'], check=True)
            subprocess.run([tmp + '/test'], check=True, timeout=10)

    def test_exchange_logs_outcomes_but_never_the_code_or_token(self):
        text = (ROOT / 'frontend/utility/PixelviewDesktopConnection.hpp').read_text()
        logs = [line for line in text.splitlines() if 'log(' in line or line.lstrip().startswith('.arg(')]
        self.assertTrue(any('pairing exchange failed' in line for line in logs))
        for line in logs:
            self.assertNotIn('object["device_token"]', line)
            self.assertNotIn('arg(code)', line)
            self.assertNotIn('arg(token', line)

    def test_unpair_removes_the_device_from_the_account_with_its_own_token(self):
        # Real QNetworkAccessManager against a loopback server: the Desktop sends
        # DELETE /desktop/device with its device token, and 200/401/404 all mean
        # the account no longer lists it; anything else is reported as not removed.
        requests = []

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_DELETE(self):
                requests.append((self.path, self.headers.get('Authorization')))
                status = int(self.headers.get('Authorization', '').rsplit('-', 1)[-1])
                self.send_response(status)
                self.send_header('Content-Length', '0')
                self.end_headers()

            def log_message(self, *args):
                pass

        code = r'''#include "frontend/utility/PixelviewDesktopConnection.hpp"
#include <QtCore/QCoreApplication>
#include <cassert>
#include <cstdio>
// The native control socket is not part of this test.
void pixelview::DesktopConnection::closeSocket(bool) {}
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);
 pixelview::DesktopConnection connection;
 QStringList logged; connection.log=[&](QString line){logged<<line;};
 const QUrl origin(argv[1]);
 auto removed=[&](QUrl url,QString token){
  int result=-1;
  connection.unregister(url,token,[&](bool ok){result=ok;});
  while(result<0) app.processEvents(QEventLoop::WaitForMoreEvents);
  return result==1;
 };
 assert(removed(origin,"token-200"));
 assert(removed(origin,"token-401"));
 assert(removed(origin,"token-404"));
 assert(!removed(origin,"token-500"));
 assert(!removed(QUrl(argv[2]),"token-200")); // nothing listening
 for(const QString &line : logged) assert(!line.contains("token-"));
 std::puts("unregister PASS");
}'''
        with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
            threading.Thread(target=server.serve_forever, daemon=True).start()
            with tempfile.TemporaryDirectory() as tmp:
                src = pathlib.Path(tmp) / 'unregister.cpp'
                src.write_text(code)
                subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT), '-F' + str(QT), '-framework', 'QtCore',
                                '-framework', 'QtNetwork', '-Wl,-rpath,' + str(QT), str(src), '-o', tmp + '/test'], check=True)
                closed = http.server.HTTPServer(('127.0.0.1', 0), Handler)
                closed_port = closed.server_address[1]
                closed.server_close()
                subprocess.run([tmp + '/test', 'http://127.0.0.1:%d' % server.server_address[1], 'http://127.0.0.1:%d' % closed_port],
                               check=True, timeout=30)
            server.shutdown()
        self.assertEqual([path for path, _ in requests], ['/desktop/device'] * 4)
        self.assertEqual([auth for _, auth in requests], ['Bearer token-%d' % s for s in (200, 401, 404, 500)])

    def test_https_pairing_has_a_bundled_tls_backend(self):
        # The exchange is the app's only Qt HTTPS client; upstream's plugin list has no TLS backend.
        helpers = (ROOT / 'cmake/common/helpers_common.cmake').read_text()
        self.assertIn('list(APPEND qt_plugins_Network tls)', helpers)
        self.assertIn('Qt::Network', (ROOT / 'frontend/cmake/ui-qt.cmake').read_text())
        verify = (ROOT / 'cmake/macos/pixelview-signed-development.py').read_text()
        self.assertIn('Contents/PlugIns/tls/libqsecuretransportbackend.dylib', verify)


if __name__ == '__main__':
    unittest.main()
