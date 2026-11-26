// SPDX-License-Identifier: AGPL-3.0-or-later
// Sound banks: where the tracks of a song get their sound (ADR-0043).
#include <elara/audio/offline_renderer.hpp>
#include <elara/audio/player.hpp>
#include <elara/audio/sequence.hpp>
#include <elara/audio/sfz.hpp>
#include <elara/audio/sfz_bank.hpp>
#include <elara/audio/sound_bank.hpp>
#include <elara/audio/synth.hpp>
#include <elara/core/song.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::audio::builtinBank;
using elara::audio::OfflineRenderer;
using elara::audio::Player;
using elara::audio::Sequence;
using elara::audio::SequenceOptions;
using elara::audio::SfzBank;
using elara::audio::SfzTracks;
using elara::audio::SoundBank;
using elara::audio::Synth;
using elara::audio::Timbre;
using elara::audio::Touch;
using elara::audio::TrackSound;
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
constexpr float kSteadyLevel = 0.25f;

// A test sound: a constant level for every key held, nothing once released.
// Counts the frames it renders in `rendered`.
class SteadySound final : public TrackSound {
public:
    explicit SteadySound(std::atomic<std::int64_t>& rendered) : rendered_(rendered) {}
    void noteOn(int /*key*/, int /*velocity*/, Touch /*touch*/) noexcept override { ++held_; }
    void noteOff(int /*key*/) noexcept override { held_ = std::max(held_ - 1, 0); }
    void glide(int /*key*/, double /*cents*/, double /*seconds*/) noexcept override {}
    void releaseAll() noexcept override { held_ = 0; }
    void reset() noexcept override { held_ = 0; }
    void render(std::span<float> out) noexcept override {
        rendered_ += static_cast<std::int64_t>(out.size());
        for (float& sample : out) {
            sample += kSteadyLevel * static_cast<float>(held_);
        }
    }
    [[nodiscard]] std::size_t activeVoices() const noexcept override {
        return static_cast<std::size_t>(held_);
    }

private:
    std::atomic<std::int64_t>& rendered_;
    int held_ = 0;
};

// Makes SteadySounds and counts them.
class SteadyBank final : public SoundBank {
public:
    [[nodiscard]] std::unique_ptr<TrackSound> sound(const Instrument& instrument,
                                                    double /*sampleRate*/) const override {
        ++made;
        programs.push_back(instrument.midiProgram);
        return std::make_unique<SteadySound>(rendered);
    }
    [[nodiscard]] std::string soundName(const Instrument& instrument) const override {
        return "steady " + std::to_string(instrument.midiProgram);
    }

    mutable std::atomic<int> made{0};
    mutable std::atomic<std::int64_t> rendered{0}; ///< frames its sounds rendered
    mutable std::vector<int> programs;
};

// An SFZ instrument of one generated sample, read from `text`.
std::shared_ptr<const SfzBank> sfzOf(std::string_view text) {
    auto program = elara::audio::parseSfz(
        text, [](std::string_view) -> std::expected<std::string, std::string> {
            return std::unexpected(std::string("no includes here"));
        });
    REQUIRE(program.has_value());
    auto bank = SfzBank::load(*std::move(program), {}, "test");
    REQUIRE(bank.has_value());
    return *std::move(bank);
}

// One measure of 4/4 at quarter = 120 (two seconds) with a quarter note C4 on
// beat 1 of each track, one track for each General MIDI program.
Song song(std::vector<int> programs, const std::string& name = "Part") {
    Song result;
    for (const int program : programs) {
        result.addTrack(Track(name, Instrument{.name = name, .midiProgram = program}, {Staff{}}));
    }
    result.appendMeasures(1);
    for (std::size_t t = 0; t < programs.size(); ++t) {
        result.track(t).staff(0).measure(0).voices[0].beats = {
            Beat{.duration = Duration(NoteValue::Quarter),
                 .notes = {Note{.pitch = Pitch::fromMidi(60)}}},
            Beat{.duration = Duration(NoteValue::Half, 1)}};
    }
    return result;
}

std::size_t frame(double seconds) {
    return static_cast<std::size_t>(std::llround(seconds * kRate));
}

// Plays `player` from the start for `seconds` and returns the left channel.
std::vector<float> playLeft(Player& player, double seconds) {
    std::vector<float> left(frame(seconds));
    std::vector<float> right(left.size());
    player.play();
    player.render(left, right);
    return left;
}

} // namespace

TEST_CASE("Sound banks: the built-in bank plays the built-in synthesizer", "[audio][soundbank]") {
    const auto bank = builtinBank();
    REQUIRE(bank != nullptr);
    CHECK(bank == builtinBank());
    const Instrument guitar{.name = "Guitar", .midiProgram = 25};
    const Instrument drums{.name = "Drums", .kind = InstrumentKind::Percussion};
    CHECK(bank->soundName(guitar) == "guitar");
    CHECK(bank->soundName(drums) == "drums");

    const auto sound = bank->sound(guitar, kRate);
    Synth synth(Timbre::Guitar, kRate);
    std::vector<float> fromBank(4800);
    std::vector<float> fromSynth(4800);
    sound->noteOn(64, 100, Touch::PalmMute);
    synth.noteOn(64, 100, Touch::PalmMute);
    sound->render(std::span(fromBank).first(1000));
    synth.render(std::span(fromSynth).first(1000));
    sound->glide(64, 200.0, 0.01);
    synth.glide(64, 200.0, 0.01);
    sound->render(std::span(fromBank).subspan(1000));
    synth.render(std::span(fromSynth).subspan(1000));
    CHECK(fromBank == fromSynth);
    CHECK(sound->activeVoices() == 1);
    CHECK(std::ranges::any_of(fromBank, [](float sample) { return sample != 0.0f; }));
}

