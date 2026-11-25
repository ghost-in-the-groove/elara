// SPDX-License-Identifier: AGPL-3.0-or-later
// SFZ instruments played by Elara's sampler (ADR-0066).
#include <elara/audio/sfz.hpp>
#include <elara/audio/sfz_bank.hpp>
#include <elara/audio/sound_bank.hpp>
#include <elara/audio/wav_writer.hpp>
#include <elara/core/instrument.hpp>

#include "pitch_detection.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using Catch::Matchers::WithinRel;
using elara::audio::parseSfz;
using elara::audio::SfzBank;
using elara::audio::SfzProgram;
using elara::audio::Touch;
using elara::audio::TrackSound;
using elara::audio::test::keyFrequency;
using elara::audio::test::pitchOf;
using elara::core::Instrument;
using elara::core::InstrumentKind;

namespace {

constexpr double kRate = 48000.0;

const Instrument kGuitar{.name = "Guitar", .kind = InstrumentKind::Fretted, .midiProgram = 25};
const Instrument kDrums{.name = "Drums", .kind = InstrumentKind::Percussion};

std::shared_ptr<const SfzBank> bankOf(std::string_view text,
                                      const std::filesystem::path& folder = {}) {
    auto program = parseSfz(text, [](std::string_view) -> std::expected<std::string, std::string> {
        return std::unexpected(std::string("no includes here"));
    });
    REQUIRE(program.has_value());
    auto bank = SfzBank::load(*std::move(program), folder, "test");
    REQUIRE(bank.has_value());
    return *std::move(bank);
}

std::vector<float> render(TrackSound& sound, double seconds) {
    std::vector<float> out(static_cast<std::size_t>(seconds * kRate), 0.0f);
    sound.render(out);
    return out;
}

double rms(std::span<const float> x) {
    double sum = 0.0;
    for (const float v : x) {
        sum += static_cast<double>(v) * static_cast<double>(v);
    }
    return x.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(x.size()));
}

// A folder of its own under the system's temporary folder, removed after.
struct TempFolder {
    std::filesystem::path path;
    explicit TempFolder(std::string_view name)
        : path(std::filesystem::temp_directory_path() / std::filesystem::path(std::string(name))) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
};

void writeSine(const std::filesystem::path& path, double frequency, double seconds,
               std::uint32_t rate) {
    std::vector<float> samples(static_cast<std::size_t>(seconds * rate));
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<float>(
            0.5 * std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(i) / rate));
    }
    REQUIRE(elara::audio::writeWavPcm16(path, samples, {.sampleRate = rate, .channels = 1}));
}

void writeText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

} // namespace

TEST_CASE("SFZ sampler: a region sounds at the pitch of the key", "[audio][sfz]") {
    const auto bank = bankOf("<region> sample=*sine pitch_keycenter=69\n");
    const auto sound = bank->sound(kGuitar, kRate);
    sound->noteOn(69, 100, Touch::Normal);
    CHECK_THAT(pitchOf(render(*sound, 0.2), kRate), WithinRel(440.0, 0.003));
    sound->reset();
    sound->noteOn(76, 100, Touch::Normal);
    CHECK_THAT(pitchOf(render(*sound, 0.2), kRate), WithinRel(keyFrequency(76), 0.003));
}

TEST_CASE("SFZ sampler: tune, transpose and key tracking move the pitch", "[audio][sfz]") {
    const auto bank = bankOf(
        "<region> sample=*sine pitch_keycenter=69 transpose=12 tune=-100 pitch_keytrack=50\n");
    const auto sound = bank->sound(kGuitar, kRate);
    sound->noteOn(71, 100, Touch::Normal); // two keys up at half a semitone each
    CHECK_THAT(pitchOf(render(*sound, 0.2), kRate), WithinRel(keyFrequency(69 + 11 + 1), 0.003));
}

