#pragma once
#include <QString>
#include <memory>

namespace pixelview {
// One latest receive credential, separate from Desktop's device-pairing service.
// The non-secret revision prevents failed replacements from resurrecting an old
// password for the same session. No plaintext or in-memory persistence fallback.
class ReceiveCredentialStore {
public:
 enum class State { Found, Missing, Error };
 struct Result { State state = State::Missing; QString password; };
 virtual ~ReceiveCredentialStore() = default;
 virtual Result load(const QString &origin, const QString &session, const QString &revision) = 0;
 virtual bool save(const QString &origin, const QString &session, const QString &revision, const QString &password) = 0;
 virtual bool clear() = 0;
};
// Custom service is an explicit test seam; shipping callers use the default.
std::unique_ptr<ReceiveCredentialStore> makeReceiveCredentialStore(
 const QString &service = QStringLiteral("com.pixelview.desktop.receiver"));
}
