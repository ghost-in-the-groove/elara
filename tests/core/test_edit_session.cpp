// SPDX-License-Identifier: AGPL-3.0-or-later
// The editing session (ADR-0127): the caret and the input duration from one
// action to the next.
#include <elara/core/commands.hpp>
#include <elara/core/document.hpp>
#include <elara/core/edit_session.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string_view>

using namespace elara::core;

namespace {

const Duration kQuarter(NoteValue::Quarter);
const Duration kEighth(NoteValue::Eighth);
const Duration kHalf(NoteValue::Half);

/// A guitar and a bass in 4/4 over three measures. The guitar plays an eighth
/// and a dotted half in the first measure, a quintuplet eighth in the second
/// and nothing in the third; the bass plays nothing.
Song bandSong() {
    Song song;
    song.appendMeasures(3);
    song.addTrack(Track::fromPreset(*findInstrumentPreset("electric-guitar")));
    song.addTrack(Track::fromPreset(*findInstrumentPreset("electric-bass")));
    Staff& staff = song.track(0).staff(0);
    staff.measure(0).voices[0].beats = {
        Beat{.duration = kEighth, .notes = {staff.frettedNote(6, 3)}},
        Beat{.duration = Duration(NoteValue::Half, 1), .notes = {staff.frettedNote(5, 2)}}};
    staff.measure(1).voices[0].beats = {
        Beat{.duration = Duration(NoteValue::Eighth, 0, Tuplet(5, 4)),
             .notes = {staff.frettedNote(1, 0)}}};
    return song;
}

const Voice& voice(const Song& song, std::size_t measure, std::size_t index = 0,
                   std::size_t track = 0) {
    return song.track(track).staff(0).measure(measure).voices.at(index);
}

/// A command that the song refuses as it is executed.
class Refused final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "Refused"; }
    void apply(Song& /*song*/) override { throw std::logic_error("refused"); }
    void revert(Song& /*song*/) override {}
};

} // namespace

TEST_CASE("Session: it starts on the first beat of a track, a quarter note to type",
          "[core][session]") {
    EditSession session;
    CHECK(session.caret() == Caret{});
    CHECK(session.input() == kQuarter);

    Document doc(bandSong());
    session.move(doc.song(), CaretMove::NextMeasure);
    session.setInput(doc, kHalf);
    session.start(1);
    CHECK(session.caret() == Caret{.track = 1});
    CHECK(session.input() == kQuarter);
}

TEST_CASE("Session: the input duration follows the beat the caret moves to", "[core][session]") {
    const Song song = bandSong();
    EditSession session;
    // Where it starts it has followed nothing yet.
    CHECK(session.input() == kQuarter);
    session.move(song, CaretMove::NextBeat);
    CHECK(session.caret().beat == 1);
    CHECK(session.input() == Duration(NoteValue::Half, 1));
    session.move(song, CaretMove::PreviousBeat);
    CHECK(session.input() == kEighth);

    // The free slot has no duration: the input stays.
    session.move(song, CaretMove::NextBeat);
    session.move(song, CaretMove::NextBeat);
    REQUIRE(onFreeSlot(song, session.caret()));
    CHECK(session.input() == Duration(NoteValue::Half, 1));

    // A tuplet typing offers is followed too.
    session.move(song, CaretMove::NextMeasure);
    CHECK(session.input() == Duration(NoteValue::Eighth, 0, Tuplet(5, 4)));
}

TEST_CASE("Session: a duration typing does not offer is not followed", "[core][session]") {
    Song song = bandSong();
    Staff& staff = song.track(0).staff(0);
    staff.measure(2).voices[0].beats = {
        Beat{.duration = Duration(NoteValue::Quarter, 2), .notes = {staff.frettedNote(1, 0)}},
        Beat{.duration = Duration(NoteValue::Eighth, 0, Tuplet(11, 8)),
             .notes = {staff.frettedNote(1, 0)}}};
    EditSession session;
    session.place(song, Caret{.measure = 2});
    CHECK(session.input() == kQuarter); // two dots
    session.move(song, CaretMove::NextBeat);
    CHECK(session.input() == kQuarter); // eleven in the time of eight
}

