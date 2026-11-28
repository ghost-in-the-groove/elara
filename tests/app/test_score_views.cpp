// SPDX-License-Identifier: AGPL-3.0-or-later
// The views of a song shown before come back as they were (ADR-0175): the
// layout worker keeps a memory of each, so that going to another track and
// back lays out only what was edited meanwhile, and nothing when nothing was.
// The continuous view shows the song in strips in place of pages (ADR-0057).
#include "score_document.hpp"
#include "songs_to_edit.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QList>
#include <QString>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>

using elara::app::ScoreDocument;
using Move = ScoreDocument::CaretMove;
using View = ScoreDocument::View;

namespace {

template <typename Done>
bool settle(Done done) {
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < 10000) {
        QCoreApplication::processEvents();
    }
    return done();
}

// A new song of a guitar and a bass, the bass shown, both laid out.
struct Views {
    ScoreDocument score;

    Views() {
        score.newSong(QStringLiteral("electric-guitar"));
        REQUIRE(laidOut());
        score.addTrack(QStringLiteral("electric-bass"));
        REQUIRE(laidOut());
        REQUIRE(score.track() == 1);
    }

    [[nodiscard]] bool laidOut() {
        return settle([this] { return score.pageCount() > 0 && !score.busy(); });
    }
    [[nodiscard]] bool show(int track) {
        score.setTrack(track);
        return laidOut() && score.track() == track;
    }
    [[nodiscard]] bool show(View view) {
        score.setView(view);
        return laidOut() && score.view() == view;
    }
};

} // namespace

TEST_CASE("views: a track shown before comes back with its pages, nothing laid out again") {
    Views views;
    ScoreDocument& score = views.score;
    // Each track shown once, its pages held so that they stay where they are:
    // the same address is then the same pages, not others that took their
    // place.
    REQUIRE(views.show(0));
    const auto guitar = score.page(0);
    REQUIRE(guitar);
    REQUIRE(views.show(1));
    const auto bass = score.page(0);
    REQUIRE(bass);
    CHECK(guitar.get() != bass.get());
    const int kept = score.layoutsKept();

    REQUIRE(views.show(0));
    CHECK(score.page(0).get() == guitar.get());
    CHECK(score.layoutsKept() == kept + 1);
    REQUIRE(views.show(1));
    CHECK(score.page(0).get() == bass.get());
    REQUIRE(views.show(0));
    CHECK(score.page(0).get() == guitar.get());
    CHECK(score.layoutsKept() == kept + 3);

    // Its tablature alone is another view, and both come back as well.
    REQUIRE(views.show(View::Tablature));
    const auto tablature = score.page(0);
    REQUIRE(tablature);
    CHECK(tablature.get() != guitar.get());
    CHECK(score.layoutsKept() == kept + 3);
    REQUIRE(views.show(View::Both));
    CHECK(score.page(0).get() == guitar.get());
    REQUIRE(views.show(View::Tablature));
    CHECK(score.page(0).get() == tablature.get());
    CHECK(score.layoutsKept() == kept + 5);

    // So does the continuous view, and the pages after it.
    score.setContinuous(true);
    REQUIRE(views.laidOut());
    CHECK(score.showsStrips());
    CHECK(score.page(0).get() != tablature.get());
    score.setContinuous(false);
    REQUIRE(views.laidOut());
    CHECK(score.page(0).get() == tablature.get());
    CHECK(score.layoutsKept() == kept + 6);
}

TEST_CASE("views: a view kept shows the song as it is now, not as it was laid out") {
    Views views;
    ScoreDocument& score = views.score;
    REQUIRE(views.show(0));
    const auto empty = score.page(0);
    REQUIRE(empty);

    // A fret on the guitar: its pages change.
    score.typeDigit(5);
    score.moveCaret(Move::StringDown); // the moment for a second digit passes
    score.moveCaret(Move::StringUp);
    REQUIRE(views.laidOut());
    const auto typed = score.page(0);
    REQUIRE(typed);
    REQUIRE_FALSE(*typed == *empty);

    // Taken back while the bass is shown: the guitar's view kept the fret.
    REQUIRE(views.show(1));
    const int kept = score.layoutsKept();
    score.undo();
    REQUIRE(views.laidOut());
    REQUIRE(views.show(0));
    CHECK(score.layoutsKept() == kept); // laid out, not given back
    const auto back = score.page(0);
    REQUIRE(back);
    CHECK(back.get() != typed.get());
    CHECK(*back == *empty);

    // And the bass's own view, laid out again by the undo, is the one kept.
    REQUIRE(views.show(1));
    CHECK(score.layoutsKept() == kept + 1);
}

TEST_CASE("views: only the views shown last are kept, and the others are laid out again") {
    Views views;
    ScoreDocument& score = views.score;
    REQUIRE(views.show(0));
    const auto first = score.page(0);
    REQUIRE(first);
    const int kept = score.layoutsKept();

    // Six other views, each for the first time and as many as are kept: the
    // tablature and the notation of the guitar, of the bass and of every track.
    REQUIRE(views.show(View::Tablature));
    REQUIRE(views.show(View::Standard));
    REQUIRE(views.show(1));
    REQUIRE(views.show(View::Tablature));
    REQUIRE(views.show(ScoreDocument::kAllTracks));
    REQUIRE(views.show(View::Standard));
    CHECK(score.layoutsKept() == kept);

    // The guitar's notation is among them still; the view shown first is
    // not, and is laid out again, the same as it was.
    REQUIRE(views.show(0));
    CHECK(score.layoutsKept() == kept + 1);
    REQUIRE(views.show(View::Both));
    CHECK(score.layoutsKept() == kept + 1);
    const auto again = score.page(0);
    REQUIRE(again);
    CHECK(again.get() != first.get());
    CHECK(*again == *first);

    // A new song starts without the views of the one before.
    score.newSong(QStringLiteral("electric-guitar"));
    REQUIRE(views.laidOut());
    CHECK(score.layoutsKept() == kept + 1);
}

TEST_CASE("views: the continuous view shows strips no higher than a page, then the pages again") {
    using elara::app::testing::kSongsToEdit;
    using elara::app::testing::SongToEdit;
    const char* source = GENERATE(from_range(kSongsToEdit));
    INFO(source);
    SongToEdit song(source);
    ScoreDocument& score = song.score;
    const int pages = score.pageCount();
    REQUIRE_FALSE(score.continuous());
    REQUIRE_FALSE(score.showsStrips());

    // ADR-0057: strips of one endless page, stacked without gaps.
    score.setContinuous(true);
    CHECK(score.continuous());
    REQUIRE(song.laidOut());
    CHECK(score.showsStrips());
    const double height = score.pageSize().height();
    const QList<qreal> strips = score.pageTops();
    REQUIRE(strips.size() == score.pageCount() + 1);
    CHECK(strips.front() == 0.0);
    for (qsizetype s = 1; s < strips.size(); ++s) {
        CHECK(strips[s] > strips[s - 1]);
        CHECK(strips[s] - strips[s - 1] <= height + 1e-6);
    }

    // The pages come back as they were, a page high each.
    score.setContinuous(false);
    REQUIRE(song.laidOut());
    CHECK_FALSE(score.showsStrips());
    CHECK(score.pageCount() == pages);
    const QList<qreal> tops = score.pageTops();
    REQUIRE(tops.size() == pages + 1);
    CHECK(std::abs(tops.back() - height * pages) < 1e-6);
}
