// SPDX-License-Identifier: AGPL-3.0-or-later
// How subcommands describe a song: JSON fragments and a short text summary.
#pragma once

#include <elara/core/song.hpp>
#include <elara/core/validation.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace elara::cli {

/// `[{"name":"Lead","mix":{...},"tuning":"E2 A2 D3 G3 B3 E4","capo":0,"staves":1,
/// "beats":8,"notes":16}, ...]`: the tuning and capo of the first staff (tuning
/// null without strings).
[[nodiscard]] std::string tracksJson(const core::Song& song);

/// `[{"time":"4/4","key":"C major"}, ...]`, one entry per measure.
[[nodiscard]] std::string measuresJson(const core::Song& song);

/// `[{"kind":"voice-incomplete","track":0,...,"beat":null,"note":null}, ...]`
[[nodiscard]] std::string issuesJson(const std::vector<core::Issue>& issues);

/// Title, tracks, length and validation issues, a few lines of text.
void printSongText(const core::Song& song);

/// The path as UTF-8 text for messages and JSON.
[[nodiscard]] std::string displayPath(const std::filesystem::path& path);

} // namespace elara::cli
