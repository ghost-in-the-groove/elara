// SPDX-License-Identifier: AGPL-3.0-or-later
// Guitar Pro's signs and jumps ("directions") and what they set in a measure
// header (ADR-0049).
#pragma once

#include <elara/core/song.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace elara::io::detail {

/// Guitar Pro's directions, in the order Guitar Pro 5 files list the measure
/// of each: five signs, twelve jumps, then "Da Coda" and "Da Double Coda",
/// which Guitar Pro names what scores write as "To Coda".
enum class GpDirection : std::uint8_t {
    Coda,
    DoubleCoda,
    Segno,
    SegnoSegno,
    Fine,
    DaCapo,
    DaCapoAlCoda,
    DaCapoAlDoubleCoda,
    DaCapoAlFine,
    DaSegno,
    DaSegnoAlCoda,
    DaSegnoAlDoubleCoda,
    DaSegnoAlFine,
    DaSegnoSegno,
    DaSegnoSegnoAlCoda,
    DaSegnoSegnoAlDoubleCoda,
    DaSegnoSegnoAlFine,
    DaCoda,
    DaDoubleCoda,
};
inline constexpr std::size_t kGpDirections = 19;
inline constexpr std::size_t kGpSigns = 5; ///< the first five: <Target> in GPIF

/// The names GPIF writes in <Directions>: a <Target> for a sign, a <Jump> for
/// the others.
inline constexpr std::array<std::string_view, kGpDirections> kGpDirectionNames{
    "Coda",
    "DoubleCoda",
    "Segno",
    "SegnoSegno",
    "Fine",
    "DaCapo",
    "DaCapoAlCoda",
    "DaCapoAlDoubleCoda",
    "DaCapoAlFine",
    "DaSegno",
    "DaSegnoAlCoda",
    "DaSegnoAlDoubleCoda",
    "DaSegnoAlFine",
    "DaSegnoSegno",
    "DaSegnoSegnoAlCoda",
    "DaSegnoSegnoAlDoubleCoda",
    "DaSegnoSegnoAlFine",
    "DaCoda",
    "DaDoubleCoda"};

/// Sets `direction` in `header`. False when it replaced another jump of the
/// header: a measure has one.
inline bool applyDirection(core::MeasureHeader& header, GpDirection direction) {
    using core::JumpThen;
    using core::JumpTo;
    switch (direction) {
    case GpDirection::Coda:
        header.coda = true;
        return true;
    case GpDirection::DoubleCoda:
        header.doubleCoda = true;
        return true;
    case GpDirection::Segno:
        header.segno = true;
        return true;
    case GpDirection::SegnoSegno:
        header.segnoSegno = true;
        return true;
    case GpDirection::Fine:
        header.fine = true;
        return true;
    case GpDirection::DaCoda:
        header.toCoda = true;
        return true;
    case GpDirection::DaDoubleCoda:
        header.toDoubleCoda = true;
        return true;
    default:
        break;
    }
    // The twelve jumps: three targets, each to the end, al Coda, al Double
    // Coda and al Fine.
    constexpr std::array kTo{JumpTo::Start, JumpTo::Segno, JumpTo::SegnoSegno};
    constexpr std::array kThen{JumpThen::End, JumpThen::Coda, JumpThen::DoubleCoda, JumpThen::Fine};
    const auto index =
        static_cast<std::size_t>(direction) - static_cast<std::size_t>(GpDirection::DaCapo);
    const JumpTo to = kTo.at(index / kThen.size());
    const JumpThen then = kThen.at(index % kThen.size());
    const bool replaced =
        header.jump != JumpTo::None && (header.jump != to || header.jumpThen != then);
    header.jump = to;
    header.jumpThen = then;
    return !replaced;
}

} // namespace elara::io::detail
