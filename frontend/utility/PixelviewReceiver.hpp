#pragma once
#include <QtCore/QObject>
#include <QtCore/QUrl>
#include <QtCore/QTimer>
#include <QtCore/QJsonObject>
#include <functional>
#include <memory>

namespace pixelview {
// Private transport seam, also used by standalone compiled fault-injection tests.
// All events must be delivered on the controller's Qt thread. cancel() detaches events.
class ReceiverTransport {
public:
 struct Events {
  std::function<void(int, QByteArray)> login;
  std::function<void()> opened;
  std::function<void(QByteArray)> message;
  std::function<void(int)> closed;
 };
 virtual ~ReceiverTransport() = default;
 virtual void login(const QUrl &, const QByteArray &, Events) = 0;
 virtual void open(const QUrl &) = 0;
 virtual void send(const QByteArray &) = 0;
 virtual void cancel() = 0;
};
std::unique_ptr<ReceiverTransport> makeReceiverTransport();

// No Q_OBJECT/moc, OBS, pairing, Keychain or scene persistence dependencies.
// Construct and use on the Qt main thread. Callbacks must not delete this object.
class PixelviewReceiver : public QObject {
public:
 enum class State { Idle, Authenticating, Registering, Ready, Reconnecting, Error };
 explicit PixelviewReceiver(QObject *parent = nullptr,
                            std::unique_ptr<ReceiverTransport> transport = {});
 ~PixelviewReceiver() override;
 bool setOrigin(const QUrl &, bool allowLoopbackDevelopment = false);
 void start(const QString &sessionId, const QString &password, const QString &name);
 void stop();
 State state() const { return current; }
 QString status() const { return text; }
 std::function<void(const QString &)> onEndpoint;
 std::function<void()> onStopped;
 std::function<void()> onChanged;
private:
 void change(State, const QString &);
 void authenticate();
 void loginFinished(int, const QByteArray &);
 void message(const QByteArray &);
 void disconnected(int);
 void fail(const QString &);
 void clearAttempt();
 void send(const QString &, const QJsonObject & = {});
 std::unique_ptr<ReceiverTransport> transport;
 QUrl origin{"https://api4.pixelview.io"};
 bool development = false, intent = false, endpointDelivered = false;
 quint64 generation = 0;
 State current = State::Idle;
 QString text{"Not receiving."};
 QString sessionId, password, name, viewerId, endpoint;
 QTimer deadline, retry, refresh;
 int retryCount = 0;
};
}
