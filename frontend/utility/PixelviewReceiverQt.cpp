// Receiver transport for platforms without NSURLSession (Windows, Linux).
// Behaviour mirrors PixelviewReceiverMac.mm: no redirects (the password and
// client_token are never forwarded), no cookies or caches, a 256 KiB bound on
// the login body and messages, TLS failures are terminal, and an auth refusal
// on the upgrade is a policy violation (1008).
#include "PixelviewReceiver.hpp"
#include "PixelviewWebSocket.hpp"
#include <QtCore/QPointer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <utility>

namespace pixelview {
namespace {
constexpr qint64 MAX_BODY = 262144;
constexpr int CLOSE_GRACE_MS = 2500;
int receiverCloseCode(const WebSocketClient::Closed &closed)
{
 if (closed.http == 401 || closed.http == 403 || (closed.http >= 300 && closed.http < 400)) return 1008;
 if (closed.tls) return 4403;
 if (closed.code == 1003 || closed.code == 1007) return 1008;
 return closed.code;
}
bool tlsFailure(QNetworkReply::NetworkError error)
{
 return error == QNetworkReply::SslHandshakeFailedError;
}
}

class QtReceiverTransport final : public ReceiverTransport {
 QNetworkAccessManager http;
 QPointer<QNetworkReply> reply;
 QPointer<WebSocketClient> socket;
 Events events;
 bool active = false;
public:
 ~QtReceiverTransport() override { cancel(); }
 void login(const QUrl &url, const QByteArray &body, Events e) override
 {
  cancel();
  active = true;
  events = std::move(e);
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
  request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
  request.setTransferTimeout(15000);
  auto *r = http.post(request, body);
  reply = r;
  auto tooLarge = std::make_shared<bool>(false);
  auto bound = [r, tooLarge] {
   const auto length = r->header(QNetworkRequest::ContentLengthHeader);
   if ((length.isValid() && length.toLongLong() > MAX_BODY) || r->bytesAvailable() > MAX_BODY) {
    *tooLarge = true; r->abort();
   }
  };
  QObject::connect(r, &QNetworkReply::metaDataChanged, r, bound);
  QObject::connect(r, &QIODevice::readyRead, r, bound);
  QObject::connect(r, &QNetworkReply::finished, r, [this, r, tooLarge] {
   r->deleteLater();
   if (reply != r || !active) return;
   reply = nullptr;
   const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
   const QByteArray content = *tooLarge ? QByteArray() : r->read(MAX_BODY);
   // An HTTP status is authoritative; TLS errors are terminal; other network errors may retry.
   int code = status;
   if (*tooLarge) code = 413;
   else if (!status && r->error() != QNetworkReply::NoError) code = tlsFailure(r->error()) ? 495 : 0;
   if (events.login) { auto callback = events.login; callback(code, content); }
  });
 }
 void open(const QUrl &url) override
 {
  if (!active) return;
  if (socket) { socket->abort(); socket->deleteLater(); }
  auto *client = new WebSocketClient;
  socket = client;
  WebSocketClient::Events e;
  e.opened = [this, client] {
   if (!active || socket != client) return;
   if (events.opened) { auto callback = events.opened; callback(); }
  };
  e.message = [this, client](QByteArray body) {
   if (!active || socket != client) return;
   if (events.message) { auto callback = events.message; callback(body); }
  };
  e.closed = [this, client](WebSocketClient::Closed closed) {
   if (!active || socket != client) return;
   if (events.closed) { auto callback = events.closed; callback(receiverCloseCode(closed)); }
  };
  WebSocketClient::Options options;
  options.maxMessage = MAX_BODY;
  options.connectTimeoutMs = 15000;
  client->open(url, std::move(e), options);
 }
 void send(const QByteArray &body) override
 {
  if (!active || !socket) return;
  if (!socket->send(body) && socket->isOpen()) {
   // Oversized outbound message: drop the connection as a transient failure.
   auto client = socket;
   client->abort();
   if (events.closed) { auto callback = events.closed; callback(1006); }
  }
 }
 void cancel() override
 {
  active = false;
  events = {};
  if (auto r = std::exchange(reply, nullptr)) { r->disconnect(); r->abort(); r->deleteLater(); }
  // Backend removes this viewer on websocket disconnect. There is no REMOVE
  // mutation. Allow the normal close frame to flush before bounded teardown.
  if (auto client = std::exchange(socket, nullptr)) {
   client->close(1000);
   QTimer::singleShot(CLOSE_GRACE_MS, client, &QObject::deleteLater);
  }
 }
};

std::unique_ptr<ReceiverTransport> makeReceiverTransport() { return std::make_unique<QtReceiverTransport>(); }
}
