// SPDX-License-Identifier: AGPL-3.0-or-later
// Results shared by the readers and writers in elara_io.
#pragma once

#include <elara/core/song.hpp>

#include <string>
#include <vector>

namespace elara::io {

/// Why a file could not be read. The message says where the problem is when
/// the format allows it (a JSON path, a byte offset).
struct FormatError {
    std::string message;
};

/// A song read from a foreign format, with what could not be represented.
struct ImportResult {
    core::Song song{};
    /// One line per dropped feature, e.g. "Piano: lyrics dropped (measure 2)".
    std::vector<std::string> warnings{};
};

/// A file written from a song in a foreign format, with what that format could
/// not hold.
struct ExportResult {
    /// The file's content (text or binary).
    std::string data{};
    /// One line per changed or dropped feature, e.g. "Guitar: let ring dropped (measure 3)".
    std::vector<std::string> warnings{};
};

} // namespace elara::io
