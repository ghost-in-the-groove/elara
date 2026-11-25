// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/notation/display_list.hpp>

#include <array>
#include <cstddef>
#include <string_view>

namespace elara::notation {

namespace {

constexpr std::array<std::string_view, kRoleCount> kRoleNames{
    "staffLine", "ledgerLine", "barline",      "clef",          "keySignature", "timeSignature",
    "notehead",  "accidental", "dot",          "stem",          "flag",         "beam",
    "rest",      "fretNumber", "tie",          "slur",          "technique",    "bend",
    "slide",     "vibrato",    "articulation", "measureNumber", "credit",       "trackName",
    "repeat",    "volta",      "tuplet",       "tempo",         "dynamic",      "navigation",
    "section",   "brace",      "arpeggio",     "hairpin",
};

// Advance widths in em of a Helvetica-like font for printable ASCII; other
// characters count as an average letter.
double advance(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return 0.556;
    }
    switch (c) {
    case ' ':
    case '.':
    case ',':
    case '/':
    case 'i':
    case 'l':
    case 'j':
    case 'I':
        return 0.278;
    case '(':
    case ')':
    case 'f':
    case 't':
    case 'r':
        return 0.333;
    case '<':
    case '>':
    case '+':
    case '=':
        return 0.584;
    case 'm':
    case 'M':
    case 'W':
        return 0.833;
    case 'w':
        return 0.722;
    default:
        return c >= 'A' && c <= 'Z' ? 0.667 : 0.556;
    }
}

} // namespace

std::string_view roleName(Role role) noexcept {
    return kRoleNames[static_cast<std::size_t>(role)];
}

double textWidth(std::string_view text, double size) noexcept {
    double width = 0.0;
    for (const char c : text) {
        width += advance(c);
    }
    return width * size;
}

} // namespace elara::notation