TEST_CASE("SFZ sampler: a glide bends only the notes of its key", "[audio][sfz]") {
    const auto bank = bankOf("<region> sample=*sine pitch_keycenter=60 hivel=64\n"
                             "<region> sample=*silence pitch_keycenter=60 lovel=65\n");
    const auto sound = bank->sound(kGuitar, kRate);
    sound->noteOn(69, 64, Touch::Normal);
    sound->noteOn(64, 127, Touch::Normal); // silent: the glide must leave 69 alone
    sound->glide(69, 200.0, 0.0);
    sound->glide(64, -500.0, 0.0);
    CHECK_THAT(pitchOf(render(*sound, 0.2), kRate), WithinRel(keyFrequency(71), 0.003));

    // Over time, a glide moves in a straight line in cents.
    sound->glide(69, 0.0, 0.1);
    (void)render(*sound, 0.15);
    CHECK_THAT(pitchOf(render(*sound, 0.2), kRate), WithinRel(keyFrequency(69), 0.003));
}

TEST_CASE("SFZ sampler: velocity picks the layer and sets the level", "[audio][sfz]") {
    const auto bank = bankOf("<region> sample=*silence hivel=40\n"
                             "<region> sample=*sine lovel=41\n");
    const auto sound = bank->sound(kGuitar, kRate);
    sound->noteOn(60, 30, Touch::Normal);
    CHECK(rms(render(*sound, 0.1)) == 0.0);
    sound->reset();
    sound->noteOn(60, 64, Touch::Normal);
    const double soft = rms(render(*sound, 0.1));
    sound->reset();
    sound->noteOn(60, 127, Touch::Normal);
    const double loud = rms(render(*sound, 0.1));
    // The default curve: gain as the square of the velocity.
    CHECK_THAT(soft / loud, WithinRel((64.0 * 64.0) / (127.0 * 127.0), 0.01));
}

TEST_CASE("SFZ sampler: volume, amplitude and amp_veltrack scale the level", "[audio][sfz]") {
    const auto bank = bankOf("<group> amp_veltrack=0\n"
                             "<region> sample=*sine hivel=40\n"
                             "<region> sample=*sine lovel=41 hivel=80 volume=-6.0206\n"
                             "<region> sample=*sine lovel=81 amplitude=25\n");
    const auto level = [&](int velocity) {
        const auto sound = bank->sound(kGuitar, kRate);
        sound->noteOn(60, velocity, Touch::Normal);
        return rms(render(*sound, 0.1));
    };
    const double full = level(10);
    CHECK_THAT(full, WithinRel(std::sqrt(0.5), 0.01)); // velocity no longer matters
    CHECK_THAT(level(60) / full, WithinRel(0.5, 0.001));
    CHECK_THAT(level(100) / full, WithinRel(0.25, 0.001));
}

TEST_CASE("SFZ sampler: round robin takes the regions of a key in turn", "[audio][sfz]") {
    const auto bank = bankOf("<group> seq_length=2\n"
                             "<region> sample=*sine seq_position=1\n"
                             "<region> sample=*silence seq_position=2\n");
    const auto sound = bank->sound(kGuitar, kRate);
    std::vector<bool> sounded;
    for (int i = 0; i < 4; ++i) {
        sound->noteOn(60, 100, Touch::Normal);
        sounded.push_back(rms(render(*sound, 0.05)) > 0.0);
        sound->reset();
    }
    CHECK(sounded == std::vector<bool>{true, false, true, false});
}

TEST_CASE("SFZ sampler: a key let go releases its note through the envelope", "[audio][sfz]") {
    const auto bank = bankOf("<region> sample=*sine ampeg_release=0.1\n"
                             "<region> sample=*silence trigger=release ampeg_release=0.01\n");
    const auto sound = bank->sound(kGuitar, kRate);
    sound->noteOn(60, 100, Touch::Normal);
    (void)render(*sound, 0.05);
    CHECK(sound->activeVoices() == 1);
    sound->noteOff(60);
    CHECK(sound->activeVoices() == 2); // the release region struck
    const auto released = render(*sound, 0.05);
    CHECK(rms(std::span(released).first(1000)) > rms(std::span(released).last(1000)));
    sound->releaseAll();
    (void)render(*sound, 0.5);
    CHECK(sound->activeVoices() == 0);
}

