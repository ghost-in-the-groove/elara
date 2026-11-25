// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/measure.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <stdexcept>
#include <vector>

using elara::core::Accent;
using elara::core::Beat;
using elara::core::BeatEffects;
using elara::core::BendCurve;
using elara::core::BendPoint;
using elara::core::Dynamic;
using elara::core::dynamicVelocity;
using elara::core::Finger;
using elara::core::fingeringText;
using elara::core::fingerOfText;
using elara::core::Fraction;
using elara::core::Harmonic;
using elara::core::HarmonicKind;
using elara::core::naturalHarmonicSemitones;
using elara::core::Note;
using elara::core::NoteEffects;
using elara::core::NoteValue;
using elara::core::Ornament;
using elara::core::ornamentSteps;
using elara::core::Ottava;
using elara::core::ottavaOctaves;
using elara::core::Pedal;
using elara::core::PickStroke;
using elara::core::pluckOfText;
using elara::core::pluckText;
using elara::core::SlideIn;
using elara::core::SlideOut;
using elara::core::Trill;
using elara::core::Vibrato;

TEST_CASE("A plain note and beat carry no effects", "[core][effects]") {
    const Note note;
    CHECK(note.effects == NoteEffects{});
    CHECK(note.effects.bend.isEmpty());
    CHECK_FALSE(note.effects.tiedFromPrevious);
    CHECK(note.effects.harmonic.kind == HarmonicKind::None);
    CHECK(note.effects.dynamic == Dynamic::MF);
    CHECK(Beat{}.effects == BeatEffects{});
    CHECK(Beat{}.effects.pickStroke == PickStroke::None);
    CHECK_FALSE(Beat{}.effects.tremoloPicking.has_value());
}

TEST_CASE("Effects are part of a note's value", "[core][effects]") {
    Note bent;
    bent.effects.bend = BendCurve::bend(200);
    bent.effects.vibrato = Vibrato::Wide;
    bent.effects.slideOut = SlideOut::Downwards;
    CHECK(bent != Note{});
    Note same = bent;
    CHECK(same == bent);
    same.effects.palmMute = true;
    CHECK(same != bent);
}

TEST_CASE("Bend presets and interpolation", "[core][effects]") {
    const BendCurve full = BendCurve::bend(200); // full step
    CHECK(full.centsAt(0) == 0);
    CHECK(full.centsAt(25) == 100); // halfway up the rise
    CHECK(full.centsAt(100) == 200);
    CHECK(full.maxCents() == 200);

    const BendCurve release = BendCurve::bendRelease(100);
    CHECK(release.centsAt(0) == 0);
    CHECK(release.centsAt(50) == 100);
    CHECK(release.centsAt(100) == 0);

    CHECK(BendCurve::preBend(200).centsAt(0) == 200);
    CHECK(BendCurve::preBend(200).centsAt(100) == 200);
    CHECK(BendCurve::preBendRelease(200).centsAt(100) == 0);

    const BendCurve dip({{.position = 0, .cents = 0}, {.position = 50, .cents = -1200}});
    CHECK(dip.centsAt(75) == -1200); // holds the last value
    CHECK(dip.maxCents() == 0);
    CHECK(BendCurve{}.centsAt(40) == 0);
}

TEST_CASE("Bend curve rejects malformed points", "[core][effects]") {
    using Points = std::vector<BendPoint>;
    CHECK_THROWS_AS(BendCurve(Points{{.position = -1, .cents = 0}}), std::invalid_argument);
    CHECK_THROWS_AS(BendCurve(Points{{.position = 101, .cents = 0}}), std::invalid_argument);
    CHECK_THROWS_AS(BendCurve(Points{{.position = 50, .cents = 0}, {.position = 50, .cents = 100}}),
                    std::invalid_argument); // positions must increase
    CHECK_THROWS_AS(BendCurve(Points{{.position = 0, .cents = BendCurve::kMaxCents + 1}}),
                    std::invalid_argument);
    CHECK_THROWS_AS(BendCurve(Points(BendCurve::kMaxPoints + 1)), std::invalid_argument);
}

