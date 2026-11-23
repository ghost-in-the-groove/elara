// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal: the Guitar Pro 6 container (ADR-0010).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace elara::io::detail {

/// The content of the file `name` in a Guitar Pro 6 container: "BCFS" (a file
/// system of 4096-byte sectors) or "BCFZ" (the same, compressed). std::nullopt
/// when there is no such file. Throws FormatError when the container is
/// damaged.
[[nodiscard]] std::optional<std::string> gpxEntry(std::span<const std::uint8_t> bytes,
                                                  std::string_view name);

} // namespace elara::io::detail
