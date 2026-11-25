// SPDX-License-Identifier: AGPL-3.0-or-later
// Writes the test SFZ instruments into the folder given, for the elara-cli
// tests that play one (ADR-0066): test-guitar.sfz, the map it includes and
// two plucked-string samples made here, so no sample sits in the repository,
// test-flac.sfz, whose one sample libFLAC encodes (through libsndfile), and
// test-legato.sfz, with a keyswitch for hammer-ons and pull-offs (ADR-0070).
#include <elara/audio/wav_writer.hpp>

#include <sndfile.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

namespace {

// A plucked string, in tune: eight harmonics, each softer than the one below
// and dying away faster.
std::vector<float> pluck(double frequency, double seconds, std::uint32_t rate) {
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

// Writes 16-bit mono FLAC through libsndfile.
bool writeFlac(const std::filesystem::path& path, const std::vector<float>& samples,
               std::uint32_t rate) {
    SF_INFO info{};
    info.samplerate = static_cast<int>(rate);
    info.channels = 1;
    info.format = SF_FORMAT_FLAC | SF_FORMAT_PCM_16;
    SNDFILE* file = sf_open(path.string().c_str(), SFM_WRITE, &info);
    if (file == nullptr) {
        return false;
    }
    const auto count = static_cast<sf_count_t>(samples.size());
    const bool written = sf_writef_float(file, samples.data(), count) == count;
    return sf_close(file) == 0 && written;
}

bool writeText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    return static_cast<bool>(out);
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    if (arguments.size() != 2) {
        std::fputs("usage: elara_make_test_sfz <folder>\n", stderr);
        return 2;
    }
    const std::filesystem::path folder(arguments[1]);
    std::filesystem::create_directories(folder / "samples");
    std::filesystem::create_directories(folder / "maps");
    constexpr std::uint32_t kRate = 22050;
    bool ok = elara::audio::writeWavPcm16(folder / "samples" / "pluck-a2.wav",
                                          pluck(110.0, 1.0, kRate), {.sampleRate = kRate})
                  .has_value();
    ok = ok && elara::audio::writeWavPcm16(folder / "samples" / "pluck-a3.wav",
                                           pluck(220.0, 1.0, kRate), {.sampleRate = kRate})
                   .has_value();
    ok = ok && writeText(folder / "test-guitar.sfz", R"(// The elara-cli tests' SFZ guitar.
<control>
default_path=samples/
label_cc70=Muting
set_cc70=0

<global>
ampeg_release=0.3
lfo01_pitch_oncc1=50 // vibrato: not played by Elara

#include "maps/plucks.sfz"

// Fret noise when a key comes up.
<group> trigger=release rt_decay=6
<region> sample=*silence lokey=0 hikey=127
)");
    ok = ok && writeText(folder / "maps" / "plucks.sfz", R"(<group> hicc70=63
<region> sample=pluck-a2.wav lokey=0 hikey=50 pitch_keycenter=45
<region> sample=pluck-a3.wav lokey=51 hikey=127 pitch_keycenter=57
)");
    ok = ok && writeFlac(folder / "samples" / "pluck-a3.flac", pluck(220.0, 1.0, kRate), kRate);
    ok = ok && writeText(folder / "test-flac.sfz", R"(// A guitar of one FLAC sample.
<region> sample=samples/pluck-a3.flac pitch_keycenter=57
)");
    ok = ok && writeText(folder / "test-legato.sfz", R"(// A guitar whose hammer-ons and pull-offs
// have a keyswitch of their own, an octave up, so that the tests tell them
// from the picked notes.
<global> sw_lokey=24 sw_hikey=25 sw_default=24 lokey=40 hikey=88
<group> sw_last=24 sw_label=Sustain
<region> sample=*sine
<group> sw_last=25 sw_label=Hammer-ons and pull-offs
<region> sample=*sine transpose=12
)");
    return ok ? 0 : 1;
}