TEST_CASE("Dynamics map to rising MIDI velocities", "[core][effects]") {
    CHECK(dynamicVelocity(Dynamic::FFF) == 127);
    CHECK(dynamicVelocity(Dynamic::MF) == 80);
    int previous = 0;
    for (const auto dynamic : {Dynamic::PPP, Dynamic::PP, Dynamic::P, Dynamic::MP, Dynamic::MF,
                               Dynamic::F, Dynamic::FF, Dynamic::FFF}) {
        CHECK(dynamicVelocity(dynamic) > previous);
        previous = dynamicVelocity(dynamic);
    }
}

TEST_CASE("Natural harmonics sound the partial of their node", "[core][effects]") {
    CHECK(naturalHarmonicSemitones(12.0) == 12); // octave
    CHECK(naturalHarmonicSemitones(7.0) == 19);  // octave + fifth
    CHECK(naturalHarmonicSemitones(19.0) == 19); // same node from the other side
    CHECK(naturalHarmonicSemitones(5.0) == 24);  // two octaves
    CHECK(naturalHarmonicSemitones(24.0) == 24);
    CHECK(naturalHarmonicSemitones(4.0) == 28); // two octaves + major third
    CHECK(naturalHarmonicSemitones(9.0) == 28);
    CHECK(naturalHarmonicSemitones(3.2) == 31);             // "3.2"
    CHECK(naturalHarmonicSemitones(2.4) == 36);             // "2.4"
    CHECK_FALSE(naturalHarmonicSemitones(0.3).has_value()); // no usable node
}

TEST_CASE("Beat effects describe picking and bass techniques", "[core][effects]") {
    Beat beat;
    beat.effects.pickStroke = PickStroke::Down;
    beat.effects.tremoloPicking = NoteValue::Sixteenth;
    beat.effects.whammyBar = BendCurve::bend(-200);
    CHECK(beat != Beat{});
    CHECK(beat.effects.whammyBar.centsAt(100) == -200);

    Note harmonic;
    harmonic.effects.harmonic = Harmonic{.kind = HarmonicKind::Artificial, .fretTenths = 120};
    harmonic.effects.accent = Accent::Marcato;
    harmonic.effects.slideIn = SlideIn::FromBelow;
    CHECK(harmonic.effects.harmonic.fretTenths == 120);
}

TEST_CASE("A bend's form is read from the shape of its curve", "[core][effects]") {
    using elara::core::bendCurve;
    using elara::core::BendForm;
    using elara::core::bendForm;
    using Points = std::vector<BendPoint>;
    CHECK(bendForm(BendCurve{}) == BendForm::None);
    CHECK(bendForm(BendCurve::bend(200)) == BendForm::Bend);
    CHECK(bendForm(BendCurve::bendRelease(100)) == BendForm::BendRelease);
    CHECK(bendForm(BendCurve::preBend(300)) == BendForm::PreBend);
    CHECK(bendForm(BendCurve::preBendRelease(50)) == BendForm::PreBendRelease);

    // Imported curves keep their own points: the shape decides, not the positions.
    CHECK(bendForm(BendCurve(Points{{.position = 0, .cents = 0},
                                    {.position = 20, .cents = 100},
                                    {.position = 60, .cents = 200}})) == BendForm::Bend);
    CHECK(bendForm(BendCurve(Points{{.position = 0, .cents = 0},
                                    {.position = 10, .cents = 200},
                                    {.position = 80, .cents = 200},
                                    {.position = 90, .cents = 0}})) == BendForm::BendRelease);
    CHECK(bendForm(BendCurve(Points{{.position = 0, .cents = 100},
                                    {.position = 70, .cents = 0}})) == BendForm::PreBendRelease);

    // Bend, release and bend again, a dip, or a curve that stays flat at zero.
    CHECK(bendForm(BendCurve(Points{{.position = 0, .cents = 0},
                                    {.position = 30, .cents = 200},
                                    {.position = 60, .cents = 0},
                                    {.position = 90, .cents = 200}})) == BendForm::Custom);
    CHECK(bendForm(BendCurve(Points{{.position = 0, .cents = 0},
                                    {.position = 50, .cents = -100}})) == BendForm::Custom);
    CHECK(bendForm(BendCurve(Points{{.position = 0, .cents = 0}})) == BendForm::Custom);

    CHECK(bendCurve(BendForm::None, 200).isEmpty());
    CHECK(bendCurve(BendForm::Bend, 100) == BendCurve::bend(100));
    CHECK(bendCurve(BendForm::BendRelease, 200) == BendCurve::bendRelease(200));
    CHECK(bendCurve(BendForm::PreBend, 50) == BendCurve::preBend(50));
    CHECK(bendCurve(BendForm::PreBendRelease, 400) == BendCurve::preBendRelease(400));
    CHECK_THROWS_AS(bendCurve(BendForm::Custom, 200), std::invalid_argument);
    CHECK_THROWS_AS(bendCurve(BendForm::Bend, 0), std::invalid_argument);
    CHECK_THROWS_AS(bendCurve(BendForm::Bend, BendCurve::kMaxCents + 1), std::invalid_argument);
}

