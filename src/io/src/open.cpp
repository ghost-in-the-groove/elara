// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/io/guitar_pro.hpp>
#include <elara/io/midi.hpp>
#include <elara/io/musicxml.hpp>
#include <elara/io/native_format.hpp>
#include <elara/io/open.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>

namespace elara::io {

namespace {

constexpr std::array<std::string_view, 13> kExtensions{
    ".elara", ".musicxml", ".xml", ".mxl", ".mid", ".midi", ".smf",
    ".kar",   ".gp",       ".gpx", ".gp5", ".gp4", ".gp3",
};

bool among(std::string_view extension, std::initializer_list<std::string_view> names) {
    return std::ranges::find(names, extension) != names.end();
}

} // namespace

std::expected<ImportResult, FormatError> openFile(const std::filesystem::path& path) {
    const std::u8string raw = path.extension().u8string();
    std::string extension(raw.begin(), raw.end());
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".elara") {
        auto song = loadNative(path);
        if (!song) {
            return std::unexpected(std::move(song.error()));
        }
        return ImportResult{.song = std::move(*song), .warnings = {}};
    }
    if (among(extension, {".musicxml", ".xml", ".mxl"})) {
        return importMusicXmlFile(path);
    }
    if (among(extension, {".mid", ".midi", ".smf", ".kar"})) {
        return importMidiFile(path);
    }
    if (among(extension, {".gp", ".gpx", ".gp5", ".gp4", ".gp3"})) {
        return importGuitarProFile(path);
    }
    return std::unexpected(FormatError{.message = "unsupported file type"});
}

std::span<const std::string_view> openExtensions() noexcept {
    return kExtensions;
}

} // namespace elara::io
