#pragma once
#include "PixelviewKeychainAction.hpp"
#include <QtCore/QThread>
#include <memory>
#include <optional>
#include <utility>

namespace pixelview {
// Only explicit button actions call this. Work owns value snapshots, never UI
// pointers. QObject disconnects completion on destruction; quitting does not
// wait on a native authorization dialog and no worker accesses a dead owner.
template<class Work, class Complete>
void runKeychainUserAction(QObject *owner, Work work, Complete complete)
{
 using Result = decltype(work());
 auto result = std::make_shared<std::optional<Result>>();
 auto *thread = QThread::create([result, work = std::move(work)]() mutable {
  KeychainUserAction action;
  result->emplace(work());
 });
 QObject::connect(thread, &QThread::finished, owner,
  [result, complete = std::move(complete)]() mutable { complete(std::move(result->value())); }, Qt::QueuedConnection);
 QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
 thread->start();
}
}
