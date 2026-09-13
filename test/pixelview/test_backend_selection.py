"""Execute production receiver origin/configuration against the real Qt controller."""
import pathlib
import subprocess
import tempfile
import unittest
from test_stream_lock import body

ROOT = pathlib.Path(__file__).resolve().parents[2]


class BackendSelection(unittest.TestCase):
    def test_receiver_backend_and_permission_are_independent_of_pairing(self):
        source = (ROOT / 'frontend/widgets/OBSBasic_PixelviewReceive.inc').read_text()
        configure = body(source, 'StartPixelviewReceive').split('pixelviewReceiver->setOrigin(', 1)[1].split(')) {', 1)[0]

        code = r'''
#include <QtCore/QCoreApplication>
#include <QtCore/QJsonDocument>
#include <cassert>
#include "frontend/utility/PixelviewReceiver.hpp"
#include "frontend/utility/PixelviewBackend.hpp"
struct Transport : pixelview::ReceiverTransport {
 QUrl loginUrl; QByteArray payload; Events events; QUrl socketUrl;
 void login(const QUrl &url,const QByteArray &data,Events e) override {loginUrl=url;payload=data;events=e;}
 void open(const QUrl &url) override {socketUrl=url;}
 void send(const QByteArray &) override {} void cancel() override {}
};
namespace pixelview {std::unique_ptr<ReceiverTransport> makeReceiverTransport(){return {};}}
struct OBSBasic {
 QUrl pixelviewOrigin; bool pixelviewDev=false;
 pixelview::PixelviewReceiver *pixelviewReceiver;
 QString PixelviewReceiveOrigin() const {ORIGIN}
 bool configure() {return pixelviewReceiver->setOrigin(CONFIGURE);}
};
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);
 for(const QByteArray value : {QByteArray(), QByteArray(""), QByteArray("0"), QByteArray("true"), QByteArray("01"), QByteArray("1 "), QByteArray("1")}) {
  if(value.isNull()) qunsetenv("PIXELVIEW_LOCAL_DEVELOPMENT"); else qputenv("PIXELVIEW_LOCAL_DEVELOPMENT",value);
  const bool local=value=="1";
  const QUrl expected(local ? "http://localhost:8000" : "https://api4.pixelview.io");
  for(const char *saved : {"", "http://localhost:9000", "https://saved.invalid", "http://remote.invalid"}) for(bool dev : {false,true}) {
   auto transport=std::make_unique<Transport>(); auto *wire=transport.get();
   pixelview::PixelviewReceiver receiver(nullptr,std::move(transport));
   OBSBasic window{QUrl(saved),dev,&receiver};
   assert(QUrl(window.PixelviewReceiveOrigin())==expected && "Receiving must ignore the sending pairing origin");
   assert(window.configure() && "Loopback opt-in must not inherit saved sending permissions");
   receiver.start("123456","receiver-secret","Receiver");
   QUrl login=expected; login.setPath("/login/player");
   assert(wire->loginUrl==login && wire->payload.contains("receiver-secret"));
   assert(window.pixelviewOrigin==QUrl(saved) && window.pixelviewDev==dev);
   // Saved sending permissions must not admit cleartext receive media.
   wire->events.login(200,R"({"player":"WHEP","client_token":"viewer-token","stream_url":"http://localhost:8000/whep"})");
   assert((receiver.state()==pixelview::PixelviewReceiver::State::Registering)==local);
   if(local) {
    QUrl socket=expected; socket.setScheme("ws"); socket.setPath("/wsocket"); socket.setQuery("token=viewer-token");
    assert(wire->socketUrl==socket);
   }
   // The controller must keep rejecting remote cleartext even in local mode.
   receiver.stop(); assert(!receiver.setOrigin(QUrl("http://remote.invalid"),local));
   assert(receiver.setOrigin(QUrl("http://localhost:8000"),local)==local);
  }
 }
}
'''.replace('ORIGIN', body(source.replace('QString OBSBasic::PixelviewReceiveOrigin() const', 'bool OBSBasic::PixelviewReceiveOrigin() const'), 'PixelviewReceiveOrigin')).replace('CONFIGURE', configure)
        qt = next((ROOT / '.deps').glob('obs-deps-qt*/lib/QtCore.framework')).parent
        with tempfile.TemporaryDirectory() as td:
            src = pathlib.Path(td) / 'selection.cpp'
            src.write_text(code)
            exe = pathlib.Path(td) / 'selection'
            subprocess.run(['clang++', '-std=c++17', '-I'+str(ROOT), '-F'+str(qt), '-framework', 'QtCore', '-Wl,-rpath,'+str(qt), str(src), str(ROOT/'frontend/utility/PixelviewReceiver.cpp'), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
