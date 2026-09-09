"""Real private-Keychain restart/locked-store coverage, with no authentication UI.

Only the SecItem target is redirected; production JSON, revisions and interaction
guards run unchanged. Never unlock or query credentials in a user Keychain.
"""
import pathlib
import subprocess
import tempfile
import unittest
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[2]

class ReceiveKeychainRestart(unittest.TestCase):
    def test_separate_process_save_reload(self):
        with tempfile.TemporaryDirectory(prefix='pixelview-keychain-') as directory:
            path = pathlib.Path(directory)
            source = r'''
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include <cstdio>
#include <cassert>
#include "PixelviewNoninteractiveKeychain.hpp"
static OSStatus traced(const char *operation, OSStatus status) {
    fprintf(stderr, "%s OSStatus=%d\n", operation, (int)status); return status;
}
// Restrict every production SecItem call to our private synthetic keychain.
static SecKeychainRef fixture=nullptr;
static NSMutableDictionary *isolated(CFDictionaryRef q, bool adding=false) {
    NSMutableDictionary *result=[(__bridge NSDictionary *)q mutableCopy];
    if(adding) result[(__bridge id)kSecUseKeychain]=(__bridge id)fixture;
    else result[(__bridge id)kSecMatchSearchList]=@[(__bridge id)fixture];
    return result;
}
static OSStatus copyItem(CFDictionaryRef q, CFTypeRef *v) { return traced("copy", SecItemCopyMatching((__bridge CFDictionaryRef)isolated(q),v)); }
static OSStatus updateItem(CFDictionaryRef q, CFDictionaryRef v) { return traced("update", SecItemUpdate((__bridge CFDictionaryRef)isolated(q),v)); }
static OSStatus addItem(CFDictionaryRef q, CFTypeRef *v) { return traced("add", SecItemAdd((__bridge CFDictionaryRef)isolated(q,true),v)); }
static OSStatus deleteItem(CFDictionaryRef q) { return traced("delete", SecItemDelete((__bridge CFDictionaryRef)isolated(q))); }
#define SecItemCopyMatching copyItem
#define SecItemUpdate updateItem
#define SecItemAdd addItem
#define SecItemDelete deleteItem
#include "PixelviewReceiveCredentialStoreMac.mm"
int main(int argc, char **argv) { @autoreleasepool {
    assert(argc==4);
    QString mode=QString::fromUtf8(argv[1]);
    {
        pixelview::NoninteractiveKeychain guard;
        assert(guard.ready);
        if(mode=="init") {
            assert(traced("create-private",SecKeychainCreate(argv[3],7,"fixture",false,nullptr,&fixture))==errSecSuccess);
            // Creation adds its own search-list entry. Remove only that new entry;
            // leave existing user keychains and the default untouched.
            CFArrayRef list=nullptr;
            assert(SecKeychainCopySearchList(&list)==errSecSuccess);
            NSMutableArray *filtered=[(__bridge NSArray *)list mutableCopy];
            [filtered removeObject:(__bridge id)fixture];
            assert(SecKeychainSetSearchList((__bridge CFArrayRef)filtered)==errSecSuccess);
            CFRelease(list); CFRelease(fixture); return 0;
        }
        assert(SecKeychainOpen(argv[3],&fixture)==errSecSuccess);
        assert(traced("unlock-private",SecKeychainUnlock(fixture,7,"fixture",true))==errSecSuccess);
        if(mode.startsWith("locked-")) {
            assert(traced("lock-private",SecKeychainLock(fixture))==errSecSuccess);
            SecKeychainStatus flags=0;
            assert(SecKeychainGetStatus(fixture,&flags)==errSecSuccess);
            assert(!(flags & kSecUnlockStateStatus));
        }
        if(mode=="destroy") {
            auto status=traced("destroy-private",SecKeychainDelete(fixture));
            CFRelease(fixture); return status==errSecSuccess ? 0 : 1;
        }
    }
    auto store=pixelview::makeReceiveCredentialStore(QString::fromUtf8(argv[2]));
    Boolean before=false, after=false;
    SecKeychainGetUserInteractionAllowed(&before);
    bool ok=false;
    if(mode=="save") ok=store->save("https://fixture.invalid","session","revision","synthetic-fixture-password");
    if(mode=="locked-save") ok=!store->save("https://fixture.invalid","session","revision","synthetic-fixture-password");
    if(mode=="locked-load") ok=store->load("https://fixture.invalid","session","revision").state==pixelview::ReceiveCredentialStore::State::Error;
    if(mode=="replace") ok=store->save("https://fixture.invalid","session","revision-2","synthetic-fixture-replacement");
    if(mode=="reload-replacement") {
        auto result=store->load("https://fixture.invalid","session","revision-2");
        ok=result.state==pixelview::ReceiveCredentialStore::State::Found && result.password=="synthetic-fixture-replacement";
        ok=ok && store->load("https://fixture.invalid","session","revision").state==pixelview::ReceiveCredentialStore::State::Missing;
    }
    if(mode=="load") {
        auto result=store->load("https://fixture.invalid","session","revision");
        ok=result.state==pixelview::ReceiveCredentialStore::State::Found && result.password=="synthetic-fixture-password";
        ok=ok && store->load("https://fixture.invalid","session","wrong-revision").state==pixelview::ReceiveCredentialStore::State::Missing;
    }
    if(mode=="clear") {
        ok=store->clear();
        ok=ok && store->load("https://fixture.invalid","session","revision").state==pixelview::ReceiveCredentialStore::State::Missing;
    }
    SecKeychainGetUserInteractionAllowed(&after);
    CFRelease(fixture);
    return ok && before==after ? 0 : 1;
} }
'''
            (path/'fixture.mm').write_text(source)
            qt = ROOT/'.deps/obs-deps-qt6-2026-08-26-universal/lib'
            subprocess.run(['clang++','-std=c++17','-fobjc-arc','-Wno-deprecated-declarations',
                            '-I'+str(ROOT/'frontend/utility'),'-F'+str(qt),'-I'+str(qt/'QtCore.framework/Headers'),
                            '-framework','QtCore','-framework','Foundation','-framework','Security',
                            '-framework','LocalAuthentication','-Wl,-rpath,'+str(qt),
                            str(path/'fixture.mm'),'-o',str(path/'fixture')],check=True)
            # Ad-hoc signing needs no identity/private key and cannot prompt.
            subprocess.run(['codesign','--force','--sign','-',str(path/'fixture')],check=True)
            subprocess.run(['codesign','--verify','--strict',str(path/'fixture')],check=True)
            service = 'com.pixelview.test.receive-restart.'+str(uuid.uuid4())
            def run(mode):
                result = subprocess.run([str(path/'fixture'),mode,service,str(path/'fixture.keychain')],capture_output=True,text=True,timeout=15)
                print(mode, result.stderr, flush=True)
                self.assertNotIn('synthetic-fixture-',result.stdout+result.stderr)
                self.assertEqual(result.returncode,0,mode+': '+result.stderr)
            search_list = subprocess.check_output(['security','list-keychains','-d','user'])
            try:
                run('init')
                run('locked-save')
                run('save')
                run('locked-load')
                run('load')
                run('replace')
                run('reload-replacement')
            finally:
                try:
                    run('clear')
                finally:
                    run('destroy')
                    self.assertFalse((path/'fixture.keychain-db').exists())
                    self.assertFalse((path/'fixture.keychain').exists())
                    self.assertEqual(subprocess.check_output(['security','list-keychains','-d','user']),search_list)

if __name__=='__main__': unittest.main()
