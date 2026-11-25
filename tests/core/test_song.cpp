// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

using elara::core::Beat;
using elara::core::Clef;
using elara::core::Duration;
using elara::core::findInstrumentPreset;
using elara::core::FretPosition;
using elara::core::InstrumentKind;
using elara::core::instrumentPresets;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Step;
using elara::core::Track;
using elara::core::TrackMix;
using elara::core::Tuning;

namespace {

Track presetTrack(std::string_view id) {
    const auto* preset = findInstrumentPreset(id);
    REQUIRE(preset != nullptr);
    return Track::fromPreset(*preset);
}

} // namespace

TEST_CASE("Instrument presets describe each family", "[core][song]") {
    const Track guitar = presetTrack("electric-guitar");
    CHECK(guitar.instrument().kind == InstrumentKind::Fretted);
    CHECK(guitar.instrument().writtenOffset == 12); // written an octave above the sound
    REQUIRE(guitar.staffCount() == 1);
    CHECK(guitar.staff(0).clef() == Clef::Treble8vb);
    CHECK(guitar.staff(0).tuning() == Tuning::standardGuitar());

    const Track bass = presetTrack("electric-bass");
    CHECK(bass.staff(0).clef() == Clef::Bass8vb);
    CHECK(bass.staff(0).tuning() == Tuning::standardBass());

    const Track drums = presetTrack("drum-kit");
    CHECK(drums.instrument().kind == InstrumentKind::Percussion);
    CHECK(drums.staff(0).clef() == Clef::Percussion);
    CHECK_FALSE(drums.staff(0).isFretted());

    const Track piano = presetTrack("piano");
    CHECK(piano.instrument().kind == InstrumentKind::Keyboard);
    REQUIRE(piano.staffCount() == 2);
    CHECK(piano.staff(0).clef() == Clef::Treble);
    CHECK(piano.staff(1).clef() == Clef::Bass);

    CHECK(presetTrack("trumpet").instrument().writtenOffset == 2); // B-flat trumpet
    CHECK(findInstrumentPreset("kazoo-orchestra") == nullptr);
    for (const auto& preset : instrumentPresets()) {
        CHECK_NOTHROW(Track::fromPreset(preset));
    }
}

TEST_CASE("A new song is empty", "[core][song]") {
    const Song song;
    CHECK(song.trackCount() == 0);
    CHECK(song.measureCount() == 0);
    CHECK(song.info().title.empty());
}

TEST_CASE("Every staff of every track has one measure per song measure", "[core][song]") {
    Song song;
    song.info().title = "Etude";
    song.addTrack(presetTrack("electric-guitar"));
    song.appendMeasures(4);
    CHECK(song.measureCount() == 4);
    CHECK(song.track(0).staff(0).measureCount() == 4);

    // Tracks added later catch up with the song.
    song.addTrack(presetTrack("piano"));
    CHECK(song.track(1).staff(0).measureCount() == 4);
    CHECK(song.track(1).staff(1).measureCount() == 4);
    CHECK(song.trackCount() == 2);
    CHECK(song.info().title == "Etude");
}

TEST_CASE("Inserting and removing measures moves content in all staves", "[core][song]") {
    Song song;
    song.addTrack(presetTrack("electric-guitar"));
    song.addTrack(presetTrack("electric-bass"));
    song.appendMeasures(3);
    const Beat marker{.duration = Duration(NoteValue::Whole), .notes = {Note{.pitch = Pitch{}}}};
    song.track(0).staff(0).measure(1).voices[0].beats.push_back(marker);
    song.track(1).staff(0).measure(1).voices[0].beats.push_back(marker);

    song.insertMeasures(1, 2); // two empty measures before the marked one
    CHECK(song.measureCount() == 5);
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        const Staff& staff = song.track(t).staff(0);
        CHECK(staff.measureCount() == 5);
        CHECK(staff.measure(1).isEmpty());
        CHECK(staff.measure(2).isEmpty());
        CHECK_FALSE(staff.measure(3).isEmpty());
    }

    song.removeMeasures(0, 3);
    CHECK(song.measureCount() == 2);
    CHECK_FALSE(song.track(0).staff(0).measure(0).isEmpty());
    CHECK_FALSE(song.track(1).staff(0).measure(0).isEmpty());

    CHECK_THROWS_AS(song.insertMeasures(3), std::out_of_range);
    CHECK_THROWS_AS(song.removeMeasures(1, 2), std::out_of_range);
    CHECK_THROWS_AS(song.track(0).staff(0).measure(2), std::out_of_range);
}

