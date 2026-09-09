// Standalone real NSURLSession + production controller error harness.
#include "frontend/utility/PixelviewReceiver.hpp"
#import <Foundation/Foundation.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QUuid>
#include <cassert>
#include <iostream>
class ObservedTransport final : public pixelview::ReceiverTransport {
 std::unique_ptr<pixelview::ReceiverTransport> native = pixelview::makeReceiverTransport();
public:
 bool liveMissing = false;
 bool verifiedMissing = false;
 void login(const QUrl &url, const QByteArray &body, Events events) override {
  auto callback = events.login;
  events.login = [this, callback](int code, QByteArray response) {
   if (liveMissing) {
    // Never dump arbitrary server responses. Prove the exact known contract.
    assert(code == 401);
    assert(response == "{\"detail\":\"Unauthorized\"}");
    verifiedMissing = true;
   }
   callback(code, response);
  };
  native->login(url, body, std::move(events));
 }
 void open(const QUrl &url) override { native->open(url); }
 void send(const QByteArray &body) override { native->send(body); }
 void cancel() override { native->cancel(); }
};
int main(int argc, char **argv)
{
 QCoreApplication app(argc, argv);
 assert(argc == 4);
 auto transport = std::make_unique<ObservedTransport>();
 auto *observed = transport.get();
 observed->liveMissing = std::string(argv[3]) == "live";
 pixelview::PixelviewReceiver receiver(nullptr, std::move(transport));
 assert(receiver.setOrigin(QUrl(QString::fromLocal8Bit(argv[1])), true));
 bool endpoint = false;
 receiver.onEndpoint = [&](const QString &) { endpoint = true; };
 receiver.start("missing-" + QUuid::createUuid().toString(QUuid::WithoutBraces),
                "synthetic-not-a-user-password", "Native login error test");
 const QString expected = QString::fromUtf8(argv[2]);
 const bool reconnect = std::string(argv[3]) == "retry";
 const auto state = reconnect ? pixelview::PixelviewReceiver::State::Reconnecting : pixelview::PixelviewReceiver::State::Error;
 QElapsedTimer timer; timer.start();
 while (timer.elapsed() < 8000 && receiver.state() != state) {
  QCoreApplication::processEvents(); CFRunLoopRunInMode(kCFRunLoopDefaultMode, .01, true);
 }
 assert(receiver.state() == state);
 assert(!endpoint);
 if (receiver.status() != expected) {
  // Only the sanitized production status, never the response or request.
  std::cerr << "Unexpected receiver status: " << receiver.status().toStdString() << '\n';
  return 1;
 }
 std::cout << "PASS: " << receiver.status().toStdString() << '\n';
 if (observed->liveMissing) {
  assert(observed->verifiedMissing);
  std::cout << "LIVE missing-session HTTP 401 {\"detail\":\"Unauthorized\"}\n";
 }
 receiver.stop();
 return 0;
}
