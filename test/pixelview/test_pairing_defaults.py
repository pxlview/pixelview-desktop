"""Execute the actual pairing dialog/submit path with offline transport/config."""
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class PairingDefaults(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        source = (ROOT / 'frontend/widgets/OBSBasic_PixelviewDesktop.inc').read_text()
        action = source.split(' QDialog dialog(this);', 1)[1].split('\n}\nvoid OBSBasic::ConnectPixelviewDesktop()', 1)[0]
        action = ' QDialog dialog(&parent);' + action.replace('connect(&buttons', 'QObject::connect(&buttons')
        code = r'''
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QDialogButtonBox>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <cassert>
#include "frontend/utility/PixelviewDesktop.hpp"
int main(int argc,char **argv) {
 QApplication app(argc,argv); QWidget parent;
 const QString mode=argv[1];
 QUrl pixelviewOrigin("https://saved.invalid"); bool pixelviewDev=true;
 bool pixelviewPairingDurable=false,pixelviewUnpairRetry=false;
 auto PixelviewPairingBusy=[]{return false;};
 struct App {int GetUserConfig(){return 0;}} application; auto App=[&]{return &application;};
 auto config_get_bool=[&](int,const char *,const char *){return mode!="guard";};
 QLabel status; auto *pixelviewConnectionStatus=&status;
 struct Transport {int pairs=0;QUrl origin;bool dev=false;QString code;
  void closeSocket(){} void pair(QUrl o,bool d,QString c){++pairs;origin=o;dev=d;code=c;}
 } transport; auto *pixelviewDesktop=&transport;
 struct {bool ready=false;} pixelviewLease;
 QTimer heartbeat; auto *pixelviewHeartbeat=&heartbeat;
 int pixelviewReconnectAt=0,pixelviewAuthDeadline=0;
 QTimer::singleShot(0,[&]{
  auto *dialog=qobject_cast<QDialog *>(app.activeModalWidget()); assert(dialog);
  QList<QLineEdit *> visible;
  for(auto *edit:dialog->findChildren<QLineEdit *>()) if(edit->isVisible()) visible.append(edit);
  const bool development=mode!="production" && mode!="cancel" && mode!="guard";
  assert(visible.size()==(development ? 2 : 1) && "Only explicit opt-in exposes backend overrides");
  QLineEdit *token=nullptr;
  for(auto *edit:visible) {
   if(edit->echoMode()==QLineEdit::Password) token=edit;
   else {
    assert(development); assert(edit->text()=="http://localhost:8000");
    if(mode=="override") edit->setText("https://override.invalid:8443");
    if(mode=="invalid") edit->setText("http://remote.invalid:8000");
   }
  }
  assert(token); token->setText("one-time-code");
  for(auto *check:dialog->findChildren<QCheckBox *>()) {
   assert(check->isVisible()==development); assert(check->isChecked()==development);
   if(mode=="override") check->setChecked(false);
  }
  if(mode=="cancel") dialog->reject(); else dialog->accept();
 });
 auto pair=[&]{ACTION}; pair();
 if(mode=="cancel" || mode=="guard") {
  assert(transport.pairs==0);
  if(mode=="guard") assert(status.text().contains("Unpair this installation"));
 } else {
  assert(transport.pairs==1);
  const QString expected=mode=="development" ? "http://localhost:8000" :
   mode=="override" ? "https://override.invalid:8443" :
   mode=="invalid" ? "http://remote.invalid:8000" : "https://api4.pixelview.io";
  assert(transport.origin==QUrl(expected));
  assert(transport.dev==(mode=="development" || mode=="invalid") && transport.code=="one-time-code");
  assert(pixelview::Desktop::validOrigin(transport.origin,transport.dev)==(mode!="invalid"));
 }
 assert(pixelviewOrigin==QUrl("https://saved.invalid") && pixelviewDev); // Dialog never rewrites saved credentials/settings.
}
'''.replace('ACTION', action)
        src = pathlib.Path(cls.tmp.name) / 'pairing.cpp'
        src.write_text(code)
        qt = ROOT / '.deps/obs-deps-qt6-2026-08-26-universal/lib'
        cls.binary = cls.tmp.name + '/pairing'
        subprocess.run(['clang++', '-std=c++17', '-I'+str(ROOT), '-F'+str(qt),
                        '-framework', 'QtCore', '-framework', 'QtGui', '-framework', 'QtWidgets',
                        '-Wl,-rpath,'+str(qt), str(src), '-o', cls.binary], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_explicit_development_shows_loopback_defaults(self):
        subprocess.run([self.binary, 'development'], check=True, timeout=10,
                       env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen', 'PIXELVIEW_LOCAL_DEVELOPMENT': '1'})

    def test_overrides_and_remote_http_validation(self):
        for mode in ('override', 'invalid'):
            with self.subTest(mode=mode):
                subprocess.run([self.binary, mode], check=True, timeout=10,
                               env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen', 'PIXELVIEW_LOCAL_DEVELOPMENT': '1'})

    def test_cancel_and_existing_origin_guard(self):
        for mode in ('cancel', 'guard'):
            with self.subTest(mode=mode):
                subprocess.run([self.binary, mode], check=True, timeout=10,
                               env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen', 'PIXELVIEW_LOCAL_DEVELOPMENT': '0'})

    def test_production_token_only_with_fixed_tls_origin(self):
        for value in (None, '', '0', 'true', 'yes', '01', '1 '):
            with self.subTest(value=value):
                env = {**os.environ, 'QT_QPA_PLATFORM': 'offscreen'}
                env.pop('PIXELVIEW_LOCAL_DEVELOPMENT', None)
                if value is not None:
                    env['PIXELVIEW_LOCAL_DEVELOPMENT'] = value
                subprocess.run([self.binary, 'production'], env=env, check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
