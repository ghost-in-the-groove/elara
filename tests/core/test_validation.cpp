// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/song.hpp>
#include <elara/core/validation.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using elara::core::Beat;
using elara::core::Duration;
using elara::core::fillWithRests;
using elara::core::findInstrumentPreset;
using elara::core::Issue;
using elara::core::IssueKind;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Step;
using elara::core::TimeSignature;
using elara::core::Track;
using elara::core::Tuplet;
using elara::core::validate;
using elara::core::Voice;
using elara::core::VoiceFill;
using elara::core::voiceFill;

namespace {

Voice voiceOf(std::initializer_list<const char*> durations) {
    Voice voice;
    for (const char* text : durations) {
        const auto duration = Duration::parse(text);
        REQUIRE(duration.has_value());
        voice.beats.push_back(Beat{.duration = *duration, .notes = {Note{}}});
    }
    return voice;
}

// The durations of the rests at the end of `voice`, as text.
std::vector<std::string> restsOf(const Voice& voice) {
    std::vector<std::string> rests;
    for (const auto& beat : voice.beats) {
        if (beat.isRest()) {
            rests.push_back(beat.duration.toString());
        }
    }
    return rests;
}

std::vector<std::string> filled(TimeSignature time, std::initializer_list<const char*> written) {
    Voice voice = voiceOf(written);
    REQUIRE(fillWithRests(voice, time));
    CHECK(voiceFill(voice, time) == VoiceFill::Complete);
    return restsOf(voice);
}

using Texts = std::vector<std::string>;

} // namespace

TEST_CASE("Durations have a compact text form", "[core][validation]") {
    CHECK(Duration(NoteValue::Quarter).toString() == "4");
    CHECK(Duration(NoteValue::Eighth, 1).toString() == "8.");
    CHECK(Duration(NoteValue::Half, 2).toString() == "2..");
    CHECK(Duration(NoteValue::Eighth, 0, Tuplet(3, 2)).toString() == "8*3:2");
    CHECK(Duration(NoteValue::DoubleWhole).toString() == "breve");
    CHECK(Duration(NoteValue::HundredTwentyEighth).toString() == "128");
    CHECK(Duration::parse("16.*5:4") == Duration(NoteValue::Sixteenth, 1, Tuplet(5, 4)));
    CHECK(Duration::parse("breve") == Duration(NoteValue::DoubleWhole));
    CHECK_FALSE(Duration::parse("3").has_value());
    CHECK_FALSE(Duration::parse("4....").has_value());
    CHECK_FALSE(Duration::parse("8*3").has_value());
    CHECK_FALSE(Duration::parse("8*0:2").has_value());
    CHECK_FALSE(Duration::parse("").has_value());
}

TEST_CASE("A voice is empty, incomplete, complete or overfull", "[core][validation]") {
    const TimeSignature common(4, 4);
    CHECK(voiceFill(Voice{}, common) == VoiceFill::Empty);
    CHECK(voiceFill(voiceOf({"4", "4"}), common) == VoiceFill::Incomplete);
    CHECK(voiceFill(voiceOf({"2", "4", "8", "8"}), common) == VoiceFill::Complete);
    CHECK(voiceFill(voiceOf({"1", "8"}), common) == VoiceFill::Overfull);
    CHECK(voiceFill(voiceOf({"4.", "4.", "4"}), TimeSignature(3, 4)) == VoiceFill::Overfull);
}

TEST_CASE("Rests fill a simple meter without hiding its beats", "[core][validation]") {
    CHECK(filled(TimeSignature(4, 4), {"4"}) == Texts{"4", "2"});
    CHECK(filled(TimeSignature(4, 4), {"8"}) == Texts{"8", "4", "2"});
    CHECK(filled(TimeSignature(4, 4), {"4", "8"}) == Texts{"8", "2"});
    CHECK(filled(TimeSignature(3, 4), {"4"}) == Texts{"4", "4"});
    CHECK(filled(TimeSignature(2, 2), {"4"}) == Texts{"4", "2"});
    CHECK(filled(TimeSignature(5, 4), {"4"}) == Texts{"4", "2", "4"});
}

TEST_CASE("Rests fill a compound meter beat by beat", "[core][validation]") {
    CHECK(filled(TimeSignature(6, 8), {"8"}) == Texts{"8", "8", "4."});
    CHECK(filled(TimeSignature(12, 8), {"4."}) == Texts{"4.", "2."});
    CHECK(filled(TimeSignature(9, 8), {"4.", "8"}) == Texts{"8", "8", "4."});
}

TEST_CASE("Rests complete an unfinished tuplet first", "[core][validation]") {
    CHECK(filled(TimeSignature(4, 4), {"8*3:2"}) == Texts{"8*3:2", "8*3:2", "4", "2"});
}

