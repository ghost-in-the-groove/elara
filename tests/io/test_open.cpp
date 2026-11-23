// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/song.hpp>
#include <elara/io/native_format.hpp>
#include <elara/io/open.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

const std::filesystem::path kCorpus = ELARA_CORPUS_DIR;

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
