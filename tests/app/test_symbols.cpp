// SPDX-License-Identifier: AGPL-3.0-or-later
// Symbols edited where they are drawn (ADR-0130): which symbol of a page the
// document finds under a point, the measure its value starts at, and a clef
// changed from a measure that is not the caret's.
#include <elara/core/entry.hpp>
#include <elara/core/song.hpp>
#include <elara/notation/display_list.hpp>

#include "caret_editor.hpp"
#include "score_document.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace {

using elara::app::CaretEditor;
using elara::app::ScoreDocument;
using elara::core::Caret;
using elara::notation::DisplayList;
using elara::notation::Point;
using elara::notation::Role;
using Move = ScoreDocument::CaretMove;
using View = ScoreDocument::View;

template <typename Done>
bool settle(Done done) {
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < 5000) {
        QCoreApplication::processEvents();
    }
    return done();
}

// A new song on `preset`, long enough for more than one system, laid out in the
// view a new song of that instrument opens in: staff and tablature for a
// guitar, the staff alone for the drums.
struct Pages {
    ScoreDocument score;
    CaretEditor editor;
    QStringList failures;

    explicit Pages(const char* preset = "electric-guitar") {
        editor.setDocument(&score);
        QObject::connect(&score, &ScoreDocument::editFailed,
                         [this](const QString& message) { failures.append(message); });
        score.newSong(QString::fromLatin1(preset));
        score.setMeasureCount(32);
        REQUIRE(laidOut());
        REQUIRE(score.song()->measureCount() == 32);
    }

    // Waits for the pages of the song as it is now.
    [[nodiscard]] bool laidOut() {
        return settle([this] { return !score.busy() && score.pageCount() > 0; });
    }
    [[nodiscard]] std::shared_ptr<const DisplayList> page() const { return score.page(0); }
};

// Where the pieces of `role` of a measure of the first page are drawn: the
// origins of its glyphs and of its texts.
std::vector<Point> drawn(const DisplayList& page, Role role, std::size_t measure) {
    std::vector<Point> origins;
    for (const auto& glyph : page.glyphs) {
        if (glyph.role == role && glyph.measure == measure) {
            origins.push_back(glyph.origin);
        }
    }
    for (const auto& text : page.texts) {
        if (text.role == role && text.measure == measure) {
            origins.push_back(text.origin);
        }
    }
    return origins;
}

// The symbol of `kind` the document finds on the ink round `origin`: a glyph's
// ink is beside its origin and a text's round it. Empty when it finds none of
// that kind there.
QVariantMap found(const ScoreDocument& score, Point origin, const char* kind) {
    for (int row = -6; row <= 6; ++row) {
        for (int column = -2; column <= 8; ++column) {
            QVariantMap symbol = score.symbolAt(0, origin.x + column * 0.25, origin.y + row * 0.25);
            if (symbol.value(QStringLiteral("kind")).toString() == QLatin1StringView(kind)) {
                return symbol;
            }
        }
    }
    return {};
}

QVariantMap found(const ScoreDocument& score, const std::vector<Point>& origins, const char* kind) {
    for (const Point origin : origins) {
        if (QVariantMap symbol = found(score, origin, kind); !symbol.isEmpty()) {
            return symbol;
        }
    }
    return {};
}

int measureOf(const QVariantMap& symbol) {
    return symbol.value(QStringLiteral("measure")).toInt();
}

// The first measure after `after` that draws a piece of `role` on the page: one
// a later system starts with.
std::size_t laterMeasureWith(const DisplayList& page, Role role, std::size_t after) {
    for (const auto& glyph : page.glyphs) {
        if (glyph.role == role && glyph.measure > after) {
            return glyph.measure;
        }
    }
    return 0;
}

} // namespace

TEST_CASE("symbols: the time signature is found where it is drawn, and nothing where nothing is") {
    Pages pages;
    const auto page = pages.page();
    REQUIRE(page);
    const std::vector<Point> digits = drawn(*page, Role::TimeSignature, 0);
    REQUIRE_FALSE(digits.empty());

    const QVariantMap symbol = found(pages.score, digits, "time");
    REQUIRE_FALSE(symbol.isEmpty());
    CHECK(measureOf(symbol) == 0);
    CHECK(symbol.value(QStringLiteral("shown")).toInt() == 0);
    CHECK(symbol.value(QStringLiteral("track")).toInt() == 0);
    CHECK(symbol.value(QStringLiteral("staff")).toInt() == 0);
    // The box is round the digits it was found by.
    const QRectF box = symbol.value(QStringLiteral("rect")).toRectF();
    CHECK(box.width() > 0.5);
    CHECK(box.height() > 2.0);
    CHECK(box.adjusted(-1, -1, 1, 1).contains(QPointF(digits.front().x, digits.front().y)));

    // The page's corner, and pages the score does not have.
    CHECK(pages.score.symbolAt(0, 0.2, 0.2).isEmpty());
    CHECK(pages.score.symbolAt(-1, digits.front().x + 0.5, digits.front().y).isEmpty());
    CHECK(pages.score.symbolAt(pages.score.pageCount(), digits.front().x + 0.5, digits.front().y)
              .isEmpty());
}