TEST_CASE("Filling leaves empty and overfull voices alone", "[core][validation]") {
    Voice empty;
    CHECK(fillWithRests(empty, TimeSignature(4, 4))); // an empty voice is a measure rest
    CHECK(empty.isEmpty());

    Voice overfull = voiceOf({"1", "4"});
    CHECK_FALSE(fillWithRests(overfull, TimeSignature(4, 4)));
    CHECK(overfull.beats.size() == 2);

    // An eighth of a 5:3 tuplet lasts 3/40; the 7/40 left before the next beat is
    // no sum of 5:3 rests, so nothing is added (not even the rests that fit).
    Voice odd = voiceOf({"8*5:3"});
    CHECK_FALSE(fillWithRests(odd, TimeSignature(4, 4)));
    CHECK(odd.beats.size() == 1);
}

TEST_CASE("Song validation reports where the model is inconsistent", "[core][validation]") {
    Song song;
    song.addTrack(Track::fromPreset(*findInstrumentPreset("electric-guitar")));
    song.appendMeasures(2);
    auto& staff = song.track(0).staff(0);
    CHECK(validate(song).empty());

    // Measure 0: complete, a tie into measure 1 from the same note.
    Beat first{.duration = Duration(NoteValue::Whole), .notes = {staff.frettedNote(6, 3)}};
    staff.measure(0).voices[0].beats.push_back(first);
    Beat tied{.duration = Duration(NoteValue::Half), .notes = {staff.frettedNote(6, 3)}};
    tied.notes[0].effects.tiedFromPrevious = true;
    staff.measure(1).voices[0].beats.push_back(tied);
    CHECK(validate(song) == std::vector<Issue>{{.kind = IssueKind::VoiceIncomplete,
                                                .track = 0,
                                                .staff = 0,
                                                .measure = 1,
                                                .voice = 0}});

    // Measure 1 gets an overfull second voice and broken notes.
    Beat wrong{.duration = Duration(NoteValue::Half), .notes = {staff.frettedNote(5, 2)}};
    wrong.notes[0].pitch = Pitch(Step::C, 0, 4);    // the 5th string at fret 2 is B2
    wrong.notes.push_back(staff.frettedNote(5, 0)); // same string twice
    Beat orphanTie{.duration = Duration(NoteValue::Half), .notes = {staff.frettedNote(1, 0)}};
    orphanTie.notes[0].effects.tiedFromPrevious = true; // previous beat has no E4
    staff.measure(1).voices[0].beats.push_back(wrong);
    auto& second = staff.measure(1).voices[1].beats;
    second.push_back(Beat{.duration = Duration(NoteValue::Whole), .notes = {}});
    second.push_back(orphanTie);

    const auto issues = validate(song);
    const auto has = [&issues](IssueKind kind) {
        return std::ranges::any_of(issues, [kind](const Issue& i) { return i.kind == kind; });
    };
    CHECK(has(IssueKind::VoiceOverfull));
    CHECK(has(IssueKind::FretMismatch));
    CHECK(has(IssueKind::StringReused));
    CHECK(has(IssueKind::TieWithoutPrevious));
    CHECK_FALSE(has(IssueKind::VoiceIncomplete)); // voice 0 of measure 1 is now complete

    const auto mismatch = std::ranges::find_if(
        issues, [](const Issue& i) { return i.kind == IssueKind::FretMismatch; });
    CHECK(mismatch->measure == 1);
    CHECK(mismatch->beat == 1);
    CHECK(mismatch->note == 0);
}

TEST_CASE("Validation flags fret positions where they cannot exist", "[core][validation]") {
    Song song;
    song.addTrack(Track::fromPreset(*findInstrumentPreset("electric-bass")));
    song.addTrack(Track::fromPreset(*findInstrumentPreset("piano")));
    song.appendMeasures(1);
    Note highString = Note{.pitch = Pitch(Step::E, 0, 4),
                           .position = elara::core::FretPosition{.string = 5, .fret = 0}};
    song.track(0).staff(0).measure(0).voices[0].beats.push_back(
        Beat{.duration = Duration(NoteValue::Whole), .notes = {highString}});
    Note fretOnPiano =
        Note{.pitch = Pitch{}, .position = elara::core::FretPosition{.string = 1, .fret = 0}};
    song.track(1).staff(0).measure(0).voices[0].beats.push_back(
        Beat{.duration = Duration(NoteValue::Whole), .notes = {fretOnPiano}});

    const auto issues = validate(song);
    REQUIRE(issues.size() == 2);
    CHECK(issues[0].kind == IssueKind::StringOutOfRange);
    CHECK(issues[0].track == 0);
    CHECK(issues[1].kind == IssueKind::PositionWithoutTuning);
    CHECK(issues[1].track == 1);
}