TEST_CASE("A whammy bar's form and depth go down or up from the written pitch", "[core][effects]") {
    using elara::core::BendForm;
    using elara::core::whammyCents;
    using elara::core::whammyCurve;
    using elara::core::whammyForm;
    using Points = std::vector<BendPoint>;
    CHECK(whammyForm(BendCurve{}) == BendForm::None);
    CHECK(whammyCents(BendCurve{}) == 0);

    // A dive goes down and holds, a dip goes down and comes back, a pre-dive
    // starts down and holds, and a pre-dive and return comes back up.
    const BendCurve dive = whammyCurve(BendForm::Bend, -400);
    CHECK(dive == BendCurve(Points{{.position = 0, .cents = 0},
                                   {.position = 50, .cents = -400},
                                   {.position = 100, .cents = -400}}));
    CHECK(whammyForm(dive) == BendForm::Bend);
    CHECK(whammyCents(dive) == -400);
    CHECK(whammyForm(whammyCurve(BendForm::BendRelease, -200)) == BendForm::BendRelease);
    CHECK(whammyForm(whammyCurve(BendForm::PreBend, -100)) == BendForm::PreBend);
    CHECK(whammyForm(whammyCurve(BendForm::PreBendRelease, -600)) == BendForm::PreBendRelease);
    CHECK(whammyCents(whammyCurve(BendForm::PreBendRelease, -600)) == -600);

    // Pulled up, the curves are the bends'.
    CHECK(whammyCurve(BendForm::BendRelease, 100) == BendCurve::bendRelease(100));
    CHECK(whammyForm(BendCurve::bendRelease(100)) == BendForm::BendRelease);
    CHECK(whammyCents(BendCurve::preBend(300)) == 300);

    // Imported curves keep their own points; one that goes both ways is custom.
    CHECK(whammyForm(BendCurve(Points{{.position = 0, .cents = 0},
                                      {.position = 25, .cents = -250},
                                      {.position = 75, .cents = 0}})) == BendForm::BendRelease);
    const BendCurve wobble(Points{{.position = 0, .cents = 0},
                                  {.position = 30, .cents = -200},
                                  {.position = 60, .cents = 300}});
    CHECK(whammyForm(wobble) == BendForm::Custom);
    CHECK(whammyCents(wobble) == 300);

    CHECK(whammyCurve(BendForm::None, -200).isEmpty());
    CHECK_THROWS_AS(whammyCurve(BendForm::Custom, -200), std::invalid_argument);
    CHECK_THROWS_AS(whammyCurve(BendForm::Bend, 0), std::invalid_argument);
    CHECK_THROWS_AS(whammyCurve(BendForm::Bend, -BendCurve::kMaxCents - 1), std::invalid_argument);
}