TEST_CASE("SFZ sampler: the envelope attacks, holds and decays to the sustain", "[audio][sfz]") {
    const auto bank = bankOf("<region> sample=*sine ampeg_attack=0.1 ampeg_hold=0.1 "
                             "ampeg_decay=0.1 ampeg_sustain=25 amp_veltrack=0\n");
    const auto sound = bank->sound(kGuitar, kRate);
    sound->noteOn(60, 100, Touch::Normal);
    const auto out = render(*sound, 0.5);
    const auto window = [&](double at) {
        return rms(std::span(out).subspan(static_cast<std::size_t>(at * kRate), 960));
    };
    const double peak = std::sqrt(0.5);
    CHECK(window(0.0) < 0.15 * peak);                      // rising from silence
    CHECK_THAT(window(0.12), WithinRel(peak, 0.02));       // held at the top
    CHECK_THAT(window(0.4), WithinRel(0.25 * peak, 0.02)); // settled at the sustain
}

TEST_CASE("SFZ sampler: a keyswitch picks which regions play", "[audio][sfz]") {
    const auto bank = bankOf("<group> sw_lokey=24 sw_hikey=25 sw_default=24\n"
                             "<region> sample=*sine sw_last=24 lokey=40 hikey=80\n"
                             "<region> sample=*silence sw_last=25 lokey=40 hikey=80\n");
    const auto sound = bank->sound(kGuitar, kRate);
    sound->noteOn(60, 100, Touch::Normal);
    CHECK(rms(render(*sound, 0.05)) > 0.0);
    sound->reset();
    sound->noteOn(25, 100, Touch::Normal); // switches, sounds nothing
    sound->noteOff(25);
    sound->noteOn(60, 100, Touch::Normal);
    CHECK(rms(render(*sound, 0.05)) == 0.0);
}

TEST_CASE("SFZ sampler: a controller range keeps a region quiet", "[audio][sfz]") {
    const auto bank = bankOf("<control> set_cc64=100\n"
                             "<region> sample=*silence hicc64=63\n"
                             "<region> sample=*sine locc64=64\n");
    const auto sound = bank->sound(kGuitar, kRate);
    sound->noteOn(60, 100, Touch::Normal);
    CHECK(rms(render(*sound, 0.05)) > 0.0);
    CHECK(sound->activeVoices() == 1);
}

TEST_CASE("SFZ sampler: a group silences the notes it is off by", "[audio][sfz]") {
    const auto bank = bankOf("<region> sample=*sine key=46 group=1 off_by=2\n"
                             "<region> sample=*sine key=42 group=2\n");
    const auto sound = bank->sound(kGuitar, kRate);
    sound->noteOn(46, 100, Touch::Normal);
    (void)render(*sound, 0.05);
    sound->noteOn(42, 100, Touch::Normal);
    (void)render(*sound, 0.05);
    CHECK(sound->activeVoices() == 1);
}

TEST_CASE("SFZ sampler: a palm mute or a dead note dies away sooner", "[audio][sfz]") {
    const auto bank = bankOf("<region> sample=*sine\n");
    const auto tail = [&](Touch touch) {
        const auto sound = bank->sound(kGuitar, kRate);
        sound->noteOn(52, 100, touch);
        const auto out = render(*sound, 0.6);
        return rms(std::span(out).last(4800));
    };
    const double normal = tail(Touch::Normal);
    CHECK(tail(Touch::PalmMute) < 0.1 * normal);
    CHECK(tail(Touch::Dead) < 0.001 * normal);
}

TEST_CASE("SFZ sampler: output does not depend on how render splits it", "[audio][sfz]") {
    const auto bank = bankOf("<region> sample=*sine ampeg_attack=0.003 ampeg_release=0.02\n");
    const auto play = [&](std::size_t block) {
        const auto sound = bank->sound(kGuitar, kRate);
        std::vector<float> out(4800, 0.0f);
        sound->noteOn(57, 90, Touch::Normal);
        sound->glide(57, 150.0, 0.05);
        for (std::size_t at = 0; at < out.size(); at += block) {
            if (at >= 2400 && at < 2400 + block) {
                sound->noteOff(57);
            }
            sound->render(std::span(out).subspan(at, std::min(block, out.size() - at)));
        }
        return out;
    };
    // The note-off lands on frame 2400 for both block sizes.
    CHECK(play(4800 / 2) == play(48 * 5));
}

