// SPDX-License-Identifier: AGPL-3.0-or-later
// The layout issues of the pages shown (ADR-0150): found on the layout worker
// for the pages a layout changed, and kept for the others.
#include <elara/core/measure.hpp>
#include <elara/core/song.hpp>
#include <elara/io/native_format.hpp>

#include "score_document.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <vector>

using elara::app::PageIssues;
using elara::app::ScoreDocument;
using elara::core::Beat;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::Instrument;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::TimeSignature;
using elara::core::Track;

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

// `count` measures of 32/4, each of 128 sixteenths with accidentals: each is
// wider than an A4 page and a system of its own.
Song tooWide(std::size_t count) {
    Song song;
    song.info().title = "Wide";
    song.appendMeasures(count);
    song.addTrack(Track("Part", Instrument{.name = "Part"}, {Staff(Clef::Treble)}));
    for (std::size_t m = 0; m < count; ++m) {
        song.measureHeader(m).timeSignature = TimeSignature(32, 4);
        std::vector<Beat>& beats = song.track(0).staff(0).measure(m).voices[0].beats;
        for (int b = 0; b < 128; ++b) {
            beats.push_back(Beat{.duration = Duration(NoteValue::Sixteenth),
                                 .notes = {Note{.pitch = Pitch::fromMidi(b % 2 == 0 ? 73 : 63)}}});
        }
    }
    return song;
}

} // namespace

TEST_CASE("layout issues: an edit checks again only the pages it changed") {
    const QTemporaryDir folder;
    REQUIRE(folder.isValid());
    const QString file = folder.filePath(QStringLiteral("wide.elara"));
    elara::io::saveNative(tooWide(30), std::filesystem::path(file.toStdU16String()));

    ScoreDocument score;
    CHECK(score.layoutIssueCount() == 0);
    score.open(QUrl::fromLocalFile(file));
    REQUIRE(
        settle([&] { return score.status() == ScoreDocument::Status::Ready && !score.busy(); }));
    REQUIRE(score.pageCount() >= 2);

    // One issue a measure, on the page that shows it, in measure order.
    const std::vector<PageIssues> before = score.pageIssues();
    REQUIRE(before.size() == static_cast<std::size_t>(score.pageCount()));
    CHECK(score.layoutIssueCount() == 30);
    const QStringList lines = score.layoutIssueList();
    REQUIRE(lines.size() == 30);
    CHECK(lines.front() == ScoreDocument::tr("Measure %1 is wider than the page").arg(1));
    CHECK(lines.back() == ScoreDocument::tr("Measure %1 is wider than the page").arg(30));
    for (const PageIssues& page : before) {
        REQUIRE(page != nullptr);
        CHECK_FALSE(page->empty());
    }

    // Another title changes the first page only: the others keep their
    // lists, the same objects, not checked again.
    score.setSongTitle(QStringLiteral("Wider"));
    REQUIRE(
        settle([&] { return !score.pageIssues().empty() && score.pageIssues()[0] != before[0]; }));
    REQUIRE(settle([&] { return !score.busy(); }));
    const std::vector<PageIssues>& after = score.pageIssues();
    REQUIRE(after.size() == before.size());
    CHECK(*after[0] == *before[0]);
    for (std::size_t p = 1; p < after.size(); ++p) {
        CHECK(after[p] == before[p]);
    }
    CHECK(score.layoutIssueCount() == 30);

    // Choosing an issue takes the caret to its measure.
    score.showLayoutIssue(17);
    CHECK(score.caretMeasure() == 17);
    score.showLayoutIssue(100); // none: the caret stays
    CHECK(score.caretMeasure() == 17);
}
