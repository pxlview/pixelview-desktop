"""Native Qt event-loop and mocked Security serialization; no live Keychain."""
import pathlib, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
class AsyncKeychain(unittest.TestCase):
 def test_prompt_does_not_block_ui_or_enable_background_and_owner_can_die(self):
  code=r'''
#import <Security/Security.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <atomic>
#include <cassert>
#include <thread>
static Boolean allowed=true;
OSStatus fakeGet(Boolean *v) { *v=allowed; return 0; }
OSStatus fakeSet(Boolean v) { allowed=v; return 0; }
OSStatus fakeDefault(SecKeychainRef *v) { *v=(SecKeychainRef)CFRetain(CFSTR("offline")); return 0; }
#define SecKeychainGetUserInteractionAllowed fakeGet
#define SecKeychainSetUserInteractionAllowed fakeSet
#define SecKeychainCopyDefault fakeDefault
#include "PixelviewNoninteractiveKeychain.hpp"
#include "PixelviewKeychainTask.hpp"
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);
 std::atomic<bool> entered=false,release=false,exited=false;
 int ticks=0; bool done=false;
 pixelview::runKeychainUserAction(&app,[&]{
  pixelview::NoninteractiveKeychain request;
  assert(request.ready && allowed && pixelview::KeychainUserAction::requested());
  entered=true;
  while(!release) std::this_thread::yield();
  return false; // native Cancel/denial is an ordinary failed result
 },[&](bool result){ assert(!result); done=true; app.quit(); });
 QTimer timer; timer.setInterval(1);
 QObject::connect(&timer,&QTimer::timeout,&app,[&]{
  if(!entered) return;
  pixelview::NoninteractiveKeychain background;
  assert(!background.ready); // fail promptly, never wait for native prompt
  if(++ticks==5) release=true;
 }); timer.start();
 QTimer::singleShot(3000,&app,[]{std::abort();});
 app.exec(); assert(done && ticks>=5 && allowed); timer.stop();
 auto *owner=new QObject;
 release=false;
 pixelview::runKeychainUserAction(owner,[&]{
  while(!release) std::this_thread::yield(); exited=true; return true;
 },[](bool){std::abort();});
 delete owner;
 release=true;
 while(!exited) QCoreApplication::processEvents();
 QCoreApplication::processEvents();
}
'''
  with tempfile.TemporaryDirectory() as d:
   p=pathlib.Path(d); (p/'test.mm').write_text(code)
   qt=ROOT/'.deps/obs-deps-qt6-2026-08-26-universal/lib'
   subprocess.run(['clang++','-std=c++17','-fobjc-arc','-I'+str(ROOT/'frontend/utility'),'-F'+str(qt),'-framework','QtCore','-framework','Security','-Wl,-rpath,'+str(qt),str(p/'test.mm'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True,timeout=10)
if __name__=='__main__': unittest.main()
