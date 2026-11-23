// SPDX-License-Identifier: AGPL-3.0-or-later
// The default drum map (docs/instruments.md): where a General MIDI percussion
// key sits on a five-line staff and which notehead it uses.
#pragma once

#include <cstdint>

namespace elara::notation::detail {

enum class DrumHead : std::uint8_t { Normal, X, Diamond };

struct DrumNote {
    int step = 4; ///< half staff spaces below the top line (4 = middle line)
    DrumHead head = DrumHead::Normal;
    bool foot = false; ///< played with a foot: stems down when hands and feet share a staff
};

/// The kit piece's place; keys the map does not name go on the middle line.
[[nodiscard]] DrumNote drumNote(int key) noexcept;

} // namespace elara::notation::detail
