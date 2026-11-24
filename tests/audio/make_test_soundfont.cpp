// SPDX-License-Identifier: AGPL-3.0-or-later
// Writes the test SoundFont (test_soundfont.hpp) to the path given, for the
// elara-cli tests that play a SoundFont (ADR-0043).
#include "test_soundfont.hpp"

#include <cstdio>
#include <filesystem>
#include <span>

int main(int argc, char** argv) {
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    if (arguments.size() != 2) {
        std::fputs("usage: elara_make_test_soundfont <out.sf2>\n", stderr);
        return 2;
    }
    const auto presets = elara::audio::test::defaultTestPresets();
    elara::audio::test::writeTestSoundFont(arguments[1], presets);
    return std::filesystem::is_regular_file(arguments[1]) ? 0 : 1;
}
