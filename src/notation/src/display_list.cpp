// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/notation/display_list.hpp>

#include <array>
#include <cstddef>
#include <string_view>

namespace elara::notation {

namespace {

constexpr std::array<std::string_view, kRoleCount> kRoleNames{
    "staffLine",   "ledgerLine", "barline",      "clef",          "keySignature", "timeSignature",
    "notehead",    "accidental", "dot",          "stem",          "flag",         "beam",
    "rest",        "fretNumber", "tie",          "slur",          "technique",    "bend",
    "slide",       "vibrato",    "articulation", "measureNumber", "credit",       "trackName",
    "repeat",      "volta",      "tuplet",       "tempo",         "dynamic",      "navigation",
    "section",     "brace",      "arpeggio",     "hairpin",       "chord",        "beatText",
    "lyric",       "fermata",    "octaveLine",   "ornament",      "pedal",        "fingering",
    "parenthesis",
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

// The same in the bold face (Helvetica-Bold, Arimo Bold): wider lowercase and
// punctuation, the same digits.
double boldAdvance(char c) noexcept {
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
    case '-':
    case ':':
        return 0.333;
    case 'r':
        return 0.389;
    case 'z':
        return 0.5;
    case 'a':
    case 'c':
    case 'e':
    case 'k':
    case 's':
    case 'v':
    case 'x':
    case 'y':
    case 'J':
        return 0.556;
    case '<':
    case '>':
    case '+':
    case '=':
        return 0.584;
    case 'F':
    case 'L':
    case 'T':
    case 'Z':
        return 0.611;
    case 'E':
    case 'P':
    case 'S':
    case 'V':
    case 'X':
    case 'Y':
        return 0.667;
    case 'w':
    case 'G':
    case 'O':
    case 'Q':
        return 0.778;
    case 'M':
        return 0.833;
    case 'm':
        return 0.889;
    case 'W':
        return 0.944;
    default:
        return c >= 'A' && c <= 'Z' ? 0.722 : 0.611;
    }
}

constexpr std::array<std::string_view, 3> kFaceNames{"regular", "italic", "bold"};

} // namespace

std::string_view roleName(Role role) noexcept {
    return kRoleNames[static_cast<std::size_t>(role)];
}

std::string_view faceName(TextFace face) noexcept {
    return kFaceNames[static_cast<std::size_t>(face)];
}

double textWidth(std::string_view text, double size, TextFace face) noexcept {
    const bool bold = face == TextFace::Bold;
    double width = 0.0;
    for (const char c : text) {
        // A letter written with several UTF-8 bytes ("é", "ñ") counts once.
        if ((static_cast<unsigned char>(c) & 0xC0U) != 0x80U) {
            width += bold ? boldAdvance(c) : advance(c);
        }
    }
    return width * size;
}

double textWidth(const TextItem& text) noexcept {
    return textWidth(text.text, text.size, text.face);
}

} // namespace elara::notation