TEST_CASE("symbols: before a song is open nothing is found") {
    ScoreDocument score;
    CHECK(score.symbolAt(0, 10, 10).isEmpty());
    CHECK(score.signatureAt(0).isEmpty());
    CaretEditor editor;
    editor.setDocument(&score);
    CHECK(editor.clefAt(0, 0, 0) == -1);
    editor.setClefFrom(0, 0, 0, 1);
    CHECK_FALSE(score.modified());
}

TEST_CASE("symbols: a key signature written again on a later system is the one set before") {
    Pages pages;
    pages.score.setKeySignatureFrom(1, 2, false); // D major from the second measure on
    REQUIRE(pages.laidOut());
    const auto page = pages.page();
    REQUIRE(page);

    // Where it is set, it is its own.
    const QVariantMap set = found(pages.score, drawn(*page, Role::KeySignature, 1), "key");
    REQUIRE_FALSE(set.isEmpty());
    CHECK(measureOf(set) == 1);
    CHECK(set.value(QStringLiteral("shown")).toInt() == 1);

    // A later system writes it again: the measure to change is still the second.
    const std::size_t later = laterMeasureWith(*page, Role::KeySignature, 1);
    REQUIRE(later > 1);
    const QVariantMap again = found(pages.score, drawn(*page, Role::KeySignature, later), "key");
    REQUIRE_FALSE(again.isEmpty());
    CHECK(again.value(QStringLiteral("shown")).toInt() == static_cast<int>(later));
    CHECK(measureOf(again) == 1);
    CHECK(pages.failures.isEmpty());
}

TEST_CASE("symbols: a measure's signatures are read by its number") {
    Pages pages;
    pages.score.setTimeSignatureFrom(2, 3, 4);
    pages.score.setKeySignatureFrom(2, -3, true); // C minor
    const QVariantMap before = pages.score.signatureAt(1);
    CHECK(before.value(QStringLiteral("numerator")).toInt() == 4);
    CHECK(before.value(QStringLiteral("denominator")).toInt() == 4);
    CHECK(before.value(QStringLiteral("fifths")).toInt() == 0);
    CHECK_FALSE(before.value(QStringLiteral("minor")).toBool());
    const QVariantMap from = pages.score.signatureAt(2);
    CHECK(from.value(QStringLiteral("numerator")).toInt() == 3);
    CHECK(from.value(QStringLiteral("denominator")).toInt() == 4);
    CHECK(from.value(QStringLiteral("fifths")).toInt() == -3);
    CHECK(from.value(QStringLiteral("minor")).toBool());
    CHECK(pages.score.signatureAt(31) == from);
    CHECK(pages.score.signatureAt(32).isEmpty());
    CHECK(pages.score.signatureAt(-1).isEmpty());
}

TEST_CASE("symbols: a clef is changed from a measure that is not the caret's, which stays") {
    Pages pages;
    CaretEditor& editor = pages.editor;
    CHECK(editor.clefAt(0, 0, 0) == 0);

    editor.setClefFrom(0, 0, 2, 1); // bass from the third measure on
    CHECK(pages.failures.isEmpty());
    CHECK(pages.score.undoName() == QStringLiteral("Change clef"));
    CHECK(editor.clefAt(0, 0, 1) == 0);
    CHECK(editor.clefAt(0, 0, 2) == 1);
    CHECK(editor.clefAt(0, 0, 31) == 1);
    CHECK(pages.score.caret() == Caret{});

    // A later system writes the clef again: it is the one set at the third.
    REQUIRE(pages.laidOut());
    const auto page = pages.page();
    REQUIRE(page);
    const std::size_t later = laterMeasureWith(*page, Role::Clef, 2);
    REQUIRE(later > 2);
    const QVariantMap clef = found(pages.score, drawn(*page, Role::Clef, later), "clef");
    REQUIRE_FALSE(clef.isEmpty());
    CHECK(measureOf(clef) == 2);
    CHECK(clef.value(QStringLiteral("shown")).toInt() == static_cast<int>(later));
    CHECK(clef.value(QStringLiteral("track")).toInt() == 0);
    CHECK(clef.value(QStringLiteral("staff")).toInt() == 0);
    // The first system's is the staff's own, from the first measure.
    const QVariantMap first = found(pages.score, drawn(*page, Role::Clef, 0), "clef");
    REQUIRE_FALSE(first.isEmpty());
    CHECK(measureOf(first) == 0);

    // A place the song lacks, or a clef that is not offered, changes nothing.
    CHECK(editor.clefAt(1, 0, 0) == -1);
    CHECK(editor.clefAt(0, 1, 0) == -1);
    CHECK(editor.clefAt(0, 0, 32) == -1);
    CHECK(editor.clefAt(0, 0, -1) == -1);
    editor.setClefFrom(0, 0, 32, 2);
    editor.setClefFrom(1, 0, 2, 2);
    editor.setClefFrom(0, 0, 2, 9);
    editor.setClefFrom(0, 0, 2, -1);
    CHECK(editor.clefAt(0, 0, 2) == 1);

    pages.score.undo();
    CHECK(editor.clefAt(0, 0, 2) == 0);
    CHECK(pages.score.caret() == Caret{});
}

