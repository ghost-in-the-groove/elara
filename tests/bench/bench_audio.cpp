// SPDX-License-Identifier: AGPL-3.0-or-later
// Micro-benchmarks for elara_audio: the sequencer, the built-in synthesizer and
// offline rendering, the tuner's pitch detector, the SFZ sampler and the FLAC
// decoder. Run optimised builds only:
//   python scripts/elara.py bench -- "[audio]"
#include <elara/audio/flac_reader.hpp>
#include <elara/audio/offline_renderer.hpp>
#include <elara/audio/pitch_detector.hpp>
#include <elara/audio/sequence.hpp>
#include <elara/audio/sfz_bank.hpp>
#include <elara/audio/sound_bank.hpp>
#include <elara/audio/synth.hpp>
#include <elara/audio/wav_reader.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/performance.hpp>
#include <elara/core/song.hpp>

#include "bench_songs.hpp"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <sndfile.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <numbers>
#include <string>
#include <vector>

TEST_CASE("sequencer benchmarks", "[benchmark][audio]") {
    elara::core::Song song = elara::bench::largeSong();
    BENCHMARK("sounding notes of the large song (what the sequencer starts from)") {
        return elara::core::soundingNotes(song).size();
    };
    BENCHMARK("sequence the large song (500 measures, 160k notes)") {
        return elara::audio::Sequence(song).events().size();
    };
    // Every four measures repeated: 1000 measures and 320k notes played.
    for (std::size_t m = 0; m < song.measureCount(); m += 4) {
        song.measureHeader(m).repeatStart = true;
        song.measureHeader(m + 3).repeatTimes = 2;
    }
    BENCHMARK("sequence the large song with every four measures repeated") {
        return elara::audio::Sequence(song).events().size();
    };
}

TEST_CASE("synthesizer benchmarks", "[benchmark][audio]") {
    using elara::audio::Synth;
    using elara::audio::Timbre;
    constexpr double kRate = 48000.0;
    // One second at 48 kHz in 512-frame buffers, all 32 voices sounding.
    const auto second = [&](Timbre timbre, int lowestKey) {
        Synth synth(timbre, kRate);
        for (int v = 0; v < 32; ++v) {
            synth.noteOn(lowestKey + v, 100);
        }
        std::vector<float> buffer(512);
        for (int block = 0; block < 94; ++block) {
            synth.render(buffer);
        }
        return buffer[0];
    };
    BENCHMARK("1 s of 32 plucked strings (guitar)") {
        return second(Timbre::Guitar, 40);
    };
    BENCHMARK("1 s of 32 struck partials (piano)") {
        return second(Timbre::Piano, 40);
    };
    BENCHMARK("1 s of 32 sawtooth voices (bowed)") {
        return second(Timbre::Bowed, 40);
    };
    BENCHMARK("1 s of 32 drum hits") {
        return second(Timbre::Drums, 35);
    };
}

TEST_CASE("offline rendering benchmarks", "[benchmark][audio]") {
    const elara::core::Song song = elara::bench::largeSong();
    const elara::audio::Sequence sequence(song);
    // The first 10 s: 20 guitar tracks, each striking two-note chords.
    const auto tenSeconds = [&](unsigned threads) {
        elara::audio::OfflineRenderer renderer(song, sequence,
                                               {.sampleRate = 48000.0, .threads = threads});
        std::vector<float> block(2 * 48000);
        for (int s = 0; s < 10; ++s) {
            static_cast<void>(renderer.render(block));
        }
        return block[0];
    };
    BENCHMARK("render 10 s of the large song (20 tracks) on every core") {
        return tenSeconds(0);
    };
    BENCHMARK("render 10 s of the large song on one thread") {
        return tenSeconds(1);
    };
}

TEST_CASE("pitch detector benchmarks", "[benchmark][audio][tuner]") {
    // A tuner listening at 48 kHz looks at one window every half window (33 ms).
    elara::audio::PitchDetector detector(48000.0);
    const auto sine = [&](double frequency) {
        std::vector<float> window(detector.frameSize());
        for (std::size_t i = 0; i < window.size(); ++i) {
            window[i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * frequency *
                                                          static_cast<double>(i) / 48000.0));
        }
        return window;
    };
    const std::vector<float> lowE = sine(82.41);
    const std::vector<float> highE = sine(329.63);
    std::vector<float> noise(detector.frameSize());
    std::uint32_t state = 12345;
    for (float& sample : noise) {
        state = (state * 1664525U) + 1013904223U;
        sample = (static_cast<float>(state >> 8U) / 8388608.0f) - 1.0f;
    }
    BENCHMARK("detect a guitar's low E in one window (48 kHz, 3201 samples)") {
        return detector.detect(lowE)->frequency;
    };
    BENCHMARK("detect a guitar's high E in one window") {
        return detector.detect(highE)->frequency;
    };
    BENCHMARK("find no pitch in one window of noise (every lag computed)") {
        return detector.detect(noise).has_value();
    };
}

