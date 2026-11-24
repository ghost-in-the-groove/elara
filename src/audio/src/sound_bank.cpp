// SPDX-License-Identifier: AGPL-3.0-or-later
// The built-in synthesizer as a sound bank (ADR-0043).
#include <elara/audio/sound_bank.hpp>
#include <elara/core/instrument.hpp>

#include <utility>

namespace elara::audio {

namespace {

class BuiltinSound final : public TrackSound {
public:
    BuiltinSound(Timbre timbre, double sampleRate) : synth_(timbre, sampleRate) {}

    void noteOn(int key, int velocity, Touch touch) noexcept override {
        synth_.noteOn(key, velocity, touch);
    }
    void noteOff(int key) noexcept override { synth_.noteOff(key); }
    void glide(int key, double cents, double seconds) noexcept override {
        synth_.glide(key, cents, seconds);
    }
    void releaseAll() noexcept override { synth_.releaseAll(); }
    void reset() noexcept override { synth_.reset(); }
    void render(std::span<float> out) noexcept override { synth_.render(out); }
    [[nodiscard]] std::size_t activeVoices() const noexcept override {
        return synth_.activeVoices();
    }

private:
    Synth synth_;
};

class BuiltinBank final : public SoundBank {
public:
    [[nodiscard]] std::unique_ptr<TrackSound> sound(const core::Instrument& instrument,
                                                    double sampleRate) const override {
        return std::make_unique<BuiltinSound>(timbreFor(instrument), sampleRate);
    }

    [[nodiscard]] std::string soundName(const core::Instrument& instrument) const override {
        return std::string(timbreName(timbreFor(instrument)));
    }
};

} // namespace

std::shared_ptr<const SoundBank> builtinBank() {
    static const std::shared_ptr<const SoundBank> bank = std::make_shared<BuiltinBank>();
    return bank;
}

} // namespace elara::audio
