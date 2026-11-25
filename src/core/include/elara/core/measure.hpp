// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/duration.hpp>
#include <elara/core/effects.hpp>
#include <elara/core/fraction.hpp>
#include <elara/core/pitch.hpp>
#include <elara/core/shared_value.hpp>
#include <elara/core/tuning.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace elara::core {

/// One sounding note (ADR-0004). `pitch` is the sounding (concert) pitch; on a
/// fretted staff `position` says where it is played and must sound `pitch` under
/// the staff's tuning and capo. Percussion notes use the General MIDI percussion
/// key as the pitch's MIDI number.
struct Note {
    Pitch pitch{};
    std::optional<FretPosition> position{};
    NoteEffects effects{};

    friend bool operator==(const Note&, const Note&) = default;
};

/// Small notes played just before a beat, or on it, that take no time in the
/// rhythm (ADR-0079). `duration` is the written value; an on-beat grace borrows
/// its time from its own beat, the others from the beat before.
struct Grace {
    Duration duration{};
    std::vector<Note> notes{};
    bool onBeat = false;

    friend bool operator==(const Grace&, const Grace&) = default;
};

/// Where a chord's strings are stopped, drawn as a small grid (ADR-0080).
/// `frets[i]` is string i + 1, the top line of the tablature downwards, counted
/// from the capo like notes' frets; `firstFret` is the fret at the top of the
/// grid, 1 at the nut.
struct ChordDiagram {
    static constexpr std::int8_t kMuted = -1; // a string not played

    std::uint8_t firstFret = 1;
    std::vector<std::int8_t> frets{};

    /// The diagram of these frets: the grid starts at the nut unless a fret
    /// lies past the fourth, and at the lowest stopped fret then.
    [[nodiscard]] static ChordDiagram ofFrets(std::vector<std::int8_t> frets);

    friend bool operator==(const ChordDiagram&, const ChordDiagram&) = default;
};

/// The harmony named above a beat, "Am7" or "C/G", with its diagram on fretted
/// instruments (ADR-0080). It does not sound.
struct ChordSymbol {
    std::string name{};
    std::optional<ChordDiagram> diagram{};

    friend bool operator==(const ChordSymbol&, const ChordSymbol&) = default;
};

/// One syllable sung on a beat (ADR-0084). `hyphen` joins it to the next
/// syllable of its word, drawn between them; `extend` holds it over the notes
/// after it that sing nothing in its verse, drawn as a line (a melisma). Empty
/// text: nothing is sung there.
struct Syllable {
    std::string text{};
    bool hyphen = false;
    bool extend = false;

    friend bool operator==(const Syllable&, const Syllable&) = default;
};

/// Notes that start together and share a written duration. No notes = a rest.
/// `graces` lead into the beat, in the order they are played (ADR-0079);
/// `chord` names its harmony, shared by the beats that name the same one
/// (ADR-0080); `text` is written above it (ADR-0081); `lyrics` holds the
/// syllable of each verse sung on it, verse n + 1 at index n, never ending
/// with an empty one (ADR-0084). None of them sounds.
struct Beat {
    Duration duration{};
    std::vector<Note> notes{};
    BeatEffects effects{};
    std::vector<Grace> graces{};
    SharedOptional<ChordSymbol> chord{};
    SharedOptional<std::string> text{};
    SharedOptional<std::vector<Syllable>> lyrics{};

    [[nodiscard]] bool isRest() const noexcept { return notes.empty(); }

    friend bool operator==(const Beat&, const Beat&) = default;
};

/// A sequence of beats. Positions are derived from the durations, never stored.
struct Voice {
    std::vector<Beat> beats{};

    [[nodiscard]] bool isEmpty() const noexcept { return beats.empty(); }

    /// Total length in whole notes.
    [[nodiscard]] Fraction length() const;

    /// Start of beat `index` from the start of the measure, in whole notes.
    /// Throws std::out_of_range for a missing beat.
    [[nodiscard]] Fraction startOf(std::size_t index) const;

    friend bool operator==(const Voice&, const Voice&) = default;
};

/// The content of one staff in one measure: up to four independent voices.
struct Measure {
    static constexpr std::size_t kMaxVoices = 4;

    std::array<Voice, kMaxVoices> voices{};

    [[nodiscard]] bool isEmpty() const noexcept;

    friend bool operator==(const Measure&, const Measure&) = default;
};

} // namespace elara::core
