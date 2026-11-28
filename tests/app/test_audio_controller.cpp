// SPDX-License-Identifier: AGPL-3.0-or-later
// The transport (ADR-0019, ADR-0122) through a device of the tests' own: it
// opens on a worker when told to, fails when told to, changes its rate, and
// plays the source as the audio thread would, a number of seconds at a time.
#include <elara/audio/audio_source.hpp>
#include <elara/audio/output_device.hpp>
#include <elara/audio/sound_bank.hpp>

#include "audio_controller.hpp"
#include "score_document.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QVariantMap>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using elara::audio::OutputDeviceChoice;
using elara::audio::OutputDeviceInfo;

template <typename Done>
bool settle(Done done) {
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < 5000) {
        QCoreApplication::processEvents();
    }
    return done();
}

// Lets the interface thread run for `ms` milliseconds.
void pump(int ms) {
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms) {
        QCoreApplication::processEvents();
    }
}

class FakeDevice final : public elara::audio::OutputDevice {
public:
    // ---- What the test sets -------------------------------------------------
    /// open() waits while this is true, as a device that is slow to wake up.
    std::atomic<bool> holdOpen{false};
    /// open() fails with this error when it is not empty.
    std::string failWith;
    /// The rate each device opens at, by name; 48 kHz for any other.
    std::vector<std::pair<std::string, double>> rates;
    std::vector<OutputDeviceInfo> outputs;

    // ---- What the test reads ------------------------------------------------
    std::vector<OutputDeviceChoice> opened() const {
        const std::scoped_lock lock(mutex_);
        return opened_;
    }
    [[nodiscard]] int tones() const { return tones_.load(); }
    [[nodiscard]] bool hasSource() const { return source_ != nullptr; }
    [[nodiscard]] const elara::audio::AudioSource* source() const { return source_; }

    /// The device restarts at another rate, as when headphones are plugged in.
    void restartAt(double rate) { rate_.store(rate); }

    /// Plays `seconds` of the source, in blocks, as the audio thread would.
    void play(double seconds) {
        if (source_ == nullptr) {
            return;
        }
        auto frames = static_cast<std::size_t>(std::lround(seconds * rate_.load()));
        std::vector<float> left(256);
        std::vector<float> right(256);
        while (frames > 0) {
            const std::size_t count = std::min(frames, left.size());
            source_->render(std::span(left.data(), count), std::span(right.data(), count));
            frames -= count;
        }
    }

    // ---- OutputDevice -------------------------------------------------------
    std::expected<void, std::string> open(const OutputDeviceChoice& device) override {
        while (holdOpen.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        {
            const std::scoped_lock lock(mutex_);
            opened_.push_back(device);
        }
        if (!failWith.empty()) {
            open_ = false;
            return std::unexpected(failWith);
        }
        double rate = 48000.0;
        for (const auto& [name, at] : rates) {
            if (name == device.name) {
                rate = at;
            }
        }
        rate_.store(rate);
        name_ = device.isDefault() ? std::string("Default") : device.name;
        open_ = true;
        return {};
    }
    std::vector<OutputDeviceInfo> outputDevices() override { return outputs; }
    [[nodiscard]] std::string deviceDescription() const override {
        return open_ ? "Fake / " + name_ : std::string();
    }
    [[nodiscard]] double sampleRate() const override { return open_ ? rate_.load() : 0.0; }
    void setSource(elara::audio::AudioSource* source) override { source_ = source; }
    void playTone(double /*frequencyHz*/, double /*seconds*/) noexcept override { ++tones_; }
    void stop() noexcept override {}
    [[nodiscard]] bool isPlaying() const noexcept override { return false; }

private:
    mutable std::mutex mutex_;
    std::vector<OutputDeviceChoice> opened_;
    std::atomic<double> rate_{48000.0};
    std::atomic<int> tones_{0};
    std::string name_;
    bool open_ = false;
    elara::audio::AudioSource* source_ = nullptr;
};

// The built-in synthesizer, made to wait while `hold` is true (up to a few
// seconds), as a large SoundFont's sounds wait while their samples are read.
// It says whether a sound was ever made on the interface thread.
class SlowBank final : public elara::audio::SoundBank {
public:
    std::atomic<bool> hold{false};

    [[nodiscard]] int made() const { return made_.load(); }
    [[nodiscard]] bool madeOnInterfaceThread() const { return onInterfaceThread_.load(); }