TEST_CASE("Offline rendering: tracks play through the render's sound bank", "[audio][soundbank]") {
    const auto bank = std::make_shared<SteadyBank>();
    const Song two = song({25, 33});
    const Sequence sequence(two);

    SECTION("each track asks the bank for its sound") {
        OfflineRenderer renderer(two, sequence, {.sampleRate = kRate, .tail = 0.5, .bank = bank});
        CHECK(bank->made == 2);
        CHECK(bank->programs == std::vector<int>{25, 33});
        CHECK(renderer.sounds() == std::vector<std::string>{"steady 25", "steady 33"});

        const std::vector<float> out = renderer.renderRest();
        // Two tracks, each held at a constant level for the quarter note.
        const auto during = static_cast<double>(out[2 * frame(0.25)]);
        CHECK(during > 0.0);
        CHECK_THAT(out[2 * frame(0.1)], WithinAbs(during, 1e-6));
        CHECK_THAT(out[(2 * frame(0.4)) + 1], WithinAbs(during, 1e-6));
        CHECK(out[2 * frame(0.6)] == 0.0f);
    }
    SECTION("the metronome keeps the built-in clicks") {
        const Sequence clicking(two, SequenceOptions{.metronome = true});
        OfflineRenderer renderer(two, clicking, {.sampleRate = kRate, .bank = bank});
        CHECK(bank->made == 2);
        CHECK(renderer.sounds().size() == 2);
    }
    SECTION("without a bank, the built-in synthesizer") {
        OfflineRenderer renderer(two, sequence, {.sampleRate = kRate});
        CHECK(bank->made == 0);
        CHECK(renderer.sounds() == std::vector<std::string>{"guitar", "bass"});
    }
}

TEST_CASE("Playback: tracks play through the player's sound bank", "[audio][soundbank]") {
    const auto bank = std::make_shared<SteadyBank>();
    const Song two = song({25, 33});
    Player player(kRate);
    player.setSoundBank(bank);
    player.load(two, Sequence(two));
    CHECK(bank->made == 2);
    const std::vector<float> left = playLeft(player, 0.3);
    CHECK(left[frame(0.2)] > 0.0f);
    CHECK_THAT(left[frame(0.1)], WithinAbs(static_cast<double>(left[frame(0.2)]), 1e-6));

    SECTION("a reload keeps the sounds of unchanged instruments") {
        player.load(two, Sequence(two));
        CHECK(bank->made == 2);
        const Song fretless = song({25, 35});
        player.load(fretless, Sequence(fretless));
        CHECK(bank->made == 3);
        CHECK(bank->programs.back() == 35);
        // Renamed instruments sound the same.
        const Song renamed = song({25, 35}, "Renamed");
        player.load(renamed, Sequence(renamed));
        CHECK(bank->made == 3);
    }
    SECTION("a kept sound stops when the new song takes over") {
        // Reloaded during the held note: playback goes on from the same place,
        // and the note that started before is not struck again.
        player.load(two, Sequence(two));
        std::vector<float> left2(64);
        std::vector<float> right2(64);
        player.render(left2, right2);
        CHECK(std::ranges::all_of(left2, [](float sample) { return sample == 0.0f; }));
    }
    SECTION("a bank that changes the guitars' instrument keeps the other tracks' sounds") {
        player.setSoundBank(sfzOf("<region> sample=*sine\n")->with(bank, SfzTracks::Guitars));
        player.load(two, Sequence(two));
        CHECK(bank->made == 2); // the bass still plays the sound it had
        player.setSoundBank(sfzOf("<region> sample=*silence\n")->with(bank, SfzTracks::Guitars));
        player.load(two, Sequence(two));
        CHECK(bank->made == 2);
        player.setSoundBank(bank);
        player.load(two, Sequence(two));
        CHECK(bank->made == 3);
        CHECK(bank->programs.back() == 25); // the guitar's sound, made again
    }
    SECTION("paused with nothing ringing, the sounds do not run") {
        player.pause();
        std::vector<float> left2(256, 1.0f);
        std::vector<float> right2(256, 1.0f);
        player.render(left2, right2); // the pause releases the note
        const std::int64_t rendered = bank->rendered;
        player.render(left2, right2);
        CHECK(bank->rendered == rendered);
        CHECK(std::ranges::all_of(left2, [](float sample) { return sample == 0.0f; }));
        CHECK(std::ranges::all_of(right2, [](float sample) { return sample == 0.0f; }));
        player.play();
        player.render(left2, right2);
        CHECK(bank->rendered > rendered);
    }
    SECTION("another bank makes new sounds") {
        player.setSoundBank(builtinBank());
        player.load(two, Sequence(two));
        CHECK(bank->made == 2);
        const auto other = std::make_shared<SteadyBank>();
        player.setSoundBank(other);
        player.load(two, Sequence(two));
        CHECK(other->made == 2);
    }
}
