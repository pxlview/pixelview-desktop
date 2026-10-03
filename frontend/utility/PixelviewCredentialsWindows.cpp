// Windows Credential Manager storage for the Desktop device token and the
// latest receive password. Generic credentials persisted per user on this
// machine only (CRED_PERSIST_LOCAL_MACHINE: never roams). Item layout and
// verification mirror the macOS Keychain implementation; there is no
// plaintext or in-memory fallback.
#include "PixelviewDesktopConnection.hpp"
#include "PixelviewReceiveCredentialStore.hpp"
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <cstdio>
#include <string>
#include <windows.h>
#include <wincred.h>

namespace pixelview {
namespace {
void (*credentialLogSink)(const char *line) = nullptr;

// Fixed categories only: never print target names, origins, tokens or item contents.
DWORD credentialStatus(const char *operation, DWORD error)
{
 if (error == ERROR_SUCCESS || error == ERROR_NOT_FOUND) return error;
 const char *category = "credential-error";
 switch (error) {
 case ERROR_NO_SUCH_LOGON_SESSION: category = "no-logon-session"; break;
 case ERROR_INVALID_PARAMETER: case ERROR_INVALID_FLAGS: category = "invalid-request"; break;
 case ERROR_BAD_LENGTH: category = "too-large"; break;
 case ERROR_ACCESS_DENIED: category = "authorization-denied"; break;
 case ERROR_INVALID_DATA: category = "invalid-record"; break;
 }
 char line[160];
 std::snprintf(line, sizeof line, "Pixelview Credential Manager %s: %s (error %lu)", operation, category, (unsigned long)error);
 std::fprintf(stderr, "%s\n", line);
 if (credentialLogSink) credentialLogSink(line);
 return error;
}

DWORD writeSecret(const QString &target, const QByteArray &secret)
{
 if (secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) return ERROR_BAD_LENGTH;
 std::wstring name = target.toStdWString();
 std::wstring user = L"Pixelview Desktop";
 CREDENTIALW credential{};
 credential.Type = CRED_TYPE_GENERIC;
 credential.TargetName = name.data();
 credential.UserName = user.data();
 credential.CredentialBlobSize = DWORD(secret.size());
 credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(secret.constData()));
 credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
 return CredWriteW(&credential, 0) ? ERROR_SUCCESS : GetLastError();
}

DWORD readSecret(const QString &target, QByteArray &out)
{
 out.clear();
 const std::wstring name = target.toStdWString();
 PCREDENTIALW credential = nullptr;
 if (!CredReadW(name.c_str(), CRED_TYPE_GENERIC, 0, &credential)) return GetLastError();
 out = QByteArray(reinterpret_cast<const char *>(credential->CredentialBlob), qsizetype(credential->CredentialBlobSize));
 SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
 CredFree(credential);
 return ERROR_SUCCESS;
}

DWORD deleteSecret(const QString &target)
{
 const std::wstring name = target.toStdWString();
 return CredDeleteW(name.c_str(), CRED_TYPE_GENERIC, 0) ? ERROR_SUCCESS : GetLastError();
}

// Absent-after-delete check shared by both stores.
bool removeVerified(const QString &target, const char *operation, const char *verification)
{
 const auto status = credentialStatus(operation, deleteSecret(target));
 if (status != ERROR_SUCCESS && status != ERROR_NOT_FOUND) return false;
 QByteArray value;
 return credentialStatus(verification, readSecret(target, value)) == ERROR_NOT_FOUND;
}

QString deviceTarget(const QString &origin) { return QStringLiteral("com.pixelview.desktop.device:") + origin; }
}

void setKeychainLog(void (*sink)(const char *line)) { credentialLogSink = sink; }

bool saveDevice(const QString &origin, const QString &token)
{
 if (credentialStatus("sender-save", writeSecret(deviceTarget(origin), token.toUtf8())) != ERROR_SUCCESS) return false;
 return loadDevice(origin) == token;
}

QString loadDevice(const QString &origin)
{
 QByteArray value;
 if (credentialStatus("sender-read", readSecret(deviceTarget(origin), value)) != ERROR_SUCCESS) return {};
 return QString::fromUtf8(value);
}

bool removeDevice(const QString &origin)
{
 return removeVerified(deviceTarget(origin), "sender-remove", "sender-remove-verification");
}

class CredentialManagerReceiveStore final : public ReceiveCredentialStore {
 QString target;
public:
 explicit CredentialManagerReceiveStore(const QString &service) : target(service + QStringLiteral(":latest-session")) {}
 Result load(const QString &origin, const QString &session, const QString &revision) override
 {
  QByteArray value;
  const auto status = credentialStatus("receiver-read", readSecret(target, value));
  if (status == ERROR_NOT_FOUND) return {};
  if (status != ERROR_SUCCESS) return {State::Error, {}};
  const auto document = QJsonDocument::fromJson(value);
  const auto object = document.object();
  if (!document.isObject() || object["version"] != 1 || !object["password"].isString()) return {State::Error, {}};
  if (revision.isEmpty() || object["origin"] != origin || object["session"] != session || object["revision"] != revision) return {};
  return {State::Found, object["password"].toString()};
 }
 bool save(const QString &origin, const QString &session, const QString &revision, const QString &password) override
 {
  if (password.isEmpty()) return clear();
  if (origin.isEmpty() || session.isEmpty() || revision.isEmpty()) return false;
  const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"origin", origin}, {"session", session},
   {"revision", revision}, {"password", password}}).toJson(QJsonDocument::Compact);
  if (credentialStatus("receiver-save", writeSecret(target, bytes)) != ERROR_SUCCESS) return false;
  const auto check = load(origin, session, revision);
  return check.state == State::Found && check.password == password;
 }
 bool clear() override { return removeVerified(target, "receiver-remove", "receiver-remove-verification"); }
};

std::unique_ptr<ReceiveCredentialStore> makeReceiveCredentialStore(const QString &service)
{
 return std::make_unique<CredentialManagerReceiveStore>(service);
}
}