    [[nodiscard]] std::unique_ptr<elara::audio::TrackSound>
    sound(const elara::core::Instrument& instrument, double sampleRate) const override {
        if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
            onInterfaceThread_ = true;
        }
        QElapsedTimer clock;
        clock.start();
        while (hold.load() && clock.elapsed() < 3000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ++made_;
        return elara::audio::builtinBank()->sound(instrument, sampleRate);
    }
    [[nodiscard]] std::string soundName(const elara::core::Instrument& instrument) const override {
        return elara::audio::builtinBank()->soundName(instrument);
    }

private:
    mutable std::atomic<int> made_{0};
    mutable std::atomic<bool> onInterfaceThread_{false};
};

// A new song of rests, and a transport playing it through `device`.
struct Rig {
    std::unique_ptr<FakeDevice> made = std::make_unique<FakeDevice>();
    FakeDevice& device = *made;
    elara::app::ScoreDocument score;
    AudioController audio;

    // The transport makes its device once, the first time it needs one.
    Rig() : audio([this] { return std::move(made); }) {
        score.newSong(QStringLiteral("electric-guitar"));
        audio.setDocument(&score);
    }

    bool deviceSettled() {
        return settle([this] { return !audio.openingDevice(); });
    }
    // The device is open and the song's sounds are made: the song plays.
    bool startsPlaying() {
        return deviceSettled() && settle([this] { return audio.playingSong(); });
    }
    // A player made at the device's new rate has taken over from `before`.
    bool switchedFrom(const elara::audio::AudioSource* before) {
        return settle([this, before] { return device.source() != before; });
    }
    bool reaches(double seconds) {
        return settle([this, seconds] { return std::abs(audio.position() - seconds) < 0.02; });
    }
};

} // namespace

TEST_CASE("transport: Play opens the device on a worker and starts once it is open") {
    Rig rig;
    rig.device.holdOpen = true;
    rig.audio.togglePlayback();
    CHECK(rig.audio.openingDevice());
    CHECK_FALSE(rig.audio.playingSong());

    rig.device.holdOpen = false;
    REQUIRE(rig.startsPlaying());
    CHECK(rig.device.hasSource());
    CHECK(rig.audio.deviceDescription() == QStringLiteral("Fake / Default"));
    CHECK(rig.device.opened() == std::vector<OutputDeviceChoice>{{}});

    rig.device.play(1.0);
    CHECK(rig.reaches(1.0));
}

TEST_CASE("transport: the first Play makes the song's sounds on a worker, then plays") {
    Rig rig;
    const auto bank = std::make_shared<SlowBank>();
    rig.audio.setSoundBank(bank);
    bank->hold = true;
    rig.audio.togglePlayback();
    REQUIRE(rig.deviceSettled());
    // The device is open and the sounds are still being made: the interface
    // goes on meanwhile, and the transport waits for them.
    CHECK_FALSE(rig.audio.playingSong());
    rig.audio.seek(1.0);
    CHECK(rig.audio.position() == Catch::Approx(1.0));

    bank->hold = false;
    REQUIRE(settle([&rig] { return rig.audio.playingSong(); }));
    CHECK_FALSE(bank->madeOnInterfaceThread());
    CHECK(rig.device.hasSource());
    // From where the transport was moved meanwhile.
    rig.device.play(0.5);
    CHECK(rig.reaches(1.5));
}

TEST_CASE("transport: a second Play while the song's sounds are made takes the first back") {
    Rig rig;
    const auto bank = std::make_shared<SlowBank>();
    rig.audio.setSoundBank(bank);
    bank->hold = true;
    rig.audio.togglePlayback();
    REQUIRE(rig.deviceSettled());
    rig.audio.togglePlayback();
    bank->hold = false;
    REQUIRE(settle([&bank] { return bank->made() == 1; }));
    pump(200); // the made song reaches the transport
    CHECK_FALSE(rig.audio.playingSong());

    // The song is ready now: Play starts at once.
    rig.audio.togglePlayback();
    CHECK(rig.audio.playingSong());
}