TEST_CASE("Session: a click puts the caret on the nearest place the song has", "[core][session]") {
    const Song song = bandSong();
    EditSession session;
    session.place(song, Caret{.measure = 0, .beat = 1, .string = 4});
    CHECK(session.caret() == Caret{.measure = 0, .beat = 1, .string = 4});
    CHECK(session.input() == Duration(NoteValue::Half, 1));

    session.place(song, Caret{.track = 7, .measure = 9, .beat = 9, .string = 12});
    CHECK(session.caret() == Caret{.track = 1, .measure = 2, .beat = 0, .string = 4});
}

TEST_CASE("Session: typing goes into another voice from its nearest beat", "[core][session]") {
    Document doc(bandSong());
    EditSession session;
    session.move(doc.song(), CaretMove::NextBeat);
    CHECK(session.setVoice(doc.song(), 1));
    // The second voice is empty: its free slot.
    CHECK(session.caret() == Caret{.voice = 1, .beat = 0});
    CHECK_FALSE(session.setVoice(doc.song(), 1)); // there already
    CHECK_FALSE(session.setVoice(doc.song(), 4)); // a measure has four
    CHECK(session.caret().voice == 1);

    REQUIRE(session.enter(doc, enterFret(doc.song(), session.caret(), 7, session.input())));
    CHECK(voice(doc.song(), 0, 1).beats.size() == 1);
    CHECK(voice(doc.song(), 0, 0).beats.size() == 2);
}

TEST_CASE("Session: entering an edit moves the caret and executes its command", "[core][session]") {
    Document doc(bandSong());
    EditSession session;
    session.place(doc.song(), Caret{.measure = 2});

    // A fret on the free slot: a beat of the input duration, the caret on it.
    CHECK(session.enter(doc, enterFret(doc.song(), session.caret(), 5, session.input())));
    REQUIRE(voice(doc.song(), 2).beats.size() == 1);
    CHECK(voice(doc.song(), 2).beats[0].duration == kQuarter);
    CHECK(session.caret() == Caret{.measure = 2});
    CHECK(doc.undoName() == "Enter note");

    // An entry without a command only moves the caret.
    CHECK_FALSE(session.enter(doc, Entry{.command = nullptr, .caret = Caret{.measure = 1}}));
    CHECK(session.caret() == Caret{.measure = 1});
    CHECK(doc.undoCount() == 1);

    // The caret an entry names may be of the song after it: a measure added
    // at the end takes the caret.
    CHECK(session.enter(doc, Entry{.command = std::make_unique<InsertMeasures>(3, 1),
                                   .caret = Caret{.measure = 3}}));
    CHECK(doc.song().measureCount() == 4);
    CHECK(session.caret() == Caret{.measure = 3});
}

TEST_CASE("Session: an edit the song refuses leaves the caret inside the song", "[core][session]") {
    Document doc(bandSong());
    EditSession session;
    CHECK_THROWS_AS(session.enter(doc, Entry{.command = std::make_unique<Refused>(),
                                             .caret = Caret{.measure = 8, .beat = 3}}),
                    std::logic_error);
    CHECK(session.caret() == Caret{.measure = 2});
    CHECK_FALSE(doc.canUndo());
}

TEST_CASE("Session: the input duration is given to the caret's beat", "[core][session]") {
    Document doc(bandSong());
    EditSession session;
    CHECK(session.setInput(doc, kHalf));
    CHECK(session.input() == kHalf);
    CHECK(voice(doc.song(), 0).beats[0].duration == kHalf);
    CHECK(doc.undoName() == "Change duration");
    CHECK_FALSE(session.setInput(doc, kHalf)); // it has it

    // The free slot has no beat to give it to; typing uses it from now on.
    session.place(doc.song(), Caret{.measure = 2});
    CHECK_FALSE(session.setInput(doc, kEighth));
    CHECK(session.input() == kEighth);
    REQUIRE(session.enter(doc, enterFret(doc.song(), session.caret(), 5, session.input())));
    CHECK(voice(doc.song(), 2).beats[0].duration == kEighth);
}

