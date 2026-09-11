#pragma once
#include <mutex>
#include "PixelviewKeychainAction.hpp"
#include <cstdio>
#import <Security/Security.h>

namespace pixelview {
// Fixed categories only: never print query dictionaries, account/origin, tokens,
// OS-provided error text, or item contents. Interaction-required is not proof
// that the login Keychain is locked (a trusted-application ACL can also deny it).
inline OSStatus keychainStatus(const char *operation, OSStatus status)
{
 if(status==errSecSuccess || status==errSecItemNotFound) return status;
 const char *category="security-error";
 switch(status) {
 case errSecNoDefaultKeychain: category="no-default-keychain"; break;
 case errSecNoSuchKeychain: case errSecNotAvailable: category="keychain-unavailable"; break;
 case errSecInteractionNotAllowed: case errSecInteractionRequired: category="interaction-required"; break;
 case errSecAuthFailed: category="authorization-denied"; break;
 case errSecUserCanceled: category="user-canceled"; break;
 case errSecDecode: category="invalid-record"; break;
 }
 std::fprintf(stderr,"Pixelview Keychain %s: %s (OSStatus %d)\n",operation,category,int(status));
 return status;
}
// LAContext does not suppress trusted-application ACL dialogs in the legacy
// file-based Keychain. Scope its process-local UI policy and serialize our
// callers (including save -> load) so nested operations restore it correctly.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
class NoninteractiveKeychain {
 static inline std::recursive_mutex mutex;
 std::unique_lock<std::recursive_mutex> lock{mutex, std::defer_lock};
 Boolean previous=true;
 bool restore=false;
public:
 const bool ready;
 NoninteractiveKeychain() : ready(disable()) {}
 ~NoninteractiveKeychain() { if(restore) keychainStatus("restore-interaction-policy",SecKeychainSetUserInteractionAllowed(previous)); }
private:
 bool disable()
 {
  // Interactive calls run on a worker, serialized against our background
  // stores, and leave macOS's normal UI policy alone. Never globally enable UI.
  // Background/UI-thread reads fail promptly while a native prompt is pending.
  if(KeychainUserAction::requested()) { lock.lock(); return true; }
  if(!lock.try_lock()) return false;
  if(keychainStatus("get-interaction-policy",SecKeychainGetUserInteractionAllowed(&previous))!=errSecSuccess) return false;
  restore=keychainStatus("set-interaction-policy",SecKeychainSetUserInteractionAllowed(false))==errSecSuccess;
  if(!restore) return false;
  // A search list containing only System must not turn a missing user default
  // into an apparently absent credential. Never create/select/unlock a Keychain.
  SecKeychainRef keychain=nullptr;
  auto status=SecKeychainCopyDefault(&keychain);
  if(status==errSecSuccess && !keychain) status=errSecNoDefaultKeychain;
  if(keychain) CFRelease(keychain);
  return keychainStatus("default-keychain",status)==errSecSuccess;
 }
};
#pragma clang diagnostic pop
}
