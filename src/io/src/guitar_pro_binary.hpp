// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal: the Guitar Pro 3, 4 and 5 reader (ADR-0010).
#pragma once

#include <elara/io/result.hpp>

#include <cstdint>
#include <span>

namespace elara::io::detail {

/// Reads a Guitar Pro 3, 4 or 5 file: bytes that start with the version string
/// ("FICHIER GUITAR PRO v5.10"). Throws FormatError for other versions and
/// damaged files.
[[nodiscard]] ImportResult readGuitarProBinary(std::span<const std::uint8_t> bytes);

} // namespace elara::io::detail
