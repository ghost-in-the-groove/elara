// SPDX-License-Identifier: AGPL-3.0-or-later
// Data compressed into the zlib format (RFC 1950), as PDF's FlateDecode filter
// reads it (ADR-0074).
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace elara::io {

/// `data` compressed into a zlib stream.
[[nodiscard]] std::string zlibCompress(std::string_view data);

/// The data of the zlib stream `stream`; nothing when it is not a complete,
/// valid stream or holds more than `limit` bytes.
[[nodiscard]] std::optional<std::string> zlibDecompress(std::string_view stream, std::size_t limit);

} // namespace elara::io
