"""Compile actual pairing store with refusing/in-memory Security boundaries.
No real Keychain APIs, prompts, credentials or applications are exercised.
"""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class KeychainReliability(unittest.TestCase):
    def test_pairing_roundtrip_and_failed_delete_verification(self):
        source = (ROOT / 'frontend/utility/PixelviewDesktopMac.mm').read_text()
        pairing = source[source.index('namespace pixelview {\nstatic NSMutableDictionary'):]
        code = r'''
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include <QtCore/QString>
#include <cassert>
static Boolean allowed=true;
static NSData *stored=nil;
static OSStatus failure=errSecSuccess, verifyFailure=errSecSuccess;
static OSStatus defaultFailure=errSecSuccess;
static int mutations=0;
static bool userAction=false;
#define CHECK_POLICY() assert(bool(allowed)==userAction)
OSStatus fakeDefault(SecKeychainRef *value) {
 if(defaultFailure) return defaultFailure;
 *value=(SecKeychainRef)CFRetain(CFSTR("offline default fixture")); return 0;
}
OSStatus fakeGet(Boolean *v) { *v=allowed; return errSecSuccess; }
OSStatus fakeSet(Boolean v) { allowed=v; return errSecSuccess; }
OSStatus fakeCopy(CFDictionaryRef,CFTypeRef *v) {
 CHECK_POLICY(); if(failure) return failure;
 if(!stored) return errSecItemNotFound;
 *v=CFBridgingRetain(stored); return errSecSuccess;
}
OSStatus fakeUpdate(CFDictionaryRef q,CFDictionaryRef attrs) {
 CHECK_POLICY(); if(failure) return failure; if(!stored) return errSecItemNotFound;
 ++mutations; stored=[(__bridge NSDictionary *)attrs objectForKey:(__bridge id)kSecValueData]; return 0;
}
OSStatus fakeAdd(CFDictionaryRef q,CFTypeRef*) {
 CHECK_POLICY(); if(failure) return failure;
 ++mutations; stored=[(__bridge NSDictionary *)q objectForKey:(__bridge id)kSecValueData]; return 0;
}
OSStatus fakeDelete(CFDictionaryRef) {
 CHECK_POLICY(); if(failure) return failure; ++mutations; stored=nil;
 failure=verifyFailure; return 0;
}
#define SecKeychainGetUserInteractionAllowed fakeGet
#define SecKeychainCopyDefault fakeDefault
#define SecKeychainSetUserInteractionAllowed fakeSet
#define SecItemCopyMatching fakeCopy
#define SecItemUpdate fakeUpdate
#define SecItemAdd fakeAdd
#define SecItemDelete fakeDelete
'''
        code += '#include "' + str(ROOT / 'frontend/utility/PixelviewNoninteractiveKeychain.hpp') + '"\n'
        code += 'namespace pixelview { QString loadDevice(const QString &); }\n' + pairing
        code += '#include "' + str(ROOT / 'frontend/utility/PixelviewReceiveCredentialStoreMac.mm') + '"\n'
        code += r'''
int main() { @autoreleasepool {
 const QString origin="https://fixture.invalid";
 assert(pixelview::saveDevice(origin,"synthetic")); assert(allowed);
 defaultFailure=errSecNoDefaultKeychain; int before=mutations;
 assert(pixelview::loadDevice(origin).isEmpty());
 assert(!pixelview::saveDevice(origin,"replacement"));
 assert(!pixelview::removeDevice(origin)); assert(mutations==before);
 defaultFailure=0;
 assert(pixelview::loadDevice(origin)=="synthetic");
 for(auto status : {errSecInteractionNotAllowed,errSecAuthFailed,errSecNoDefaultKeychain,errSecNoSuchKeychain}) {
  failure=status; int before=mutations;
  assert(pixelview::loadDevice(origin).isEmpty());
  assert(!pixelview::saveDevice(origin,"replacement"));
  assert(!pixelview::removeDevice(origin)); assert(allowed); assert(mutations==before);
  failure=0; assert(pixelview::loadDevice(origin)=="synthetic");
 }
 assert(pixelview::removeDevice(origin)); assert(pixelview::loadDevice(origin).isEmpty());
 assert(pixelview::removeDevice(origin));
 assert(pixelview::saveDevice(origin,"synthetic"));
 verifyFailure=errSecInteractionNotAllowed;
 assert(!pixelview::removeDevice(origin)); // Inaccessible readback is not confirmed deletion.
 failure=verifyFailure=0;
 assert(pixelview::removeDevice(origin)); assert(allowed);
 auto receiver=pixelview::makeReceiveCredentialStore("offline-test-only");
 using State=pixelview::ReceiveCredentialStore::State;
 assert(receiver->save(origin,"session","revision","synthetic"));
 assert(receiver->load(origin,"session","revision").password=="synthetic");
 for(auto status : {errSecInteractionNotAllowed,errSecAuthFailed,errSecNoDefaultKeychain,errSecNoSuchKeychain}) {
  failure=status; int before=mutations;
  assert(receiver->load(origin,"session","revision").state==State::Error);
  assert(!receiver->save(origin,"session","revision","replacement"));
  assert(!receiver->clear()); assert(mutations==before); assert(allowed);
  failure=0; assert(receiver->load(origin,"session","revision").password=="synthetic");
 }
 defaultFailure=errSecNoDefaultKeychain; before=mutations;
 assert(receiver->load(origin,"session","revision").state==State::Error);
 assert(!receiver->save(origin,"session","revision","replacement"));
 assert(!receiver->clear()); assert(mutations==before); assert(allowed);
 defaultFailure=0;
 assert(receiver->load(origin,"session","revision").password=="synthetic");
 assert(receiver->clear()); assert(receiver->load(origin,"session","revision").state==State::Missing);
 assert(receiver->save(origin,"session","revision","synthetic"));
 verifyFailure=errSecAuthFailed; assert(!receiver->clear());
 failure=verifyFailure=0; assert(receiver->clear()); assert(allowed);
 userAction=true;
 {
  pixelview::KeychainUserAction action;
  defaultFailure=errSecNoDefaultKeychain; // Interactive operations reach Security, not preflight.
  assert(pixelview::saveDevice(origin,"synthetic"));
  for(auto status : {errSecUserCanceled,errSecAuthFailed,errSecNotAvailable}) {
   failure=status; int before=mutations;
   assert(!pixelview::saveDevice(origin,"replacement"));
   assert(!pixelview::removeDevice(origin)); assert(mutations==before);
   failure=0; assert(pixelview::loadDevice(origin)=="synthetic");
  }
  assert(pixelview::saveDevice(origin,"replacement"));
  assert(pixelview::removeDevice(origin));
  assert(receiver->save(origin,"session","revision","synthetic"));
  failure=errSecUserCanceled; assert(!receiver->clear());
  failure=0; assert(receiver->load(origin,"session","revision").password=="synthetic");
  assert(receiver->clear());
 }
 assert(allowed);
} }
'''
        with tempfile.TemporaryDirectory() as directory:
            p = pathlib.Path(directory)
            (p / 'test.mm').write_text(code)
            qt = ROOT / '.deps/obs-deps-qt6-2026-08-26-universal/lib'
            subprocess.run(['clang++', '-std=c++17', '-fobjc-arc', '-F'+str(qt), '-I'+str(qt/'QtCore.framework/Headers'),
                            '-framework', 'QtCore', '-framework', 'Foundation', '-framework', 'Security', '-framework', 'LocalAuthentication',
                            '-Wl,-rpath,'+str(qt), str(p/'test.mm'), '-o', str(p/'test')], check=True)
            result = subprocess.run([str(p/'test')], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn('synthetic', result.stderr)
            self.assertNotIn('fixture.invalid', result.stderr)
            for category in ('no-default-keychain', 'interaction-required', 'authorization-denied', 'keychain-unavailable'):
                self.assertIn(category, result.stderr)

if __name__ == '__main__':
    unittest.main()
