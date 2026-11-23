// SPDX-License-Identifier: AGPL-3.0-or-later
#include "drum_map.hpp"

namespace elara::notation::detail {

DrumNote drumNote(int key) noexcept {
    // Steps count down from the top line (F5 in treble-clef terms): A5 is -2,
    // G5 -1, E5 1, D5 2, C5 3, A4 5, F4 7, D4 9.
    switch (key) {
    case 35: // acoustic bass drum
    case 36: // bass drum
        return {.step = 7, .foot = true};
    case 41: // low floor tom
    case 43: // high floor tom
        return {.step = 5};
    case 37: // side stick (a circled x in the drum map; an x until that glyph exists)
        return {.step = 3, .head = DrumHead::X};
    case 38: // snare
    case 40: // electric snare
        return {.step = 3};
    case 45: // low tom
    case 47: // low-mid tom
        return {.step = 2};
    case 48: // hi-mid tom
    case 50: // high tom
        return {.step = 1};
    case 51: // ride cymbal
    case 59: // ride cymbal 2
        return {.step = 0, .head = DrumHead::X};
    case 53: // ride bell
        return {.step = 0, .head = DrumHead::Diamond};
    case 42: // closed hi-hat
    case 46: // open hi-hat
        return {.step = -1, .head = DrumHead::X};
    case 49: // crash cymbal
    case 57: // crash cymbal 2
        return {.step = -2, .head = DrumHead::X};
    case 44: // hi-hat pedal
        return {.step = 9, .head = DrumHead::X, .foot = true};
    default:
        return {};
    }
}

} // namespace elara::notation::detail