TEST_CASE("SFZ sampler: notes beyond its voices take the oldest", "[audio][sfz]") {
    const auto bank = bankOf("<region> sample=*sine\n");
    const auto sound = bank->sound(kGuitar, kRate);
    for (int i = 0; i < 200; ++i) {
        sound->noteOn(30 + (i % 60), 100, Touch::Normal);
    }
    CHECK(sound->activeVoices() <= 64);
    CHECK(sound->activeVoices() > 0);
}

TEST_CASE("SFZ sampler: percussion tracks play on the other bank", "[audio][sfz]") {
    const auto bank = bankOf("<region> sample=*sine\n");
    CHECK(bank->soundName(kGuitar) == "test");
    CHECK(bank->soundName(kDrums) == elara::audio::builtinBank()->soundName(kDrums));
}

TEST_CASE("SFZ sampler: WAV samples play at their key's pitch, whatever their rate",
          "[audio][sfz]") {
    const TempFolder folder("elara-sfz-wav-test");
    std::filesystem::create_directories(folder.path / "samples");
    writeSine(folder.path / "samples" / "a3.wav", 220.0, 0.3, 44100);
    writeText(folder.path / "guitar.sfz",
              "<control> default_path=samples/\n"
              "<region> sample=a3.wav pitch_keycenter=57 lokey=50 hikey=70\n"
              "<region> sample=a3.wav pitch_keycenter=57 key=40 loop_mode=loop_continuous\n"
              "<region> sample=missing.wav key=30\n");
    const auto bank = SfzBank::open(folder.path / "guitar.sfz");
    REQUIRE(bank.has_value());
    CHECK((*bank)->name() == "guitar");
    CHECK((*bank)->program().regions.size() == 2);
    CHECK((*bank)->missingSamples() == std::vector<std::string>{"samples/missing.wav"});
    CHECK((*bank)->sampleCount() == 1);
    CHECK((*bank)->sampleBytes() == static_cast<std::size_t>(0.3 * 44100) * sizeof(float));

    const auto sound = (*bank)->sound(kGuitar, kRate);
    sound->noteOn(57, 100, Touch::Normal);
    CHECK_THAT(pitchOf(render(*sound, 0.2), kRate), WithinRel(220.0, 0.003));
    sound->reset();
    sound->noteOn(69, 100, Touch::Normal);
    CHECK_THAT(pitchOf(render(*sound, 0.2), kRate), WithinRel(440.0, 0.003));

    // Without a loop the note ends with its sample; with one it goes on.
    sound->reset();
    sound->noteOn(57, 100, Touch::Normal);
    (void)render(*sound, 0.4);
    CHECK(sound->activeVoices() == 0);
    sound->noteOn(40, 100, Touch::Normal);
    (void)render(*sound, 0.6);
    CHECK(sound->activeVoices() == 1);
}

TEST_CASE("SFZ sampler: an instrument with no sample read is an error", "[audio][sfz]") {
    const TempFolder folder("elara-sfz-missing-test");
    writeText(folder.path / "empty.sfz", "<region> sample=nothing.wav\n");
    const auto bank = SfzBank::open(folder.path / "empty.sfz");
    REQUIRE_FALSE(bank.has_value());
    CHECK(bank.error().find("nothing.wav") != std::string::npos);
    CHECK_FALSE(SfzBank::open(folder.path / "absent.sfz").has_value());
}