TEST_CASE("Session: the caret stays on the track shown alone", "[core][session]") {
    Document doc(bandSong());
    EditSession session;
    session.place(doc.song(), Caret{.measure = 1, .string = 6});
    session.showTrack(doc.song(), 1);
    // The bass has four strings and nothing in that measure.
    CHECK(session.caret() == Caret{.track = 1, .measure = 1, .beat = 0, .string = 4});

    // An edit whose caret is on another track comes back to the one shown.
    session.enter(doc, Entry{.command = nullptr, .caret = Caret{.track = 0, .measure = 1}});
    CHECK(session.caret().track == 1);

    // With every track shown the caret stays on the one it was on.
    session.showTrack(doc.song(), std::nullopt);
    CHECK(session.caret().track == 1);
    session.enter(doc, Entry{.command = nullptr, .caret = Caret{.track = 0, .measure = 1}});
    CHECK(session.caret().track == 0);

    // The track shown goes: the caret is on the last the song has.
    session.showTrack(doc.song(), 1);
    doc.execute(std::make_unique<RemoveTrack>(1));
    session.showTrack(doc.song(), 0);
    CHECK(session.caret().track == 0);
}

TEST_CASE("Session: a song without tracks or measures has no caret to move", "[core][session]") {
    Song empty;
    EditSession session;
    session.move(empty, CaretMove::NextBeat);
    session.place(empty, Caret{.measure = 3});
    session.showTrack(empty, 2);
    CHECK_FALSE(session.setVoice(empty, 1));
    CHECK(session.caret() == Caret{});

    Document doc(empty);
    CHECK_FALSE(session.setInput(doc, kHalf));
    CHECK(session.input() == kHalf);
    CHECK_FALSE(session.enter(doc, Entry{.command = nullptr, .caret = Caret{.measure = 3}}));
}

// ---- Two digits for one fret --------------------------------------------------

namespace {

using namespace std::chrono_literals;

/// A session on the first beat of the third measure of bandSong(), which is
/// empty, and the time its digits are typed at.
struct Typing {
    Document doc{bandSong()};
    EditSession session;
    EditSession::Clock::time_point now{};

    Typing() { session.place(doc.song(), Caret{.measure = 2}); }

    bool type(int digit, std::chrono::milliseconds later = 100ms) {
        now += later;
        return session.typeDigit(doc, digit, now);
    }
    /// The fret on the first string of the caret's beat; -1 without a note there.
    [[nodiscard]] int fret() const {
        const Voice& typed = voice(doc.song(), 2);
        if (session.caret().beat >= typed.beats.size()) {
            return -1;
        }
        for (const Note& note : typed.beats[session.caret().beat].notes) {
            if (note.position && note.position->string == session.caret().string) {
                return note.position->fret;
            }
        }
        return -1;
    }
};

} // namespace

TEST_CASE("Session: two digits make one fret, as one undo step", "[core][session]") {
    Typing typing;
    CHECK(typing.type(1));
    CHECK(typing.fret() == 1);
    CHECK(typing.type(2));
    CHECK(typing.fret() == 12);
    CHECK(typing.doc.undoCount() == 1);
    CHECK_FALSE(typing.doc.canRedo());
    typing.doc.undo();
    CHECK(typing.fret() == -1);

    // A third digit starts another fret.
    typing.doc.redo();
    typing.session.songChanged(typing.doc.song());
    CHECK(typing.type(1));
    CHECK(typing.type(2));
    CHECK(typing.type(3));
    CHECK(typing.fret() == 3);
}

TEST_CASE("Session: the second digit comes within the moment, or starts a fret of its own",
          "[core][session]") {
    Typing typing;
    typing.type(1);
    typing.type(2, EditSession::kTwoDigitWindow); // just in time
    CHECK(typing.fret() == 12);

    typing.type(1, 5s);
    typing.type(2, EditSession::kTwoDigitWindow + 1ms); // just late
    CHECK(typing.fret() == 2);
    typing.doc.undo();
    CHECK(typing.fret() == 1); // the first digit was a step of its own
}

