"""Compile production mode widget refresh with real Qt; source boundary checks."""
import os
import pathlib
import subprocess
import tempfile
import unittest
import uuid
from test_stream_lock import body
ROOT = pathlib.Path(__file__).resolve().parents[2]
INC = ROOT / 'frontend/widgets/OBSBasic_PixelviewReceive.inc'

class ReceiveUI(unittest.TestCase):
    def test_compact_native_receive_layout(self):
        self._run_native_receive_ui(layout_only=True)

    def test_full_native_receive_ui(self):
        self._run_native_receive_ui()

    def test_offline_native_receive_lifecycle(self):
        self._run_native_receive_ui(offline=True)

    def test_native422_preview_telemetry(self):
        self._run_native_receive_ui(offline=True, layout_only=True)

    def test_unavailable_credential_preserved_until_explicit_replacement(self):
        self._run_native_receive_ui(offline=True, credential_error=True)

    def _run_native_receive_ui(self, layout_only=False, offline=False, credential_error=False):
        header=(ROOT/'frontend/widgets/OBSBasic.hpp').read_text()
        declarations=header.split('void InitPixelviewReceive',1)[1].split('void ShowPixelviewLicense',1)[0]
        code=(ROOT/'test/pixelview/receive_ui_native.cpp.in').read_text().replace('// DECLARATIONS', 'void InitPixelviewReceive'+declarations)
        main=(ROOT/'frontend/widgets/OBSBasic.cpp').read_text()
        shell=body(main, 'InitPixelview')
        shell=shell[shell.index('auto *bar = new QToolBar'):shell.index('pixelviewFPS = new QComboBox')]
        transport=body(main, 'InitPixelviewStreaming')
        transport=transport[transport.index('auto *separator = new QFrame'):transport.index('// Keep OBSBasicStatusBar')]
        code=code.replace('// SIDEBAR SETUP', shell + '\nsidebarLayout->addStretch(1);\n' + transport + '\nInitPixelviewReceive(sharedSidebar);')
        qt=next((ROOT/'.deps').glob('obs-deps-qt*/lib/QtWidgets.framework')).parent
        deps=next((ROOT/'.deps').glob('obs-deps-20*/lib'))
        frameworks=ROOT/'build_macos/libobs/RelWithDebInfo'
        with tempfile.TemporaryDirectory() as td:
            src=pathlib.Path(td)/'native.cpp'; src.write_text(code); exe=pathlib.Path(td)/'native'
            subprocess.run(['clang++','-std=c++17','-fPIC','-I'+str(ROOT),'-I'+str(ROOT/'frontend'),'-I'+str(ROOT/'libobs'),'-I'+str(ROOT/'build_macos/config'),'-I'+str(ROOT/'build_macos/libobs'),'-I'+str(deps.parent/'include'),'-I'+str(qt/'QtWidgets.framework/Headers'),'-I'+str(qt/'QtCore.framework/Headers'),'-I'+str(qt/'QtGui.framework/Headers'),'-F'+str(qt),'-F'+str(frameworks),'-framework','QtWidgets','-framework','QtGui','-framework','QtCore','-framework','libobs','-Wl,-rpath,'+str(qt),'-Wl,-rpath,'+str(frameworks),'-Wl,-rpath,'+str(deps),str(src),str(ROOT/'frontend/utility/PixelviewReceiver.cpp'),str(ROOT/'frontend/utility/PixelviewReceiveCredentialStoreMac.mm'),'-fobjc-arc','-framework','Security','-framework','Foundation','-framework','LocalAuthentication','-o',str(exe)],check=True)
            service = 'com.pixelview.test.receiver.' + str(uuid.uuid4())
            if offline:
                run = subprocess.run([str(exe),td+'/offline.ini',service] + (['credential-error'] if credential_error else ['layout'] if layout_only else []),env={**os.environ,'QT_QPA_PLATFORM':'offscreen','PIXELVIEW_RECEIVE_OFFLINE':'1'},timeout=30,capture_output=True,text=True)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                return
            try:
                layout = subprocess.run([str(exe),td+'/layout.ini',service,'layout'],env={**os.environ,'QT_QPA_PLATFORM':'offscreen'},timeout=30,capture_output=True,text=True)
                self.assertEqual(layout.returncode, 0, layout.stdout + layout.stderr)
                if layout_only:
                    return
                run = subprocess.run([str(exe),td+'/receiver.ini',service],env={**os.environ,'QT_QPA_PLATFORM':'offscreen'},timeout=30,capture_output=True,text=True)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                restarts = []
                for mode in ('restore', 'other-origin', 'missing', 'denied', 'corrupt'):
                    if mode in ('denied', 'corrupt'):
                        # Only the corrupt-data fixture trusts the reader. The denied
                        # fixture deliberately belongs to security, exercising the
                        # legacy ACL path that must fail without an access dialog.
                        trusted = ['-T', str(exe)] if mode == 'corrupt' else []
                        # Recreate rather than updating the previous fixture's ACL.
                        deleted = subprocess.run(['security','delete-generic-password','-s',service,'-a','latest-session'],capture_output=True,timeout=10)
                        self.assertIn(deleted.returncode, (0, 44))
                        subprocess.run(['security','add-generic-password','-s',service,'-a','latest-session','-w','invalid-fixture',*trusted],check=True,capture_output=True,timeout=10)
                    restarted = subprocess.run([str(exe),td+'/receiver.ini',service,mode],env={**os.environ,'QT_QPA_PLATFORM':'offscreen'},timeout=30,capture_output=True,text=True)
                    self.assertEqual(restarted.returncode, 0, restarted.stdout + restarted.stderr)
                    restarts.append(restarted.stdout + restarted.stderr)
            finally:
                subprocess.run(['security','delete-generic-password','-s',service,'-a','latest-session'],capture_output=True,timeout=10)
                absent = subprocess.run(['security','find-generic-password','-s',service,'-a','latest-session'],capture_output=True,timeout=10)
                self.assertEqual(absent.returncode, 44, 'Isolated receive Keychain fixture must be absent after cleanup')
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            for secret in ('policy-password', 'never-persist-me', 'corrected-password', 'päss', 'cMOkc3M', 'edited-password', 'failed-password', 'restart-password'):
                self.assertNotIn(secret, run.stdout + run.stderr + ''.join(restarts))
                for saved in pathlib.Path(td).glob('receiver.ini*'):
                    self.assertNotIn(secret.encode(), saved.read_bytes())

    def test_native_receive_credentials_lifecycle_and_shared_outputs(self):
        self.assertTrue(INC.exists())
        text = INC.read_text()
        self.assertIn('void OBSBasic::InitPixelviewReceive', text)
        init = body(text, 'InitPixelviewReceive')
        self.assertIn('QLineEdit::Password', init)
        self.assertIn('QSysInfo::machineHostName()', init)
        self.assertNotRegex(init, r'calldata_set_\w+\([^;]*"latency"')
        self.assertNotIn('->start(', init)
        start = body(text, 'StartPixelviewReceive')
        self.assertLess(start.index('PixelviewModeBusy()'), start.index('->start('))
        self.assertNotIn('pixelviewReceivePassword->clear()', start)
        self.assertNotRegex(text, r'config_set_string\([^;]*(?:Password|Endpoint|endpoint|password)')
        self.assertIn('obs_source_create_private("pixelview_whep_source"', text)
        self.assertIn('obs_scene_create_private', text)
        self.assertIn('proc_handler_call', text)
        switch = body(text, 'SelectPixelviewMode')
        self.assertLess(switch.index('PixelviewModeBusy()'), switch.index('pixelviewReceiving ='))
        self.assertIn('deactivate_when_not_showing', switch)
        self.assertIn('obs_set_output_source', switch)
        self.assertIn('ClearPixelviewAudio()', switch)
        audio=(ROOT/'frontend/widgets/OBSBasic_PixelviewAudio.inc').read_text()
        self.assertIn('PixelviewManagedSource()', audio)
        self.assertIn('source == pixelviewReceiveSource', body(audio, 'SavePixelviewAudioSource'))
        main=(ROOT/'frontend/widgets/OBSBasic.cpp').read_text()
        # Receive teardown now starts in the close-event gate shared with closeWindow.
        self.assertIn('StopPixelviewReceive()', body(main, 'PixelviewShutdownReady'))
        self.assertIn('PixelviewShutdownReady()', body(main, 'closeWindow'))
        self.assertIn('StopPixelviewReceive()', body(main.replace(' noexcept', ''), 'applicationShutdown'))
        self.assertIn('AddProjectorMenuMonitors', init)
        self.assertIn('OpenPreviewProjector', init)
        self.assertIn('strcmp(state, "ended")', init)
        self.assertIn('obs_module_get_locale_text', init)
        self.assertNotIn('new OBSQTDisplay', text)

    def test_shared_monitor_device_receive_and_native_busy_guards(self):
        audio=(ROOT/'frontend/widgets/OBSBasic_PixelviewAudio.inc').read_text()
        refresh=body(audio, 'RefreshPixelviewAudio')
        enabled=refresh.split('pixelviewMonitorDevice->setEnabled(',1)[1].split(';',1)[0][:-1]
        handler=body(audio, 'SelectPixelviewMonitorDevice')
        guard=handler.split('if ((PixelviewConfigurationLocked()',1)[1].split(' {',1)[0]
        guard='(PixelviewConfigurationLocked()'+guard[:-1]
        configuration=body((ROOT/'frontend/widgets/OBSBasic_PixelviewDesktop.inc').read_text(), 'PixelviewConfigurationLocked')
        decklink=body(INC.read_text(), 'InitPixelviewReceive').split('connect(decklink,',1)[1].split('[this] {',1)[1].split('auto *module',1)[0]
        code=r'''#include <cassert>
int main() {
 bool pixelviewReceiving=false, pixelviewPairingDurable=false, nativeBusy=false;
 bool pixelviewShutdownPending=false, pixelviewAudioShuttingDown=false, closing=false, available=true;
 auto isClosing=[&]{return closing;};
 auto PixelviewSettingsBusy=[&]{return nativeBusy;};
 struct {bool intent=false;} pixelviewLease;
 auto PixelviewConfigurationLocked=[&]{ CONFIGURATION };
 auto obs_audio_monitoring_available=[&]{return available;};
 auto enabled=[&]{return ENABLED;};
 auto blocked=[&]{return GUARD;};
 assert(!enabled() && blocked());
 pixelviewReceiving=true; assert(enabled() && !blocked());
 nativeBusy=true; assert(!enabled() && blocked()); nativeBusy=false;
 for(bool *flag : {&pixelviewShutdownPending,&pixelviewAudioShuttingDown,&closing}) {
  *flag=true; assert(!enabled() && blocked()); *flag=false;
 }
 available=false; assert(!enabled() && blocked()); available=true;
 pixelviewReceiving=false; pixelviewPairingDurable=true; assert(enabled() && !blocked());
 int opened=0; auto openDecklink=[&]{ DECKLINK ++opened; };
 nativeBusy=true; openDecklink(); assert(opened==1); // Live playout settings remain usable.
 pixelviewShutdownPending=true; openDecklink(); assert(opened==1);
 pixelviewShutdownPending=false; closing=true; openDecklink(); assert(opened==1);
}
'''.replace('#include <cassert>', '#include <cassert>\n#include <initializer_list>').replace('ENABLED', enabled).replace('GUARD', guard).replace('DECKLINK', decklink).replace('CONFIGURATION', configuration)
        with tempfile.TemporaryDirectory() as td:
            src=pathlib.Path(td)/'guards.cpp'; src.write_text(code); exe=pathlib.Path(td)/'guards'
            subprocess.run(['clang++','-std=c++17',str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_actual_mode_widgets_and_busy_lock(self):
        self.assertTrue(INC.exists(), 'Native receive UI is missing')
        text = INC.read_text()
        refresh = body(text, 'RefreshPixelviewModes')
        qt = next((ROOT / '.deps').glob('obs-deps-qt*/lib/QtWidgets.framework')).parent
        code = '''#include <QtWidgets/QtWidgets>
#include <cassert>
class OBSBasic { public:
bool receive=false, busy=false, closing=false;
QTabBar tabs; QWidget sending, receiving; QLineEdit id, password, name; QPushButton start;
QTabBar *pixelviewModeTabs=&tabs;
QWidget *pixelviewSendingPanel=&sending, *pixelviewReceivingPanel=&receiving;
QLineEdit *pixelviewReceiveId=&id, *pixelviewReceivePassword=&password, *pixelviewReceiveName=&name;
QPushButton *pixelviewReceiveButton=&start;
bool pixelviewReceiving=false, pixelviewReceiveIntent=false;
bool PixelviewModeBusy() const { return busy; }
bool isClosing() const { return closing; }
bool pixelviewShutdownPending=false;
void RefreshPixelviewModes() { BODY }
};
int main(int argc,char **argv) { QApplication app(argc,argv); OBSBasic w;
w.tabs.addTab("Sending"); w.tabs.addTab("Receiving");
w.RefreshPixelviewModes(); assert(!w.sending.isHidden() && w.receiving.isHidden());
w.pixelviewReceiving=true; w.RefreshPixelviewModes(); assert(w.sending.isHidden() && !w.receiving.isHidden());
assert(w.tabs.currentIndex()==1 && w.tabs.isEnabled() && w.id.isEnabled());
w.busy=true; w.pixelviewReceiveIntent=true; w.RefreshPixelviewModes();
assert(!w.tabs.isEnabled() && !w.id.isEnabled() && !w.password.isEnabled() && !w.name.isEnabled());
assert(w.start.isEnabled() && w.start.text()=="Stop receiving");
w.closing=true; w.RefreshPixelviewModes(); assert(!w.start.isEnabled());
w.closing=false; w.busy=false; w.pixelviewReceiveIntent=false; w.RefreshPixelviewModes();
assert(w.start.text()=="Start receiving" && w.id.isEnabled());
}'''.replace('BODY', refresh)
        with tempfile.TemporaryDirectory() as td:
            src=pathlib.Path(td)/'ui.cpp'; src.write_text(code); binary=pathlib.Path(td)/'ui'
            subprocess.run(['clang++','-std=c++17','-fPIC',str(src),'-F'+str(qt),'-I'+str(qt/'QtWidgets.framework/Headers'),'-I'+str(qt/'QtCore.framework/Headers'),'-I'+str(qt/'QtGui.framework/Headers'),'-framework','QtWidgets','-framework','QtGui','-framework','QtCore','-Wl,-rpath,'+str(qt),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],env={**os.environ,'QT_QPA_PLATFORM':'offscreen'},check=True)

if __name__ == '__main__': unittest.main()
