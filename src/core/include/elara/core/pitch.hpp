// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace elara::core {

/// Diatonic step (note name without accidental).
enum class Step : std::uint8_t { C, D, E, F, G, A, B };

/// Accidental used when a pitch has to be spelled from a MIDI number alone.
enum class SpellingPreference : std::uint8_t { Sharps, Flats };

/// Written pitch: step, alteration in semitones and octave, in scientific pitch
/// notation (C4 is middle C, MIDI 60; the octave number changes between B and C).
///
/// The spelling is kept: B#3, C4 and Dbb4 sound the same (isEnharmonicWith) but
/// are different pitches. Always inside the MIDI range 0..127.
class Pitch {
public:
    static constexpr int kMinAlter = -2; // double flat
    static constexpr int kMaxAlter = 2;  // double sharp

    constexpr Pitch() noexcept = default; // middle C

    /// Throws std::invalid_argument when `alter` is outside [kMinAlter, kMaxAlter]
    /// or the pitch falls outside MIDI 0..127.
    constexpr Pitch(Step step, int alter, int octave)
        : step_(step), alter_(static_cast<std::int8_t>(alter)),
          octave_(static_cast<std::int8_t>(octave)) {
        if (alter < kMinAlter || alter > kMaxAlter) {
            throw std::invalid_argument("Pitch: unsupported alteration");
        }
        if (const int midi = midiFor(step, alter, octave); midi < 0 || midi > 127) {
            throw std::invalid_argument("Pitch: outside the MIDI range");
        }
    }

    [[nodiscard]] constexpr Step step() const noexcept { return step_; }
    [[nodiscard]] constexpr int alter() const noexcept { return alter_; }
    [[nodiscard]] constexpr int octave() const noexcept { return octave_; }

    /// MIDI note number, 0..127.
    [[nodiscard]] constexpr int midiNumber() const noexcept {
        return midiFor(step_, alter_, octave_);
    }

    /// Spells a MIDI number: naturals stay natural, black keys use one sharp or
    /// one flat. Throws std::invalid_argument outside 0..127.
    [[nodiscard]] static constexpr Pitch
    fromMidi(int midiNumber, SpellingPreference preference = SpellingPreference::Sharps) {
        if (midiNumber < 0 || midiNumber > 127) {
            throw std::invalid_argument("Pitch: outside the MIDI range");
        }
        struct Spelled {
            Step step;
            int alter;
        };
        constexpr std::array<Spelled, 12> kSharps{{{Step::C, 0},
                                                   {Step::C, 1},
                                                   {Step::D, 0},
                                                   {Step::D, 1},
                                                   {Step::E, 0},
                                                   {Step::F, 0},
                                                   {Step::F, 1},
                                                   {Step::G, 0},
                                                   {Step::G, 1},
                                                   {Step::A, 0},
                                                   {Step::A, 1},
                                                   {Step::B, 0}}};
        constexpr std::array<Spelled, 12> kFlats{{{Step::C, 0},
                                                  {Step::D, -1},
                                                  {Step::D, 0},
                                                  {Step::E, -1},
                                                  {Step::E, 0},
                                                  {Step::F, 0},
                                                  {Step::G, -1},
                                                  {Step::G, 0},
                                                  {Step::A, -1},
                                                  {Step::A, 0},
                                                  {Step::B, -1},
                                                  {Step::B, 0}}};
        const auto pitchClass = static_cast<std::size_t>(midiNumber % 12);
        const Spelled s =
            preference == SpellingPreference::Sharps ? kSharps[pitchClass] : kFlats[pitchClass];
        return Pitch(s.step, s.alter, midiNumber / 12 - 1);
    }

    /// Text form: step, accidentals ("#", "##", "b", "bb") and octave, e.g. "F#3",
    /// "Bb2", "C-1".
    [[nodiscard]] std::string toString() const;

    /// Reads the toString() form. The step is case-insensitive and "x" is
    /// accepted for a double sharp. Returns std::nullopt for malformed text or a
    /// pitch outside MIDI 0..127.
    [[nodiscard]] static std::optional<Pitch> parse(std::string_view text);

    /// Frequency in Hz in twelve-tone equal temperament, with A4 at `a4Hz`.
    [[nodiscard]] double frequency(double a4Hz = 440.0) const noexcept;

    /// True when both pitches sound the same (same MIDI number).
    [[nodiscard]] constexpr bool isEnharmonicWith(const Pitch& other) const noexcept {
        return midiNumber() == other.midiNumber();
    }

    /// Spelling-sensitive: B#3 != C4.
    friend constexpr bool operator==(const Pitch&, const Pitch&) noexcept = default;

private:
    static constexpr int midiFor(Step step, int alter, int octave) noexcept {
        constexpr std::array<int, 7> kSemitones{0, 2, 4, 5, 7, 9, 11};
        return (octave + 1) * 12 + kSemitones[static_cast<std::size_t>(step)] + alter;
    }

    Step step_ = Step::C;
    std::int8_t alter_ = 0;
    std::int8_t octave_ = 4;
};

} // namespace elara::core
