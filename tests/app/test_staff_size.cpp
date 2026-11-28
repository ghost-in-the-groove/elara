// SPDX-License-Identifier: AGPL-3.0-or-later
// The score's staff size follows the staves shown (ADR-0171): every track at
// once lays out smaller staves than one track alone, the page keeps its size
// on paper, and the caret stays on its staff. A measure wider than the page
// gets smaller staves too, so that it fits (ADR-0176).
#include <elara/core/measure.hpp>
#include <elara/core/song.hpp>
#include <elara/io/native_format.hpp>
#include <elara/notation/page_layout.hpp>
#include <elara/notation/staff_size.hpp>

#include "score_document.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSizeF>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

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

// A guitar playing twelve measures of four quarter notes, the second of 48
// sixteenth notes with two-digit frets in 12/4: wider than an A4 page with
// staff spaces 1.75 mm apart.
elara::core::Song wideMeasure() {
    using namespace elara::core;
    Song song;
    song.info().title = "Wide";
    song.appendMeasures(12);
    song.addTrack(Track("Guitar", Instrument{.name = "Guitar", .writtenOffset = 12},
                        {Staff(Clef::Treble8vb, Tuning::standardGuitar())}));
    const auto note = [](NoteValue value, int fret) {
        const auto at = static_cast<std::int8_t>(fret);
        return Beat{.duration = Duration(value),
                    .notes = {Note{.pitch = Pitch::fromMidi(Tuning::standardGuitar().midiAt(1, at)),
                                   .position = FretPosition{.string = 1, .fret = at}}}};
    };
    for (std::size_t m = 0; m < 12; ++m) {
        std::vector<Beat>& beats = song.track(0).staff(0).measure(m).voices[0].beats;
        if (m == 1) {
            song.measureHeader(m).timeSignature = TimeSignature(12, 4);
            for (int b = 0; b < 48; ++b) {
                beats.push_back(note(NoteValue::Sixteenth, 10 + b % 5));
            }
        } else {
            beats = {note(NoteValue::Quarter, 3), note(NoteValue::Quarter, 5),
                     note(NoteValue::Quarter, 7), note(NoteValue::Quarter, 5)};
        }
    }
    return song;
}

// How far the widest system of the pages shown runs past the right margin.
double pastTheMargin(const ScoreDocument& score) {
    double past = 0.0;
    const double limit = score.pageSize().width() - 15.0 / score.staffSpace();
    for (int p = 0; p < score.pageCount(); ++p) {
        for (const elara::notation::SystemBox& system : score.page(p)->systems) {
            past = std::max(past, system.x + system.width - limit);
        }
    }
    return past;
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

TEST_CASE("Staff size: a measure wider than the page gets smaller staves, and fits", "[app]") {
    const QTemporaryDir folder;
    REQUIRE(folder.isValid());
    const QString file = folder.filePath(QStringLiteral("wide.elara"));
    elara::io::saveNative(wideMeasure(), std::filesystem::path(file.toStdU16String()));

    ScoreDocument score;
    score.open(QUrl::fromLocalFile(file));
    REQUIRE(settle([&] { return laidOut(score); }));
    // Smaller than a part's, no smaller than the least share of it, on paper
    // of the same size; nothing runs past the margin and nothing is reported.
    const double narrowed = score.staffSpace();
    CHECK(narrowed < 1.75);
    CHECK(narrowed >= elara::notation::kLeastStaffShare * 1.75 - 1e-9);
    CHECK_THAT(score.pageSize().width() * narrowed, WithinAbs(210.0, 1e-6));
    CHECK(pastTheMargin(score) < 1e-6);
    CHECK(score.layoutIssueCount() == 0);

    // An edit elsewhere keeps the staves as they are.
    score.caretToMeasure(5);
    score.typeDigit(7);
    score.moveCaret(ScoreDocument::CaretMove::StringDown);
    REQUIRE(settle([&] { return laidOut(score); }));
    CHECK(score.staffSpace() == narrowed);
    CHECK(pastTheMargin(score) < 1e-6);

    // Its tablature alone is narrower: it has its own size, at least as large.
    score.setView(ScoreDocument::View::Tablature);
    REQUIRE(settle([&] { return laidOut(score); }));
    CHECK(score.staffSpace() >= narrowed);
    CHECK(pastTheMargin(score) < 1e-6);
    score.setView(ScoreDocument::View::Both);
    REQUIRE(settle([&] { return laidOut(score); }));
    CHECK(score.staffSpace() == narrowed);

    // The wide measure goes: back to the staff space of a part.
    score.caretToMeasure(1);
    score.removeMeasure();
    REQUIRE(settle([&] { return laidOut(score); }));
    CHECK(score.measureCount() == 11);
    CHECK(score.staffSpace() == 1.75);
    // And comes back with an undo.
    score.undo();
    REQUIRE(settle([&] { return laidOut(score); }));
    CHECK(score.staffSpace() == narrowed);
    CHECK(pastTheMargin(score) < 1e-6);
}