TEST_CASE("Session: a first digit 0 is fret 0, and a fret off the neck keeps the first digit",
          "[core][session]") {
    Typing typing;
    typing.type(0);
    typing.type(7);
    CHECK(typing.fret() == 7);
    typing.doc.undo();
    CHECK(typing.fret() == 0);
    typing.session.songChanged(typing.doc.song());

    typing.type(4, 5s);
    CHECK(typing.type(5)); // no fret 45
    CHECK(typing.fret() == 5);
    typing.doc.undo();
    CHECK(typing.fret() == 4);
}

TEST_CASE("Session: two digits that change nothing leave the song as it was", "[core][session]") {
    Typing typing;
    typing.type(1);
    typing.type(2);
    typing.type(1, 5s); // fret 1 over 12
    // Fret 12 again: the first digit is undone, and there is nothing to enter.
    CHECK(typing.type(2));
    CHECK(typing.fret() == 12);
    CHECK(typing.doc.undoCount() == 1);
}

TEST_CASE("Session: any other action ends the wait for a second digit", "[core][session]") {
    Typing typing;
    const auto second = [&typing](auto&& between) {
        typing.type(1, 5s);
        between(typing.session, typing.doc);
        typing.type(2);
        return typing.fret();
    };
    CHECK(second([](EditSession&, Document&) {}) == 12);

    // The caret moved away and came back.
    CHECK(second([](EditSession& session, Document& doc) {
              session.move(doc.song(), CaretMove::StringDown);
              session.move(doc.song(), CaretMove::StringUp);
          }) == 2);
    CHECK(second([](EditSession& session, Document& doc) {
              session.place(doc.song(), session.caret());
          }) == 2);
    CHECK(second([](EditSession& session, Document& doc) {
              session.setVoice(doc.song(), 1);
              session.setVoice(doc.song(), 0);
          }) == 2);
    // An edit, though it changed nothing, and one that is never entered.
    CHECK(second([](EditSession& session, Document& doc) {
              session.enter(doc, Entry{.command = nullptr, .caret = session.caret()});
          }) == 2);
    CHECK(second([](EditSession& session, Document&) { session.endTyping(); }) == 2);
    CHECK(second([](EditSession& session, Document& doc) {
              session.setInput(doc, session.input());
          }) == 2);
    // The song changed under the session, and came back to the same state.
    CHECK(second([](EditSession& session, Document& doc) {
              doc.undo();
              doc.redo();
              session.songChanged(doc.song());
          }) == 2);
    // The same, though nobody told the session: the song is not as the digit left it.
    CHECK(second([](EditSession&, Document& doc) { doc.undo(); }) == 2);
    // Another song.
    typing.type(1, 5s);
    typing.session.start(0);
    typing.session.place(typing.doc.song(), Caret{.measure = 2});
    typing.type(2);
    CHECK(typing.fret() == 2);
}

TEST_CASE("Session: a digit where no fret can be typed says why and waits for nothing",
          "[core][session]") {
    Song song;
    song.appendMeasures(1);
    song.addTrack(Track::fromPreset(*findInstrumentPreset("piano")));
    Document doc(song);
    EditSession session;
    CHECK_THROWS_AS(session.typeDigit(doc, 3, {}), std::logic_error);
    CHECK_FALSE(doc.canUndo());
}

// ---- The selection, the clipboard and transposing ------------------------------

namespace {

using Measures = EditSession::Measures;

/// bandSong() with a note for the bass in the first measure.
Song bandSongWithBass() {
    Song song = bandSong();
    Staff& bass = song.track(1).staff(0);
    bass.measure(0).voices[0].beats = {Beat{.duration = kHalf, .notes = {bass.frettedNote(4, 3)}}};
    return song;
}

int midiOf(const Song& song, std::size_t track, std::size_t measure) {
    return voice(song, measure, 0, track).beats.at(0).notes.at(0).pitch.midiNumber();
}

} // namespace