TEST_CASE("transport: Play after another song opens waits for that song's sounds") {
    Rig rig;
    const auto bank = std::make_shared<SlowBank>();
    rig.audio.setSoundBank(bank);
    rig.audio.togglePlayback();
    REQUIRE(rig.deviceSettled());
    REQUIRE(settle([&rig] { return rig.audio.playingSong(); }));
    rig.audio.stopPlayback();

    // The bass needs a sound of its own: the song that was open does not play
    // meanwhile.
    bank->hold = true;
    rig.score.newSong(QStringLiteral("electric-bass"));
    rig.audio.togglePlayback();
    CHECK_FALSE(rig.audio.playingSong());
    bank->hold = false;
    REQUIRE(settle([&rig] { return rig.audio.playingSong(); }));
    CHECK(bank->made() == 2);
    CHECK_FALSE(bank->madeOnInterfaceThread());
}

TEST_CASE("transport: a second Play while the device opens takes the first back") {
    Rig rig;
    rig.device.holdOpen = true;
    rig.audio.togglePlayback();
    rig.audio.togglePlayback();
    rig.device.holdOpen = false;
    REQUIRE(rig.deviceSettled());
    CHECK_FALSE(rig.audio.playingSong());
    CHECK(rig.device.opened().size() == 1);

    // The device is open now: Play starts once the song's sounds are made.
    rig.audio.togglePlayback();
    CHECK(rig.startsPlaying());
    CHECK(rig.device.opened().size() == 1);
}

TEST_CASE("transport: a device that does not open says why and plays nothing") {
    Rig rig;
    rig.device.failWith = "no output device \"Fake / Gone\"";
    rig.audio.togglePlayback();
    rig.audio.playTestTone();
    REQUIRE(rig.deviceSettled());
    CHECK_FALSE(rig.audio.playingSong());
    CHECK(rig.device.tones() == 0);
    CHECK(rig.audio.errorMessage().contains(QStringLiteral("no output device \"Fake / Gone\"")));
    CHECK(rig.audio.deviceDescription().isEmpty());

    // Play asks again, and plays once the device opens.
    rig.device.failWith.clear();
    rig.audio.togglePlayback();
    REQUIRE(rig.startsPlaying());
    CHECK(rig.audio.errorMessage().isEmpty());
}

TEST_CASE("transport: the test tone waits for the device") {
    Rig rig;
    rig.device.holdOpen = true;
    rig.audio.playTestTone(440.0, 0.5);
    CHECK(rig.device.tones() == 0);
    rig.device.holdOpen = false;
    REQUIRE(rig.deviceSettled());
    CHECK(rig.device.tones() == 1);
}

TEST_CASE("transport: a device chosen while another opens is opened next, and Play waits for it") {
    Rig rig;
    rig.device.holdOpen = true;
    rig.audio.togglePlayback();
    rig.audio.setOutputDevice(QStringLiteral("Fake"), QStringLiteral("Phones"));
    CHECK(rig.audio.outputDeviceName() == QStringLiteral("Phones"));
    rig.device.holdOpen = false;
    REQUIRE(settle([&rig] { return rig.audio.playingSong(); }));
    CHECK(rig.device.opened() ==
          std::vector<OutputDeviceChoice>{{}, {.type = "Fake", .name = "Phones"}});
    CHECK(rig.audio.deviceDescription() == QStringLiteral("Fake / Phones"));
}

TEST_CASE("transport: a device chosen while playing takes over at the same place, at its rate") {
    Rig rig;
    rig.device.rates = {{"Phones", 44100.0}};
    rig.audio.togglePlayback();
    REQUIRE(rig.startsPlaying());
    rig.device.play(1.0);
    REQUIRE(rig.reaches(1.0));

    const auto* before = rig.device.source();
    rig.audio.setOutputDevice(QStringLiteral("Fake"), QStringLiteral("Phones"));
    REQUIRE(rig.deviceSettled());
    CHECK(rig.audio.playingSong());
    CHECK(rig.device.sampleRate() == 44100.0);
    REQUIRE(rig.switchedFrom(before));
    // A player made at the new rate plays the music at its speed: half a
    // second of the device is half a second of the song.
    rig.device.play(0.5);
    CHECK(rig.reaches(1.5));

    // Back to the default, which the choice of none stands for.
    rig.audio.setOutputDevice(QStringLiteral("Fake"), QString());
    REQUIRE(rig.deviceSettled());
    CHECK(rig.audio.outputDeviceType().isEmpty());
    CHECK(rig.device.opened().back() == OutputDeviceChoice{});
}