TEST_CASE("SFZ sampler: controllers at their set values move a region", "[audio][sfz]") {
    const auto level = [](std::string_view text) {
        const auto bank = bankOf(text);
        const auto sound = bank->sound(kGuitar, kRate);
        sound->noteOn(60, 127, Touch::Normal);
        const double value = rms(render(*sound, 0.1));
        CHECK(sound->activeVoices() == (value > 0.0 ? 1U : 0U));
        return value;
    };
    // A unison layer: silent, and no voice taken, until its switch is up.
    CHECK(level("<region> sample=*sine amplitude_oncc100=100\n") == 0.0);
    const double full =
        level("<control> set_cc100=127\n<region> sample=*sine amplitude_oncc100=100\n");
    CHECK_THAT(full, WithinRel(std::sqrt(0.5), 0.01));
    CHECK_THAT(level("<control> set_cc27=64\n<region> sample=*sine amplitude_cc27=100\n") / full,
               WithinRel(64.0 / 127.0, 0.001));
    CHECK_THAT(level("<control> set_cc7=127\n<region> sample=*sine volume_oncc7=-6.0206\n") / full,
               WithinRel(0.5, 0.001));

    const auto bank = bankOf("<control> set_cc1=127\n<region> sample=*sine pitch_keycenter=60 "
                             "tune_oncc1=1200\n");
    const auto sound = bank->sound(kGuitar, kRate);
    sound->noteOn(60, 100, Touch::Normal);
    CHECK_THAT(pitchOf(render(*sound, 0.2), kRate), WithinRel(keyFrequency(72), 0.003));
}

TEST_CASE("SFZ sampler: an instrument for guitars or basses leaves the other tracks",
          "[audio][sfz]") {
    using elara::audio::SfzTracks;
    const Instrument bass{.name = "Bass", .kind = InstrumentKind::Fretted, .midiProgram = 33};
    const Instrument piano{.name = "Piano", .kind = InstrumentKind::Keyboard, .midiProgram = 0};
    const auto builtin = elara::audio::builtinBank();
    const auto pitched = bankOf("<region> sample=*sine\n");
    CHECK(pitched->tracks() == SfzTracks::Pitched);
    CHECK(pitched->soundName(piano) == "test");

    const auto guitars = pitched->with(builtin, SfzTracks::Guitars);
    CHECK(guitars->tracks() == SfzTracks::Guitars);
    CHECK(guitars->soundName(kGuitar) == "test");
    CHECK(guitars->soundName(bass) == builtin->soundName(bass));
    CHECK(guitars->soundName(piano) == builtin->soundName(piano));
    CHECK(guitars->program().regions.size() == 1); // the same samples, shared

    // A bass instrument under the guitar one: each plays its own tracks.
    const auto both = guitars->with(pitched->with(builtin, SfzTracks::Basses), SfzTracks::Guitars);
    CHECK(both->soundName(kGuitar) == "test");
    CHECK(both->soundName(bass) == "test");
    CHECK(both->soundName(piano) == builtin->soundName(piano));
    CHECK(both->soundName(kDrums) == builtin->soundName(kDrums));

    CHECK(elara::audio::sfzTracksName(SfzTracks::Basses) == "basses");
    CHECK(elara::audio::sfzTracksFromName("guitars") == SfzTracks::Guitars);
    CHECK_FALSE(elara::audio::sfzTracksFromName("drums").has_value());
}

namespace {

// The level of the last 0.1 s of 0.6 s of a note: near 0 once a touch the
// sampler imitates has damped it.
double tailOf(TrackSound& sound, int key, Touch touch) {
    sound.reset();
    sound.noteOn(key, 100, touch);
    const auto out = render(sound, 0.6);
    sound.noteOff(key);
    return rms(std::span(out).last(4800));
}

} // namespace

TEST_CASE("SFZ sampler: a palm mute plays the library's palm-mute keyswitch, for that note only",
          "[audio][sfz]") {
    const auto bank = bankOf("<global> sw_lokey=24 sw_hikey=26 sw_default=24 lokey=40 hikey=80\n"
                             "<group> sw_last=24 sw_label=Sustain\n"
                             "<region> sample=*silence\n"
                             "<group> sw_last=25 sw_label=Palm Mute\n"
                             "<region> sample=*sine\n");
    const auto& techniques = bank->techniques();
    CHECK(techniques.palmMute.keyswitch == 25);
    CHECK(techniques.palmMute.cc == -1);
    CHECK(techniques.palmMute.label == "Palm Mute");
    CHECK_FALSE(techniques.dead.found());
    CHECK_FALSE(techniques.harmonic.found());

    const auto reference = bankOf("<region> sample=*sine\n")->sound(kGuitar, kRate);
    const double open = tailOf(*reference, 52, Touch::Normal);
    const auto sound = bank->sound(kGuitar, kRate);
    // The library's samples play the palm mute: Elara does not damp it too.
    CHECK(tailOf(*sound, 52, Touch::PalmMute) > 0.5 * open);
    // The track stays on its keyswitch: the next plain note sustains.
    CHECK(tailOf(*sound, 52, Touch::Normal) == 0.0);
}

