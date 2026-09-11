"""Offline Security API seam: never accesses a real Keychain."""
import pathlib, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
class Noninteractive(unittest.TestCase):
 def test_failed_read_preserves_pairing_identity(self):
  source=(ROOT/'frontend/widgets/OBSBasic_PixelviewDesktop.inc').read_text()
  body=source.split('void OBSBasic::ConnectPixelviewDesktop()',1)[1].split('if(token.isEmpty()) {',1)[1].split('return;',1)[0]
  self.assertNotIn('pixelviewIdentity.clear()',body)
  self.assertNotIn('SavePixelviewIdentity()',body)
  self.assertIn('saved pairing has not been removed',body)

 def test_pairing_operations_disable_ui_and_restore_policy(self):
  source=(ROOT/'frontend/utility/PixelviewDesktopMac.mm').read_text()
  pairing=source[source.index('namespace pixelview {\nstatic NSMutableDictionary'):]
  with tempfile.TemporaryDirectory() as d:
   p=pathlib.Path(d)
   code='''#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include <QtCore/QString>
#include <cassert>
#include <mutex>
static Boolean allowed=true;
static bool policyFailure=false;
static int calls=0;
OSStatus fakeGet(Boolean *v) { *v=allowed; return policyFailure ? errSecAuthFailed : errSecSuccess; }
OSStatus fakeSet(Boolean v) { allowed=v; return errSecSuccess; }
OSStatus fakeDefault(SecKeychainRef *v) { *v=(SecKeychainRef)CFRetain(CFSTR("offline")); return 0; }
OSStatus denied() { ++calls; assert(!allowed && "Keychain operation would show ACL prompt / hang"); return errSecInteractionNotAllowed; }
OSStatus fakeCopy(CFDictionaryRef,CFTypeRef*) { return denied(); }
OSStatus fakeUpdate(CFDictionaryRef,CFDictionaryRef) { return denied(); }
OSStatus fakeAdd(CFDictionaryRef,CFTypeRef*) { return denied(); }
OSStatus fakeDelete(CFDictionaryRef) { return denied(); }
#define SecKeychainGetUserInteractionAllowed fakeGet
#define SecKeychainSetUserInteractionAllowed fakeSet
#define SecKeychainCopyDefault fakeDefault
#define SecItemCopyMatching fakeCopy
#define SecItemUpdate fakeUpdate
#define SecItemAdd fakeAdd
#define SecItemDelete fakeDelete
'''
   guard=ROOT/'frontend/utility/PixelviewNoninteractiveKeychain.hpp'
   if guard.exists(): code+='#include "'+str(guard)+'"\n'
   code+='namespace pixelview { QString loadDevice(const QString &); }\n'+pairing
   code+='''
int main() { @autoreleasepool {
 for(Boolean initial : {true,false}) {
  allowed=initial;
  assert(pixelview::loadDevice("https://fixture.invalid").isEmpty()); assert(allowed==initial);
  assert(!pixelview::saveDevice("https://fixture.invalid","synthetic")); assert(allowed==initial);
  assert(!pixelview::removeDevice("https://fixture.invalid")); assert(allowed==initial);
 }
 policyFailure=true; int before=calls;
 assert(pixelview::loadDevice("https://fixture.invalid").isEmpty());
 assert(!pixelview::saveDevice("https://fixture.invalid","synthetic"));
 assert(!pixelview::removeDevice("https://fixture.invalid")); assert(calls==before);
} }
'''
   (p/'test.mm').write_text(code)
   qt=ROOT/'.deps/obs-deps-qt6-2026-08-26-universal/lib'
   subprocess.run(['clang++','-std=c++17','-fobjc-arc','-F'+str(qt),'-framework','QtCore','-framework','Foundation','-framework','Security','-Wl,-rpath,'+str(qt),str(p/'test.mm'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True,timeout=10)
if __name__=='__main__': unittest.main()
