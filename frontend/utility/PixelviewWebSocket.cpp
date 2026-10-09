#include "PixelviewWebSocket.hpp"
#include "PixelviewControlPing.hpp"
#include <QtCore/QCryptographicHash>
#include <QtCore/QPointer>
#include <QtCore/QRandomGenerator>
#include <QtCore/QStringDecoder>
#include <QtNetwork/QSslSocket>
#include <utility>

namespace pixelview {
namespace {
constexpr qsizetype MAX_UPGRADE_RESPONSE = 16384;
constexpr int CLOSE_GRACE_MS = 2000;
bool headerHasToken(const QByteArray &value, const QByteArray &token)
{
 for (const auto &part : value.split(','))
  if (part.trimmed().compare(token, Qt::CaseInsensitive) == 0) return true;
 return false;
}
}

WebSocketClient::WebSocketClient(QObject *parent) : QObject(parent)
{
 deadline.setSingleShot(true);
 connect(&deadline, &QTimer::timeout, this, [this] {
  if (state == State::Closing) abort();
  else finish({});
 });
 keepaliveTimer.setTimerType(Qt::PreciseTimer);
 connect(&keepaliveTimer, &QTimer::timeout, this, [this] { ping(); });
}

WebSocketClient::~WebSocketClient() { abort(); }

QByteArray WebSocketClient::acceptKey(const QByteArray &k)
{
 return QCryptographicHash::hash(k + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
}

QByteArray WebSocketClient::frame(quint8 opcode, const QByteArray &payload, quint32 mask)
{
 QByteArray out;
 out.reserve(payload.size() + 14);
 out.append(char(0x80 | (opcode & 0x0f)));
 const quint64 n = quint64(payload.size());
 if (n < 126) {
  out.append(char(0x80 | n));
 } else if (n <= 0xffff) {
  out.append(char(0x80 | 126));
  out.append(char(n >> 8)).append(char(n & 0xff));
 } else {
  out.append(char(0x80 | 127));
  for (int shift = 56; shift >= 0; shift -= 8) out.append(char((n >> shift) & 0xff));
 }
 const char m[4] = {char(mask >> 24), char((mask >> 16) & 0xff), char((mask >> 8) & 0xff), char(mask & 0xff)};
 out.append(m, 4);
 for (qsizetype i = 0; i < payload.size(); ++i) out.append(char(payload[i] ^ m[i % 4]));
 return out;
}

void WebSocketClient::open(const QUrl &target, Events e, Options o)
{
 abort();
 url = target;
 events = std::move(e);
 options = o;
 buffer.clear(); fragments.clear(); fragmented = false; tlsFailed = false;
 const bool secure = url.scheme() == QStringLiteral("wss") || url.scheme() == QStringLiteral("https");
 if (!url.isValid() || url.host().isEmpty() || !(secure || url.scheme() == QStringLiteral("ws") || url.scheme() == QStringLiteral("http"))) {
  state = State::Connecting;
  QTimer::singleShot(0, this, [this] { finish({}); });
  return;
 }
 state = State::Connecting;
 socket = new QSslSocket(this);
 auto *s = socket;
 connect(s, &QSslSocket::sslErrors, this, [this, s](const QList<QSslError> &) {
  if (s == socket) tlsFailed = true; // Never ignored: the handshake fails.
 });
 connect(s, &QAbstractSocket::errorOccurred, this, [this, s](QAbstractSocket::SocketError error) {
  if (s != socket) return;
  if (error == QAbstractSocket::SslHandshakeFailedError || error == QAbstractSocket::SslInvalidUserDataError) tlsFailed = true;
  if (state == State::Closing) { abort(); return; }
  Closed closed;
  closed.code = state == State::Open ? 1006 : 0;
  closed.tls = tlsFailed && state != State::Open;
  finish(closed);
 });
 connect(s, &QAbstractSocket::disconnected, this, [this, s] {
  if (s != socket) return;
  if (state == State::Closing) { abort(); return; }
  finish({state == State::Open ? 1006 : 0, 0, false});
 });
 connect(s, &QIODevice::readyRead, this, [this, s] { if (s == socket) readyRead(); });
 if (secure) {
  connect(s, &QSslSocket::encrypted, this, [this, s] { if (s == socket) connected(); });
  s->connectToHostEncrypted(url.host(), quint16(url.port(443)));
 } else {
  connect(s, &QAbstractSocket::connected, this, [this, s] { if (s == socket) connected(); });
  s->connectToHost(url.host(), quint16(url.port(80)));
 }
 deadline.start(options.connectTimeoutMs);
}

void WebSocketClient::connected()
{
 if (state != State::Connecting) return;
 state = State::Upgrading;
 QByteArray nonce(16, Qt::Uninitialized);
 QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(nonce.data()), 4);
 key = nonce.toBase64();
 QByteArray host = url.host(QUrl::FullyEncoded).toUtf8();
 if (host.contains(':')) host = '[' + host + ']';
 const bool secure = url.scheme() == QStringLiteral("wss") || url.scheme() == QStringLiteral("https");
 if (url.port() >= 0 && url.port() != (secure ? 443 : 80)) host += ':' + QByteArray::number(url.port());
 QByteArray target = url.path(QUrl::FullyEncoded).toUtf8();
 if (target.isEmpty()) target = "/";
 if (url.hasQuery()) target += '?' + url.query(QUrl::FullyEncoded).toUtf8();
 socket->write("GET " + target + " HTTP/1.1\r\n"
               "Host: " + host + "\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Sec-WebSocket-Key: " + key + "\r\n"
               "Sec-WebSocket-Version: 13\r\n"
               "User-Agent: Pixelview Desktop\r\n"
               "\r\n");
}

void WebSocketClient::readyRead()
{
 if (!socket) return;
 buffer.append(socket->readAll());
 if (state == State::Upgrading && !parseUpgrade()) return;
 if (state == State::Open || state == State::Closing) parseFrames();
}

bool WebSocketClient::parseUpgrade()
{
 const auto end = buffer.indexOf("\r\n\r\n");
 if (end < 0) {
  if (buffer.size() > MAX_UPGRADE_RESPONSE) finish({});
  return false;
 }
 const auto head = buffer.left(end);
 buffer.remove(0, end + 4);
 const auto lines = head.split('\n');
 const auto status = lines.value(0).trimmed().split(' ');
 if (status.size() < 2 || !status[0].startsWith("HTTP/1.")) { finish({}); return false; }
 bool ok = false;
 const int code = status[1].toInt(&ok);
 if (!ok || code != 101) { finish({0, ok ? code : 0, false}); return false; }
 QByteArray upgrade, connection, accept;
 bool negotiated = false;
 for (qsizetype i = 1; i < lines.size(); ++i) {
  const auto line = lines[i].trimmed();
  const auto colon = line.indexOf(':');
  if (colon <= 0) continue;
  const auto name = line.left(colon).trimmed().toLower();
  const auto value = line.mid(colon + 1).trimmed();
  if (name == "upgrade") upgrade = value;
  else if (name == "connection") connection = value;
  else if (name == "sec-websocket-accept") accept = value;
  else if (name == "sec-websocket-extensions" || name == "sec-websocket-protocol") negotiated = true;
 }
 // Nothing was offered, so any negotiated extension or subprotocol is a protocol error.
 if (upgrade.compare("websocket", Qt::CaseInsensitive) != 0 || !headerHasToken(connection, "upgrade") ||
     accept != acceptKey(key) || negotiated) {
  finish({});
  return false;
 }
 state = State::Open;
 deadline.stop();
 if (options.keepalive) {
  pingPolicy = std::make_unique<ControlPing>();
  clock.start();
  keepaliveTimer.start(250);
 }
 QPointer<WebSocketClient> guard(this);
 if (events.opened) { auto callback = events.opened; callback(); }
 return guard && state == State::Open;
}

void WebSocketClient::parseFrames()
{
 QPointer<WebSocketClient> guard(this);
 while (guard && (state == State::Open || state == State::Closing) && buffer.size() >= 2) {
  const auto b0 = quint8(buffer[0]), b1 = quint8(buffer[1]);
  const bool fin = b0 & 0x80;
  const quint8 opcode = b0 & 0x0f;
  if ((b0 & 0x70) || (b1 & 0x80)) { protocolError(1002); return; } // No extensions; servers never mask.
  quint64 length = b1 & 0x7f;
  qsizetype header = 2;
  if (length == 126) {
   if (buffer.size() < 4) return;
   length = (quint64(quint8(buffer[2])) << 8) | quint8(buffer[3]);
   header = 4;
  } else if (length == 127) {
   if (buffer.size() < 10) return;
   length = 0;
   for (int i = 2; i < 10; ++i) length = (length << 8) | quint8(buffer[i]);
   header = 10;
  }
  if (opcode >= 8 && (!fin || length > 125)) { protocolError(1002); return; }
  if (length > quint64(options.maxMessage)) { protocolError(1009); return; }
  if (buffer.size() < header + qsizetype(length)) return;
  const auto payload = buffer.mid(header, qsizetype(length));
  buffer.remove(0, header + qsizetype(length));
  QByteArray complete;
  bool deliver = false;
  switch (opcode) {
  case 0x0:
   if (!fragmented) { protocolError(1002); return; }
   if (fragments.size() + payload.size() > options.maxMessage) { protocolError(1009); return; }
   fragments += payload;
   if (fin) { complete = std::exchange(fragments, {}); fragmented = false; deliver = true; }
   break;
  case 0x1:
   if (fragmented) { protocolError(1002); return; }
   if (fin) { complete = payload; deliver = true; }
   else { fragments = payload; fragmented = true; }
   break;
  case 0x2: protocolError(1003); return; // Text-only protocol.
  case 0x8: case 0x9: case 0xA: control(opcode, payload); continue;
  default: protocolError(1002); return;
  }
  if (!deliver || state != State::Open) continue;
  QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
  const QString text = utf8.decode(complete); // decode() is lazy until converted.
  if (utf8.hasError()) { protocolError(1007); return; }
  if (events.message) { auto callback = events.message; callback(complete); }
 }
}

void WebSocketClient::control(quint8 opcode, const QByteArray &payload)
{
 if (opcode == 0x9) { if (state == State::Open) write(0xA, payload); return; }
 if (opcode == 0xA) {
  if (!pingPolicy || !pingPolicy->pending) return; // Unsolicited pongs time nothing.
  pingPolicy->pong();
  if (events.pong) { auto callback = events.pong; callback(int(clock.elapsed() - pingPolicy->sent)); }
  return;
 }
 // Close: echo the code (RFC 6455 5.5.1), then report it.
 if (payload.size() == 1) { protocolError(1002); return; }
 const int code = payload.size() >= 2 ? (quint8(payload[0]) << 8) | quint8(payload[1]) : 1005;
 if (state == State::Closing) { abort(); return; }
 QByteArray echo;
 if (payload.size() >= 2) echo = payload.left(2);
 write(0x8, echo);
 finish({code, 0, false});
}

void WebSocketClient::write(quint8 opcode, const QByteArray &payload)
{
 if (socket) socket->write(frame(opcode, payload, QRandomGenerator::system()->generate()));
}

bool WebSocketClient::send(const QByteArray &text)
{
 if (state != State::Open || text.size() > options.maxMessage) return false;
 write(0x1, text);
 return true;
}

void WebSocketClient::ping()
{
 if (state != State::Open || !pingPolicy) return;
 const auto action = pingPolicy->poll(clock.elapsed());
 if (action == ControlPing::Timeout) finish({1006, 0, false});
 else if (action == ControlPing::Ping) write(0x9, {});
}

void WebSocketClient::protocolError(int code)
{
 if (state == State::Open) {
  QByteArray payload;
  payload.append(char(code >> 8)).append(char(code & 0xff));
  write(0x8, payload);
 }
 finish({code, 0, false});
}

void WebSocketClient::finish(Closed closed)
{
 if (state == State::Done || state == State::Idle) return;
 state = State::Done;
 deadline.stop(); keepaliveTimer.stop(); pingPolicy.reset();
 if (socket) {
  auto *s = std::exchange(socket, nullptr);
  s->disconnect(this);
  s->disconnectFromHost(); // Flushes any echoed close frame.
  QTimer::singleShot(CLOSE_GRACE_MS, s, [s] { s->abort(); });
  connect(s, &QAbstractSocket::disconnected, s, &QObject::deleteLater);
  if (s->state() == QAbstractSocket::UnconnectedState) s->deleteLater();
 }
 auto callback = std::move(events.closed);
 events = {};
 if (callback) callback(closed);
}

void WebSocketClient::close(int code)
{
 events = {};
 if (state != State::Open) { abort(); return; }
 QByteArray payload;
 payload.append(char(code >> 8)).append(char(code & 0xff));
 write(0x8, payload);
 state = State::Closing;
 keepaliveTimer.stop(); pingPolicy.reset();
 deadline.start(CLOSE_GRACE_MS);
}

void WebSocketClient::abort()
{
 events = {};
 deadline.stop(); keepaliveTimer.stop(); pingPolicy.reset();
 if (socket) {
  auto *s = std::exchange(socket, nullptr);
  s->disconnect(this);
  s->abort();
  s->deleteLater();
 }
 state = state == State::Idle ? State::Idle : State::Done;
}
}
