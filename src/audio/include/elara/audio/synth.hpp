// SPDX-License-Identifier: AGPL-3.0-or-later
// The built-in synthesizer (ADR-0018).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace elara::core {
struct Instrument;
}

namespace elara::audio {

/// The sounds of the built-in synthesizer, one for each family of instruments.
enum class Timbre : std::uint8_t {
    Piano,     ///< pianos and electric pianos: struck, decaying partials
    Mallet,    ///< chromatic percussion and timpani: bright, bell-like, decaying
    Organ,     ///< organs, accordions and harmonicas: steady partials
    Guitar,    ///< guitars and other plucked strings
    Overdrive, ///< overdriven and distortion guitars: a plucked string through a clipper
    Bass,      ///< basses: a darker plucked string
    Bowed,     ///< strings, ensembles, pads: a filtered sawtooth with a slow attack
    Wind,      ///< brass, reeds, pipes and synth leads: a filtered sawtooth
    Drums,     ///< percussion: one sound per General MIDI percussion key
};

/// How a note is struck (ADR-0022). Plucked timbres (guitar, overdrive,
/// bass) play each touch differently; the others play every touch as Normal.
enum class Touch : std::uint8_t {
    Normal,
    PalmMute, ///< damped by the picking hand: dark, short
    Dead,     ///< fretting hand muting the string: a short, pitchless click
    Harmonic, ///< only the note's partial sounds: pure and bell-like
};

/// Lower-case name ("palm-mute"), as `elara-cli` prints it.
[[nodiscard]] std::string_view touchName(Touch touch) noexcept;

/// The timbre for an instrument: drums for percussion, otherwise by the
/// family of its General MIDI program.
[[nodiscard]] Timbre timbreFor(const core::Instrument& instrument) noexcept;

/// Lower-case name ("guitar", "drums"), as `elara-cli` prints it.
[[nodiscard]] std::string_view timbreName(Timbre timbre) noexcept;

/// A small polyphonic synthesizer that plays one track in one timbre.
///
/// Real-time safe after construction: noteOn(), noteOff(), releaseAll(),
/// reset() and render() never allocate, lock or throw. The output depends only
/// on the calls and the frames between them, not on how render() splits them.
class Synth {
public:
    static constexpr std::size_t kDefaultVoices = 32;

    Synth(Timbre timbre, double sampleRate, std::size_t voices = kDefaultVoices);
    ~Synth();
    Synth(Synth&&) noexcept;
    Synth& operator=(Synth&&) noexcept;
    Synth(const Synth&) = delete;
    Synth& operator=(const Synth&) = delete;

    [[nodiscard]] Timbre timbre() const noexcept { return timbre_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

    /// Strikes a MIDI key (0-127) with a velocity (1-127) and a touch. When
    /// every voice sounds, the quietest released voice is taken, or else the
    /// oldest.
    void noteOn(int key, int velocity, Touch touch = Touch::Normal) noexcept;

    /// Releases every voice playing `key`. Drums ignore it and ring out.
    void noteOff(int key) noexcept;

    /// Releases every voice.
    void releaseAll() noexcept;

    /// Silences every voice at once.
    void reset() noexcept;

    /// Adds the next `out.size()` mono frames to `out`.
    void render(std::span<float> out) noexcept;

    /// Voices still sounding, released ones included.
    [[nodiscard]] std::size_t activeVoices() const noexcept;

    struct Voice;

private:
    void renderBlock(std::span<float> out) noexcept;

    Timbre timbre_;
    double sampleRate_;
    std::vector<Voice> voices_;
    std::vector<float> strings_; ///< one delay line per voice for plucked strings
    std::size_t stringLength_ = 0;
    std::vector<float> scratch_;
    std::uint64_t struck_ = 0;      ///< notes struck so far, to find the oldest voice
    std::uint32_t minimumLife_ = 0; ///< frames a voice sounds before it may end
    float followerFall_ = 0.0f;     ///< per-frame fall of a voice's level follower
    float toneCoefficient_ = 0.0f;  ///< the overdrive's tone filter
    float toneState_ = 0.0f;
};

} // namespace elara::audio
