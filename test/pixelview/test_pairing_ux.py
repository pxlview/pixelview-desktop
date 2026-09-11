"""Compiled production UI gates; isolated boundaries, no credentials or app IO."""
import pathlib
import subprocess
import tempfile
import unittest
from test_stream_lock import body
ROOT = pathlib.Path(__file__).resolve().parents[2]
DESKTOP = ROOT / 'frontend/widgets/OBSBasic_PixelviewDesktop.inc'

class PairingUX(unittest.TestCase):
    def test_configuration_requires_durable_pairing_and_locks_retry_intent(self):
        text = DESKTOP.read_text()
        # Compile the production predicate, not a reimplementation of policy.
        predicate = body(text, 'PixelviewConfigurationLocked')
        code = '''#include <cassert>
int main() {
 bool pixelviewReceiving=false, pixelviewPairingDurable=false, nativeBusy=false;
 struct {bool intent=false;} pixelviewLease;
 auto PixelviewSettingsBusy=[&]{return nativeBusy;};
 auto locked=[&]{PREDICATE};
 assert(locked());
 pixelviewPairingDurable=true; assert(!locked());
 nativeBusy=true; assert(locked()); nativeBusy=false;
 pixelviewLease.intent=true; assert(locked());
 pixelviewLease.intent=false; assert(!locked());
 pixelviewPairingDurable=false; assert(locked());
}'''.replace('PREDICATE', predicate)
        with tempfile.TemporaryDirectory() as tmp:
            src=pathlib.Path(tmp)/'gate.cpp'; src.write_text(code)
            subprocess.run(['clang++','-std=c++17',str(src),'-o',tmp+'/gate'],check=True)
            subprocess.run([tmp+'/gate'],check=True)

    def test_startup_encoder_normalization_can_run_before_pairing(self):
        text=(ROOT/'frontend/widgets/OBSBasic_PixelviewEncoding.inc').read_text()
        init=body(text,'InitPixelviewEncoding')
        self.assertIn('SavePixelviewEncoding(saved.toUtf8().constData(), data, true)',init)
        self.assertIn('!initializing && PixelviewConfigurationLocked()',body(text,'SavePixelviewEncoding'))

    def test_redundant_capture_footer_is_not_installed(self):
        source=(ROOT/'frontend/widgets/OBSBasic.cpp').read_text()
        init=body(source,'InitPixelview')
        self.assertNotIn('addToolBar(Qt::BottomToolBarArea, footer)',init)
        self.assertNotIn('Device selected • Local preview',source)

    def test_configuration_and_durable_identity_wiring(self):
        for filename, methods in {
            'OBSBasic.cpp':['SelectPixelviewDevice','SelectPixelviewFPS','FitPixelviewCapture','RefreshPixelviewDevices','RefreshPixelviewFPS'],
            'OBSBasic_PixelviewEncoding.inc':['SavePixelviewEncoding','AdvancedPixelviewEncoding','RefreshPixelviewEncoding'],
            'OBSBasic_PixelviewAudio.inc':['SelectPixelviewMonitorDevice']}.items():
            text=(ROOT/'frontend/widgets'/filename).read_text()
            for method in methods:
                self.assertIn('PixelviewConfigurationLocked()',body(text,method),method)
        text=DESKTOP.read_text()
        ready=text.split('if(!wasReady && pixelviewLease.ready)',1)[1].split('pixelviewAuthDeadline=0',1)[0]
        self.assertIn('pixelviewPairingDurable=SavePixelviewIdentity()',ready)
        self.assertIn('if(!pixelviewPairingDurable)',ready)
        audio=(ROOT/'frontend/widgets/OBSBasic_PixelviewAudio.inc').read_text()
        self.assertIn('!pixelviewPairingDurable',body(audio,'ChangePixelviewAudio'))

    def test_actual_pairing_widgets_and_action_guards(self):
        import os
        text=DESKTOP.read_text()
        refresh=body(text, 'RefreshPixelviewPairing')
        guard=body(text, 'PixelviewPairingBusy')
        cleanup=body(text, 'FinishPixelviewUnpair').replace('pixelview::removeDevice', 'removeDevice').replace('(this,', '(&app,').replace('[this]', '[&]').replace('[origin]', '[&,origin]')
        initialization=text[text.index(' auto heading='):text.index(' connect(unpair,')]
        code=r'''#include <QtWidgets/QApplication>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <cassert>
#include "frontend/utility/PixelviewKeychainTask.hpp"
int main(int argc,char **argv) {
 QApplication app(argc,argv);
 // Yami maps both Mid and Window to grey7; Mid is not a text role.
 QPalette theme=app.palette();
 theme.setColor(QPalette::Window,QColor("#1D1F26"));
 theme.setColor(QPalette::Mid,theme.color(QPalette::Window));
 theme.setColor(QPalette::WindowText,Qt::white);
 app.setPalette(theme);
 bool pixelviewPairingDurable=false, pixelviewUnpairRetry=false, nativeBusy=false, closing=false;
 bool pixelviewActualStreaming=false;
 bool pixelviewShutdownPending=false,pixelviewUnpairPending=false,pixelviewClosingSocket=false,pixelviewStopPending=false;
 struct {bool intent=false,ready=false,pending=false,leased=false;} pixelviewLease;
 struct {QString authorizedToken;bool exchanging=false;void closeSocket(){}} desktop; auto *pixelviewDesktop=&desktop;
 struct Identity {QString nodeId="707880";void clear(){nodeId.clear();}} pixelviewIdentity,pixelviewExpectedIdentity;
 auto PixelviewSettingsBusy=[&]{return nativeBusy;};
 auto isClosing=[&]{return closing;};
 auto PixelviewPairingBusy=[&]{GUARD};
 QWidget sidebarWidget; auto *sidebar=&sidebarWidget; new QVBoxLayout(sidebar);
 QLabel *pixelviewPairingHeading=nullptr,*pixelviewIdentityStatus=nullptr;
 QPushButton *pixelviewUnpair=nullptr;
 { INITIALIZATION }
 auto &heading=*pixelviewPairingHeading; auto &identity=*pixelviewIdentityStatus; auto &unpair=*pixelviewUnpair;
 QLabel connection; QPushButton pair; auto *pixelviewPair=&pair;
 auto *pixelviewConnectionStatus=&connection;
 assert(identity.parentWidget() == unpair.parentWidget());
 assert(identity.parentWidget() != sidebar);
 auto *row=identity.parentWidget(); row->resize(308,36); row->layout()->setGeometry(row->rect());
 assert(identity.geometry().right() < unpair.geometry().left());
 assert(identity.geometry().center().y() == unpair.geometry().center().y());
 connection.show();
 auto refresh=[&]{REFRESH};
 refresh(); assert(!pair.isHidden() && pair.isEnabled()); assert(identity.text().contains("Pair first"));
 assert(unpair.isHidden());
 sidebar->layout()->addWidget(&pair); sidebar->layout()->addWidget(&connection);
 static_cast<QVBoxLayout *>(sidebar->layout())->addStretch();
 sidebar->resize(330,600); sidebar->show(); app.processEvents();
 auto assertReadableIdentity=[&]{
  app.processEvents();
  assert(identity.isVisible() && row->isVisible());
  assert(identity.width()>200 && identity.height()>=identity.heightForWidth(identity.width()));
  assert(row->height()<=identity.sizeHint().height()+8);
  const QImage rendered=identity.grab().toImage();
  const QColor background=theme.color(QPalette::Window);
  int readablePixels=0;
  for(int y=0;y<rendered.height();++y) for(int x=0;x<rendered.width();++x) {
   const QColor c=rendered.pixelColor(x,y);
   if(qAbs(c.red()-background.red())+qAbs(c.green()-background.green())+qAbs(c.blue()-background.blue())>180)
    ++readablePixels;
  }
  assert(readablePixels>20 && "Pairing guidance must paint readable text, not a blank gap");
 };
 assertReadableIdentity();
 pixelviewPairingDurable=true; refresh();
 assert(!pair.isHidden() && pair.text()=="Pair with Pixelview" && !unpair.isHidden() && unpair.isEnabled() && heading.isHidden());
 for(auto *button : sidebar->findChildren<QPushButton *>()) assert(!button->text().contains("Keychain"));
 assert(identity.text().contains("707880") && identity.text().contains("Offline"));
 desktop.authorizedToken="synthetic-secret"; refresh();
 assert(pair.isHidden() && !unpair.isHidden() && "Transient disconnect must not expose a pairing/authorization button");
 assert(!identity.styleSheet().contains("#"));
 pixelviewLease.ready=true; connection.setText("Connected · WHIP ready to request"); refresh();
 assert(connection.isHidden()); assert(identity.text().contains("Connected")); assert(identity.styleSheet().contains("#"));
 pixelviewLease.ready=false; pixelviewLease.intent=true; refresh();
 assert(identity.text().contains("Reconnecting") && !unpair.isEnabled());
 pixelviewLease.intent=false;
 for(bool *state : {&nativeBusy,&pixelviewStopPending,&pixelviewShutdownPending,&pixelviewClosingSocket,&pixelviewUnpairPending,&desktop.exchanging,&closing,&pixelviewLease.pending,&pixelviewLease.leased}) {
  *state=true; refresh(); assert(!unpair.isEnabled()); *state=false;
 }
 pixelviewPairingDurable=false; nativeBusy=true; refresh(); assert(!pair.isEnabled());
 nativeBusy=false; pixelviewUnpairRetry=true; refresh();
 assert(!unpair.isHidden() && unpair.isEnabled() && unpair.text()=="Unpair");
 assert(pair.isHidden() && !pair.isEnabled());
 assert(identity.text().contains("Unpair") && !identity.text().contains("Pair first"));
 pixelviewUnpairRetry=false; refresh(); assert(unpair.isHidden() && pair.isEnabled());
 // Execute the actual cleanup transition with fault-injected storage boundaries.
 struct Timer {void stop(){}} heartbeat; auto *pixelviewHeartbeat=&heartbeat;
 int pixelviewReconnectAt=1,pixelviewAuthDeadline=1;
 struct Origin {QString toString() const{return "fixture.invalid";}} pixelviewOrigin;
 QString credential="synthetic-secret"; int removals=0;
 bool removed=false,saved=true; auto removeDevice=[&](QString origin){
  assert(origin=="fixture.invalid"); ++removals;
  if(removed) credential.clear(); return removed;
 };
 struct AppStub {int GetUserConfig(){return 0;}} appStub; auto App=[&]{return &appStub;};
 auto config_set_bool=[](int,const char *,const char *,bool){};
 auto SavePixelviewIdentity=[&]{pixelviewPairingDurable=false;return saved;};
 auto RefreshPixelviewPairing=[&]{refresh();};
 auto finish=[&]{CLEANUP};
 for (int failure : {0,1,2}) {
  removed=failure!=0; saved=failure!=1; pixelviewUnpairPending=true;
  finish(); assert(pixelviewUnpairPending); assert(desktop.authorizedToken.isEmpty());
  while(pixelviewUnpairPending) app.processEvents();
  assert(!pixelviewUnpairPending && !pixelviewPairingDurable);
  assert(pixelviewUnpairRetry==(failure!=2));
  assert(unpair.isHidden()==(failure==2));
  assert(failure==2 || unpair.isEnabled());
  if(failure==0) {
   assert(pixelviewConnectionStatus->text().contains("Keychain removal failed"));
   assert(!pixelviewConnectionStatus->text().contains("Unlock Keychain"));
   assert(pixelviewConnectionStatus->text().contains("Unpair incomplete"));
   assert(pixelviewConnectionStatus->text().contains("click Unpair"));
   assert(credential=="synthetic-secret");
  }
  assert(!pixelviewConnectionStatus->text().contains("cleanup",Qt::CaseInsensitive));
  assert(failure==2 || (unpair.text()=="Unpair" && pair.isHidden()));
 }
 assert(removals==3 && credential.isEmpty() && pair.isEnabled() && !pair.isHidden());

}'''.replace('GUARD',guard).replace('REFRESH',refresh).replace('INITIALIZATION',initialization).replace('CLEANUP',cleanup)
        with tempfile.TemporaryDirectory() as tmp:
            src=pathlib.Path(tmp)/'ui.cpp';src.write_text(code)
            qt=ROOT/'.deps/obs-deps-qt6-2026-08-26-universal/lib'
            subprocess.run(['clang++','-std=c++17','-I'+str(ROOT),'-F'+str(qt),'-framework','QtCore','-framework','QtGui','-framework','QtWidgets','-Wl,-rpath,'+str(qt),str(src),'-o',tmp+'/ui'],check=True)
            subprocess.run([tmp+'/ui'],check=True,env={**os.environ,'QT_QPA_PLATFORM':'offscreen'})
        pair=body(text,'PairPixelviewDesktop')
        self.assertGreaterEqual(pair.count('PixelviewPairingBusy()'),2, 'Guard before and after modal event loop')
        unpair=text.split('connect(unpair,',1)[1].split('});',1)[0]
        self.assertLess(unpair.index('PixelviewPairingBusy()'),unpair.index('pixelviewUnpairPending=true'))

    def test_real_pair_completion_and_inaccessible_credential(self):
        # Compile real callback/method bodies; only storage, transport and config
        # are offline fixtures. No Security API calls or network are possible.
        import os
        text=DESKTOP.read_text()
        transport=(ROOT/'frontend/utility/PixelviewDesktopConnection.hpp').read_text()
        completion=transport.split('   if(!ok ||',1)[1].split('\n  });',1)[0]
        completion=('if(!ok ||'+completion).replace('(this,','(&app,').replace('[requestOrigin,token]','[&,requestOrigin,token]').replace('[this,object,token]','[&,object,token]')
        ready=text.split(' pixelviewDesktop->message=[this](QByteArray body){',1)[1].split('\n };',1)[0]
        connect_body=body(text,'ConnectPixelviewDesktop').replace('pixelview::loadDevice','loadDevice')
        code=r'''#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtCore/QJsonDocument>
#include <QtCore/QUrl>
#include <cassert>
#include "frontend/utility/PixelviewDesktop.hpp"
#include "frontend/utility/PixelviewKeychainTask.hpp"
using pixelview::runKeychainUserAction;
int main(int argc,char **argv) {
 QApplication app(argc,argv);
 QUrl requestOrigin("https://fixture.invalid");
 QString secret="existing-secret",statusText,authorizedToken; bool exchanging=false,storageOK=false; int pairedCalls=0;
 pixelview::DesktopIdentity identity;
 auto status=[&](QString s){statusText=s;}; auto paired=[&]{++pairedCalls;};
 auto saveDevice=[&](QString origin,QString token){
  assert(origin==requestOrigin.toString());
  if(!storageOK) return false; secret=token; return true;
 };
 auto complete=[&](QJsonObject object){bool ok=true; COMPLETION};
 const QJsonObject exchange{{"node_id","node"},{"desktop_id","desktop"},{"device_token","new-secret"}};
 complete(exchange); while(exchanging) app.processEvents();
 assert(pairedCalls==0 && secret=="existing-secret" && identity.nodeId.isEmpty());
 assert(statusText.contains("canceled or failed") && statusText.contains("not removed"));
 storageOK=true; complete(exchange); while(exchanging) app.processEvents();
 assert(pairedCalls==1 && secret=="new-secret" && identity.nodeId=="node");
 bool pixelviewShutdownPending=false,pixelviewStopPending=false,pixelviewClosingSocket=false,pixelviewUnpairPending=false;
 bool pixelviewPairingDurable=false,pixelviewUnpairRetry=false,pixelviewDev=false;
 auto isClosing=[]{return false;};
 pixelview::DesktopIdentity pixelviewIdentity,pixelviewExpectedIdentity=identity;
 pixelview::Desktop pixelviewLease;
 QLabel label; auto *pixelviewConnectionStatus=&label;
 pixelviewLease.error=[&](QString s){label.setText(s);};
 struct Clock {qint64 elapsed(){return 100;}} pixelviewClock;
 int pixelviewAuthDeadline=0,pixelviewReconnectAt=0,pixelviewBackoff=0;
 struct Timer {bool active=false;void start(){active=true;}} timer; auto *pixelviewHeartbeat=&timer;
 int saves=0; bool configOK=true;
 auto SavePixelviewIdentity=[&]{++saves;return configOK;};
 auto RefreshPixelviewReconnect=[]{};
 auto receive=[&](QByteArray body){READY};
 const QByteArray valid=R"({"type":"ready","node_id":"node","desktop_id":"desktop","heartbeat_interval":15,"lease_seconds":45})";
 receive(valid);
 assert(pixelviewPairingDurable && pixelviewLease.ready && saves==1);
 // A later inaccessible read must not erase the established identity or record.
 bool inaccessible=true,disabled=false; int opens=0;
 auto loadDevice=[&](QString origin){assert(origin==requestOrigin.toString());return inaccessible ? QString{} : secret;};
 struct AppStub {int GetUserConfig(){return 0;}} appStub; auto App=[&]{return &appStub;};
 auto config_get_bool=[&](int,const char *,const char *){return disabled;};
 QUrl pixelviewOrigin=requestOrigin;
 struct Transport {int *opens;QString authorizedToken;bool exchanging=false;void openSocket(QUrl,QString token){assert(token=="new-secret");++*opens;}} client{&opens};
 auto *pixelviewDesktop=&client;
 auto connect=[&]{CONNECT};
 connect();
 assert(opens==0 && saves==1 && pixelviewPairingDurable && pixelviewIdentity.nodeId=="node" && secret=="new-secret");
 assert(label.text().contains("saved pairing has not been removed"));
 assert(pixelviewLease.transientFailure && "Unavailable background reads must remain retryable without native prompts");
 inaccessible=false; connect(); assert(opens==1 && pixelviewAuthDeadline>0);
 // A transient reconnect reuses the process-held credential even if storage
 // becomes unavailable; reconnect must not consume the authorization result.
 inaccessible=true; connect(); assert(opens==2 && client.authorizedToken==secret);
 client.exchanging=true; connect(); assert(opens==2 && client.authorizedToken==secret);
 client.exchanging=false;
 // A failed durable identity write cannot advertise a successful pairing.
 pixelviewLease.ready=false; configOK=false; receive(valid);
 assert(!pixelviewPairingDurable && pixelviewUnpairRetry && !pixelviewLease.ready);
 assert(label.text().contains("Click Unpair") && !label.text().contains("cleanup"));
}'''.replace('COMPLETION',completion).replace('READY',ready).replace('CONNECT',connect_body)
        with tempfile.TemporaryDirectory() as tmp:
            src=pathlib.Path(tmp)/'completion.cpp'; src.write_text(code)
            qt=ROOT/'.deps/obs-deps-qt6-2026-08-26-universal/lib'
            subprocess.run(['clang++','-std=c++17','-I'+str(ROOT),'-F'+str(qt),'-framework','QtCore','-framework','QtGui','-framework','QtWidgets','-Wl,-rpath,'+str(qt),str(src),'-o',tmp+'/completion'],check=True)
            subprocess.run([tmp+'/completion'],check=True,env={**os.environ,'QT_QPA_PLATFORM':'offscreen'})

    def test_explicit_sender_reauthorization_preserves_identity_and_retries(self):
        code = r'''#include <QtCore/QCoreApplication>
#include <QtCore/QUrl>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <cassert>
#include "frontend/utility/PixelviewKeychainTask.hpp"
int main(int argc,char **argv) {
 QApplication app(argc,argv);
 bool busy=false, pixelviewPairingDurable=true, pixelviewUnpairRetry=false;
 bool closing=false,pixelviewShutdownPending=false,denied=true;
 const QUrl pixelviewOrigin("https://fixture.invalid");
 struct {bool exchanging=false;QString authorizedToken;} desktop;
 auto *pixelviewDesktop=&desktop;
 QLabel label; auto *pixelviewConnectionStatus=&label;
 int connects=0,reads=0;
 auto PixelviewPairingBusy=[&]{return busy || desktop.exchanging;};
 auto isClosing=[&]{return closing;};
 auto RefreshPixelviewPairing=[]{};
 auto ConnectPixelviewDesktop=[&]{++connects; assert(desktop.authorizedToken=="retained-secret");};
 auto loadDevice=[&](QString origin) {
  assert(pixelview::KeychainUserAction::requested());
  assert(origin==pixelviewOrigin.toString()); ++reads;
  return denied ? QString() : QString("retained-secret");
 };
 auto click=[&]{ACTION};
 busy=true; click(); assert(reads==0); busy=false;
 click(); assert(desktop.exchanging); click();
 while(desktop.exchanging) app.processEvents();
 assert(reads==1 && connects==0 && pixelviewPairingDurable);
 assert(label.text().contains("not been removed"));
 denied=false; click(); while(desktop.exchanging) app.processEvents();
 assert(reads==2 && connects==1 && pixelviewPairingDurable);
 click(); pixelviewShutdownPending=true;
 while(desktop.exchanging) app.processEvents();
 assert(connects==1);
}
'''
        action=body(DESKTOP.read_text(),'PairPixelviewDesktop').split(' QDialog dialog',1)[0]
        action=action.replace('(this,','(&app,').replace('[this,origin]','[&,origin]').replace('[origin]','[&,origin]').replace('pixelview::loadDevice','loadDevice')
        code=code.replace('ACTION',action)
        import os
        with tempfile.TemporaryDirectory() as tmp:
            src=pathlib.Path(tmp)/'reauthorize.cpp'; src.write_text(code)
            qt=ROOT/'.deps/obs-deps-qt6-2026-08-26-universal/lib'
            subprocess.run(['clang++','-std=c++17','-I'+str(ROOT),'-F'+str(qt),'-framework','QtCore','-framework','QtGui','-framework','QtWidgets','-Wl,-rpath,'+str(qt),str(src),'-o',tmp+'/test'],check=True)
            subprocess.run([tmp+'/test'],check=True,timeout=10,env={**os.environ,'QT_QPA_PLATFORM':'offscreen'})

    def test_no_user_facing_cleanup_workflow(self):
        import re
        for path in [DESKTOP, ROOT/'frontend/utility/PixelviewDesktopConnection.hpp']:
            strings=re.findall(r'"([^"\n]*)"',path.read_text())
            self.assertFalse(any('cleanup' in s.lower() and ' ' in s for s in strings))
            self.assertFalse(any('after unlocking' in s for s in strings))

if __name__ == '__main__': unittest.main()
