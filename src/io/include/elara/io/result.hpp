// SPDX-License-Identifier: AGPL-3.0-or-later
// Results shared by every reader in elara_io.
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
    /// One line per dropped feature, e.g. "Piano: grace notes dropped (measure 2)".
    std::vector<std::string> warnings{};
};

} // namespace elara::io
