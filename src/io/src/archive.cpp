// SPDX-License-Identifier: AGPL-3.0-or-later
#include "archive.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <format>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace elara::io::detail {

namespace {

// Writes `pieces`, one after the other, to `path`, replacing it, and returns
// once the system has put the data on the disk: renaming a file whose data
// still sits in a cache can, after a power cut, leave the new name over an
// empty or partial file on some file systems. False on any failure.
bool writeToDisk(const std::filesystem::path& path, std::span<const std::string_view> pieces) {
#if defined(_WIN32)
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = true;
    for (const std::string_view content : pieces) {
        for (std::size_t done = 0; ok && done < content.size();) {
            const auto chunk =
                static_cast<DWORD>(std::min<std::size_t>(content.size() - done, 1U << 30U));
            DWORD written = 0;
            ok = WriteFile(file, content.data() + done, chunk, &written, nullptr) != 0 &&
                 written > 0;
            done += written;
        }
    }
    ok = ok && FlushFileBuffers(file) != 0;
    return CloseHandle(file) != 0 && ok;
#else
    const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (file < 0) {
        return false;
    }
    bool ok = true;
    for (const std::string_view content : pieces) {
        for (std::size_t done = 0; ok && done < content.size();) {
            const ssize_t written = ::write(file, content.data() + done, content.size() - done);
            if (written < 0 && errno == EINTR) {
                continue;
            }
            ok = written > 0;
            if (ok) {
                done += static_cast<std::size_t>(written);
            }
        }
    }
#if defined(__APPLE__)
    // fsync() leaves the data in the drive's cache on macOS; F_FULLFSYNC
    // flushes it, where the file system supports it.
    ok = ok && (::fcntl(file, F_FULLFSYNC) == 0 || ::fsync(file) == 0);
#else
    ok = ok && ::fsync(file) == 0;
#endif
    return ::close(file) == 0 && ok;
#endif
}

// Renames `from` over `to`, and on POSIX syncs the folder so that the rename
// itself survives a power cut. Windows writes the move through to the disk.
std::error_code replaceOnDisk(const std::filesystem::path& from, const std::filesystem::path& to) {
#if defined(_WIN32)
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ==
        0) {
        return {static_cast<int>(GetLastError()), std::system_category()};
    }
    return {};
#else
    std::error_code error;
    std::filesystem::rename(from, to, error);
    if (error) {
        return error;
    }
    const std::filesystem::path folder =
        to.has_parent_path() ? to.parent_path() : std::filesystem::path(".");
    if (const int dir = ::open(folder.c_str(), O_RDONLY | O_CLOEXEC); dir >= 0) {
        static_cast<void>(::fsync(dir)); // not every file system syncs folders
        ::close(dir);
    }
    return {};
#endif
}

} // namespace

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

std::optional<std::string> ZipReader::entry(const std::string& name, std::size_t limit,
                                            std::size_t padding) {
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
    const auto size = static_cast<std::size_t>(stat.m_uncomp_size);
    std::string content(size + padding, '\0');
    if (mz_zip_reader_extract_to_mem(&zip_, file, content.data(), size, 0) == 0) {
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

namespace {

// miniz writes the archive through this, at any offset: it goes back to fill
// in an entry's header once the entry's sizes are known.
// Nothing is thrown through miniz: a failure to grow writes nothing, and
// miniz fails the entry.
template <class Bytes>
std::size_t writeAt(void* opaque, mz_uint64 offset, const void* data, std::size_t size) noexcept {
    Bytes& bytes = *static_cast<Bytes*>(opaque);
    const auto at = static_cast<std::size_t>(offset);
    try {
        if (bytes.size() < at + size) {
            bytes.resize(at + size);
        }
    } catch (...) {
        return 0;
    }
    std::memcpy(bytes.data() + at, data, size);
    return size;
}

// An entry's pieces read as one text, from any offset (miniz reads them in
// order, so the piece a read starts in is kept from one read to the next).
class PieceReader {
public:
    explicit PieceReader(std::span<const std::string_view> pieces) : pieces_(pieces) {}

    static std::size_t read(void* opaque, mz_uint64 offset, void* out, std::size_t size) {
        return static_cast<PieceReader*>(opaque)->readAt(static_cast<std::size_t>(offset),
                                                         static_cast<char*>(out), size);
    }

private:
    std::size_t readAt(std::size_t offset, char* out, std::size_t size) {
        if (offset < start_) {
            piece_ = 0;
            start_ = 0;
        }
        std::size_t done = 0;
        while (done < size && piece_ < pieces_.size()) {
            const std::string_view piece = pieces_[piece_];
            const std::size_t at = offset + done - start_;
            if (at >= piece.size()) {
                start_ += piece.size();
                ++piece_;
                continue;
            }
            const std::size_t count = std::min(size - done, piece.size() - at);
            std::memcpy(out + done, piece.data() + at, count);
            done += count;
        }
        return done;
    }

    std::span<const std::string_view> pieces_;
    std::size_t piece_ = 0; // the piece that starts at start_
    std::size_t start_ = 0;
};

} // namespace

template <class Bytes>
Bytes writeZip(std::span<const ZipEntry> entries) {
    std::size_t total = 0;
    for (const ZipEntry& entry : entries) {
        total += entry.content.size();
        for (const std::string_view piece : entry.pieces) {
            total += piece.size();
        }
    }
    Bytes bytes;
    bytes.reserve(total / 4 + 4096);
    mz_zip_archive zip{};
    zip.m_pWrite = &writeAt<Bytes>;
    zip.m_pIO_opaque = &bytes;
    if (mz_zip_writer_init(&zip, 0) == 0) {
        throw std::runtime_error("cannot create the zip archive");
    }
    // Every entry is read through the same callback, dated now as
    // mz_zip_writer_add_mem() dates its entries, with its sizes in its header
    // (no data descriptor after it).
    const MZ_TIME_T now = std::time(nullptr);
    bool written = true;
    for (const ZipEntry& entry : entries) {
        const std::string name(entry.name);
        const std::span<const std::string_view> pieces =
            entry.pieces.empty() ? std::span(&entry.content, 1) : entry.pieces;
        std::size_t size = 0;
        for (const std::string_view piece : pieces) {
            size += piece.size();
        }
        PieceReader reader(pieces);
        const auto flags = static_cast<mz_uint>(entry.level) | MZ_ZIP_FLAG_WRITE_HEADER_SET_SIZE;
        written = written && mz_zip_writer_add_read_buf_callback(
                                 &zip, name.c_str(), &PieceReader::read, &reader, size, &now,
                                 nullptr, 0, flags, nullptr, 0, nullptr, 0) != 0;
    }
    written = written && mz_zip_writer_finalize_archive(&zip) != 0;
    mz_zip_writer_end(&zip);
    if (!written) {
        throw std::runtime_error("cannot write the zip archive");
    }
    return bytes;
}

template std::vector<std::uint8_t> writeZip(std::span<const ZipEntry> entries);
template std::string writeZip(std::span<const ZipEntry> entries);

void writeFileAtomically(const std::filesystem::path& path, std::string_view content) {
    writeFileAtomically(path, std::span(&content, 1));
}

void writeFileAtomically(const std::filesystem::path& path,
                         std::span<const std::string_view> pieces) {
    std::filesystem::path temporary = path;
    temporary += ".saving";
    if (!writeToDisk(temporary, pieces)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw std::runtime_error(std::format("cannot write {}", displayPath(path)));
    }
    if (const std::error_code error = replaceOnDisk(temporary, path)) {
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
