// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/instrument.hpp>

#include <algorithm>

namespace elara::core {

namespace {

using enum InstrumentKind;

constexpr std::array<Clef, 2> treble8vb{Clef::Treble8vb, Clef::Treble8vb};
constexpr std::array<Clef, 2> treble{Clef::Treble, Clef::Treble};
constexpr std::array<Clef, 2> bass8vb{Clef::Bass8vb, Clef::Bass8vb};
constexpr std::array<Clef, 2> grandStaff{Clef::Treble, Clef::Bass};
constexpr std::array<Clef, 2> percussion{Clef::Percussion, Clef::Percussion};

// General MIDI programs are 0-based. Written offsets follow common practice:
// guitars and basses are written an octave above the sound, B-flat instruments
// a major second above, E-flat alto saxophone a major sixth above.
constexpr std::array kPresets{
    InstrumentPreset{"electric-guitar", "Electric Guitar", Fretted, 27, 12, 1, treble8vb, "guitar"},
    InstrumentPreset{"distortion-guitar", "Distortion Guitar", Fretted, 30, 12, 1, treble8vb,
                     "guitar"},
    InstrumentPreset{"acoustic-guitar", "Acoustic Guitar", Fretted, 25, 12, 1, treble8vb, "guitar"},
    InstrumentPreset{"classical-guitar", "Classical Guitar", Fretted, 24, 12, 1, treble8vb,
                     "guitar"},
    InstrumentPreset{"seven-string-guitar", "7-String Guitar", Fretted, 30, 12, 1, treble8vb,
                     "guitar-7"},
    InstrumentPreset{"electric-bass", "Electric Bass", Fretted, 33, 12, 1, bass8vb, "bass"},
    InstrumentPreset{"five-string-bass", "5-String Bass", Fretted, 33, 12, 1, bass8vb, "bass-5"},
    InstrumentPreset{"fretless-bass", "Fretless Bass", Fretted, 35, 12, 1, bass8vb, "bass"},
    InstrumentPreset{"ukulele", "Ukulele", Fretted, 24, 0, 1, treble, "ukulele"},
    InstrumentPreset{"mandolin", "Mandolin", Fretted, 25, 0, 1, treble, "mandolin"},
    InstrumentPreset{"banjo", "Banjo", Fretted, 105, 12, 1, treble8vb, "banjo"},
    InstrumentPreset{"drum-kit", "Drums", Percussion, 0, 0, 1, percussion, ""},
    InstrumentPreset{"piano", "Piano", Keyboard, 0, 0, 2, grandStaff, ""},
    InstrumentPreset{"voice", "Voice", Pitched, 53, 0, 1, treble, ""},
    InstrumentPreset{"violin", "Violin", Pitched, 40, 0, 1, treble, ""},
    InstrumentPreset{"flute", "Flute", Pitched, 73, 0, 1, treble, ""},
    InstrumentPreset{"trumpet", "Trumpet in B-flat", Pitched, 56, 2, 1, treble, ""},
    InstrumentPreset{"alto-sax", "Alto Saxophone", Pitched, 65, 9, 1, treble, ""},
    InstrumentPreset{"tenor-sax", "Tenor Saxophone", Pitched, 66, 14, 1, treble, ""},
};

} // namespace

std::span<const InstrumentPreset> instrumentPresets() noexcept {
    return kPresets;
}

const InstrumentPreset* findInstrumentPreset(std::string_view id) noexcept {
    const auto found = std::ranges::find(kPresets, id, &InstrumentPreset::id);
    return found == kPresets.end() ? nullptr : &*found;
}

namespace {

constexpr std::array kDrumPieces{
    DrumPiece{49, "Crash cymbal"}, DrumPiece{42, "Closed hi-hat"}, DrumPiece{46, "Open hi-hat"},
    DrumPiece{51, "Ride cymbal"},  DrumPiece{53, "Ride bell"},     DrumPiece{50, "High tom"},
    DrumPiece{47, "Mid tom"},      DrumPiece{38, "Snare"},         DrumPiece{37, "Side stick"},
    DrumPiece{43, "Floor tom"},    DrumPiece{36, "Bass drum"},     DrumPiece{44, "Hi-hat pedal"},
};

} // namespace

std::span<const DrumPiece> drumPieces() noexcept {
    return kDrumPieces;
}

} // namespace elara::core
