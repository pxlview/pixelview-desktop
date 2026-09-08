"""Offline compiled production deep-link tests; no OS registration/network."""
import os
import pathlib
import subprocess
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]

class DeepLinks(unittest.TestCase):
    def compile_run(self, code, extra=()):
        qt = next((ROOT/'.deps').glob('obs-deps-qt*/lib/QtWidgets.framework')).parent
        with tempfile.TemporaryDirectory() as td:
            src=pathlib.Path(td)/'test.mm'; src.write_text(code)
            exe=pathlib.Path(td)/'test'
            args=['clang++','-std=c++17','-fobjc-arc','-I'+str(ROOT),'-I'+str(ROOT/'frontend'),'-F'+str(qt)]
            for f in ['QtCore','QtGui','QtWidgets']:
                args += ['-I'+str(qt/(f+'.framework/Headers')),'-framework',f]
            subprocess.run(args+['-framework','AppKit','-Wl,-rpath,'+str(qt),str(src),*map(str,extra),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,env={**os.environ,'QT_QPA_PLATFORM':'offscreen'},timeout=20)

    def test_player_utf8_token(self):
        self.assertTrue((ROOT/'frontend/utility/PixelviewDeepLink.hpp').exists(), 'deep-link parser missing')
        self.compile_run('''#include "frontend/utility/PixelviewDeepLink.hpp"
#include <cassert>
int main() {
 auto link = pixelview::parseDeepLink("https://play.pixelview.io/Abc-123?token=cMOkc3M");
 assert(link && link->session == "Abc-123" && link->password == QString::fromUtf8("päss"));
}''')

    def test_strict_player_contract(self):
        self.compile_run(r'''#include "frontend/utility/PixelviewDeepLink.hpp"
#include <cassert>
int main() {
 for (const auto &prefix : {QString("https://play.pixelview.io/"), QString("pixelview://play/")}) {
  for (const auto &token : {"cMOkc3M", "cMOkc3M=", "8J-YgA", "8J+YgA==", "8J%2BYgA%3D%3D"})
   assert(pixelview::parseDeepLink(prefix+"MiXeD?token="+token));
  auto empty=pixelview::parseDeepLink(prefix+"session"); assert(empty && empty->password.isEmpty());
  assert(pixelview::parseDeepLink(prefix+"session?token=&option=yes"));
  assert(pixelview::parseDeepLink(prefix+"s%C3%A9ance?token=cGFzcw"));
  for (const auto &suffix : {"", "a/", "a/b", ".", "..", "%2e", "a%2fb", "a%5cb", "%00", "%20a", "a%20", "%FF", "a#x", "a?token=Zg&token=Zg", "a?token=Zg&%74oken=Zg", "a?x=1&x=2", "a?token=Z", "a?token=Zh", "a?token=Zg=", "a?token=Zg===", "a?token=Z=g=", "a?token=Z g", "a?token=_w", "a?token=wK8", "a?token=AA", "a?token=Cg", "a?token=7aCA", "a?token=%GG", "a?token=8J YgA=="})
   assert(!pixelview::parseDeepLink(prefix+suffix));
 }
 for (const auto &raw : {"http://play.pixelview.io/a", "https://evil.test/a", "https://play.pixelview.io.evil/a", "https://u@play.pixelview.io/a", "https://play.pixelview.io:443/a", "https://play.pixelview.io./a", "pixelview://a?token=Zg", "pixelview://play:80/a", "https://play.pixelview.io/a\n"})
  assert(!pixelview::parseDeepLink(raw));
 assert(!pixelview::parseDeepLink("https://play.pixelview.io/"+QString(257,'a')));
 assert(!pixelview::parseDeepLink("https://play.pixelview.io/a?token="+QString(6000,'Y')));
 assert(!pixelview::parseDeepLink("https://play.pixelview.io/a?x="+QString(8192,'a')));
}''')

    def test_memory_queue_and_qt_delivery(self):
        self.assertTrue((ROOT/'frontend/utility/PixelviewDeepLinkInbox.hpp').exists(), 'URL inbox missing')
        self.compile_run(r'''#include "frontend/utility/PixelviewDeepLinkInbox.hpp"
#include <QApplication>
#include <cassert>
int main(int argc, char **argv) {
 QApplication app(argc,argv); pixelview::DeepLinkInbox inbox(&app);
 int calls=0; QString id; bool valid=false;
 inbox.submit("https://play.pixelview.io/first?token=Zg");
 inbox.submit("pixelview://play/latest?token=cA");
 QObject ui;
 inbox.attach(&ui,[&](const std::optional<pixelview::DeepLink> &link){ ++calls; valid=bool(link); if(link) id=link->session; });
 assert(calls==0); inbox.ready(); assert(calls==1 && id=="latest");
 inbox.ready(); assert(calls==1);
 QFileOpenEvent event(QUrl("pixelview://play/warm?token=cA"));
 QCoreApplication::sendEvent(&app,&event); assert(calls==2 && id=="warm");
 inbox.submit("https://evil.test/session?token=cA"); assert(calls==3 && !valid);
 inbox.shutdown(); inbox.submit("pixelview://play/ignored"); assert(calls==3);
}''')

    def test_native_prefill_never_starts_or_interrupts(self):
        inc=ROOT/'frontend/widgets/OBSBasic_PixelviewDeepLinks.inc'
        self.assertTrue(inc.exists(), 'UI prefill missing')
        self.compile_run(r'''#include <QtWidgets/QtWidgets>
#include "frontend/utility/PixelviewDeepLinkInbox.hpp"
#include <cassert>
class OBSBasic : public QMainWindow { public:
 QLineEdit id, password; QLabel status; bool busy=false, pixelviewReceiving=false, closing=false, pixelviewShutdownPending=false;
 QLineEdit *pixelviewReceiveId=&id, *pixelviewReceivePassword=&password; QLabel *pixelviewReceiveStatus=&status;
 int switches=0;
 bool PixelviewModeBusy() const { return busy || closing || pixelviewShutdownPending; }
 bool isClosing() const { return closing; }
 void SelectPixelviewMode(int i) { ++switches; pixelviewReceiving=i==1; }
 void ApplyPixelviewDeepLink(const std::optional<pixelview::DeepLink> &link);
};
#include "frontend/widgets/OBSBasic_PixelviewDeepLinks.inc"
int main(int argc,char **argv) {
 QApplication app(argc,argv); OBSBasic w; w.password.setEchoMode(QLineEdit::Password);
 auto link=pixelview::parseDeepLink("pixelview://play/MySession?token=cMOkc3M");
 w.ApplyPixelviewDeepLink(link);
 assert(w.pixelviewReceiving && w.id.text()=="MySession" && w.password.text()==QString::fromUtf8("päss"));
 assert(w.password.echoMode()==QLineEdit::Password);
 for (bool *flag : {&w.busy,&w.closing,&w.pixelviewShutdownPending}) {
  *flag=true; int switches=w.switches;
  w.ApplyPixelviewDeepLink(pixelview::parseDeepLink("pixelview://play/other?token=Zg"));
  assert(w.id.text()=="MySession" && w.password.text()==QString::fromUtf8("päss") && w.switches==switches);
  *flag=false;
 }
 w.ApplyPixelviewDeepLink(std::nullopt); assert(w.id.text()=="MySession");
 w.ApplyPixelviewDeepLink(pixelview::parseDeepLink("pixelview://play/empty"));
 assert(w.id.text()=="empty" && w.password.text().isEmpty());
}''')

    def test_native_activity_preserves_delegate(self):
        native=ROOT/'frontend/utility/PixelviewDeepLinkMac.mm'
        self.assertTrue(native.exists(), 'NSUserActivity bridge missing')
        self.compile_run(r'''#import <AppKit/AppKit.h>
#include "frontend/utility/PixelviewDeepLinkInbox.hpp"
#include <QApplication>
#include <cassert>
static int forwarded=0;
@interface ExistingDelegate : NSObject <NSApplicationDelegate>
@end
@implementation ExistingDelegate
- (BOOL)application:(NSApplication *)app continueUserActivity:(NSUserActivity *)activity restorationHandler:(void (^)(NSArray<id<NSUserActivityRestoring>> *))handler { ++forwarded; return NO; }
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app { return YES; }
@end
int main(int argc,char **argv) {
 QApplication app(argc,argv); [NSApplication sharedApplication];
 auto original=[NSApp delegate]; ExistingDelegate *delegate=[ExistingDelegate new]; [NSApp setDelegate:delegate];
 pixelview::installMacDeepLinks(); pixelview::installMacDeepLinks();
 assert([NSApp delegate]==delegate);
 assert([[NSApp delegate] applicationShouldTerminateAfterLastWindowClosed:NSApp]);
 auto &inbox=pixelview::deepLinkInbox(); int calls=0; QObject ui;
 inbox.attach(&ui,[&](const auto &link){ assert(link && link->session=="Cold"); ++calls; });
 NSUserActivity *activity=[[NSUserActivity alloc] initWithActivityType:NSUserActivityTypeBrowsingWeb];
 activity.webpageURL=[NSURL URLWithString:@"https://play.pixelview.io/Cold?token=cA"];
 __block int restored=0;
 assert([[NSApp delegate] application:NSApp willContinueUserActivityWithType:NSUserActivityTypeBrowsingWeb]);
 assert([[NSApp delegate] application:NSApp continueUserActivity:activity restorationHandler:^(NSArray *objects){ ++restored; }]);
 assert(calls==0 && restored==1); inbox.ready(); assert(calls==1);
 assert([[NSApp delegate] application:NSApp continueUserActivity:activity restorationHandler:^(NSArray *objects){}]);
 assert(calls==2);
 activity.webpageURL=[NSURL URLWithString:@"https://other.test/Cold"];
 assert(![[NSApp delegate] application:NSApp continueUserActivity:activity restorationHandler:^(NSArray *objects){}]);
 assert(forwarded==1 && calls==2);
 [NSApp setDelegate:original];
}''', [native])

    def test_application_wiring_and_no_argument_logging(self):
        app=(ROOT/'frontend/OBSApp.cpp').read_text()
        main=(ROOT/'frontend/obs-main.cpp').read_text()
        ui=(ROOT/'frontend/widgets/OBSBasic.cpp').read_text()
        self.assertIn('pixelview::deepLinkInbox();', app)
        self.assertIn('pixelview::installMacDeepLinks();', app)
        self.assertIn('pixelview::deepLinkInbox().ready();', main)
        self.assertGreater(main.index('pixelview::deepLinkInbox().ready();'), main.index('if (!program.OBSInit())'))
        self.assertNotIn('Command Line Arguments: %s', main)
        self.assertNotIn('stor << argv', main)
        self.assertIn('pixelview::deepLinkInbox().attach(this,', ui)
        self.assertIn('OBSBasic_PixelviewDeepLinks.inc', ui)
        self.assertIn('pixelview::installMacDeepLinks();', (ROOT/'frontend/utility/platform-osx.mm').read_text())
        self.assertIn('utility/PixelviewDeepLinkMac.mm', (ROOT/'frontend/cmake/os-macos.cmake').read_text())

    def test_bundle_scheme_and_opt_in_association(self):
        import plistlib
        info=plistlib.loads((ROOT/'frontend/cmake/macos/Info.plist.in').read_bytes().replace(b'${SPARKLE_UPDATE_INTERVAL}', b'3600'))
        self.assertEqual(info['CFBundleURLTypes'][0]['CFBundleURLSchemes'], ['pixelview'])
        base=plistlib.loads((ROOT/'frontend/cmake/macos/entitlements.plist').read_bytes(), fmt=plistlib.FMT_XML)
        self.assertNotIn('com.apple.developer.associated-domains', base)
        associated=plistlib.loads((ROOT/'frontend/cmake/macos/pixelview-associated-domains.plist').read_bytes())
        self.assertEqual(associated['com.apple.developer.associated-domains'], ['applinks:play.pixelview.io'])
        for key,value in base.items(): self.assertEqual(associated[key], value)
        config=(ROOT/'frontend/cmake/pixelview-deep-links.cmake').read_text()
        self.assertIn('PIXELVIEW_ENABLE_UNIVERSAL_LINKS', config)
        self.assertIn('OFF)', config)
        self.assertIn('FATAL_ERROR', config)
        self.assertIn('MACOSX_PACKAGE_LOCATION "."', config)
        build=(ROOT/'cmake/macos/pixelview-build.sh').read_text()
        self.assertIn('"-DPIXELVIEW_ENABLE_UNIVERSAL_LINKS=${PIXELVIEW_ENABLE_UNIVERSAL_LINKS:-OFF}"', build)
        self.assertIn('"-DPIXELVIEW_ASSOCIATED_DOMAINS_PROFILE=${PIXELVIEW_ASSOCIATED_DOMAINS_PROFILE:-}"', build)

    def test_profile_authorization_validation(self):
        import runpy
        from datetime import datetime, timedelta, timezone
        validator=ROOT/'frontend/cmake/validate-associated-domains.py'
        self.assertTrue(validator.exists(), 'profile validator missing')
        validate=runpy.run_path(str(validator))['validate_profile']
        now=datetime.now(timezone.utc)
        profile={'TeamIdentifier':['MA47F3M8W9'], 'ExpirationDate':now+timedelta(days=1),
                 'Platform':['OSX'], 'Entitlements': {
                     'com.apple.application-identifier':'MA47F3M8W9.com.pixelview.desktop',
                     'com.apple.developer.team-identifier':'MA47F3M8W9',
                     'com.apple.developer.associated-domains':['applinks:play.pixelview.io']}}
        self.assertTrue(validate(profile,now))
        import copy
        for field,value in [('ExpirationDate',now-timedelta(days=1)),('TeamIdentifier',['OTHER']),('Platform',['iOS'])]:
            bad=copy.deepcopy(profile); bad[field]=value; self.assertFalse(validate(bad,now))
        for field,value in [('com.apple.application-identifier','MA47F3M8W9.*'),('com.apple.developer.associated-domains',[]),('com.apple.developer.team-identifier','OTHER')]:
            bad=copy.deepcopy(profile); bad['Entitlements'][field]=value; self.assertFalse(validate(bad,now))
        self.assertFalse(validate({},now))

    def test_cmake_adhoc_default_and_fail_closed_opt_in(self):
        with tempfile.TemporaryDirectory() as td:
            source=pathlib.Path(td)/'src'; source.mkdir()
            (source/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\nproject(DeepLinks NONE)\nadd_custom_target(obs-studio)\ninclude("'+str(ROOT/'frontend/cmake/pixelview-deep-links.cmake')+'")\n')
            for options,success in [([],True), (['-DPIXELVIEW_ENABLE_UNIVERSAL_LINKS=ON','-DOBS_CODESIGN_IDENTITY=-'],False)]:
                build=pathlib.Path(td)/('default' if success else 'rejected')
                result=subprocess.run(['cmake','-S',str(source),'-B',str(build),*options],capture_output=True,text=True)
                self.assertEqual(result.returncode==0,success,result.stderr)

if __name__ == '__main__': unittest.main()