TEST_CASE("A staff's clef holds until a measure changes it", "[core][song][clef]") {
    Song song;
    song.addTrack(presetTrack("piano"));
    song.appendMeasures(6);
    Staff& left = song.track(0).staff(1);
    REQUIRE(left.clef() == Clef::Bass);
    left.measure(2).clef = Clef::Treble;
    left.measure(4).clef = Clef::Bass;

    CHECK(left.clefAt(0) == Clef::Bass);
    CHECK(left.clefAt(1) == Clef::Bass);
    CHECK(left.clefAt(2) == Clef::Treble);
    CHECK(left.clefAt(3) == Clef::Treble);
    CHECK(left.clefAt(4) == Clef::Bass);
    CHECK(left.clefAt(5) == Clef::Bass);
    CHECK(song.track(0).staff(0).clefAt(3) == Clef::Treble); // the other staff
    CHECK_THROWS_AS(left.clefAt(6), std::out_of_range);

    // A change on the first measure overrides the staff's own clef.
    left.measure(0).clef = Clef::Tenor;
    CHECK(left.clefAt(1) == Clef::Tenor);

    // The change goes with its measure.
    song.insertMeasures(1, 2);
    CHECK(left.clefAt(3) == Clef::Tenor);
    CHECK(left.clefAt(4) == Clef::Treble);
    song.removeMeasures(0, 4);
    CHECK(left.clefAt(0) == Clef::Treble);
    CHECK(left.clefAt(2) == Clef::Bass);
}

TEST_CASE("A copy of a song shares its measures until either changes one", "[core][song]") {
    Song song;
    song.addTrack(presetTrack("electric-guitar"));
    song.appendMeasures(3);
    const Beat marker{.duration = Duration(NoteValue::Whole), .notes = {Note{.pitch = Pitch{}}}};
    song.track(0).staff(0).measure(1).voices[0].beats.push_back(marker);

    Song copy = song;
    const Staff& original = std::as_const(song).track(0).staff(0);
    const Staff& copied = std::as_const(copy).track(0).staff(0);
    // ADR-0051: copying a song copies no measure, so an edit's snapshot is cheap.
    for (std::size_t m = 0; m < 3; ++m) {
        CHECK(&copied.measure(m) == &original.measure(m));
    }
    CHECK(copy == song);

    // A change to the copy leaves the song as it was, and the other way round.
    copy.track(0).staff(0).measure(1).voices[0].beats.clear();
    CHECK(original.measure(1).voices[0].beats.size() == 1);
    song.track(0).staff(0).measure(2).voices[0].beats.push_back(marker);
    CHECK(copied.measure(2).isEmpty());
    CHECK(copy != song);
    // What neither changed is still shared.
    CHECK(&copied.measure(0) == &original.measure(0));

    // Measures inserted together start as one empty measure, and filling one
    // leaves the others empty.
    song.insertMeasures(0, 3);
    song.track(0).staff(0).measure(1).voices[0].beats.push_back(marker);
    CHECK(original.measure(0).isEmpty());
    CHECK_FALSE(original.measure(1).isEmpty());
    CHECK(original.measure(2).isEmpty());
}

TEST_CASE("Removing a track keeps the others", "[core][song]") {
    Song song;
    song.addTrack(presetTrack("electric-guitar"));
    song.addTrack(presetTrack("drum-kit"));
    song.appendMeasures(2);
    song.removeTrack(0);
    REQUIRE(song.trackCount() == 1);
    CHECK(song.track(0).instrument().kind == InstrumentKind::Percussion);
    CHECK_THROWS_AS(song.track(1), std::out_of_range);
    CHECK_THROWS_AS(song.removeTrack(1), std::out_of_range);
}

