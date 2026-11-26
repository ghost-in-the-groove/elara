// SPDX-License-Identifier: AGPL-3.0-or-later
// The editing session (ADR-0127): what keyboard entry keeps from one key to
// the next, and how each action moves it. The edits themselves are entry.hpp's
// (ADR-0021); this is the state they are entered with.
#pragma once

#include <elara/core/clipboard.hpp>
#include <elara/core/document.hpp>
#include <elara/core/duration.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/song.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace elara::core {

/// The caret, the duration typing uses, the digit waiting for a second one,
/// the selected measures and the measures copied; all but the last kept
/// inside the song they edit.
///
/// The session holds no song: each action takes the song, or the document it
/// edits, and leaves the caret on a place that song has. A song without
/// tracks or measures has no place for a caret (hasCaret()): the actions do
/// nothing on it.
class EditSession {
public:
    /// The note values the input duration takes, longest first.
    static constexpr std::array kInputValues{
        NoteValue::Whole,     NoteValue::Half,         NoteValue::Quarter,    NoteValue::Eighth,
        NoteValue::Sixteenth, NoteValue::ThirtySecond, NoteValue::SixtyFourth};

    /// A tuplet the input duration takes (ADR-0050): `actual` notes in the
    /// time of `normal` ones.
    struct InputTuplet {
        int actual;
        int normal;
    };
    static constexpr std::array kInputTuplets{
        InputTuplet{2, 3}, InputTuplet{3, 2}, InputTuplet{4, 3}, InputTuplet{5, 4},
        InputTuplet{6, 4}, InputTuplet{7, 4}, InputTuplet{9, 8}};

    /// How long after a digit a second one makes a two-digit fret.
    static constexpr std::chrono::milliseconds kTwoDigitWindow{700};
    /// The clock typeDigit() is told the time by.
    using Clock = std::chrono::steady_clock;

    /// Measures of the song: the first and how many.
    struct Measures {
        std::size_t first = 0;
        std::size_t count = 0;

        [[nodiscard]] std::size_t last() const noexcept { return first + count - 1; }
        friend bool operator==(const Measures&, const Measures&) = default;
    };

    [[nodiscard]] const Caret& caret() const noexcept { return caret_; }
    /// The duration typing gives a new beat.
    [[nodiscard]] Duration input() const noexcept { return input_; }

    /// Another song: the caret on the first beat of `track`, a quarter note to
    /// type, nothing selected. What was copied stays, to paste into this one.
    void start(std::size_t track = 0) noexcept;

    /// The track shown alone, which the caret stays on, or none when every
    /// track is shown and the caret stays on the one it was on.
    void showTrack(const Song& song, std::optional<std::size_t> track);

    /// The song changed otherwise than through the session (another edit, an
    /// undo, a redo): the caret and the selection go back inside it, and a
    /// digit no longer waits for a second one.
    void songChanged(const Song& song);

    // ---- The caret ----------------------------------------------------------

    /// An arrow key (moveCaret()). The input duration follows the beat the
    /// caret lands on, and the selection ends.
    void move(const Song& song, CaretMove move);

    /// Typing goes into voice `voice` (0 to 3) from now on: the caret moves to
    /// it, at its beat or the nearest one that voice has. Returns false, and
    /// changes nothing, for a voice the measure does not have or the one the
    /// caret is in.
    bool setVoice(const Song& song, std::size_t voice);

    /// A click: the caret goes to `caret`, or the nearest place the song has,
    /// and the selection ends.
    void place(const Song& song, const Caret& caret);

    // ---- The selection (ADR-0027) -------------------------------------------

    /// Whether measures are selected: from where the selection started to the
    /// caret's measure, whichever comes first.
    [[nodiscard]] bool hasSelection() const noexcept { return anchor_.has_value(); }

    /// The measures an edit of measures acts on: the selected ones, or the
    /// caret's alone.
    [[nodiscard]] Measures selection() const noexcept;

    /// Shift and an arrow: the selection, started at the caret's measure when
    /// there is none, reaches `measures` further (back when negative, and no
    /// further than the ends of the song). The caret goes to the first beat of
    /// the measure it reaches.
    void extendSelection(const Song& song, long long measures);

    /// Every measure, the caret on the first.
    void selectAll(const Song& song);

    /// Returns whether there was a selection to end.
    bool clearSelection() noexcept;

    // ---- The clipboard (ADR-0027) and transposing (ADR-0026) ----------------

    [[nodiscard]] bool canPaste() const noexcept { return clip_.has_value(); }

    /// Copies selection() from the track shown alone, or from every track,
    /// and returns the measures copied.
    Measures copy(const Song& song);

    /// Pastes what was copied at the first measure of selection(): over the
    /// measures from it, or into new ones before it (makePaste()). One copied
    /// track goes to the caret's, several to the tracks they came from. What
    /// was pasted is selected, the caret on its last measure. Throws
    /// std::logic_error when nothing was copied and what makePaste() throws
    /// for measures that do not fit; nothing changes then.
    bool paste(Document& document, PasteMode mode);

    /// Moves the notes of selection() by `semitones`, on the track shown
    /// alone or on every track (makeTranspose()). Returns whether the song
    /// changed; what makeTranspose() throws goes on to the caller, and
    /// nothing changes then.
    bool transpose(Document& document, int semitones);

    // ---- Edits --------------------------------------------------------------

    /// Enters an edit: the caret goes where `entry` says, and its command, if
    /// it has one, is executed on `document`. Returns whether the song
    /// changed. What the command throws goes on to the caller; the caret is
    /// inside the song either way.
    bool enter(Document& document, Entry entry);

    /// Types a digit at `now`: a fret on the caret's string (enterFret()).
    /// A second digit within kTwoDigitWindow of a first one that was not 0,
    /// with nothing done in between, makes the two-digit fret instead, as one
    /// undo step; when that fret is off the neck the first digit stays and
    /// the second starts a fret of its own. Returns whether the song changed.
    /// What enterFret() throws for a fret of one digit goes on to the caller.
    bool typeDigit(Document& document, int digit, Clock::time_point now);

    /// Ends the wait for a second digit, as every action but a digit does:
    /// for an edit at the caret that may turn out to change nothing and never
    /// be entered.
    void endTyping() noexcept { typed_.reset(); }

    /// The duration typing uses from now on, which the caret's beat takes too
    /// (setBeatDuration()). Returns whether the song changed. When the beat
    /// cannot take it, what the edit throws goes on to the caller and the
    /// input duration is `duration` all the same.
    bool setInput(Document& document, Duration duration);

private:
    /// The caret back on the track shown and inside `song`, and the selection
    /// inside it.
    void fit(const Song& song);
    /// The caret on `caret`, or the nearest place the song has; the input
    /// duration follows its beat.
    void moveTo(const Song& song, const Caret& caret);
    /// The input duration takes the caret beat's, when typing offers it.
    void followBeat(const Song& song);

    /// A digit just typed, which a second one may follow.
    struct Typed {
        int digit = 0;
        Caret caret;                ///< where it was typed
        std::uint64_t revision = 0; ///< the song's state after it
        bool changed = false;       ///< whether it changed the song, and can be undone
        Clock::time_point at;
    };

    Caret caret_;
    Duration input_;
    std::optional<std::size_t> shown_; ///< the track shown alone
    std::optional<Typed> typed_;
    std::optional<std::size_t> anchor_; ///< the measure the selection started at
    std::optional<MeasureClip> clip_;   ///< the measures copied
};

} // namespace elara::core
