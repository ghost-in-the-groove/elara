// SPDX-License-Identifier: AGPL-3.0-or-later
// The score's staff size follows the staves shown (ADR-0171): every track at
// once lays out smaller staves than one track alone, the page keeps its size
// on paper, and the caret stays on its staff.
#include <elara/notation/page_layout.hpp>

#include "score_document.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSizeF>
#include <QString>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace {

using Catch::Matchers::WithinAbs;
using elara::app::ScoreDocument;

template <typename Done>
bool settle(Done done) {
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < 10000) {
        QCoreApplication::processEvents();
    }
    return done();
}

bool laidOut(const ScoreDocument& score) {
    return !score.busy() && score.status() == ScoreDocument::Status::Ready && score.pageCount() > 0;
}

} // namespace

TEST_CASE("Staff size: every track at once gets smaller staves on pages of the same size",
          "[app]") {
    ScoreDocument score;
    score.newSong(QStringLiteral("electric-guitar"));
    for (int i = 0; i < 4; ++i) {
        score.addTrack(QStringLiteral("electric-guitar"));
    }
    score.setMeasureCount(96);
    REQUIRE(settle([&] { return laidOut(score) && score.trackNames().size() == 5; }));

    // One track alone (a track added is shown): the staff space asked for.
    score.setTrack(0);
    REQUIRE(settle([&] { return laidOut(score) && score.track() == 0; }));
    const double alone = score.staffSpace();
    CHECK(alone == 1.75);
    const QSizeF page = score.pageSize() * alone; // in mm
    const int pagesAlone = score.pageCount();

    score.setTrack(ScoreDocument::kAllTracks);
    REQUIRE(settle([&] { return laidOut(score) && score.track() == ScoreDocument::kAllTracks; }));
    const double all = score.staffSpace();
    CHECK(all < alone);
    // The paper stays A4: the page is wider and taller in staff spaces.
    CHECK_THAT(score.pageSize().width() * all, WithinAbs(page.width(), 1e-6));
    CHECK_THAT(score.pageSize().height() * all, WithinAbs(page.height(), 1e-6));
    CHECK(score.pageSize().width() > page.width() / alone);
    // Two systems of five guitars, notation over tablature, on a page after
    // the first, which also holds the credits.
    REQUIRE(score.pageCount() >= 2);
    const auto second = score.page(1);
    REQUIRE(second);
    CHECK(second->systems.size() >= 2);
    CHECK(score.pageCount() > pagesAlone);
    // The caret is still on the page, on its staff.
    CHECK(score.caretPage() >= 0);

    // Back to one track, back to the staff space asked for.
    score.setTrack(0);
    REQUIRE(settle([&] { return laidOut(score) && score.track() == 0; }));
    CHECK(score.staffSpace() == alone);
    CHECK(score.pageCount() == pagesAlone);
}
