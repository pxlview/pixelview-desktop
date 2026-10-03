// Drives the portable (Windows/Linux) Qt transport against a loopback server.
// Prints one JSON object per event; test_websocket_qt.py asserts on them.
//   probe ws <url> [keepalive] [echo] [close-on-message] [send-big]
//   probe desktop <url>
//   probe receiver <login-url> <ws-url>
#include "frontend/utility/PixelviewDesktopConnection.hpp"
#include "frontend/utility/PixelviewReceiver.hpp"
#include "frontend/utility/PixelviewWebSocket.hpp"
#include <QtCore/QCoreApplication>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimer>
#include <cstdio>

static void report(const QJsonObject &o)
{
 std::printf("%s\n", QJsonDocument(o).toJson(QJsonDocument::Compact).constData());
 std::fflush(stdout);
}

int main(int argc, char **argv)
{
 QCoreApplication app(argc, argv);
 const auto args = app.arguments();
 if (args.size() < 3) return 2;
 const auto mode = args[1];
 auto has = [&](const char *flag) { return args.contains(QString::fromLatin1(flag)); };
 QTimer::singleShot(has("keepalive") ? 60000 : 8000, &app, [] { report({{"event", "timeout"}}); QCoreApplication::exit(3); });
 auto quitSoon = [] { QTimer::singleShot(300, qApp, [] { QCoreApplication::exit(0); }); };

 pixelview::WebSocketClient client;
 pixelview::DesktopConnection desktop;
 std::unique_ptr<pixelview::ReceiverTransport> receiver;

 if (mode == "ws") {
  pixelview::WebSocketClient::Events e;
  e.opened = [&] {
   report({{"event", "opened"}});
   if (has("send-big")) report({{"event", "sent"}, {"ok", client.send(QByteArray(20000, 'x'))}});
  };
  e.message = [&](QByteArray body) {
   report({{"event", "message"}, {"data", QString::fromUtf8(body)}, {"size", int(body.size())}});
   if (has("echo")) client.send("echo:" + body);
   if (has("close-on-message")) { client.close(1000); report({{"event", "closing"}}); QTimer::singleShot(2500, qApp, [] { QCoreApplication::exit(0); }); }
  };
  e.closed = [&](pixelview::WebSocketClient::Closed c) {
   report({{"event", "closed"}, {"code", c.code}, {"http", c.http}, {"tls", c.tls}});
   quitSoon();
  };
  pixelview::WebSocketClient::Options o;
  o.maxMessage = 16384;
  o.keepalive = has("keepalive");
  client.open(QUrl(args[2]), std::move(e), o);
 } else if (mode == "desktop") {
  desktop.message = [&](QByteArray body) {
   report({{"event", "message"}, {"data", QString::fromUtf8(body)}});
   desktop.sendSocket(QJsonDocument(QJsonObject{{"message", "PONG_RESPONSE"}}).toJson(QJsonDocument::Compact));
  };
  desktop.disconnected = [&](int code) { report({{"event", "disconnected"}, {"code", code}}); desktop.closeSocket(); quitSoon(); };
  desktop.openSocket(QUrl(args[2]), QStringLiteral("device-token"));
 } else if (mode == "receiver" && args.size() >= 4) {
  receiver = pixelview::makeReceiverTransport();
  const QUrl ws(args[3]);
  pixelview::ReceiverTransport::Events e;
  e.login = [&, ws](int code, QByteArray body) {
   report({{"event", "login"}, {"code", code}, {"size", int(body.size())}});
   if (code == 200) receiver->open(ws); else quitSoon();
  };
  e.opened = [&] { report({{"event", "opened"}}); receiver->send(R"({"message":"ADD_VIEWER_WEB"})"); };
  e.message = [&](QByteArray body) {
   report({{"event", "message"}, {"data", QString::fromUtf8(body)}});
   receiver->cancel();
   report({{"event", "cancelled"}});
   QTimer::singleShot(2500, qApp, [] { QCoreApplication::exit(0); });
  };
  e.closed = [&](int code) { report({{"event", "closed"}, {"code", code}}); quitSoon(); };
  receiver->login(QUrl(args[2]), R"({"password":"fixture"})", std::move(e));
 } else {
  return 2;
 }
 return app.exec();
}
