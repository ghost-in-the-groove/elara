// SPDX-License-Identifier: AGPL-3.0-or-later
#include "archive.hpp"

#include <format>
#include <fstream>
#include <ios>

namespace elara::io::detail {

bool looksLikeZip(std::span<const std::uint8_t> bytes) noexcept {
    return bytes.size() >= 4 && bytes[0] == 'P' && bytes[1] == 'K' && bytes[2] == 3 &&
           bytes[3] == 4;
}

ZipReader::ZipReader(std::span<const std::uint8_t> bytes) {
    ok_ = mz_zip_reader_init_mem(&zip_, bytes.data(), bytes.size(), 0) != 0;
}

ZipReader::~ZipReader() {
    if (ok_) {
        mz_zip_reader_end(&zip_);
    }
}

std::optional<std::string> ZipReader::entry(const std::string& name, std::size_t limit) {
    const int index = mz_zip_reader_locate_file(&zip_, name.c_str(), nullptr, 0);
    if (index < 0) {
        return std::nullopt;
    }
    const auto file = static_cast<mz_uint>(index);
    mz_zip_archive_file_stat stat{};
    if (mz_zip_reader_file_stat(&zip_, file, &stat) == 0) {
        throw FormatError{std::format("{} is damaged", name)};
    }
    if (stat.m_uncomp_size > limit) {
        throw FormatError{std::format("{} is too large ({} bytes)", name, stat.m_uncomp_size)};
    }
    std::string content(static_cast<std::size_t>(stat.m_uncomp_size), '\0');
    if (mz_zip_reader_extract_to_mem(&zip_, file, content.data(), content.size(), 0) == 0) {
        throw FormatError{std::format("{} is damaged", name)};
    }
    return content;
}

std::vector<std::string> ZipReader::names() {
    std::vector<std::string> result;
    const mz_uint count = mz_zip_reader_get_num_files(&zip_);
    result.reserve(count);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (mz_zip_reader_file_stat(&zip_, i, &stat) != 0) {
            result.emplace_back(stat.m_filename);
        }
    }
    return result;
}

std::expected<std::vector<std::uint8_t>, FormatError> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return std::unexpected(FormatError{"cannot open the file"});
    }
    const std::streamoff size = in.tellg();
    if (size < 0) {
        return std::unexpected(FormatError{"cannot read the file"});
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in) {
        return std::unexpected(FormatError{"cannot read the file"});
    }
    return bytes;
}

std::string displayPath(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

} // namespace elara::io::detail