TEST_CASE("Session: a selection runs from where it started to the caret's measure",
          "[core][session]") {
    const Song song = bandSong();
    EditSession session;
    session.place(song, Caret{.measure = 1});
    // Without one, an edit of measures takes the caret's.
    CHECK_FALSE(session.hasSelection());
    CHECK(session.selection() == Measures{.first = 1, .count = 1});

    session.extendSelection(song, 1);
    CHECK(session.hasSelection());
    CHECK(session.selection() == Measures{.first = 1, .count = 2});
    CHECK(session.caret() == Caret{.measure = 2});
    // Back past where it started: it turns round, the caret on a first beat.
    session.extendSelection(song, -2);
    CHECK(session.selection() == Measures{.first = 0, .count = 2});
    CHECK(session.selection().last() == 1);
    CHECK(session.caret() == Caret{.measure = 0});
    CHECK(session.input() == kEighth); // the beat it landed on
    // It stops at the ends of the song, and no measures further is no change.
    session.extendSelection(song, -5);
    CHECK(session.selection() == Measures{.first = 0, .count = 2});
    session.extendSelection(song, 50);
    CHECK(session.selection() == Measures{.first = 1, .count = 2});
    session.extendSelection(song, 0);
    CHECK(session.selection() == Measures{.first = 1, .count = 2});

    session.selectAll(song);
    CHECK(session.selection() == Measures{.first = 0, .count = 3});
    CHECK(session.caret() == Caret{.measure = 0});
}

TEST_CASE("Session: an arrow or a click ends the selection, another voice keeps it",
          "[core][session]") {
    const Song song = bandSong();
    EditSession session;
    session.extendSelection(song, 1);
    CHECK(session.setVoice(song, 1));
    CHECK(session.hasSelection());
    session.move(song, CaretMove::StringDown);
    CHECK_FALSE(session.hasSelection());

    session.extendSelection(song, 1);
    session.place(song, session.caret());
    CHECK_FALSE(session.hasSelection());

    session.extendSelection(song, 1);
    CHECK(session.clearSelection());
    CHECK_FALSE(session.clearSelection()); // none to end
    CHECK_FALSE(session.hasSelection());
}

TEST_CASE("Session: the selection stays inside the song, and ends with it", "[core][session]") {
    Document doc(bandSong());
    EditSession session;
    session.selectAll(doc.song()); // it started at the third measure
    doc.execute(std::make_unique<RemoveMeasures>(1, 2));
    session.songChanged(doc.song());
    CHECK(session.selection() == Measures{.first = 0, .count = 1});
    CHECK(session.hasSelection());

    session.start(0);
    CHECK_FALSE(session.hasSelection());
}

TEST_CASE("Session: copy and paste take the track shown alone, or every track", "[core][session]") {
    Document doc(bandSongWithBass());
    EditSession session;
    CHECK_FALSE(session.canPaste());

    // The guitar alone, its first two measures.
    session.showTrack(doc.song(), 0);
    session.extendSelection(doc.song(), 1);
    CHECK(session.copy(doc.song()) == Measures{.first = 0, .count = 2});
    CHECK(session.canPaste());
    CHECK(session.hasSelection()); // copying leaves it

    // Over the third measure on: the song grows by the one that does not fit.
    session.place(doc.song(), Caret{.measure = 2});
    CHECK(session.paste(doc, PasteMode::Replace));
    CHECK(doc.song().measureCount() == 4);
    CHECK(voice(doc.song(), 2) == voice(doc.song(), 0));
    CHECK(voice(doc.song(), 3) == voice(doc.song(), 1));
    CHECK(voice(doc.song(), 2, 0, 1).beats.empty()); // the bass was not copied
    // What was pasted is selected, the caret on the first beat of its last measure.
    CHECK(session.selection() == Measures{.first = 2, .count = 2});
    CHECK(session.caret() == Caret{.measure = 3});

    // Every track, into new measures before the selection's first.
    doc.undo();
    session.songChanged(doc.song());
    session.showTrack(doc.song(), std::nullopt);
    session.place(doc.song(), Caret{.measure = 0});
    CHECK(session.copy(doc.song()) == Measures{.first = 0, .count = 1});
    session.place(doc.song(), Caret{.track = 1, .measure = 1});
    CHECK(session.paste(doc, PasteMode::Insert));
    CHECK(doc.song().measureCount() == 4);
    CHECK(voice(doc.song(), 1, 0, 0) == voice(doc.song(), 0, 0, 0));
    CHECK(voice(doc.song(), 1, 0, 1) == voice(doc.song(), 0, 0, 1));
    CHECK(session.selection() == Measures{.first = 1, .count = 1});
    CHECK(session.caret().track == 1);
    CHECK(session.caret().measure == 1);
}

