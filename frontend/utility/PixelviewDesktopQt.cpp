// Desktop control socket for platforms without NSURLSession (Windows, Linux).
// Close classification matches PixelviewDesktopMac.mm: a rejected token
// (HTTP 401) is 4401, a forbidden/redirected upgrade or TLS failure is 4403,
// everything else is a transient drop.
#include "PixelviewDesktopConnection.hpp"
#include "PixelviewWebSocket.hpp"
#include <QtCore/QUrlQuery>
#include <utility>

namespace pixelview {
namespace {
constexpr int CLOSE_GRACE_MS = 2500;
int desktopCloseCode(const WebSocketClient::Closed &closed)
{
 if (closed.http == 401) return 4401;
 if (closed.http == 403 || (closed.http >= 300 && closed.http < 400) || closed.tls) return 4403;
 if (closed.code == 1003 || closed.code == 1007) return 4400; // Non-text payload.
 return closed.code;
}
}

void DesktopConnection::openSocket(QUrl url, QString token)
{
 closeSocket();
 // The device token authenticates the upgrade itself; there is no first message.
 QUrlQuery query; query.addQueryItem("token", token);
 url.setQuery(query);
 auto *client = new WebSocketClient(this);
 socket = client;
 WebSocketClient::Events events;
 events.message = [this, client](QByteArray body) {
  if (socket != client) return;
  auto callback = message; callback(body);
 };
 events.closed = [this, client](WebSocketClient::Closed closed) {
  if (socket != client) return;
  auto callback = disconnected; callback(desktopCloseCode(closed));
 };
 WebSocketClient::Options options;
 options.maxMessage = 16384;
 options.connectTimeoutMs = 10000;
 client->open(url, std::move(events), options);
}

void DesktopConnection::sendSocket(QByteArray body)
{
 if (!socket || body.size() > 16384) return;
 auto *client = static_cast<WebSocketClient *>(socket);
 if (!client->send(body) && client->isOpen()) client->abort();
}

void DesktopConnection::closeSocket(bool normal)
{
 if (!socket) return;
 auto *client = static_cast<WebSocketClient *>(std::exchange(socket, nullptr));
 if (normal) {
  client->close(1000);
  QTimer::singleShot(CLOSE_GRACE_MS, client, &QObject::deleteLater);
 } else {
  client->abort();
  client->deleteLater();
 }
}
}
