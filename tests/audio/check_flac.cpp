// SPDX-License-Identifier: AGPL-3.0-or-later
// Checks Elara's FLAC decoder against libFLAC: signals made here are encoded
// by libFLAC (through libsndfile) at several bit depths, channel counts and
// compression levels, then decoded by elara::audio::readFlac, which must give
// every sample back exactly. Leaves a440.flac, a second of A4, for the CLI
// tests. Usage: elara_check_flac DIRECTORY
#include <elara/audio/flac_reader.hpp>

#include <sndfile.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <numbers>
#include <string>
#include <vector>

namespace {

struct Case {
    const char* name;
    int sampleRate;
    int channels;
    int bitDepth;       // 8, 16 or 24
    double compression; // 0 (fast, fixed predictors) to 1 (best, long LPC)
    double seconds;
};

// A deterministic noise source.
class Noise {
public:
    double next() {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(state_ >> 11U) / static_cast<double>(1ULL << 53U) * 2.0 - 1.0;
    }

private:
    std::uint64_t state_ = 0x9E3779B97F4A7C15ULL;
};

// Integer samples of `bitDepth` bits, interleaved: plucked tones with some
// noise, a stretch of silence and a full-scale square, so that the encoder
// uses every subframe kind and stereo decorrelation.
std::vector<int> signal(const Case& c) {
    const auto frames = static_cast<std::size_t>(c.seconds * c.sampleRate);
    const double full = std::ldexp(1.0, c.bitDepth - 1) - 1.0;
    std::vector<int> samples(frames * static_cast<std::size_t>(c.channels));
    Noise noise;
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / c.sampleRate;
        for (int ch = 0; ch < c.channels; ++ch) {
            double value = 0.0;
            if (t < c.seconds * 0.6) {
                const double f = 110.0 * (1.0 + 0.5 * ch);
                for (int h = 1; h <= 6; ++h) {
                    value += std::sin(2.0 * std::numbers::pi * f * h * t) / h;
                }
                value = 0.4 * value * std::exp(-2.0 * t) + 0.01 * noise.next();
            } else if (t > c.seconds * 0.8) {
                value = std::fmod(t * 220.0, 1.0) < 0.5 ? 1.0 : -1.0;
            }
            samples[i * static_cast<std::size_t>(c.channels) + static_cast<std::size_t>(ch)] =
                static_cast<int>(std::lround(std::clamp(value, -1.0, 1.0) * full));
        }
    }
    return samples;
}

bool write(const std::filesystem::path& path, const Case& c, const std::vector<int>& samples) {
    SF_INFO info{};
    info.samplerate = c.sampleRate;
    info.channels = c.channels;
    info.format = SF_FORMAT_FLAC | (c.bitDepth == 8    ? SF_FORMAT_PCM_S8
                                    : c.bitDepth == 16 ? SF_FORMAT_PCM_16
                                                       : SF_FORMAT_PCM_24);
    SNDFILE* file = sf_open(path.string().c_str(), SFM_WRITE, &info);
    if (file == nullptr) {
        std::fprintf(stderr, "cannot write %s: %s\n", path.string().c_str(), sf_strerror(nullptr));
        return false;
    }
    double level = c.compression;
    sf_command(file, SFC_SET_COMPRESSION_LEVEL, &level, sizeof(level));
    // libsndfile takes integers at full 32-bit scale.
    std::vector<int> scaled(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        scaled[i] = static_cast<int>(static_cast<unsigned>(samples[i])
                                     << (32U - static_cast<unsigned>(c.bitDepth)));
    }
    const auto written = sf_write_int(file, scaled.data(), static_cast<sf_count_t>(scaled.size()));
    sf_close(file);
    return written == static_cast<sf_count_t>(scaled.size());
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: elara_check_flac DIRECTORY\n");
        return 2;
    }
    const std::filesystem::path directory = argv[1];
    std::filesystem::create_directories(directory);
    const Case cases[] = {
        {"mono16-fast", 44100, 1, 16, 0.0, 2.0}, {"mono16-best", 44100, 1, 16, 1.0, 2.0},
        {"stereo16", 48000, 2, 16, 0.5, 2.0},    {"stereo24-best", 48000, 2, 24, 1.0, 2.0},
        {"mono8", 22050, 1, 8, 0.5, 1.0},        {"quad24", 96000, 4, 24, 0.5, 0.5},
    };
    bool ok = true;
    for (const Case& c : cases) {
        const auto samples = signal(c);
        const auto path = directory / (std::string(c.name) + ".flac");
        if (!write(path, c, samples)) {
            return 1;
        }
        const auto start = std::chrono::steady_clock::now();
        const auto decoded = elara::audio::readFlac(path);
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (!decoded) {
            std::fprintf(stderr, "%s: %s\n", c.name, decoded.error().c_str());
            ok = false;
            continue;
        }
        std::size_t wrong = 0;
        const double unit = std::ldexp(1.0, c.bitDepth - 1);
        const bool sameShape = decoded->samples.size() == samples.size() &&
                               decoded->format.channels == c.channels &&
                               static_cast<int>(decoded->format.sampleRate) == c.sampleRate;
        for (std::size_t i = 0; sameShape && i < samples.size(); ++i) {
            if (std::lround(static_cast<double>(decoded->samples[i]) * unit) != samples[i]) {
                ++wrong;
            }
        }
        const auto bytes = std::filesystem::file_size(path);
        std::printf(
            "%s: %zu samples in %ju bytes, %zu wrong, decoded in %.2f ms (%.0f x real time)\n",
            c.name, samples.size(), static_cast<std::uintmax_t>(bytes), wrong, seconds * 1000.0,
            c.seconds / seconds);
        if (!sameShape || wrong > 0) {
            std::fprintf(stderr, "%s: the decoded audio differs\n", c.name);
            ok = false;
        }
    }

    // A second of A4 for `elara-cli tune`.
    const Case a440{"a440", 44100, 1, 16, 0.5, 1.0};
    std::vector<int> tone(44100);
    for (std::size_t i = 0; i < tone.size(); ++i) {
        tone[i] = static_cast<int>(std::lround(
            16000.0 * std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(i) / 44100.0)));
    }
    if (!write(directory / "a440.flac", a440, tone)) {
        return 1;
    }
    std::printf("flac check: %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
