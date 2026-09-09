#pragma once
#include <mutex>
#import <Security/Security.h>

namespace pixelview {
// LAContext does not suppress trusted-application ACL dialogs in the legacy
// file-based Keychain. Scope its process-local UI policy and serialize our
// callers (including save -> load) so nested operations restore it correctly.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
class NoninteractiveKeychain {
 static inline std::recursive_mutex mutex;
 std::lock_guard<std::recursive_mutex> lock{mutex};
 Boolean previous=true;
 bool restore=false;
public:
 const bool ready;
 NoninteractiveKeychain() : ready(disable()) {}
 ~NoninteractiveKeychain() { if(restore) SecKeychainSetUserInteractionAllowed(previous); }
private:
 bool disable()
 {
  if(SecKeychainGetUserInteractionAllowed(&previous)!=errSecSuccess) return false;
  restore=SecKeychainSetUserInteractionAllowed(false)==errSecSuccess;
  return restore;
 }
};
#pragma clang diagnostic pop
}
