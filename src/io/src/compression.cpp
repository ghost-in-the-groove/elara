// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/io/compression.hpp>

#include <miniz.h>

#include <array>

namespace elara::io {

std::string zlibCompress(std::string_view data) {
    mz_ulong size = mz_compressBound(static_cast<mz_ulong>(data.size()));
    std::string stream(size, '\0');
    const int status = mz_compress2(reinterpret_cast<unsigned char*>(stream.data()), &size,
                                    reinterpret_cast<const unsigned char*>(data.data()),
                                    static_cast<mz_ulong>(data.size()), MZ_DEFAULT_LEVEL);
    if (status != MZ_OK) {
        return {}; // only when out of memory
    }
    stream.resize(size);
    return stream;
}

std::optional<std::string> zlibDecompress(std::string_view stream, std::size_t limit) {
    mz_stream inflater{};
    if (mz_inflateInit(&inflater) != MZ_OK) {
        return std::nullopt;
    }
    inflater.next_in = reinterpret_cast<const unsigned char*>(stream.data());
    inflater.avail_in = static_cast<unsigned int>(stream.size());
    std::string data;
    std::array<unsigned char, 16384> chunk{};
    int status = MZ_OK;
    while (status == MZ_OK) {
        inflater.next_out = chunk.data();
        inflater.avail_out = static_cast<unsigned int>(chunk.size());
        status = mz_inflate(&inflater, MZ_NO_FLUSH);
        data.append(reinterpret_cast<const char*>(chunk.data()), chunk.size() - inflater.avail_out);
        if (data.size() > limit) {
            status = MZ_DATA_ERROR;
        }
        if (status == MZ_BUF_ERROR && inflater.avail_in == 0) {
            break; // the stream ends early
        }
    }
    mz_inflateEnd(&inflater);
    if (status != MZ_STREAM_END) {
        return std::nullopt;
    }
    return data;
}

} // namespace elara::io
