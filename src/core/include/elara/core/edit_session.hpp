// SPDX-License-Identifier: AGPL-3.0-or-later
// The editing session (ADR-0127): what keyboard entry keeps from one key to
// the next, and how each action moves it. The edits themselves are entry.hpp's
// (ADR-0021); this is the state they are entered with.
#pragma once

#include <elara/core/document.hpp>
#include <elara/core/duration.hpp>
#include <elara/core/entry.hpp>
#include <elara/core/song.hpp>

#include <array>
#include <cstddef>
#include <optional>

namespace elara::core {

/// The caret and the duration typing uses, kept inside the song they edit.
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

    [[nodiscard]] const Caret& caret() const noexcept { return caret_; }
    /// The duration typing gives a new beat.
    [[nodiscard]] Duration input() const noexcept { return input_; }

    /// Another song: the caret on the first beat of `track`, a quarter note to
    /// type.
    void start(std::size_t track = 0) noexcept;

    /// The track shown alone, which the caret stays on, or none when every
    /// track is shown and the caret stays on the one it was on.
    void showTrack(const Song& song, std::optional<std::size_t> track);

    /// The song changed otherwise than through the session (another edit, an
    /// undo, a redo): the caret goes back inside it.
    void songChanged(const Song& song);

    // ---- The caret ----------------------------------------------------------

    /// An arrow key (moveCaret()). The input duration follows the beat the
    /// caret lands on.
    void move(const Song& song, CaretMove move);

    /// Typing goes into voice `voice` (0 to 3) from now on: the caret moves to
    /// it, at its beat or the nearest one that voice has. Returns false, and
    /// changes nothing, for a voice the measure does not have or the one the
    /// caret is in.
    bool setVoice(const Song& song, std::size_t voice);

    /// A click: the caret goes to `caret`, or the nearest place the song has.
    void place(const Song& song, const Caret& caret);

    // ---- Edits --------------------------------------------------------------

    /// Enters an edit: the caret goes where `entry` says, and its command, if
    /// it has one, is executed on `document`. Returns whether the song
    /// changed. What the command throws goes on to the caller; the caret is
    /// inside the song either way.
    bool enter(Document& document, Entry entry);

    /// The duration typing uses from now on, which the caret's beat takes too
    /// (setBeatDuration()). Returns whether the song changed. When the beat
    /// cannot take it, what the edit throws goes on to the caller and the
    /// input duration is `duration` all the same.
    bool setInput(Document& document, Duration duration);

private:
    /// The caret back on the track shown and inside `song`.
    void fit(const Song& song);
    /// The input duration takes the caret beat's, when typing offers it.
    void followBeat(const Song& song);

    Caret caret_;
    Duration input_;
    std::optional<std::size_t> shown_; ///< the track shown alone
};

} // namespace elara::core
