#pragma once

#include <QObject>

class QAction;
#ifdef __OBJC__
@class PixelviewUpdateObserver;
#endif

class PixelviewSparkle : public QObject {
	Q_OBJECT

public:
	explicit PixelviewSparkle(QAction *checkForUpdatesAction);
	~PixelviewSparkle() override;
	PixelviewSparkle(const PixelviewSparkle &) = delete;
	PixelviewSparkle &operator=(const PixelviewSparkle &) = delete;
	PixelviewSparkle(PixelviewSparkle &&) = delete;
	PixelviewSparkle &operator=(PixelviewSparkle &&) = delete;

	void checkForUpdates(bool manualCheck);

private:
#ifdef __OBJC__
	PixelviewUpdateObserver *observer;
#else
	void *observer;
#endif
};
