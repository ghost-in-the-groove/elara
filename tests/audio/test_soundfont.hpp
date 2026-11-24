// SPDX-License-Identifier: AGPL-3.0-or-later
// A tiny SoundFont 2 for tests (ADR-0043), written from the SoundFont 2.01
// specification: one looped 440 Hz sine wave, and presets that play it with
// different root keys, so a test can tell them apart by pitch.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace elara::audio::test {

struct TestPreset {
    std::string name;
    int bank = 0;
    int program = 0;
    int rootKey = 69; ///< the key that plays the sine at 440 Hz
};

/// Every preset a test needs: a piano, a guitar with its muted and harmonics
/// programs (an octave and 19 semitones higher), a bass and a drum kit.
inline std::vector<TestPreset> defaultTestPresets() {
    return {{.name = "Test Piano", .bank = 0, .program = 0, .rootKey = 69},
            {.name = "Test Guitar", .bank = 0, .program = 25, .rootKey = 69},
            {.name = "Test Muted", .bank = 0, .program = 28, .rootKey = 57},
            {.name = "Test Harmonics", .bank = 0, .program = 31, .rootKey = 50},
            {.name = "Test Bass", .bank = 0, .program = 33, .rootKey = 69},
            {.name = "Test Kit", .bank = 128, .program = 0, .rootKey = 69}};
}

namespace detail {

class Bytes {
public:
    void u8(int value) { data_.push_back(static_cast<char>(value & 0xFF)); }
    void u16(int value) {
        u8(value);
        u8(value >> 8);
    }
    void u32(std::uint32_t value) {
        u16(static_cast<int>(value & 0xFFFFU));
        u16(static_cast<int>(value >> 16U));
    }
    void tag(std::string_view fourCc) { data_.append(fourCc.substr(0, 4)); }
    void name(std::string_view text) { // 20 bytes, zero padded
        std::string field(text.substr(0, 19));
        field.resize(20, '\0');
        data_ += field;
    }
    void append(const Bytes& other) { data_ += other.data_; }
    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] const std::string& data() const noexcept { return data_; }

    /// A chunk: its tag, its size and its body, padded to an even size.
    void chunk(std::string_view fourCc, const Bytes& body) {
        tag(fourCc);
        u32(static_cast<std::uint32_t>(body.size()));
        append(body);
        if (body.size() % 2 != 0) {
            u8(0);
        }
    }
    /// A LIST chunk of `type` holding `body`.
    void list(std::string_view type, const Bytes& body) {
        Bytes inner;
        inner.tag(type);
        inner.append(body);
        chunk("LIST", inner);
    }

private:
    std::string data_;
};

} // namespace detail

/// Writes a SoundFont with `presets` to `path`.
inline void writeTestSoundFont(const std::filesystem::path& path,
                               std::span<const TestPreset> presets) {
    using detail::Bytes;
    constexpr int kSampleRate = 44000; // 100 frames per period at 440 Hz
    constexpr int kFrames = 8800;
    constexpr int kLoopStart = 1000;
    constexpr int kLoopEnd = 8000;
    constexpr int kPadding = 46; // zero frames the specification asks after a sample

    Bytes info;
    Bytes version;
    version.u16(2);
    version.u16(1);
    info.chunk("ifil", version);
    Bytes engine;
    engine.tag("EMU8");
    engine.tag("000");
    engine.u8(0);
    info.chunk("isng", engine);
    Bytes title; // zero-terminated, padded inside the chunk to an even size
    title.tag("Test");
    title.u8(0);
    title.u8(0);
    info.chunk("INAM", title);

    Bytes samples;
    for (int i = 0; i < kFrames + kPadding; ++i) {
        const double phase = 2.0 * std::numbers::pi * static_cast<double>(i % 100) / 100.0;
        samples.u16(i < kFrames ? static_cast<int>(std::lround(16000.0 * std::sin(phase))) : 0);
    }
    Bytes sampleData;
    sampleData.chunk("smpl", samples);

    const auto count = static_cast<int>(presets.size());
    Bytes phdr;
    Bytes pbag;
    Bytes pgen;
    Bytes inst;
    Bytes ibag;
    Bytes igen;
    constexpr int kInstrumentGenerators = 4;
    for (int i = 0; i < count; ++i) {
        const TestPreset& preset = presets[static_cast<std::size_t>(i)];
        phdr.name(preset.name);
        phdr.u16(preset.program);
        phdr.u16(preset.bank);
        phdr.u16(i); // its zone
        phdr.u32(0);
        phdr.u32(0);
        phdr.u32(0);
        pbag.u16(i); // its generator
        pbag.u16(0);
        pgen.u16(41); // instrument
        pgen.u16(i);
        inst.name(preset.name);
        inst.u16(i);
        ibag.u16(i * kInstrumentGenerators);
        ibag.u16(0);
        igen.u16(58); // overridingRootKey
        igen.u16(preset.rootKey);
        igen.u16(38); // releaseVolEnv: 0.1 s
        igen.u16(static_cast<std::uint16_t>(-3986));
        igen.u16(54); // sampleModes: loop
        igen.u16(1);
        igen.u16(53); // sampleID, last
        igen.u16(0);
    }
    phdr.name("EOP");
    phdr.u16(0);
    phdr.u16(0);
    phdr.u16(count);
    phdr.u32(0);
    phdr.u32(0);
    phdr.u32(0);
    pbag.u16(count);
    pbag.u16(0);
    pgen.u16(0);
    pgen.u16(0);
    inst.name("EOI");
    inst.u16(count);
    ibag.u16(count * kInstrumentGenerators);
    ibag.u16(0);
    igen.u16(0);
    igen.u16(0);
    Bytes noModulator;
    for (int i = 0; i < 10; ++i) {
        noModulator.u8(0);
    }
    Bytes shdr;
    shdr.name("Sine");
    shdr.u32(0);
    shdr.u32(kFrames);
    shdr.u32(kLoopStart);
    shdr.u32(kLoopEnd);
    shdr.u32(kSampleRate);
    shdr.u8(69); // original pitch
    shdr.u8(0);  // pitch correction
    shdr.u16(0); // sample link
    shdr.u16(1); // mono
    shdr.name("EOS");
    for (int i = 0; i < 26; ++i) {
        shdr.u8(0);
    }

    Bytes hydra;
    hydra.chunk("phdr", phdr);
    hydra.chunk("pbag", pbag);
    hydra.chunk("pmod", noModulator);
    hydra.chunk("pgen", pgen);
    hydra.chunk("inst", inst);
    hydra.chunk("ibag", ibag);
    hydra.chunk("imod", noModulator);
    hydra.chunk("igen", igen);
    hydra.chunk("shdr", shdr);

    Bytes body;
    body.tag("sfbk");
    Bytes lists;
    lists.list("INFO", info);
    lists.list("sdta", sampleData);
    lists.list("pdta", hydra);
    body.append(lists);
    Bytes file;
    file.chunk("RIFF", body);
    std::ofstream out(path, std::ios::binary);
    out.write(file.data().data(), static_cast<std::streamsize>(file.size()));
}

} // namespace elara::audio::test
