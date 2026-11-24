// SPDX-License-Identifier: AGPL-3.0-or-later
// SoundFonts as sound banks, played through FluidSynth (ADR-0043).
#include <elara/audio/offline_renderer.hpp>
#include <elara/audio/sequence.hpp>
#include <elara/audio/sound_font.hpp>
#include <elara/audio/tone_generator.hpp>
#include <elara/core/song.hpp>

#include "pitch_detection.hpp"
#include "test_soundfont.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <system_error>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;
using elara::audio::OfflineRenderer;
using elara::audio::peakLevel;
using elara::audio::Sequence;
using elara::audio::SoundFont;
using elara::audio::SoundFontPreset;
using elara::audio::Touch;
using elara::audio::TrackSound;
using elara::audio::test::defaultTestPresets;
using elara::audio::test::keyFrequency;
using elara::audio::test::pitchOf;
using elara::audio::test::writeTestSoundFont;
using elara::core::Beat;
using elara::core::Duration;
using elara::core::Instrument;
using elara::core::InstrumentKind;
using elara::core::Note;
using elara::core::NoteValue;
using elara::core::Pitch;
using elara::core::Song;
using elara::core::Staff;
using elara::core::Track;

namespace {

constexpr double kRate = 48000.0;

// A file in the temporary directory, named for this process so tests running
// in parallel do not share it, and removed at the end.
class TemporaryFile {
public:
    explicit TemporaryFile(const std::string& extension)
        : path_(std::filesystem::temp_directory_path() /
                ("elara-test-" + std::to_string(std::random_device{}()) + extension)) {}
    ~TemporaryFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    TemporaryFile(TemporaryFile&&) = delete;
    TemporaryFile& operator=(TemporaryFile&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

const std::filesystem::path& testFontPath() {
    static const TemporaryFile file(".sf2");
    static const bool written = [] {
        writeTestSoundFont(file.path(), defaultTestPresets());
        return true;
    }();
    static_cast<void>(written);
    return file.path();
}

std::shared_ptr<const SoundFont> testFont() {
    auto opened = SoundFont::open(testFontPath());
    if (!opened) {
        FAIL(opened.error());
    }
    return *opened;
}

Instrument pitched(int program) {
    return {.name = "Part", .midiProgram = program};
}

const Instrument kGuitar = pitched(25);
const Instrument kDrums{.name = "Drums", .kind = InstrumentKind::Percussion};

std::size_t frames(double seconds) {
    return static_cast<std::size_t>(std::llround(seconds * kRate));
}

// Renders `seconds` of `sound` into a new buffer.
std::vector<float> render(TrackSound& sound, double seconds) {
    std::vector<float> out(frames(seconds), 0.0f);
    sound.render(out);
    return out;
}

// The pitch of the last `seconds` of `out`.
double pitchAtEnd(const std::vector<float>& out, double seconds) {
    return pitchOf(std::span(out).last(frames(seconds)), kRate);
}

// The pitch of `key` struck with `touch` on a track of `instrument`.
double pitchOfTouch(const Instrument& instrument, int key, Touch touch) {
    const auto sound = testFont()->sound(instrument, kRate);
    sound->noteOn(key, 100, touch);
    return pitchAtEnd(render(*sound, 0.3), 0.2);
}

} // namespace

TEST_CASE("SoundFont: opening reads its presets", "[audio][soundfont]") {
    const auto font = testFont();
    CHECK(font->path() == testFontPath());
    const std::vector<SoundFontPreset>& presets = font->presets();
    REQUIRE(presets.size() == defaultTestPresets().size());
    // By bank, then program.
    CHECK(presets.front() == SoundFontPreset{.bank = 0, .program = 0, .name = "Test Piano"});
    CHECK(presets[1] == SoundFontPreset{.bank = 0, .program = 25, .name = "Test Guitar"});
    CHECK(presets.back() == SoundFontPreset{.bank = 128, .program = 0, .name = "Test Kit"});
}

TEST_CASE("SoundFont: a file that is no SoundFont does not open", "[audio][soundfont]") {
    SECTION("a missing file") {
        const auto opened = SoundFont::open("no-such-font.sf2");
        REQUIRE_FALSE(opened.has_value());
        CHECK_THAT(opened.error(), ContainsSubstring("no-such-font.sf2"));
    }
    SECTION("a text file") {
        const TemporaryFile text(".sf2");
        std::ofstream(text.path()) << "not a SoundFont\n";
        const auto opened = SoundFont::open(text.path());
        REQUIRE_FALSE(opened.has_value());
        CHECK_THAT(opened.error(), ContainsSubstring("SoundFont"));
    }
}

TEST_CASE("SoundFont: each instrument plays its General MIDI preset", "[audio][soundfont]") {
    const auto font = testFont();
    CHECK(font->presetFor(kGuitar).name == "Test Guitar");
    CHECK(font->presetFor(pitched(33)).name == "Test Bass");
    CHECK(font->presetFor(kDrums).name == "Test Kit");
    CHECK(font->soundName(kGuitar) == "Test Guitar");
    SECTION("a missing program: the first of its family, else the first program") {
        CHECK(font->presetFor(pitched(27)).name == "Test Guitar"); // guitars are 24-31
        CHECK(font->presetFor(pitched(40)).name == "Test Piano");  // no strings at all
    }
}

TEST_CASE("SoundFont: a note sounds at its pitch until released", "[audio][soundfont]") {
    const auto sound = testFont()->sound(kGuitar, kRate);
    sound->noteOn(57, 100, Touch::Normal);
    const std::vector<float> held = render(*sound, 0.3);
    CHECK_THAT(pitchAtEnd(held, 0.2), WithinRel(keyFrequency(57), 0.005));
    CHECK(peakLevel(held) > 0.05f);
    CHECK(sound->activeVoices() == 1);

    sound->noteOff(57);
    const std::vector<float> released = render(*sound, 0.5);
    CHECK(peakLevel(std::span(released).last(frames(0.1))) < 1e-4f);
    CHECK(sound->activeVoices() == 0);
}

TEST_CASE("SoundFont: touches play the guitar's muted and harmonics presets",
          "[audio][soundfont]") {
    // The muted preset sounds an octave higher and the harmonics preset 19
    // semitones higher than the guitar's, so the pitch tells which one played.
    CHECK_THAT(pitchOfTouch(kGuitar, 57, Touch::PalmMute), WithinRel(keyFrequency(69), 0.005));
    CHECK_THAT(pitchOfTouch(kGuitar, 57, Touch::Dead), WithinRel(keyFrequency(69), 0.005));
    CHECK_THAT(pitchOfTouch(kGuitar, 57, Touch::Harmonic), WithinRel(keyFrequency(76), 0.005));
    // A bass has no muted program in General MIDI: its own preset.
    CHECK_THAT(pitchOfTouch(pitched(33), 57, Touch::PalmMute), WithinRel(keyFrequency(57), 0.005));
}

TEST_CASE("SoundFont: glides move the notes of their key", "[audio][soundfont]") {
    const auto sound = testFont()->sound(kGuitar, kRate);
    sound->noteOn(57, 100, Touch::Normal);
    static_cast<void>(render(*sound, 0.05));
    SECTION("at once") {
        sound->glide(57, 200.0, 0.0);
        CHECK_THAT(pitchAtEnd(render(*sound, 0.25), 0.2),
                   WithinRel(keyFrequency(57, 200.0), 0.005));
    }
    SECTION("over time, then held") {
        sound->glide(57, 1200.0, 0.4);
        const double halfway = pitchAtEnd(render(*sound, 0.2), 0.05);
        CHECK(halfway > keyFrequency(57, 400.0));
        CHECK(halfway < keyFrequency(57, 800.0));
        CHECK_THAT(pitchAtEnd(render(*sound, 0.5), 0.2), WithinRel(keyFrequency(69), 0.005));
    }
    SECTION("another key's glide leaves it") {
        sound->glide(60, 300.0, 0.0);
        CHECK_THAT(pitchAtEnd(render(*sound, 0.25), 0.2), WithinRel(keyFrequency(57), 0.005));
    }
    SECTION("a new note starts at its key") {
        sound->glide(57, 500.0, 0.0);
        sound->noteOff(57);
        sound->noteOn(57, 100, Touch::Normal);
        CHECK_THAT(pitchAtEnd(render(*sound, 0.3), 0.1), WithinRel(keyFrequency(57), 0.005));
    }
}

TEST_CASE("SoundFont: the output does not depend on how render() splits it", "[audio][soundfont]") {
    const auto font = testFont();
    const auto whole = font->sound(kGuitar, kRate);
    const auto pieces = font->sound(kGuitar, kRate);
    std::vector<float> a(9000, 0.0f);
    std::vector<float> b(9000, 0.0f);
    for (TrackSound* sound : {whole.get(), pieces.get()}) {
        sound->noteOn(57, 90, Touch::Normal);
    }
    whole->render(std::span(a).first(3001));
    const std::vector<std::size_t> cuts{0, 1, 38, 102, 2000, 3001};
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
        pieces->render(std::span(b).subspan(cuts[i], cuts[i + 1] - cuts[i]));
    }
    for (TrackSound* sound : {whole.get(), pieces.get()}) {
        sound->glide(57, 300.0, 0.05);
        sound->noteOn(64, 70, Touch::PalmMute);
    }
    whole->render(std::span(a).subspan(3001));
    for (std::size_t at = 3001; at < b.size(); at += 777) {
        pieces->render(std::span(b).subspan(at, std::min<std::size_t>(777, b.size() - at)));
    }
    CHECK(a == b);
}

TEST_CASE("SoundFont: drums play the kit and ring on after a note off", "[audio][soundfont]") {
    const auto sound = testFont()->sound(kDrums, kRate);
    sound->noteOn(38, 100, Touch::Normal);
    static_cast<void>(render(*sound, 0.05));
    sound->noteOff(38);
    CHECK(peakLevel(render(*sound, 0.3)) > 0.05f);
    sound->reset();
    // Silent from the next of FluidSynth's 64-frame blocks: the one under way
    // was rendered before the reset.
    const std::vector<float> after = render(*sound, 0.05);
    CHECK(peakLevel(std::span(after).subspan(64)) == 0.0f);
}

TEST_CASE("Offline rendering: a song through a SoundFont", "[audio][soundfont]") {
    Song song;
    song.addTrack(Track("Guitar", kGuitar, {Staff{}}));
    song.appendMeasures(1);
    song.track(0).staff(0).measure(0).voices[0].beats = {Beat{
        .duration = Duration(NoteValue::Whole), .notes = {Note{.pitch = Pitch::fromMidi(57)}}}};
    const Sequence sequence(song);
    OfflineRenderer renderer(song, sequence,
                             {.sampleRate = kRate, .tail = 0.5, .bank = testFont()});
    CHECK(renderer.sounds() == std::vector<std::string>{"Test Guitar"});
    const std::vector<float> stereo = renderer.renderRest();
    std::vector<float> left;
    for (std::size_t i = frames(0.5); i < frames(1.0); ++i) {
        left.push_back(stereo[2 * i]);
    }
    CHECK_THAT(pitchOf(left, kRate), WithinRel(keyFrequency(57), 0.005));
}
