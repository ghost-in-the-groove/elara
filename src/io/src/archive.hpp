// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal helpers shared by the readers and writers: zip archives and files.
#pragma once

#include <elara/io/result.hpp>

#include <miniz.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace elara::io::detail {

/// Largest entry read from an archive (zip-bomb guard).
inline constexpr std::size_t kMaxEntryBytes = std::size_t{512} * 1024 * 1024;

/// True when `bytes` start with a zip local file header ("PK\3\4").
[[nodiscard]] bool looksLikeZip(std::span<const std::uint8_t> bytes) noexcept;

/// A zip archive read from memory; the bytes must outlive the reader.
class ZipReader {
public:
    explicit ZipReader(std::span<const std::uint8_t> bytes);
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;
    ZipReader(ZipReader&&) = delete;
    ZipReader& operator=(ZipReader&&) = delete;
    ~ZipReader();

    [[nodiscard]] bool ok() const noexcept { return ok_; }

    /// The entry's content; nullopt when it is missing. Throws FormatError when
    /// it is damaged or larger than `limit`.
    [[nodiscard]] std::optional<std::string> entry(const std::string& name,
                                                   std::size_t limit = kMaxEntryBytes);

    /// Names of the entries, in archive order.
    [[nodiscard]] std::vector<std::string> names();

private:
    mz_zip_archive zip_{};
    bool ok_ = false;
};

/// One file of an archive being written.
struct ZipEntry {
    std::string_view name{};
    std::string_view content{};
    /// Deflate level: 0 stores the entry, 1 is the fastest, 6 the default.
    int level = 6;
};

/// A zip archive holding `entries`, in that order. Throws std::runtime_error.
[[nodiscard]] std::vector<std::uint8_t> writeZip(std::span<const ZipEntry> entries);

/// Writes `content` to a temporary file next to `path`, then renames it over
/// `path`, so a failure never leaves a half-written file. Throws
/// std::runtime_error with the path.
void writeFileAtomically(const std::filesystem::path& path, std::string_view content);

/// The whole file. Errors: "cannot open the file", "cannot read the file".
[[nodiscard]] std::expected<std::vector<std::uint8_t>, FormatError>
readFile(const std::filesystem::path& path);

/// The path as UTF-8 text, for messages.
[[nodiscard]] std::string displayPath(const std::filesystem::path& path);

} // namespace elara::io::detail
