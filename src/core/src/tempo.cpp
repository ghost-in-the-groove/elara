// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/tempo.hpp>

namespace elara::core {

Fraction swung(TripletFeel feel, const Fraction& offset, const Fraction& length) {
    Fraction pair;  // the length of a pair of the feel's note value
    Fraction first; // the part of a pair its first note takes
    switch (feel) {
    case TripletFeel::None:
        return offset;
    case TripletFeel::Eighth:
        pair = Fraction(1, 4);
        first = Fraction(2, 3);
        break;
    case TripletFeel::Sixteenth:
        pair = Fraction(1, 8);
        first = Fraction(2, 3);
        break;
    case TripletFeel::DottedEighth:
        pair = Fraction(1, 4);
        first = Fraction(3, 4);
        break;
    case TripletFeel::DottedSixteenth:
        pair = Fraction(1, 8);
        first = Fraction(3, 4);
        break;
    case TripletFeel::ScottishEighth:
        pair = Fraction(1, 4);
        first = Fraction(1, 4);
        break;
    case TripletFeel::ScottishSixteenth:
        pair = Fraction(1, 8);
        first = Fraction(1, 4);
        break;
    }
    if (offset < Fraction() || offset >= length) {
        return offset;
    }
    const Fraction pairs = offset / pair;
    const Fraction start = Fraction(pairs.numerator() / pairs.denominator()) * pair;
    if (start + pair > length) {
        return offset; // the end of the measure cuts the pair: straight
    }
    const Fraction into = offset - start;
    const Fraction half = pair / Fraction(2);
    if (into <= half) {
        return start + into * first * Fraction(2);
    }
    return start + first * pair + (into - half) * (Fraction(1) - first) * Fraction(2);
}

} // namespace elara::core
