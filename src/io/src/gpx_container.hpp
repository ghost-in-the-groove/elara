// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal: the Guitar Pro 6 container (ADR-0010).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace elara::io::detail {

/// A Guitar Pro 6 container: "BCFS" (a file system of 4096-byte sectors) or
/// "BCFZ" (the same, compressed). A compressed one is unpacked once, when it
/// is made, for every file read from it.
class GpxContainer {
public:
    /// Throws FormatError when `bytes` is not a container or does not unpack.
    /// A "BCFS" container reads `bytes`, which must outlive it.
    explicit GpxContainer(std::span<const std::uint8_t> bytes);

    /// The content of the file `name`; std::nullopt when there is no such
    /// file. Throws FormatError when the file system is damaged.
    [[nodiscard]] std::optional<std::string> entry(std::string_view name) const;

private:
    std::span<const std::uint8_t> plain_; ///< a "BCFS" container's bytes
    std::vector<std::uint8_t> unpacked_;  ///< a "BCFZ" container, unpacked
};

} // namespace elara::io::detail
