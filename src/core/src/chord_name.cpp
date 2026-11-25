// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/chord_name.hpp>

#include <cstddef>
#include <cstdlib>
#include <utility>

namespace elara::core {

namespace {

constexpr std::string_view kLetters = "CDEFGAB";

/// The pitch at the start of `text`, and how many characters it takes.
std::optional<std::pair<ChordPitch, std::size_t>> pitchAtStart(std::string_view text) {
    const std::size_t index = text.empty() ? std::string_view::npos : kLetters.find(text[0]);
    if (index == std::string_view::npos) {
        return std::nullopt;
    }
    ChordPitch pitch{.step = static_cast<Step>(index), .alter = 0};
    std::size_t used = 1;
    for (; used < text.size() && std::abs(pitch.alter) < 2; ++used) {
        if (text[used] == '#') {
            ++pitch.alter;
        } else if (text[used] == 'b') {
            --pitch.alter;
        } else {
            break;
        }
    }
    return std::pair{pitch, used};
}

} // namespace

std::string ChordPitch::toString() const {
    std::string text(1, kLetters[static_cast<std::size_t>(step)]);
    text.append(static_cast<std::size_t>(std::abs(alter)), alter < 0 ? 'b' : '#');
    return text;
}

std::optional<ChordNameParts> splitChordName(std::string_view name) {
    const auto root = pitchAtStart(name);
    if (!root) {
        return std::nullopt;
    }
    ChordNameParts parts{.root = root->first, .suffix = name.substr(root->second), .bass = {}};
    if (const std::size_t slash = parts.suffix.rfind('/'); slash != std::string_view::npos) {
        const std::string_view after = parts.suffix.substr(slash + 1);
        if (const auto bass = pitchAtStart(after); bass && bass->second == after.size()) {
            parts.bass = bass->first;
            parts.suffix = parts.suffix.substr(0, slash);
        }
    }
    return parts;
}

} // namespace elara::core
