// SPDX-License-Identifier: AGPL-3.0-or-later
// The built-in synthesizer (ADR-0018).
#include <elara/audio/synth.hpp>
#include <elara/core/instrument.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace elara::audio {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kLn1000 = 6.907755278982137; // a fall of 60 dB, in nepers
constexpr float kSilence = 1.0e-4f;           // -80 dB: a voice below this has ended
constexpr float kTiny = 1.0e-10f;             // amplitudes below this are flushed to zero
constexpr double kMinimumLife = 0.01;         // seconds a voice sounds before it may end
constexpr double kFollowerFall = 0.01;        // seconds for the level follower to fall by 1/e
constexpr double kLowestString = 20.0;        // Hz: sets the length of the delay lines
constexpr std::size_t kBlock = 256;           // frames mixed per pass over the voices
constexpr std::uint32_t kTidyEvery = 1024;    // frames between phasor renormalisations
constexpr std::size_t kMaxPartials = 4;
constexpr std::uint32_t kGlideStep = 32; // frames between pitch updates while a voice glides
constexpr float kVoiceLevel = 0.4f;

double keyFrequency(int key) {
    return 440.0 * std::pow(2.0, (key - 69) / 12.0);
}

// Per-frame multiplier that falls by 60 dB in `seconds`.
float fall60(double seconds, double rate) {
    return static_cast<float>(std::exp(-kLn1000 / (seconds * rate)));
}

// Per-frame multiplier that falls by 1/e in `seconds`.
float fallE(double seconds, double rate) {
    return static_cast<float>(std::exp(-1.0 / (seconds * rate)));
}

// One-pole low-pass coefficient for a cut-off in Hz.
float lowpassCoefficient(double cutoff, double rate) {
    return static_cast<float>(1.0 - std::exp(-kTwoPi * std::min(cutoff, 0.45 * rate) / rate));
}

