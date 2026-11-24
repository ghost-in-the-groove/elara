// SPDX-License-Identifier: AGPL-3.0-or-later
// Results of worker jobs, handed to the interface thread from its event loop
// (ADR-0054).
#pragma once

#include <QFuture>
#include <QMetaObject>
#include <QObject>

#include <utility>

namespace elara::app {

/// Calls `done` with the result of `job` on `context`'s thread, always from
/// its event loop and never inside this call, or not at all once `context` is
/// gone.
///
/// `QFuture::then(context, ...)` alone runs the continuation at once when the
/// job has already finished as it is attached, inside the call that started
/// it. Then an edit shows its layout although the next edit of the same call
/// has already replaced it, and how many layouts a call shows depends on how
/// fast the workers are.
template <typename T, typename Done>
void whenDone(QFuture<T> job, QObject* context, Done done) {
    std::move(job).then(context, [context, done = std::move(done)](const T& result) mutable {
        QMetaObject::invokeMethod(
            context, [done = std::move(done), result]() mutable { done(result); },
            Qt::QueuedConnection);
    });
}

} // namespace elara::app