namespace {

// A plucked string, as make_test_sfz writes it: eight harmonics dying away.
std::vector<float> benchPluck(double frequency, double seconds, std::uint32_t rate) {
    std::vector<float> out(static_cast<std::size_t>(seconds * rate));
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        double value = 0.0;
        for (int h = 1; h <= 8; ++h) {
            value +=
                std::sin(2.0 * std::numbers::pi * h * frequency * t) * std::exp(-t * (1.0 + h)) / h;
        }
        out[i] = static_cast<float>(0.4 * value);
    }
    return out;
}

std::vector<std::byte> fileBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(chars.size());
    std::ranges::transform(chars, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return bytes;
}

// `samples` in each of `channels`, each channel 37 frames later than the one
// before (alike but not equal, as a stereo recording), through libsndfile.
bool writeSndFile(const std::filesystem::path& path, int format, const std::vector<float>& samples,
                  int channels, int rate) {
    SF_INFO info{};
    info.samplerate = rate;
    info.channels = channels;
    info.format = format;
    SNDFILE* file = sf_open(path.string().c_str(), SFM_WRITE, &info);
    if (file == nullptr) {
        return false;
    }
    std::vector<float> frames(samples.size() * static_cast<std::size_t>(channels));
    const auto width = static_cast<std::size_t>(channels);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        frames[i] = samples[(i / width + 37 * (i % width)) % samples.size()];
    }
    const auto count = static_cast<sf_count_t>(samples.size());
    const bool written = sf_writef_float(file, frames.data(), count) == count;
    return sf_close(file) == 0 && written;
}

} // namespace

TEST_CASE("sampler benchmarks", "[benchmark][audio][sfz]") {
    constexpr double kRate = 48000.0;
    const auto folder = std::filesystem::temp_directory_path() / "elara-bench-sfz";
    std::filesystem::create_directories(folder);
    // 64 samples of 2 s at 48 kHz, 16-bit mono: one region per key from 30 on,
    // as a library with a sample for every fret.
    std::string text;
    for (int key = 30; key < 94; ++key) {
        const auto name = std::format("key{}.wav", key);
        REQUIRE(writeSndFile(folder / name, SF_FORMAT_WAV | SF_FORMAT_PCM_16,
                             benchPluck(440.0 * std::exp2((key - 69) / 12.0), 2.0, 48000), 1,
                             48000));
        text += std::format("<region> sample={} key={}\n", name, key);
    }
    std::filesystem::path sfzPath = folder / "bench.sfz";
    {
        std::ofstream out(sfzPath, std::ios::binary);
        out << text;
    }
    BENCHMARK("read an SFZ of 64 samples (12 MB of WAV) on every core") {
        return elara::audio::SfzBank::open(sfzPath).has_value();
    };

    const auto bank = elara::audio::SfzBank::open(sfzPath);
    REQUIRE(bank.has_value());
    const elara::core::Instrument guitar{
        .name = "Guitar", .kind = elara::core::InstrumentKind::Fretted, .midiProgram = 25};
    const auto sound = (*bank)->sound(guitar, kRate);
    // One second at 48 kHz in 512-frame buffers, all 32 voices sounding.
    const auto second = [&](bool bent) {
        sound->reset();
        for (int v = 0; v < 32; ++v) {
            sound->noteOn(40 + v, 100, elara::audio::Touch::Normal);
            if (bent) {
                sound->glide(40 + v, 200.0, 0.5);
            }
        }
        std::vector<float> buffer(512);
        for (int block = 0; block < 94; ++block) {
            std::ranges::fill(buffer, 0.0f);
            sound->render(buffer);
        }
        return buffer[0];
    };
    BENCHMARK("1 s of 32 sampled voices (SFZ)") {
        return second(false);
    };
    BENCHMARK("1 s of 32 sampled voices, each bending a whole step") {
        return second(true);
    };
    std::filesystem::remove_all(folder);
}

TEST_CASE("FLAC benchmarks", "[benchmark][audio][flac]") {
    const auto folder = std::filesystem::temp_directory_path() / "elara-bench-flac";
    std::filesystem::create_directories(folder);
    // A minute of a plucked chord, stereo at 44.1 kHz in 16 bits, as libFLAC
    // encodes it at its default level, and the same as WAV.
    std::vector<float> minute;
    for (int i = 0; i < 30; ++i) {
        const auto pluck = benchPluck(110.0 * (1 + i % 4), 2.0, 44100);
        minute.insert(minute.end(), pluck.begin(), pluck.end());
    }
    REQUIRE(writeSndFile(folder / "minute.flac", SF_FORMAT_FLAC | SF_FORMAT_PCM_16, minute, 2,
                         44100));
    REQUIRE(writeSndFile(folder / "minute.wav", SF_FORMAT_WAV | SF_FORMAT_PCM_16, minute, 2,
                         44100));
    const auto flac = fileBytes(folder / "minute.flac");
    const auto wav = fileBytes(folder / "minute.wav");
    std::filesystem::remove_all(folder);
    REQUIRE(elara::audio::decodeFlac(flac).has_value());
    BENCHMARK("decode 1 min of 16-bit stereo FLAC") {
        return elara::audio::decodeFlac(flac)->samples.size();
    };
    BENCHMARK("decode the same minute as WAV") {
        return elara::audio::decodeWav(wav)->samples.size();
    };
}
