#pragma once
#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <functional>
#include <memory>

class QSslSocket;

namespace pixelview {
struct ControlPing;
// Minimal RFC 6455 client over QSslSocket for platforms without a native
// WebSocket stack (Windows, Linux); macOS keeps NSURLSessionWebSocketTask.
// Text messages only, no extensions, no redirects, no cookies or credential
// storage. Construct, use and destroy on one Qt thread. Callbacks may call
// close()/abort() or deleteLater() the client, but never delete it directly.
class WebSocketClient final : public QObject {
public:
 struct Closed {
  int code = 0;     // Close frame code, 1006 for an abnormal drop, 0 before open.
  int http = 0;     // Upgrade response status when the handshake was refused.
  bool tls = false; // Certificate or TLS handshake failure (never ignored).
 };
 struct Events {
  std::function<void()> opened;
  std::function<void(QByteArray)> message;
  std::function<void(Closed)> closed;
 };
 struct Options {
  qsizetype maxMessage = 16384;
  int connectTimeoutMs = 10000; // TCP, TLS and upgrade together.
  bool keepalive = true;        // RFC 6455 ping/pong liveness (ControlPing).
 };
 explicit WebSocketClient(QObject *parent = nullptr);
 ~WebSocketClient() override;
 void open(const QUrl &url, Events events, Options options);
 bool send(const QByteArray &text);
 // Detaches events, sends a close frame and tears down after a bounded grace.
 void close(int code = 1000);
 // Detaches events and drops the connection immediately.
 void abort();
 bool isOpen() const { return state == State::Open; }

 // Pure helpers, exposed for offline tests.
 static QByteArray acceptKey(const QByteArray &key);
 static QByteArray frame(quint8 opcode, const QByteArray &payload, quint32 mask);

private:
 enum class State { Idle, Connecting, Upgrading, Open, Closing, Done };
 void connected();
 void readyRead();
 bool parseUpgrade();
 void parseFrames();
 void control(quint8 opcode, const QByteArray &payload);
 void write(quint8 opcode, const QByteArray &payload);
 void finish(Closed);
 void protocolError(int code);
 void ping();

 State state = State::Idle;
 Events events;
 Options options;
 QUrl url;
 QByteArray key, buffer, fragments;
 bool fragmented = false, tlsFailed = false;
 QSslSocket *socket = nullptr;
 QTimer deadline, keepaliveTimer;
 QElapsedTimer clock;
 std::unique_ptr<ControlPing> pingPolicy;
};
}
