// SPDX-License-Identifier: AGPL-3.0-or-later
// How subcommands describe a song: JSON fragments and a short text summary.
#pragma once

#include <elara/core/song.hpp>
#include <elara/core/validation.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace elara::cli {

/// `[{"name":"Lead","mix":{...},"tuning":"E2 A2 D3 G3 B3 E4","capo":0,"staves":1,
/// "beats":8,"notes":16}, ...]`: the tuning and capo of the first staff (tuning
/// null without strings).
[[nodiscard]] std::string tracksJson(const core::Song& song);

/// A navigation mark of a measure (ADR-0049) as files and steps name it.
struct SignName {
    std::string_view name;
    bool core::MeasureHeader::* flag;
};
inline constexpr std::array kSignNames{
    SignName{"segno", &core::MeasureHeader::segno},
    SignName{"segno-segno", &core::MeasureHeader::segnoSegno},
    SignName{"coda", &core::MeasureHeader::coda},
    SignName{"double-coda", &core::MeasureHeader::doubleCoda},
    SignName{"fine", &core::MeasureHeader::fine},
    SignName{"to-coda", &core::MeasureHeader::toCoda},
    SignName{"to-double-coda", &core::MeasureHeader::toDoubleCoda},
};
/// Names of core::JumpTo and core::JumpThen, by value.
inline constexpr std::array kJumpToNames =
    std::to_array<std::string_view>({"none", "da-capo", "dal-segno", "dal-segno-segno"});
inline constexpr std::array kJumpThenNames =
    std::to_array<std::string_view>({"end", "al-fine", "al-coda", "al-double-coda"});
/// Names of core::TripletFeel, by value (ADR-0104).
inline constexpr std::array kTripletFeelNames =
    std::to_array<std::string_view>({"none", "eighth", "sixteenth", "dotted-eighth",
                                     "dotted-sixteenth", "scottish-eighth", "scottish-sixteenth"});

/// `[{"time":"4/4","key":"C major"}, ...]`, one entry per measure, with
/// `"signs":["segno","to-coda"]`, `"jump":"dal-segno"`, `"jumpThen":"al-coda"`
/// and `"tripletFeel":"eighth"` where the measure has them.
[[nodiscard]] std::string measuresJson(const core::Song& song);

/// `[{"kind":"voice-incomplete","track":0,...,"beat":null,"note":null}, ...]`
[[nodiscard]] std::string issuesJson(const std::vector<core::Issue>& issues);

/// Title, tracks, length and validation issues, a few lines of text.
void printSongText(const core::Song& song);

/// The path as UTF-8 text for messages and JSON.
[[nodiscard]] std::string displayPath(const std::filesystem::path& path);

} // namespace elara::cli
