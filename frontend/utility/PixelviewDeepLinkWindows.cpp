// Pixelview modification: pixelview:// player links on Windows.
// Windows starts a new process with the link as an argument. When Pixelview is
// already running, that process hands the link to the running instance over a
// per-user local socket and exits; otherwise the link waits in the inbox until
// the receive UI is ready. The link (which carries the session password) is
// never logged. https://play.pixelview.io links are not claimed on Windows.
#include "PixelviewDeepLinkInbox.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>
#include <windows.h>

namespace pixelview {
namespace {
constexpr qsizetype MAX_LINK = 8192;
constexpr int FORWARD_TIMEOUT_MS = 2000;

QString linkArgument()
{
 const auto arguments = QCoreApplication::arguments();
 for (qsizetype i = 1; i < arguments.size(); ++i)
  if (arguments[i].startsWith(QStringLiteral("pixelview://"), Qt::CaseInsensitive)) return arguments[i];
 return {};
}

QString pipeName()
{
 // Named pipes are machine-wide; scope the name to this Windows user.
 const auto user = qEnvironmentVariable("USERDOMAIN") + '\\' + qEnvironmentVariable("USERNAME");
 const auto hash = QCryptographicHash::hash(user.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
 return QStringLiteral("PixelviewDesktop-links-") + QString::fromLatin1(hash);
}

QString registryString(HKEY root, const wchar_t *key, const wchar_t *value)
{
 wchar_t data[2048];
 DWORD size = sizeof data;
 if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, data, &size) != ERROR_SUCCESS) return {};
 return QString::fromWCharArray(data);
}

bool setRegistryString(const wchar_t *key, const wchar_t *value, const QString &data)
{
 const auto wide = data.toStdWString();
 return RegSetKeyValueW(HKEY_CURRENT_USER, key, value, REG_SZ, wide.c_str(),
                        DWORD((wide.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

// The installer owns the registration. A development or portable build only
// claims pixelview:// when nothing usable is registered, so it never takes the
// scheme over from an installed copy.
void registerScheme()
{
 const auto existing = registryString(HKEY_CLASSES_ROOT, L"pixelview\\shell\\open\\command", nullptr);
 if (!existing.isEmpty()) {
  QString target = existing.trimmed();
  if (target.startsWith('"')) target = target.mid(1, target.indexOf('"', 1) - 1);
  else target = target.section(' ', 0, 0);
  if (QFileInfo::exists(target)) return;
 }
 const auto exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
 const bool ok = setRegistryString(L"Software\\Classes\\pixelview", nullptr, QStringLiteral("URL:Pixelview Desktop")) &&
  setRegistryString(L"Software\\Classes\\pixelview", L"URL Protocol", QString()) &&
  setRegistryString(L"Software\\Classes\\pixelview\\DefaultIcon", nullptr, QStringLiteral("\"%1\",0").arg(exe)) &&
  setRegistryString(L"Software\\Classes\\pixelview\\shell\\open\\command", nullptr, QStringLiteral("\"%1\" \"%2\"").arg(exe, QStringLiteral("%1")));
 if (!ok) qWarning("Pixelview links: could not register the pixelview:// handler for this user");
}
}

bool forwardWindowsDeepLink()
{
 const auto link = linkArgument();
 if (link.isEmpty() || link.size() > MAX_LINK) return false;
 QLocalSocket socket;
 socket.connectToServer(pipeName());
 if (!socket.waitForConnected(FORWARD_TIMEOUT_MS)) return false;
 socket.write(link.toUtf8() + '\n');
 if (!socket.waitForBytesWritten(FORWARD_TIMEOUT_MS)) return false;
 // The running instance acknowledges after taking the link.
 return socket.waitForReadyRead(FORWARD_TIMEOUT_MS) && socket.read(1) == "1";
}

void installWindowsDeepLinks()
{
 registerScheme();
 auto *server = new QLocalServer(QCoreApplication::instance());
 server->setSocketOptions(QLocalServer::UserAccessOption);
 server->setMaxPendingConnections(4);
 if (!server->listen(pipeName())) {
  qWarning("Pixelview links: could not listen for links from other launches");
  server->deleteLater();
 } else {
  QObject::connect(server, &QLocalServer::newConnection, server, [server] {
   while (auto *client = server->nextPendingConnection()) {
    QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
    QObject::connect(client, &QLocalSocket::readyRead, client, [client] {
     if (client->bytesAvailable() > MAX_LINK * 4 + 1) { client->abort(); return; }
     if (!client->canReadLine()) return;
     const auto line = QString::fromUtf8(client->readLine(MAX_LINK * 4 + 1)).trimmed();
     deepLinkInbox().submit(line);
     client->write("1");
     client->disconnectFromServer();
    });
   }
  });
 }
 const auto link = linkArgument();
 if (!link.isEmpty()) deepLinkInbox().submit(link);
}
}