TEST_CASE("Octave lines move the written notes one or two octaves", "[core][effects]") {
    // ADR-0089: notes under 8va sound an octave above where they are written.
    CHECK(ottavaOctaves(Ottava::None) == 0);
    CHECK(ottavaOctaves(Ottava::Alta) == 1);
    CHECK(ottavaOctaves(Ottava::Bassa) == -1);
    CHECK(ottavaOctaves(Ottava::QuindicesimaAlta) == 2);
    CHECK(ottavaOctaves(Ottava::QuindicesimaBassa) == -2);
    BeatEffects effects;
    CHECK(effects.ottava == Ottava::None);
    effects.ottava = Ottava::QuindicesimaBassa; // the largest value fits its bits
    effects.slur = true;
    CHECK(effects.ottava == Ottava::QuindicesimaBassa);
    CHECK(effects.slur);
    CHECK(effects != BeatEffects{});
}

TEST_CASE("Fingerings are written from the thumb, or from the index on strings",
          "[core][effects]") {
    // ADR-0093.
    CHECK(fingeringText(Finger::Thumb, false) == "1");
    CHECK(fingeringText(Finger::Little, false) == "5");
    CHECK(fingeringText(Finger::Thumb, true) == "T");
    CHECK(fingeringText(Finger::Index, true) == "1");
    CHECK(fingeringText(Finger::Little, true) == "4");
    CHECK(fingeringText(Finger::None, true).empty());
    CHECK(fingerOfText("1", false) == Finger::Thumb);
    CHECK(fingerOfText("1", true) == Finger::Index);
    CHECK(fingerOfText("t", true) == Finger::Thumb);
    CHECK(fingerOfText("0", true) == Finger::None); // an open string
    CHECK(fingerOfText("5", true) == std::nullopt);
    CHECK(fingerOfText("T", false) == std::nullopt);
    CHECK(fingerOfText("3-1", false) == std::nullopt);
    CHECK(pluckText(Finger::Ring) == "a");
    CHECK(pluckOfText("p") == Finger::Thumb);
    CHECK(pluckOfText("x") == Finger::Little);
    CHECK(pluckOfText("q") == std::nullopt);
}

TEST_CASE("A beat goes under the sustain pedal or changes it", "[core][effects]") {
    // ADR-0092: no pedal unless one is set; a change differs from going on.
    BeatEffects effects;
    CHECK(effects.pedal == Pedal::None);
    effects.pedal = Pedal::Down;
    BeatEffects changed = effects;
    changed.pedal = Pedal::Change;
    CHECK(effects != BeatEffects{});
    CHECK(changed != effects);
}

TEST_CASE("A trill alternates with another note at its speed", "[core][effects]") {
    // ADR-0090: no trill by default; a trill goes up some semitones.
    const NoteEffects plain;
    CHECK(!plain.trill.isTrill());
    NoteEffects trilled;
    trilled.trill = Trill{.semitones = 2, .speed = NoteValue::Sixteenth};
    CHECK(trilled.trill.isTrill());
    CHECK(trilled != plain);
    CHECK(Trill{.semitones = 1} != Trill{.semitones = 2});
    CHECK(Trill{.semitones = 1}.speed == NoteValue::ThirtySecond);
}

TEST_CASE("Effects: an ornament's quick notes go round the note on the steps of the key",
          "[core][effects]") {
    // ADR-0091: a mordent dips below and back, an upper mordent goes above
    // and back, a turn goes round from above and an inverted turn from below.
    const auto steps = [](Ornament ornament) {
        const auto found = ornamentSteps(ornament);
        return std::vector<int>(found.steps.begin(),
                                found.steps.begin() + static_cast<std::ptrdiff_t>(found.count));
    };
    CHECK(steps(Ornament::None).empty());
    CHECK(steps(Ornament::Mordent) == std::vector<int>{0, -1});
    CHECK(steps(Ornament::UpperMordent) == std::vector<int>{0, 1});
    CHECK(steps(Ornament::Turn) == std::vector<int>{1, 0, -1});
    CHECK(steps(Ornament::InvertedTurn) == std::vector<int>{-1, 0, 1});
}
