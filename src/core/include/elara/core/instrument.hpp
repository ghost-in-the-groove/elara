// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace elara::core {

/// What code paths an instrument needs (docs/instruments.md). Everything else
/// about an instrument is data.
enum class InstrumentKind : std::uint8_t {
    Fretted,    // has a tuning; can show tablature
    Percussion, // notes are General MIDI percussion keys placed by a drum map
    Keyboard,   // usually two staves
    Pitched,    // any other melodic instrument or voice
};

enum class Clef : std::uint8_t {
    Treble,
    Treble8vb, // guitar: sounds an octave below the written note
    Bass,
    Bass8vb, // bass guitar
    Alto,
    Tenor,
    Percussion,
};

struct Instrument {
    std::string name{};
    InstrumentKind kind = InstrumentKind::Pitched;
    int midiProgram = 0;   // General MIDI program, 0-based
    int writtenOffset = 0; // semitones the written note sits above the sound

    friend bool operator==(const Instrument&, const Instrument&) = default;
};

/// A ready-made instrument: what a new track of this kind starts with.
struct InstrumentPreset {
    std::string_view id;   // stable identifier, e.g. "electric-guitar"
    std::string_view name; // default track name
    InstrumentKind kind;
    int midiProgram;
    int writtenOffset;
    int staffCount;                // 1 or 2
    std::array<Clef, 2> clefs;     // one per staff
    std::string_view tuningPreset; // Tuning::preset() id; empty when not fretted
};

[[nodiscard]] std::span<const InstrumentPreset> instrumentPresets() noexcept;

/// nullptr when there is no preset with that id.
[[nodiscard]] const InstrumentPreset* findInstrumentPreset(std::string_view id) noexcept;

} // namespace elara::core
