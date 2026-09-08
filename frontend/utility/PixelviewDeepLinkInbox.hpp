// Pixelview modification: bounded memory-only OS delivery until the receive UI is ready.
#pragma once
#include "PixelviewDeepLink.hpp"
#include <QCoreApplication>
#include <QFileOpenEvent>
#include <QObject>
#include <QPointer>
#include <functional>

namespace pixelview {
class DeepLinkInbox final : public QObject {
	QPointer<QObject> target;
	std::function<void(const std::optional<DeepLink> &)> handler;
	std::optional<DeepLink> pending;
	bool hasPending = false, uiReady = false, stopped = false;
	void drain()
	{
		if (!uiReady || !target || !handler || !hasPending || stopped) return;
		auto link = std::move(pending);
		pending.reset(); hasPending = false;
		handler(link);
	}
protected:
	bool eventFilter(QObject *object, QEvent *event) override
	{
		if (object == parent() && event->type() == QEvent::FileOpen) {
			auto *open = static_cast<QFileOpenEvent *>(event);
			if (!open->url().isEmpty() && !open->url().isLocalFile()) {
				submit(open->url().toString(QUrl::FullyEncoded));
				event->accept(); return true;
			}
		}
		return QObject::eventFilter(object, event);
	}
public:
	explicit DeepLinkInbox(QObject *application) : QObject(application)
	{
		application->installEventFilter(this);
		connect(application, &QObject::destroyed, this, [this] { shutdown(); });
	}
	void submit(const QString &raw)
	{
		if (stopped) return;
		pending = parseDeepLink(raw); hasPending = true; drain();
	}
	void attach(QObject *context, std::function<void(const std::optional<DeepLink> &)> callback)
	{
		target = context; handler = std::move(callback); drain();
		connect(context, &QObject::destroyed, this, [this] { shutdown(); });
	}
	void ready() { uiReady = true; drain(); }
	void shutdown() { stopped = true; pending.reset(); hasPending = false; handler = {}; target = nullptr; }
};
inline DeepLinkInbox &deepLinkInbox()
{
	static QPointer<DeepLinkInbox> inbox;
	if (!inbox) inbox = new DeepLinkInbox(QCoreApplication::instance());
	return *inbox;
}
#ifdef __APPLE__
void installMacDeepLinks();
#endif
} // namespace pixelview
