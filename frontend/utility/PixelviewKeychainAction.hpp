#pragma once

namespace pixelview {
// Explicit operator intent, scoped to the worker executing a credential request.
// This is NOT the Security framework's process-wide interaction policy.
class KeychainUserAction {
 static inline thread_local bool active = false;
 bool previous = active;
public:
 KeychainUserAction() { active = true; }
 ~KeychainUserAction() { active = previous; }
 KeychainUserAction(const KeychainUserAction &) = delete;
 KeychainUserAction &operator=(const KeychainUserAction &) = delete;
 static bool requested() { return active; }
};
}
