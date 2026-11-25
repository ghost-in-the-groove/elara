// SPDX-License-Identifier: AGPL-3.0-or-later
// One shared chord symbol for the beats that name the same chord (ADR-0080).
#pragma once

#include <elara/core/measure.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace elara::io::detail {

/// Hands out one shared symbol for equal chord symbols, so the beats of a song
/// that name the same chord point to one. Not for use from several threads.
class ChordPool {
public:
    [[nodiscard]] core::SharedOptional<core::ChordSymbol> share(core::ChordSymbol symbol) {
        const auto known = std::ranges::find_if(
            chords_,
            [&](const core::SharedOptional<core::ChordSymbol>& chord) { return *chord == symbol; });
        if (known != chords_.end()) {
            return *known;
        }
        return chords_.emplace_back(std::move(symbol));
    }

private:
    std::vector<core::SharedOptional<core::ChordSymbol>> chords_;
};

} // namespace elara::io::detail