TEST_CASE("symbols: the TAB of a tablature and the clef of the drums are not ones to choose") {
    SECTION("a tablature") {
        Pages pages;
        pages.score.setView(View::Tablature);
        REQUIRE(pages.laidOut());
        const auto page = pages.page();
        REQUIRE(page);
        const std::vector<Point> clefs = drawn(*page, Role::Clef, 0);
        REQUIRE_FALSE(clefs.empty());
        CHECK(found(pages.score, clefs, "clef").isEmpty());
    }
    SECTION("the drums") {
        Pages pages("drum-kit");
        const auto page = pages.page();
        REQUIRE(page);
        const std::vector<Point> clefs = drawn(*page, Role::Clef, 0);
        REQUIRE_FALSE(clefs.empty());
        CHECK(found(pages.score, clefs, "clef").isEmpty());
        CHECK(pages.editor.clefAt(0, 0, 0) == -1);
        pages.editor.setClefFrom(0, 0, 0, 1);
        CHECK(pages.score.undoName() != QStringLiteral("Change clef"));
    }
}

TEST_CASE("symbols: a tempo mark is its measure's tempo, a triplet feel mark is not one") {
    Pages pages;
    pages.score.setTempoFrom(1, 90);
    pages.score.setTripletFeelFrom(2, 1);
    REQUIRE(pages.failures.isEmpty());
    REQUIRE(pages.laidOut());
    const auto page = pages.page();
    REQUIRE(page);

    const QVariantMap tempo = found(pages.score, drawn(*page, Role::Tempo, 1), "tempo");
    REQUIRE_FALSE(tempo.isEmpty());
    CHECK(measureOf(tempo) == 1);
    // The song's first tempo is one as well.
    const QVariantMap first = found(pages.score, drawn(*page, Role::Tempo, 0), "tempo");
    REQUIRE_FALSE(first.isEmpty());
    CHECK(measureOf(first) == 0);

    const std::vector<Point> feel = drawn(*page, Role::Tempo, 2);
    REQUIRE_FALSE(feel.empty());
    CHECK(found(pages.score, feel, "tempo").isEmpty());
}

TEST_CASE(
    "symbols: a section's name, a chord symbol and a beat's text are found on their measure") {
    Pages pages;
    pages.score.setSection(1, QStringLiteral("Chorus"));
    // Words go on a beat: a note is typed for each.
    pages.score.moveCaret(Move::NextMeasure);
    pages.score.moveCaret(Move::NextMeasure);
    pages.score.typeDigit(3);
    pages.editor.setChordSymbol(QStringLiteral("Am"));
    REQUIRE(pages.editor.caretChord() == QStringLiteral("Am"));
    pages.score.moveCaret(Move::NextMeasure);
    pages.score.typeDigit(5);
    pages.editor.setBeatText(QStringLiteral("ad lib."));
    REQUIRE(pages.editor.caretText() == QStringLiteral("ad lib."));
    REQUIRE(pages.failures.isEmpty());
    REQUIRE(pages.laidOut());
    const auto page = pages.page();
    REQUIRE(page);

    const QVariantMap section = found(pages.score, drawn(*page, Role::Section, 1), "section");
    REQUIRE_FALSE(section.isEmpty());
    CHECK(measureOf(section) == 1);
    const QVariantMap chord = found(pages.score, drawn(*page, Role::Chord, 2), "chord");
    REQUIRE_FALSE(chord.isEmpty());
    CHECK(measureOf(chord) == 2);
    const QVariantMap text = found(pages.score, drawn(*page, Role::BeatText, 3), "text");
    REQUIRE_FALSE(text.isEmpty());
    CHECK(measureOf(text) == 3);
}

TEST_CASE("symbols: one of a measure the song no longer has is not found") {
    Pages pages;
    pages.score.setTimeSignatureFrom(1, 3, 4);
    REQUIRE(pages.laidOut());
    const auto page = pages.page();
    REQUIRE(page);
    const std::vector<Point> digits = drawn(*page, Role::TimeSignature, 1);
    REQUIRE_FALSE(digits.empty());
    REQUIRE_FALSE(found(pages.score, digits, "time").isEmpty());

    // The song loses the measure, and the pages still show it until they are
    // laid out again.
    pages.score.setMeasureCount(1);
    REQUIRE(pages.score.page(0).get() == page.get());
    CHECK(found(pages.score, digits, "time").isEmpty());
}