TEST_CASE("Session: what was copied can be pasted into another song", "[core][session]") {
    Document doc(bandSong());
    EditSession session;
    session.showTrack(doc.song(), 0);
    session.copy(doc.song());

    Document other(bandSong());
    session.start(0);
    session.showTrack(other.song(), 0);
    session.place(other.song(), Caret{.measure = 2});
    CHECK(session.paste(other, PasteMode::Replace));
    CHECK(voice(other.song(), 2) == voice(doc.song(), 0));
}

TEST_CASE("Session: a paste that cannot be changes nothing", "[core][session]") {
    Song song = bandSong();
    song.addTrack(Track::fromPreset(*findInstrumentPreset("drum-kit")));
    Document doc(song);
    EditSession session;
    session.showTrack(doc.song(), 0);
    CHECK_THROWS_AS(session.paste(doc, PasteMode::Replace), std::logic_error);

    // A guitar's measure does not go to the drums.
    session.copy(doc.song());
    session.showTrack(doc.song(), 2);
    session.place(doc.song(), Caret{.track = 2, .measure = 1});
    const Caret before = session.caret();
    CHECK_THROWS_AS(session.paste(doc, PasteMode::Replace), std::invalid_argument);
    CHECK(session.caret() == before);
    CHECK_FALSE(session.hasSelection());
    CHECK_FALSE(doc.canUndo());

    // Copied from every track, it needs as many tracks from the first.
    Document small(bandSong());
    session.showTrack(doc.song(), std::nullopt);
    session.copy(doc.song());
    CHECK_THROWS_AS(session.paste(small, PasteMode::Replace), std::out_of_range);
    CHECK_FALSE(small.canUndo());
}

TEST_CASE("Session: transposing moves the selection on the track shown, or on every track",
          "[core][session]") {
    Document doc(bandSongWithBass());
    EditSession session;
    const int guitar = midiOf(doc.song(), 0, 0);
    const int second = midiOf(doc.song(), 0, 1);
    const int bass = midiOf(doc.song(), 1, 0);

    // The caret's measure of the guitar.
    session.showTrack(doc.song(), 0);
    CHECK(session.transpose(doc, 2));
    CHECK(midiOf(doc.song(), 0, 0) == guitar + 2);
    CHECK(midiOf(doc.song(), 0, 1) == second);
    CHECK(midiOf(doc.song(), 1, 0) == bass);

    // Every measure of every track; the selection stays.
    session.showTrack(doc.song(), std::nullopt);
    session.selectAll(doc.song());
    CHECK(session.transpose(doc, -1));
    CHECK(midiOf(doc.song(), 0, 0) == guitar + 1);
    CHECK(midiOf(doc.song(), 0, 1) == second - 1);
    CHECK(midiOf(doc.song(), 1, 0) == bass - 1);
    CHECK(session.selection() == Measures{.first = 0, .count = 3});

    CHECK_FALSE(session.transpose(doc, 0));
    const std::size_t steps = doc.undoCount();
    CHECK_THROWS(session.transpose(doc, -60)); // below the lowest string
    CHECK(doc.undoCount() == steps);
    CHECK(midiOf(doc.song(), 1, 0) == bass - 1);
}