std::uint32_t nextRandom(std::uint32_t& state) noexcept {
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

// Uniform in [-1, 1).
float whiteNoise(std::uint32_t& state) noexcept {
    return static_cast<float>(static_cast<std::int32_t>(nextRandom(state))) *
           (1.0f / 2147483648.0f);
}

struct DrumSound {
    float toneStart = 0.0f; // Hz; 0 for no tone
    float toneEnd = 0.0f;   // Hz
    float sweep = 0.02f;    // seconds for the tone to fall by 1/e toward its end
    float toneT60 = 0.1f;
    float toneLevel = 0.0f;
    float noiseT60 = 0.1f;
    float noiseLevel = 0.0f;
    float highpass = 3000.0f; // Hz
};

constexpr int kClosedHiHat = 42;
constexpr int kPedalHiHat = 44;
constexpr int kOpenHiHat = 46;

DrumSound tom(int key) {
    const auto f = static_cast<float>(80.0 * std::pow(2.0, (key - 41) / 6.8));
    return {.toneStart = 1.4f * f,
            .toneEnd = f,
            .sweep = 0.05f,
            .toneT60 = 0.6f,
            .toneLevel = 0.9f,
            .noiseT60 = 0.05f,
            .noiseLevel = 0.15f,
            .highpass = 800.0f};
}

DrumSound drumSound(int key) {
    switch (key) {
    case 35: // acoustic bass drum
    case 36: // bass drum
        return {.toneStart = 130.0f,
                .toneEnd = 48.0f,
                .sweep = 0.035f,
                .toneT60 = 0.45f,
                .toneLevel = 1.0f,
                .noiseT60 = 0.02f,
                .noiseLevel = 0.25f,
                .highpass = 1500.0f};
    case 37: // side stick
        return {.toneStart = 420.0f,
                .toneEnd = 400.0f,
                .sweep = 0.01f,
                .toneT60 = 0.06f,
                .toneLevel = 0.4f,
                .noiseT60 = 0.05f,
                .noiseLevel = 0.5f,
                .highpass = 2500.0f};
    case 38: // snare
    case 40: // electric snare
        return {.toneStart = 200.0f,
                .toneEnd = 175.0f,
                .sweep = 0.02f,
                .toneT60 = 0.15f,
                .toneLevel = 0.5f,
                .noiseT60 = 0.25f,
                .noiseLevel = 0.75f,
                .highpass = 1500.0f};
    case 39: // hand clap
        return {.noiseT60 = 0.18f, .noiseLevel = 0.8f, .highpass = 1200.0f};
    case 41: // toms, low to high
    case 43:
    case 45:
    case 47:
    case 48:
    case 50:
        return tom(key);
    case kClosedHiHat:
        return {.noiseT60 = 0.06f, .noiseLevel = 0.45f, .highpass = 7000.0f};
    case kPedalHiHat:
        return {.noiseT60 = 0.08f, .noiseLevel = 0.35f, .highpass = 6000.0f};
    case kOpenHiHat:
        return {.noiseT60 = 0.5f, .noiseLevel = 0.4f, .highpass = 7000.0f};
    case 49: // crash cymbals
    case 57:
        return {.noiseT60 = 1.6f, .noiseLevel = 0.45f, .highpass = 4500.0f};
    case 52: // Chinese cymbal
        return {.toneStart = 520.0f,
                .toneEnd = 500.0f,
                .toneT60 = 0.8f,
                .toneLevel = 0.08f,
                .noiseT60 = 1.4f,
                .noiseLevel = 0.5f,
                .highpass = 3000.0f};
    case 55: // splash cymbal
        return {.noiseT60 = 0.7f, .noiseLevel = 0.45f, .highpass = 5000.0f};
    case 51: // ride cymbals
    case 59:
        return {.toneStart = 3100.0f,
                .toneEnd = 3100.0f,
                .toneT60 = 0.7f,
                .toneLevel = 0.07f,
                .noiseT60 = 0.9f,
                .noiseLevel = 0.25f,
                .highpass = 6000.0f};
    case 53: // ride bell
        return {.toneStart = 2350.0f,
                .toneEnd = 2350.0f,
                .toneT60 = 0.9f,
                .toneLevel = 0.3f,
                .noiseT60 = 0.3f,
                .noiseLevel = 0.1f,
                .highpass = 6000.0f};
    case 54: // tambourine
        return {.noiseT60 = 0.25f, .noiseLevel = 0.4f, .highpass = 6500.0f};
    case 56: // cowbell
        return {.toneStart = 560.0f,
                .toneEnd = 560.0f,
                .toneT60 = 0.3f,
                .toneLevel = 0.45f,
                .noiseT60 = 0.02f,
                .noiseLevel = 0.1f,
                .highpass = 3000.0f};
    default: // everything else: a short, bright burst
        return {.noiseT60 = 0.12f, .noiseLevel = 0.35f, .highpass = 3000.0f};
    }
}

} // namespace

struct Synth::Voice {
    enum class Stage : std::uint8_t { Off, Attack, Held, Released };

    Stage stage = Stage::Off;
    int key = -1;
    std::uint64_t order = 0; // when it was struck, to find the oldest voice
    std::uint32_t age = 0;   // frames since it was struck
    float gain = 0.0f;       // from the velocity
    float level = 0.0f;      // envelope
    float attackStep = 1.0f;
    float release = 0.0f;  // per-frame multiplier once released
    float follower = 0.0f; // peak follower of the voice's output

    // Pitch: an offset in cents from the key, moved by glide().
    double frequency = 0.0; // Hz at no offset
    double cents = 0.0;
    double centsTarget = 0.0;
    double centsStep = 0.0;        // per frame while gliding
    std::uint32_t glide = 0;       // frames left to glide
    std::uint32_t untilRetune = 0; // frames until the next pitch update while gliding

    // Partials (piano, mallet, organ): phasors turned by a fixed rotation.
    std::size_t partials = 0;
    std::array<float, kMaxPartials> re{};
    std::array<float, kMaxPartials> im{};
    std::array<float, kMaxPartials> cosine{};
    std::array<float, kMaxPartials> sine{};
    std::array<float, kMaxPartials> amplitude{};
    std::array<float, kMaxPartials> decay{};
    std::array<double, kMaxPartials> omega{}; // radians per frame at no offset

