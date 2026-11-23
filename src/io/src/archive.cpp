// SPDX-License-Identifier: AGPL-3.0-or-later
#include "archive.hpp"

#include <format>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <system_error>

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

std::vector<std::uint8_t> writeZip(std::span<const ZipEntry> entries) {
    std::size_t total = 0;
    for (const ZipEntry& entry : entries) {
        total += entry.content.size();
    }
    mz_zip_archive zip{};
    if (mz_zip_writer_init_heap(&zip, 0, total / 4 + 4096) == 0) {
        throw std::runtime_error("cannot create the zip archive");
    }
    bool written = true;
    for (const ZipEntry& entry : entries) {
        const std::string name(entry.name);
        const auto level = static_cast<mz_uint>(entry.level);
        written = written && mz_zip_writer_add_mem(&zip, name.c_str(), entry.content.data(),
                                                   entry.content.size(), level) != 0;
    }
    void* buffer = nullptr;
    std::size_t size = 0;
    written = written && mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size) != 0;
    std::vector<std::uint8_t> bytes;
    if (written) {
        const auto* begin = static_cast<const std::uint8_t*>(buffer);
        bytes.assign(begin, begin + size);
    }
    mz_free(buffer);
    mz_zip_writer_end(&zip);
    if (!written) {
        throw std::runtime_error("cannot write the zip archive");
    }
    return bytes;
}

void writeFileAtomically(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::path temporary = path;
    temporary += ".saving";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error(std::format("cannot write {}", displayPath(path)));
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.close();
        if (!out) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw std::runtime_error(std::format("cannot write {}", displayPath(path)));
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw std::runtime_error(
            std::format("cannot replace {}: {}", displayPath(path), error.message()));
    }
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