TEST_CASE("transport: a device that restarts at another rate keeps the place in the music") {
    Rig rig;
    rig.audio.togglePlayback();
    REQUIRE(rig.startsPlaying());
    rig.device.play(1.0);
    REQUIRE(rig.reaches(1.0));

    const auto* before = rig.device.source();
    rig.device.restartAt(96000.0);
    rig.device.play(0.5); // still the old player, at the old rate: one second of music
    REQUIRE(rig.reaches(2.0));
    REQUIRE(rig.switchedFrom(before)); // a player made at 96 kHz, on a worker
    CHECK(rig.audio.playingSong());
    rig.device.play(0.5);
    CHECK(rig.reaches(2.5));
}

TEST_CASE("transport: the player for a device's new rate makes its sounds on a worker") {
    Rig rig;
    const auto bank = std::make_shared<SlowBank>();
    rig.audio.setSoundBank(bank);
    rig.audio.togglePlayback();
    REQUIRE(rig.startsPlaying());
    rig.device.play(1.0);
    REQUIRE(rig.reaches(1.0));

    const auto* before = rig.device.source();
    bank->hold = true;
    rig.device.restartAt(96000.0);
    pump(100); // the transport sees the new rate
    // The old player plays on meanwhile, at the old rate.
    CHECK(rig.device.source() == before);
    rig.device.play(0.5);
    REQUIRE(rig.reaches(2.0));

    bank->hold = false;
    REQUIRE(rig.switchedFrom(before));
    CHECK_FALSE(bank->madeOnInterfaceThread());
    rig.device.play(0.5);
    CHECK(rig.reaches(2.5));
}

TEST_CASE("transport: another speed plays on from the same place in the music, loop and all") {
    Rig rig;
    rig.audio.togglePlayback();
    REQUIRE(rig.startsPlaying());
    rig.device.play(1.0);
    REQUIRE(rig.reaches(1.0));
    rig.audio.markLoop();
    rig.device.play(1.0);
    REQUIRE(rig.reaches(2.0));
    rig.audio.markLoop();
    REQUIRE(rig.audio.looping());
    CHECK(rig.audio.loopFrom() == Catch::Approx(1.0).margin(0.02));
    CHECK(rig.audio.loopTo() == Catch::Approx(2.0).margin(0.02));
    const double before = rig.audio.position();

    // At half the speed every place in the music comes twice as late.
    rig.score.setSpeed(50);
    REQUIRE(settle([&rig] { return std::abs(rig.audio.loopFrom() - 2.0) < 0.02; }));
    CHECK(rig.audio.loopTo() == Catch::Approx(4.0).margin(0.02));
    CHECK(rig.reaches(2.0 * before));
    CHECK(rig.audio.playingSong());
}

TEST_CASE("transport: another song starts from the beginning, without the loop") {
    Rig rig;
    rig.audio.togglePlayback();
    REQUIRE(rig.startsPlaying());
    rig.device.play(1.0);
    REQUIRE(rig.reaches(1.0));
    rig.audio.markLoop();
    REQUIRE(rig.audio.loopMarked());

    rig.score.newSong(QStringLiteral("electric-guitar"));
    CHECK(rig.reaches(0.0));
    CHECK_FALSE(rig.audio.loopMarked());
    // The device stays open: no second open for the new song.
    CHECK(rig.device.opened().size() == 1);
}

TEST_CASE("transport: the outputs are looked for on a worker") {
    Rig rig;
    rig.device.outputs = {{.type = "Fake", .name = "Speakers", .isDefault = true},
                          {.type = "Fake", .name = "Phones", .isDefault = false}};
    rig.audio.listOutputDevices();
    CHECK(rig.audio.listingOutputDevices());
    REQUIRE(settle([&rig] { return !rig.audio.listingOutputDevices(); }));
    const QVariantList outputs = rig.audio.outputDevices();
    REQUIRE(outputs.size() == 2);
    const QVariantMap phones = outputs[1].toMap();
    CHECK(phones[QStringLiteral("type")] == QStringLiteral("Fake"));
    CHECK(phones[QStringLiteral("name")] == QStringLiteral("Phones"));
    CHECK(phones[QStringLiteral("isDefault")] == false);
    // Looking for the outputs opens no device.
    CHECK(rig.device.opened().empty());
}