    // Sawtooth (bowed, wind).
    double phase = 0.0;
    double step = 0.0;
    float lowpass = 0.0f;
    float lowpassCoefficient = 0.0f;

    // Plucked string (guitar, overdrive, bass): Karplus-Strong.
    std::size_t line = 0;     // offset of its delay line
    std::uint32_t length = 0; // whole frames of loop delay
    std::uint32_t read = 0;   // where the line is read: `length` frames behind `write`
    std::uint32_t write = 0;
    float previous = 0.0f;
    float allpass = 0.0f;
    float allpassIn = 0.0f;
    float allpassOut = 0.0f;
    float loopGain = 0.0f;

    // Drum: a swept sine and high-passed noise.
    std::uint32_t seed = 1;
    float noiseLevel = 0.0f;
    float noiseDecay = 0.0f;
    float highpass = 0.0f;
    float highpassIn = 0.0f;
    float highpassOut = 0.0f;
    float tonePhase = 0.0f;
    float toneTarget = 0.0f; // radians per frame
    float toneDelta = 0.0f;  // radians per frame above the target, shrinking
    float toneSweep = 0.0f;
    float toneLevel = 0.0f;
    float toneDecay = 0.0f;
};

namespace {

using Voice = Synth::Voice;
using Stage = Voice::Stage;

struct Context {
    float followerFall;
    std::uint32_t minimumLife;
    float* strings;
    std::uint32_t lineLength; // frames in each delay line
};

// Advances the envelope by one frame and returns its level.
inline float envelope(Voice& voice) noexcept {
    if (voice.stage == Stage::Attack) {
        voice.level += voice.attackStep;
        if (voice.level >= 1.0f) {
            voice.level = 1.0f;
            voice.stage = Stage::Held;
        }
    } else if (voice.stage == Stage::Released) {
        voice.level *= voice.release;
    }
    return voice.level;
}

// Follows the voice's output; true once it has ended.
inline bool ended(Voice& voice, float sample, const Context& context) noexcept {
    const float magnitude = std::abs(sample);
    voice.follower = magnitude > voice.follower ? magnitude : voice.follower * context.followerFall;
    ++voice.age;
    if (voice.age > context.minimumLife && voice.stage != Stage::Attack &&
        voice.follower < kSilence) {
        voice.stage = Stage::Off;
        return true;
    }
    return false;
}

inline float flushed(float value) noexcept {
    return std::abs(value) < kTiny ? 0.0f : value;
}

void tidyPartials(Voice& voice) noexcept {
    for (std::size_t p = 0; p < voice.partials; ++p) {
        const float norm = std::sqrt(voice.re[p] * voice.re[p] + voice.im[p] * voice.im[p]);
        if (norm > 0.0f) {
            voice.re[p] /= norm;
            voice.im[p] /= norm;
        }
        voice.amplitude[p] = flushed(voice.amplitude[p]);
    }
}

void renderPartials(Voice& voice, std::span<float> out, const Context& context) noexcept {
    for (float& sample : out) {
        if (voice.age % kTidyEvery == 0) {
            tidyPartials(voice);
        }
        float sum = 0.0f;
        for (std::size_t p = 0; p < voice.partials; ++p) {
            const float re = voice.re[p] * voice.cosine[p] - voice.im[p] * voice.sine[p];
            const float im = voice.re[p] * voice.sine[p] + voice.im[p] * voice.cosine[p];
            voice.re[p] = re;
            voice.im[p] = im;
            sum += im * voice.amplitude[p];
            voice.amplitude[p] *= voice.decay[p];
        }
        const float value = sum * envelope(voice) * voice.gain;
        sample += value;
        if (ended(voice, value, context)) {
            return;
        }
    }
}

// Band-limited step correction for a sawtooth (PolyBLEP).
inline double polyBlep(double t, double dt) noexcept {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

void renderSaw(Voice& voice, std::span<float> out, const Context& context) noexcept {
    for (float& sample : out) {
        const double saw = 2.0 * voice.phase - 1.0 - polyBlep(voice.phase, voice.step);
        voice.phase += voice.step;
        if (voice.phase >= 1.0) {
            voice.phase -= 1.0;
        }
        voice.lowpass += voice.lowpassCoefficient * (static_cast<float>(saw) - voice.lowpass);
        const float value = voice.lowpass * envelope(voice) * voice.gain;
        sample += value;
        if (ended(voice, value, context)) {
            return;
        }
    }
}

void renderString(Voice& voice, std::span<float> out, const Context& context) noexcept {
    float* line = context.strings + voice.line;
    for (float& sample : out) {
        const float x = line[voice.read];
        const float averaged = 0.5f * (x + voice.previous);
        voice.previous = x;
        const float delayed =
            voice.allpass * averaged + voice.allpassIn - voice.allpass * voice.allpassOut;
        voice.allpassIn = averaged;
        voice.allpassOut = delayed;
        line[voice.write] = delayed * voice.loopGain;
        if (++voice.write == context.lineLength) {
            voice.write = 0;
        }
        if (++voice.read == context.lineLength) {
            voice.read = 0;
        }
        const float value = x * envelope(voice) * voice.gain;
        sample += value;
        if (ended(voice, value, context)) {
            return;
        }
    }
}

// Sets a plucked string's loop delay for `frequency`: the line, half a frame
// of averaging and the all-pass.
void tuneString(Voice& voice, double frequency, double rate, std::size_t lineLength) noexcept {
    const double delay = std::min(rate / frequency, static_cast<double>(lineLength - 1)) - 0.5;
    const double whole = std::max(1.0, std::floor(delay - 0.1));
    const double fraction = std::clamp(delay - whole, 0.1, 1.1);
    voice.length = static_cast<std::uint32_t>(whole);
    voice.allpass = static_cast<float>((1.0 - fraction) / (1.0 + fraction));
}

// Tunes a voice to its key plus its current offset.
void retune(Voice& voice, Timbre timbre, double rate, std::size_t lineLength) noexcept {
    const double ratio = std::exp2(voice.cents / 1200.0);
    switch (timbre) {
    case Timbre::Piano:
    case Timbre::Mallet:
    case Timbre::Organ:
        for (std::size_t p = 0; p < voice.partials; ++p) {
            const double w = std::min(voice.omega[p] * ratio, kTwoPi * 0.45);
            voice.cosine[p] = static_cast<float>(std::cos(w));
            voice.sine[p] = static_cast<float>(std::sin(w));
        }
        break;
    case Timbre::Bowed:
    case Timbre::Wind:
        voice.step = voice.frequency * ratio / rate;
        break;
    case Timbre::Guitar:
    case Timbre::Overdrive:
    case Timbre::Bass: {
        tuneString(voice, voice.frequency * ratio, rate, lineLength);
        const auto line = static_cast<std::uint32_t>(lineLength);
        voice.read = (voice.write + line - voice.length) % line;
        break;
    }
    case Timbre::Drums:
        break;
    }
}

void renderDrum(Voice& voice, std::span<float> out, const Context& context) noexcept {
    for (float& sample : out) {
        if (voice.age % kTidyEvery == 0) {
            voice.toneLevel = flushed(voice.toneLevel);
            voice.noiseLevel = flushed(voice.noiseLevel);
        }
        const float tone = std::sin(voice.tonePhase) * voice.toneLevel;
        voice.tonePhase += voice.toneTarget + voice.toneDelta;
        if (voice.tonePhase >= static_cast<float>(kTwoPi)) {
            voice.tonePhase -= static_cast<float>(kTwoPi);
        }
        voice.toneDelta *= voice.toneSweep;
        voice.toneLevel *= voice.toneDecay;
        const float white = whiteNoise(voice.seed);
        voice.highpassOut = voice.highpass * (voice.highpassOut + white - voice.highpassIn);
        voice.highpassIn = white;
        const float noise = voice.highpassOut * voice.noiseLevel;
        voice.noiseLevel *= voice.noiseDecay;
        const float value = (tone + noise) * envelope(voice) * voice.gain;
        sample += value;
        if (ended(voice, value, context)) {
            return;
        }
    }
}

} // namespace

Timbre timbreFor(const core::Instrument& instrument) noexcept {
    if (instrument.kind == core::InstrumentKind::Percussion) {
        return Timbre::Drums;
    }
    const int program = std::clamp(instrument.midiProgram, 0, 127);
    switch (program) {
    case 29: // overdriven guitar
    case 30: // distortion guitar
        return Timbre::Overdrive;
    case 45:  // pizzicato strings
    case 46:  // harp
    case 104: // sitar
    case 105: // banjo
    case 106: // shamisen
    case 107: // koto
        return Timbre::Guitar;
    case 47:  // timpani
    case 108: // kalimba
        return Timbre::Mallet;
    case 109: // bagpipe
    case 111: // shanai
        return Timbre::Wind;
    default:
        break;
    }
    switch (program / 8) {
    case 0:
        return Timbre::Piano;
    case 1:
    case 14: // percussive
        return Timbre::Mallet;
    case 2:
        return Timbre::Organ;
    case 3:
        return Timbre::Guitar;
    case 4:
        return Timbre::Bass;
    case 7:  // brass
    case 8:  // reed
    case 9:  // pipe
    case 10: // synth lead
        return Timbre::Wind;
    default: // strings, ensembles, pads, effects
        return Timbre::Bowed;
    }
}

std::string_view touchName(Touch touch) noexcept {
    switch (touch) {
    case Touch::Normal:
        return "normal";
    case Touch::PalmMute:
        return "palm-mute";
    case Touch::Dead:
        return "dead";
    case Touch::Harmonic:
        return "harmonic";
    }
    return "normal";
}

std::string_view timbreName(Timbre timbre) noexcept {
    switch (timbre) {
    case Timbre::Piano:
        return "piano";
    case Timbre::Mallet:
        return "mallet";
    case Timbre::Organ:
        return "organ";
    case Timbre::Guitar:
        return "guitar";
    case Timbre::Overdrive:
        return "overdrive";
    case Timbre::Bass:
        return "bass";
    case Timbre::Bowed:
        return "bowed";
    case Timbre::Wind:
        return "wind";
    case Timbre::Drums:
        return "drums";
    }
    return "piano";
}

Synth::Synth(Timbre timbre, double sampleRate, std::size_t voices)
    : timbre_(timbre), sampleRate_(sampleRate), voices_(std::max<std::size_t>(voices, 1)),
      scratch_(kBlock),
      minimumLife_(static_cast<std::uint32_t>(std::lround(kMinimumLife * sampleRate))),
      followerFall_(fallE(kFollowerFall, sampleRate)),
      toneCoefficient_(lowpassCoefficient(3500.0, sampleRate)) {
    const bool plucked =
        timbre == Timbre::Guitar || timbre == Timbre::Overdrive || timbre == Timbre::Bass;
    if (plucked) {
        stringLength_ = static_cast<std::size_t>(std::ceil(sampleRate / kLowestString)) + 2;
        strings_.assign(stringLength_ * voices_.size(), 0.0f);
    }
    for (std::size_t v = 0; v < voices_.size(); ++v) {
        voices_[v].line = v * stringLength_;
    }
}

Synth::~Synth() = default;
Synth::Synth(Synth&&) noexcept = default;
Synth& Synth::operator=(Synth&&) noexcept = default;

void Synth::noteOn(int key, int velocity, Touch touch) noexcept {
    key = std::clamp(key, 0, 127);
    velocity = std::clamp(velocity, 1, 127);
    const double rate = sampleRate_;
    if (timbre_ == Timbre::Drums && (key == kClosedHiHat || key == kPedalHiHat)) {
        for (Voice& voice : voices_) {
            if (voice.stage != Stage::Off && voice.key == kOpenHiHat) {
                voice.stage = Stage::Released;
                voice.release = fall60(0.05, rate);
            }
        }
    }

    // A free voice, else the quietest released one, else the oldest.
    Voice* chosen = nullptr;
    for (Voice& voice : voices_) {
        if (voice.stage == Stage::Off) {
            chosen = &voice;
            break;
        }
        if (chosen == nullptr) {
            chosen = &voice;
            continue;
        }
        const bool released = voice.stage == Stage::Released;
        const bool chosenReleased = chosen->stage == Stage::Released;
        if (released != chosenReleased
                ? released
                : (released ? voice.follower < chosen->follower : voice.order < chosen->order)) {
            chosen = &voice;
        }
    }
    Voice& voice = *chosen;
    const std::size_t lineOffset = voice.line;
    voice = Voice{};
    voice.line = lineOffset;
    voice.key = key;
    voice.order = ++struck_;
    voice.gain = kVoiceLevel * static_cast<float>(std::pow(velocity / 127.0, 1.5));
    const double frequency = keyFrequency(key);
    voice.frequency = frequency;
    const double brightness = velocity / 127.0;

    const auto startAttack = [&](double seconds) {
        voice.stage = Stage::Attack;
        voice.attackStep = static_cast<float>(1.0 / std::max(1.0, seconds * rate));
    };
    const auto startHeld = [&] {
        voice.stage = Stage::Held;
        voice.level = 1.0f;
    };
    const auto addPartial = [&](double ratio, double level, double t60) {
        const double w = kTwoPi * frequency * ratio / rate;
        if (w >= kTwoPi * 0.45 || voice.partials == kMaxPartials) {
            return;
        }
        const std::size_t p = voice.partials++;
        voice.re[p] = 1.0f;
        voice.im[p] = 0.0f;
        voice.cosine[p] = static_cast<float>(std::cos(w));
        voice.sine[p] = static_cast<float>(std::sin(w));
        voice.amplitude[p] = static_cast<float>(level);
        voice.decay[p] = t60 > 0.0 ? fall60(t60, rate) : 1.0f;
        voice.omega[p] = w;
    };
    const auto normalisePartials = [&] {
        float total = 0.0f;
        for (std::size_t p = 0; p < voice.partials; ++p) {
            total += voice.amplitude[p];
        }
        for (std::size_t p = 0; p < voice.partials; ++p) {
            voice.amplitude[p] /= total;
        }
    };

    switch (timbre_) {
    case Timbre::Piano: {
        const double t60 = std::clamp(7.0 * std::pow(110.0 / frequency, 0.7), 0.6, 12.0);
        const double bright = 0.5 + 0.5 * brightness;
        constexpr double kInharmonicity = 0.0002;
        constexpr std::array<double, 4> kLevels{1.0, 0.45, 0.2, 0.1};
        for (std::size_t k = 1; k <= kLevels.size(); ++k) {
            const auto n = static_cast<double>(k);
            addPartial(n * std::sqrt(1.0 + kInharmonicity * n * n),
                       kLevels[k - 1] * std::pow(bright, n - 1.0), t60 / std::pow(n, 0.7));
        }
        normalisePartials();
        startAttack(0.003);
        voice.release = fall60(0.25, rate);
        break;
    }
    case Timbre::Mallet: {
        const double t60 = std::clamp(3.0 * std::sqrt(220.0 / frequency), 0.5, 5.0);
        addPartial(1.0, 1.0, t60);
        addPartial(3.93, 0.35 * brightness, 0.25);
        addPartial(9.52, 0.12 * brightness, 0.12);
        normalisePartials();
        startAttack(0.001);
        voice.release = fall60(1.0, rate);
        break;
    }
    case Timbre::Organ:
        addPartial(1.0, 1.0, 0.0);
        addPartial(2.0, 0.5, 0.0);
        addPartial(3.0, 0.3, 0.0);
        addPartial(4.0, 0.25, 0.0);
        normalisePartials();
        startAttack(0.005);
        voice.release = fall60(0.08, rate);
        break;
    case Timbre::Bowed:
    case Timbre::Wind: {
        const bool bowed = timbre_ == Timbre::Bowed;
        voice.step = frequency / rate;
        voice.lowpassCoefficient = lowpassCoefficient(frequency * (bowed ? 6.0 : 3.5), rate);
        voice.gain *= 0.6f;
        startAttack(bowed ? 0.08 : 0.03);
        voice.release = fall60(bowed ? 0.35 : 0.15, rate);
        break;
    }
    case Timbre::Guitar:
    case Timbre::Overdrive:
    case Timbre::Bass: {
        const bool bass = timbre_ == Timbre::Bass;
        tuneString(voice, frequency, rate, stringLength_);
        double t60 = bass ? std::clamp(6.0 * std::sqrt(55.0 / frequency), 1.0, 10.0)
                          : std::clamp(4.0 * std::sqrt(110.0 / frequency), 0.6, 8.0);
        auto smoothing =
            static_cast<float>(bass ? 0.15 + 0.3 * brightness : 0.3 + 0.6 * brightness);
        switch (touch) {
        case Touch::Normal:
            break;
        case Touch::PalmMute: // the picking hand damps the string: darker and short
            t60 = std::min(t60, bass ? 0.5 : 0.3);
            smoothing *= 0.5f;
            voice.gain *= 0.85f;
            break;
        case Touch::Dead: // a muted string: a bright click that dies at once
            t60 = 0.04;
            smoothing = 0.9f;
            voice.gain *= 0.6f;
            break;
        case Touch::Harmonic: // a node touched: only the partial, ringing longer
            t60 = std::min(1.5 * t60, 10.0);
            voice.gain *= 0.8f;
            break;
        }
        voice.loopGain = static_cast<float>(std::pow(10.0, -3.0 / (t60 * frequency)));

        // Excitation: noise, darker for soft notes and for basses; for a
        // harmonic one period of a sine, which the string keeps pure.
        float* line = strings_.data() + voice.line;
        std::uint32_t seed = 0x9E3779B9U ^ (static_cast<std::uint32_t>(key) * 2654435761U) ^
                             static_cast<std::uint32_t>(struck_);
        float state = 0.0f;
        double mean = 0.0;
        for (std::uint32_t i = 0; i < voice.length; ++i) {
            if (touch == Touch::Harmonic) {
                state = static_cast<float>(
                    std::sin(kTwoPi * static_cast<double>(i) / static_cast<double>(voice.length)));
            } else {
                state += smoothing * (whiteNoise(seed) - state);
            }
            line[i] = state;
            mean += static_cast<double>(state);
        }
        mean /= voice.length;
        float peak = 0.0f;
        for (std::uint32_t i = 0; i < voice.length; ++i) {
            line[i] -= static_cast<float>(mean);
            peak = std::max(peak, std::abs(line[i]));
        }
        if (peak > 0.0f) {
            for (std::uint32_t i = 0; i < voice.length; ++i) {
                line[i] /= peak;
            }
        }
        voice.read = 0;
        voice.write = voice.length;
        startHeld();
        voice.release = fall60(bass ? 0.12 : 0.15, rate);
        break;
    }
    case Timbre::Drums: {
        const DrumSound sound = drumSound(key);
        voice.seed = 0x2545F491U ^ (static_cast<std::uint32_t>(key) << 8U) ^
                     static_cast<std::uint32_t>(struck_);
        voice.noiseLevel = sound.noiseLevel;
        voice.noiseDecay = fall60(static_cast<double>(sound.noiseT60), rate);
        voice.highpass =
            static_cast<float>(1.0 / (1.0 + kTwoPi * static_cast<double>(sound.highpass) / rate));
        if (sound.toneStart > 0.0f) {
            const auto end = static_cast<float>(kTwoPi * static_cast<double>(sound.toneEnd) / rate);
            voice.toneTarget = end;
            voice.toneDelta =
                static_cast<float>(kTwoPi * static_cast<double>(sound.toneStart) / rate) - end;
            voice.toneSweep = fallE(static_cast<double>(sound.sweep), rate);
            voice.toneLevel = sound.toneLevel;
            voice.toneDecay = fall60(static_cast<double>(sound.toneT60), rate);
        }
        startHeld();
        voice.release = fall60(0.05, rate);
        break;
    }
    }
}

void Synth::noteOff(int key) noexcept {
    if (timbre_ == Timbre::Drums) {
        return;
    }
    for (Voice& voice : voices_) {
        if (voice.key == key && (voice.stage == Stage::Attack || voice.stage == Stage::Held)) {
            voice.stage = Stage::Released;
        }
    }
}

void Synth::glide(int key, double cents, double seconds) noexcept {
    if (timbre_ == Timbre::Drums) {
        return;
    }
    const auto frames =
        static_cast<std::uint32_t>(std::lround(std::clamp(seconds, 0.0, 60.0) * sampleRate_));
    for (Voice& voice : voices_) {
        if (voice.key != key || (voice.stage != Stage::Attack && voice.stage != Stage::Held)) {
            continue;
        }
        voice.centsTarget = cents;
        voice.untilRetune = 0;
        if (frames == 0) {
            voice.cents = cents;
            voice.glide = 0;
            retune(voice, timbre_, sampleRate_, stringLength_);
        } else {
            voice.centsStep = (cents - voice.cents) / frames;
            voice.glide = frames;
        }
    }
}

void Synth::releaseAll() noexcept {
    for (Voice& voice : voices_) {
        if (voice.stage == Stage::Attack || voice.stage == Stage::Held) {
            voice.stage = Stage::Released;
        }
    }
}

void Synth::reset() noexcept {
    for (Voice& voice : voices_) {
        voice.stage = Stage::Off;
    }
    toneState_ = 0.0f;
}

std::size_t Synth::activeVoices() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(
        voices_, [](const Voice& voice) { return voice.stage != Stage::Off; }));
}

