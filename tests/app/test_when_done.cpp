// SPDX-License-Identifier: AGPL-3.0-or-later
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFuture>
#include <QObject>
#include <QPromise>
#include <catch2/catch_test_macros.hpp>
#include <when_done.hpp>

#include <memory>

using elara::app::whenDone;

namespace {

// Processes events until `done` holds or a second has gone by.
template <typename Done>
bool settle(Done done) {
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < 1000) {
        QCoreApplication::processEvents();
    }
    return done();
}

} // namespace

TEST_CASE("whenDone hands over a job that has already finished from the event loop",
          "[app][jobs]") {
    QObject context;
    int got = 0;
    whenDone(QtFuture::makeReadyValueFuture(42), &context, [&got](int value) { got = value; });
    // Not inside the call that started the job, where a later edit of the
    // same call may already have replaced its result.
    CHECK(got == 0);
    CHECK(settle([&got] { return got == 42; }));
}

TEST_CASE("whenDone hands over a job that finishes later from the event loop", "[app][jobs]") {
    QObject context;
    QPromise<int> promise;
    promise.start();
    int got = 0;
    whenDone(promise.future(), &context, [&got](int value) { got = value; });
    promise.addResult(7);
    promise.finish();
    // Finished on this very thread: still not at once.
    CHECK(got == 0);
    CHECK(settle([&got] { return got == 7; }));
}

TEST_CASE("whenDone hands nothing to a context that has gone", "[app][jobs]") {
    int got = 0;
    {
        auto context = std::make_unique<QObject>();
        whenDone(QtFuture::makeReadyValueFuture(5), context.get(),
                 [&got](int value) { got = value; });
        // The result waits in the event queue; its context goes first.
    }
    for (int i = 0; i < 10; ++i) {
        QCoreApplication::processEvents();
    }
    CHECK(got == 0);
}
