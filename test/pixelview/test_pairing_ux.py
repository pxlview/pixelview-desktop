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
 bool pixelviewPairingDurable=false, nativeBusy=false;
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
        cleanup=body(text, 'FinishPixelviewUnpair').replace('pixelview::removeDevice', 'removeDevice')
        initialization=text[text.index(' auto heading='):text.index(' connect(unpair,')]
        code=r'''#include <QtWidgets/QApplication>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <cassert>
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
 struct {bool exchanging=false;void closeSocket(){}} desktop; auto *pixelviewDesktop=&desktop;
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
 assert(pair.isHidden() && !unpair.isHidden() && unpair.isEnabled() && heading.isHidden());
 assert(identity.text().contains("707880") && identity.text().contains("Offline"));
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
 assert(!unpair.isHidden() && unpair.isEnabled() && unpair.text().contains("Retry"));
 assert(!pair.isEnabled());
 pixelviewUnpairRetry=false; refresh(); assert(unpair.isHidden() && pair.isEnabled());
 // Execute the actual cleanup transition with fault-injected storage boundaries.
 struct Timer {void stop(){}} heartbeat; auto *pixelviewHeartbeat=&heartbeat;
 int pixelviewReconnectAt=1,pixelviewAuthDeadline=1;
 struct Origin {QString toString(){return "fixture.invalid";}} pixelviewOrigin;
 bool removed=false,saved=true; auto removeDevice=[&](QString){return removed;};
 struct AppStub {int GetUserConfig(){return 0;}} appStub; auto App=[&]{return &appStub;};
 auto config_set_bool=[](int,const char *,const char *,bool){};
 auto SavePixelviewIdentity=[&]{pixelviewPairingDurable=false;return saved;};
 auto RefreshPixelviewPairing=[&]{refresh();};
 auto finish=[&]{CLEANUP};
 for (int failure : {0,1,2}) {
  removed=failure!=0; saved=failure!=1; pixelviewUnpairPending=true;
  finish(); assert(!pixelviewUnpairPending && !pixelviewPairingDurable);
  assert(pixelviewUnpairRetry==(failure!=2));
  assert(unpair.isHidden()==(failure==2));
  assert(failure==2 || unpair.isEnabled());
 }

}'''.replace('GUARD',guard).replace('REFRESH',refresh).replace('INITIALIZATION',initialization).replace('CLEANUP',cleanup)
        with tempfile.TemporaryDirectory() as tmp:
            src=pathlib.Path(tmp)/'ui.cpp';src.write_text(code)
            qt=ROOT/'.deps/obs-deps-qt6-2026-08-26-universal/lib'
            subprocess.run(['clang++','-std=c++17','-F'+str(qt),'-framework','QtCore','-framework','QtGui','-framework','QtWidgets','-Wl,-rpath,'+str(qt),str(src),'-o',tmp+'/ui'],check=True)
            subprocess.run([tmp+'/ui'],check=True,env={**os.environ,'QT_QPA_PLATFORM':'offscreen'})
        pair=body(text,'PairPixelviewDesktop')
        self.assertGreaterEqual(pair.count('PixelviewPairingBusy()'),2, 'Guard before and after modal event loop')
        unpair=text.split('connect(unpair,',1)[1].split('});',1)[0]
        self.assertLess(unpair.index('PixelviewPairingBusy()'),unpair.index('pixelviewUnpairPending=true'))

if __name__ == '__main__': unittest.main()