TEST_CASE("A new track sits in the middle of the mix", "[core][song]") {
    const Track guitar = presetTrack("electric-guitar");
    CHECK(guitar.mix() == TrackMix{});
    CHECK(guitar.mix().volume == TrackMix::kDefaultVolume);
    CHECK(guitar.mix().pan == 0.0);
    CHECK_FALSE(guitar.mix().mute);
    CHECK_FALSE(guitar.mix().solo);
}

TEST_CASE("A track's mix keeps volume from 0 to 1 and pan from -1 to 1", "[core][song]") {
    Track guitar = presetTrack("electric-guitar");
    guitar.setMix(TrackMix{.volume = 0.25, .pan = -1.0, .mute = true, .solo = false});
    CHECK(guitar.mix() == TrackMix{.volume = 0.25, .pan = -1.0, .mute = true, .solo = false});
    guitar.setMix(TrackMix{.volume = 1.0, .pan = 1.0, .mute = false, .solo = true});
    CHECK(guitar.mix().solo);

    const TrackMix before = guitar.mix();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS(guitar.setMix(TrackMix{.volume = 1.01}), std::invalid_argument);
    CHECK_THROWS_AS(guitar.setMix(TrackMix{.volume = -0.1}), std::invalid_argument);
    CHECK_THROWS_AS(guitar.setMix(TrackMix{.pan = 1.5}), std::invalid_argument);
    CHECK_THROWS_AS(guitar.setMix(TrackMix{.volume = nan}), std::invalid_argument);
    CHECK_THROWS_AS(guitar.setMix(TrackMix{.pan = nan}), std::invalid_argument);
    CHECK(guitar.mix() == before); // unchanged by a refused mix
}

TEST_CASE("Mute silences a track and solo silences the others", "[core][song]") {
    Song song;
    for (int t = 0; t < 3; ++t) {
        song.addTrack(presetTrack("electric-guitar"));
    }
    CHECK(song.audible(0));
    CHECK(song.audible(1));
    CHECK(song.audible(2));

    song.track(1).setMix(TrackMix{.mute = true});
    CHECK(song.audible(0));
    CHECK_FALSE(song.audible(1));

    // Once a track is soloed, only soloed tracks sound, and mute still wins.
    song.track(2).setMix(TrackMix{.solo = true});
    CHECK_FALSE(song.audible(0));
    CHECK_FALSE(song.audible(1));
    CHECK(song.audible(2));
    song.track(1).setMix(TrackMix{.mute = true, .solo = true});
    CHECK_FALSE(song.audible(1));

    CHECK_THROWS_AS((void)song.audible(3), std::out_of_range);
}

TEST_CASE("A track needs at least one staff", "[core][song]") {
    const auto* preset = findInstrumentPreset("electric-guitar");
    REQUIRE(preset != nullptr);
    CHECK_THROWS_AS(Track("Empty", Track::fromPreset(*preset).instrument(), {}),
                    std::invalid_argument);
}

TEST_CASE("A fretted staff turns string and fret into a note", "[core][song]") {
    const Staff guitar(Clef::Treble8vb, Tuning::standardGuitar(), 0);
    const Note g = guitar.frettedNote(6, 3);
    CHECK(g.pitch == Pitch(Step::G, 0, 2));
    CHECK(g.position == FretPosition{.string = 6, .fret = 3});

    const Staff capo2(Clef::Treble8vb, Tuning::standardGuitar(), 2);
    CHECK(capo2.capo() == 2);
    CHECK(capo2.frettedNote(6, 0).pitch == Pitch(Step::F, 1, 2));
    CHECK(capo2.frettedNote(6, 0, elara::core::SpellingPreference::Flats).pitch ==
          Pitch(Step::G, -1, 2));

    const Staff drums(Clef::Percussion);
    CHECK_THROWS_AS(drums.frettedNote(1, 0), std::logic_error);
    CHECK_THROWS_AS(Staff(Clef::Treble8vb, Tuning::standardGuitar(), -1), std::invalid_argument);
}
