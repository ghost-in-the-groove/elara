// SPDX-License-Identifier: AGPL-3.0-or-later
#include "common.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <elara/io/guitar_pro.hpp>
#include <elara/io/midi.hpp>
#include <elara/io/musicxml.hpp>
#include <elara/io/native_format.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <iostream>
#include <system_error>

namespace elara::cli {

std::string jsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned>(c));
            } else {
                out += c;
            }
        }
    }
    return out;
}

std::string jsonNumber(double value) {
    const double rounded = std::round(value * 10'000.0) / 10'000.0;
    return std::format("{}", rounded == 0.0 ? 0.0 : rounded);
}

std::filesystem::path executableDirectory() {
#if defined(_WIN32)
    std::wstring buffer(260, L'\0');
    for (;;) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return std::filesystem::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    buffer.resize(std::char_traits<char>::length(buffer.c_str()));
    std::error_code error;
    const std::filesystem::path path = std::filesystem::weakly_canonical(buffer, error);
    return error ? std::filesystem::path{} : path.parent_path();
#else
    std::error_code error;
    const std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : path.parent_path();
#endif
}

std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    std::string out;
    out.reserve(text.size());
    for (const char8_t c : text) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::filesystem::path defaultFontMetadata() {
    return executableDirectory() / "fonts" / "Bravura.json";
}

std::expected<core::Song, std::string> openSong(const std::filesystem::path& path) {
    std::string extension = utf8(path.extension());
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (extension == ".elara") {
        auto song = io::loadNative(path);
        if (!song) {
            return std::unexpected(song.error().message);
        }
        return std::move(*song);
    }
    std::expected<io::ImportResult, io::FormatError> imported =
        std::unexpected(io::FormatError{.message = "unsupported file type"});
    if (extension == ".musicxml" || extension == ".xml" || extension == ".mxl") {
        imported = io::importMusicXmlFile(path);
    } else if (extension == ".mid" || extension == ".midi" || extension == ".smf") {
        imported = io::importMidiFile(path);
    } else if (extension == ".gp" || extension == ".gpx" || extension == ".gp5" ||
               extension == ".gp4" || extension == ".gp3") {
        imported = io::importGuitarProFile(path);
    }
    if (!imported) {
        return std::unexpected(imported.error().message);
    }
    return std::move(imported->song);
}

int fail(std::string_view message) {
    std::cerr << "error: " << message << '\n';
    return kExitError;
}

int usageError(std::string_view message) {
    std::cerr << "error: " << message << '\n';
    return kExitUsage;
}

} // namespace elara::cli
