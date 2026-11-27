// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/song.hpp>
#include <elara/core/version.hpp>
#include <elara/io/native_format.hpp>
#include <elara/io/open.hpp>

#include <catch2/catch_test_macros.hpp>
#include <miniz.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>

namespace {

const std::filesystem::path kCorpus = ELARA_CORPUS_DIR;

// `song` in a native file as Elara 99.0.0 would save it.
void saveAsNewer(const elara::core::Song& song, const std::filesystem::path& path) {
    std::string json = elara::io::songToJson(song);
    const std::string mine = std::format(R"("generator":"Elara {}")", elara::core::version());
    const std::size_t at = json.find(mine);
    REQUIRE(at != std::string::npos);
    json.replace(at, mine.size(), R"("generator":"Elara 99.0.0")");
    mz_zip_archive zip{};
    REQUIRE(mz_zip_writer_init_heap(&zip, 0, 4096) != 0);
    const std::string_view mime = elara::io::kNativeMimeType;
    REQUIRE(mz_zip_writer_add_mem(&zip, "mimetype", mime.data(), mime.size(), 0) != 0);
    REQUIRE(mz_zip_writer_add_mem(&zip, "song.json", json.data(), json.size(),
                                  static_cast<mz_uint>(MZ_DEFAULT_LEVEL)) != 0);
    void* buffer = nullptr;
    std::size_t size = 0;
    REQUIRE(mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size) != 0);
    std::ofstream(path, std::ios::binary)
        .write(static_cast<const char*>(buffer), static_cast<std::streamsize>(size));
    mz_free(buffer);
    mz_zip_writer_end(&zip);
}

} // namespace

TEST_CASE("Open: every format by its extension", "[io]") {
    for (const char* file :
         {"gp/guitar-techniques.gp", "gp/duet.gpx", "gp/techniques.gp5", "gp/quartet.gp4",
          "gp/melody.gp3", "musicxml/melody.musicxml", "midi/drums.mid"}) {
        INFO(file);
        const auto opened = elara::io::openFile(kCorpus / file);
        REQUIRE(opened.has_value());
        CHECK(opened->song.trackCount() > 0);
        CHECK(opened->song.measureCount() > 0);
    }
}

TEST_CASE("Open: native files, and extensions in any case", "[io]") {
    const auto imported = elara::io::openFile(kCorpus / "gp/band.gp");
    REQUIRE(imported.has_value());
    const auto native = std::filesystem::temp_directory_path() / "elara-open-test.ELARA";
    elara::io::saveNative(imported->song, native);
    const auto opened = elara::io::openFile(native);
    REQUIRE(opened.has_value());
    CHECK(opened->song == imported->song);
    CHECK(opened->warnings.empty());
    std::filesystem::remove(native);
}

TEST_CASE("Open: a native file from a newer Elara says what saving here loses", "[io]") {
    const auto imported = elara::io::openFile(kCorpus / "gp/band.gp");
    REQUIRE(imported.has_value());
    const auto newer = std::filesystem::temp_directory_path() / "elara-open-test-newer.elara";
    saveAsNewer(imported->song, newer);
    const auto opened = elara::io::openFile(newer);
    REQUIRE(opened.has_value());
    CHECK(opened->song == imported->song);
    REQUIRE(opened->warnings.size() == 1);
    CHECK(opened->warnings[0].starts_with("saved by Elara 99.0.0, newer than this one"));
    std::filesystem::remove(newer);
}

TEST_CASE("Open: unsupported and broken files are errors with a reason", "[io]") {
    const auto text = std::filesystem::temp_directory_path() / "elara-open-test.txt";
    std::ofstream(text) << "not music";
    const auto unsupported = elara::io::openFile(text);
    REQUIRE_FALSE(unsupported.has_value());
    CHECK(unsupported.error().message.find("unsupported") != std::string::npos);
    std::filesystem::remove(text);

    const auto broken = std::filesystem::temp_directory_path() / "elara-open-test.gp5";
    std::ofstream(broken) << "not a guitar pro file";
    CHECK_FALSE(elara::io::openFile(broken).has_value());
    std::filesystem::remove(broken);

    CHECK_FALSE(elara::io::openFile(kCorpus / "gp/missing.gp").has_value());
}

TEST_CASE("Open: the extensions it reads", "[io]") {
    const auto extensions = elara::io::openExtensions();
    for (const std::string_view extension : {".elara", ".gp", ".gpx", ".gp5", ".gp4", ".gp3",
                                             ".musicxml", ".xml", ".mxl", ".mid", ".midi"}) {
        INFO(extension);
        CHECK(std::ranges::find(extensions, extension) != extensions.end());
    }
}