TEST_CASE("SFZ sampler: a palm mute turns up the library's muting controller", "[audio][sfz]") {
    const auto bank = bankOf("<control> label_cc70=Muting\n"
                             "<region> sample=*silence hicc70=63\n"
                             "<region> sample=*sine locc70=64\n");
    CHECK(bank->techniques().palmMute.cc == 70);
    CHECK(bank->techniques().palmMute.keyswitch == -1);
    CHECK(bank->techniques().palmMute.label == "Muting");
    const auto sound = bank->sound(kGuitar, kRate);
    CHECK(tailOf(*sound, 52, Touch::Normal) == 0.0);
    CHECK(tailOf(*sound, 52, Touch::PalmMute) > 0.1);
    CHECK(tailOf(*sound, 52, Touch::Normal) == 0.0); // the controller went back

    // A name on a controller no region listens to is not a technique.
    const auto unused = bankOf("<control> label_cc70=Muting\n<region> sample=*sine\n");
    CHECK_FALSE(unused->techniques().palmMute.found());
    const auto damped = unused->sound(kGuitar, kRate);
    CHECK(tailOf(*damped, 52, Touch::PalmMute) < 0.1 * tailOf(*damped, 52, Touch::Normal));
}

TEST_CASE("SFZ sampler: dead notes and harmonics take the keyswitches named for them",
          "[audio][sfz]") {
    const auto bank = bankOf("<global> sw_lokey=24 sw_hikey=27 sw_default=24 lokey=40 hikey=80\n"
                             "<group> sw_last=24 sw_label=Open\n"
                             "<region> sample=*silence\n"
                             "<group> sw_last=26 sw_label=Dead notes\n"
                             "<region> sample=*sine\n"
                             "<group> sw_last=27 sw_label=Harmonics\n"
                             "<region> sample=*sine amplitude=50\n");
    const auto& techniques = bank->techniques();
    CHECK_FALSE(techniques.palmMute.found());
    CHECK(techniques.dead.keyswitch == 26);
    CHECK(techniques.dead.label == "Dead notes");
    CHECK(techniques.harmonic.keyswitch == 27);
    const auto sound = bank->sound(kGuitar, kRate);
    const double dead = tailOf(*sound, 52, Touch::Dead);
    const double harmonic = tailOf(*sound, 52, Touch::Harmonic);
    CHECK(dead > 0.1);
    CHECK(harmonic > 0.1);
    CHECK(harmonic < 0.6 * dead); // the harmonics' own region, at half amplitude
    CHECK(tailOf(*sound, 52, Touch::Normal) == 0.0);
}

TEST_CASE("SFZ sampler: a technique with no region for the key is imitated instead",
          "[audio][sfz]") {
    const auto bank = bankOf("<global> sw_lokey=24 sw_hikey=25 sw_default=24\n"
                             "<group> sw_last=24 sw_label=Sustain\n"
                             "<region> sample=*sine lokey=40 hikey=80\n"
                             "<group> sw_last=25 sw_label=Palm Mute\n"
                             "<region> sample=*sine lokey=40 hikey=50\n");
    const auto sound = bank->sound(kGuitar, kRate);
    const double open = tailOf(*sound, 60, Touch::Normal);
    CHECK(tailOf(*sound, 45, Touch::PalmMute) > 0.5 * open); // the library's palm mute
    CHECK(tailOf(*sound, 60, Touch::PalmMute) < 0.1 * open); // none up there: damped
}
