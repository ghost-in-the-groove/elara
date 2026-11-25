// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/measure.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>

using elara::core::Beat;
using elara::core::Duration;
using elara::core::Fraction;
using elara::core::Measure;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Step;
using elara::core::Tuplet;
using elara::core::Voice;

namespace {

Beat note(NoteValue value, int dots = 0, Tuplet tuplet = {}) {
    return Beat{.duration = Duration(value, dots, tuplet), .notes = {Note{.pitch = Pitch{}}}};
}

Beat rest(NoteValue value) {
    return Beat{.duration = Duration(value), .notes = {}};
}

} // namespace

TEST_CASE("A beat without notes is a rest", "[core][measure]") {
    CHECK(rest(NoteValue::Quarter).isRest());
    CHECK_FALSE(note(NoteValue::Quarter).isRest());
}

TEST_CASE("Beat positions are the sum of the durations before them", "[core][measure]") {
    Voice voice;
    voice.beats = {note(NoteValue::Quarter, 1),
                   note(NoteValue::Eighth),
                   note(NoteValue::Eighth, 0, Tuplet(3, 2)),
                   note(NoteValue::Eighth, 0, Tuplet(3, 2)),
                   note(NoteValue::Eighth, 0, Tuplet(3, 2)),
                   rest(NoteValue::Quarter)};
    CHECK(voice.startOf(0) == Fraction(0));
    CHECK(voice.startOf(1) == Fraction(3, 8));
    CHECK(voice.startOf(2) == Fraction(1, 2));
    CHECK(voice.startOf(3) == Fraction(7, 12));
    CHECK(voice.startOf(5) == Fraction(3, 4));
    CHECK(voice.length() == Fraction(1)); // a full 4/4 measure
    CHECK_THROWS_AS(voice.startOf(6), std::out_of_range);
}

TEST_CASE("An empty voice has no length", "[core][measure]") {
    const Voice voice;
    CHECK(voice.isEmpty());
    CHECK(voice.length() == Fraction(0));
}

TEST_CASE("A measure has four voices, all empty at first", "[core][measure]") {
    const Measure measure;
    CHECK(measure.voices.size() == Measure::kMaxVoices);
    CHECK(Measure::kMaxVoices == 4);
    for (const auto& voice : measure.voices) {
        CHECK(voice.isEmpty());
    }
    CHECK(measure.isEmpty());

    Measure written;
    written.voices[1].beats.push_back(note(NoteValue::Whole));
    CHECK_FALSE(written.isEmpty());
    CHECK(written != measure);
}

TEST_CASE("Beats share chord symbols and compare them by value", "[core][measure]") {
    using elara::core::ChordDiagram;
    using elara::core::ChordSymbol;
    using elara::core::SharedOptional;
    constexpr std::int8_t x = ChordDiagram::kMuted;
    const ChordSymbol am{.name = "Am",
                         .diagram = ChordDiagram{.firstFret = 1, .frets = {0, 1, 2, 2, 0, x}}};

    Beat plain = rest(NoteValue::Quarter);
    CHECK_FALSE(plain.chord);
    Beat named = plain;
    named.chord = SharedOptional<ChordSymbol>(am);
    REQUIRE(named.chord);
    CHECK(named.chord->name == "Am");
    CHECK(named.chord->diagram->frets[5] == x);
    CHECK(named.isRest()); // a chord symbol does not sound
    CHECK(named != plain);

    // A copy points to the same symbol; a symbol read again compares equal.
    const Beat copy = named;
    CHECK(&*copy.chord == &*named.chord);
    Beat again = plain;
    again.chord = SharedOptional<ChordSymbol>(am);
    CHECK(&*again.chord != &*named.chord);
    CHECK(again == named);

    again.chord = SharedOptional<ChordSymbol>(ChordSymbol{.name = "Am", .diagram = {}});
    CHECK(again != named);
    again.chord.reset();
    CHECK(again == plain);
}