void Synth::render(std::span<float> out) noexcept {
    while (!out.empty()) {
        const std::size_t n = std::min(out.size(), kBlock);
        renderBlock(out.first(n));
        out = out.subspan(n);
    }
}

void Synth::renderBlock(std::span<float> out) noexcept {
    const std::span<float> mix(scratch_.data(), out.size());
    std::ranges::fill(mix, 0.0f);
    const Context context{.followerFall = followerFall_,
                          .minimumLife = minimumLife_,
                          .strings = strings_.data(),
                          .lineLength = static_cast<std::uint32_t>(stringLength_)};
    const auto renderVoice = [&](Voice& voice, std::span<float> part) {
        switch (timbre_) {
        case Timbre::Piano:
        case Timbre::Mallet:
        case Timbre::Organ:
            renderPartials(voice, part, context);
            break;
        case Timbre::Bowed:
        case Timbre::Wind:
            renderSaw(voice, part, context);
            break;
        case Timbre::Guitar:
        case Timbre::Overdrive:
        case Timbre::Bass:
            renderString(voice, part, context);
            break;
        case Timbre::Drums:
            renderDrum(voice, part, context);
            break;
        }
    };
    for (Voice& voice : voices_) {
        // A gliding voice is retuned every kGlideStep of its own frames, so
        // the output does not depend on how render() splits them.
        std::span<float> rest = mix;
        while (voice.stage != Stage::Off && !rest.empty()) {
            std::size_t n = rest.size();
            if (voice.glide > 0 && voice.untilRetune == 0) {
                const std::uint32_t step = std::min(voice.glide, kGlideStep);
                voice.glide -= step;
                voice.cents =
                    voice.glide == 0 ? voice.centsTarget : voice.cents + (voice.centsStep * step);
                retune(voice, timbre_, sampleRate_, stringLength_);
                voice.untilRetune = kGlideStep;
            }
            if (voice.untilRetune > 0) {
                n = std::min<std::size_t>(n, voice.untilRetune);
                voice.untilRetune -= static_cast<std::uint32_t>(n);
            }
            renderVoice(voice, rest.first(n));
            rest = rest.subspan(n);
        }
    }
    if (timbre_ == Timbre::Overdrive) {
        for (float& sample : mix) {
            const float driven = 0.3f * std::tanh(6.0f * sample);
            toneState_ = flushed(toneState_ + toneCoefficient_ * (driven - toneState_));
            sample = toneState_;
        }
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] += mix[i];
    }
}

} // namespace elara::audio
