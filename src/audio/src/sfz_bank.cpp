// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/audio/flac_reader.hpp>
#include <elara/audio/sfz_bank.hpp>
#include <elara/core/instrument.hpp>
#include <elara/core/parallel.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <map>
#include <numbers>
#include <set>
#include <utility>

namespace elara::audio {

/// A sample in memory, mono. Generators are one looping period, played at the
/// frequency of the region's key centre.
struct SfzSample {
    std::vector<float> frames;
    double rate = 0.0;      ///< frames per second (0 for a generator)
    bool generator = false; ///< loops over all its frames, pitched from its period
};

/// Everything a bank's sounds share: the regions, their samples, and which
/// regions each key can strike. Never changes once made.
struct SfzData {
    std::string name;
    SfzProgram program;
    std::vector<SfzSample> samples;
    std::vector<std::uint32_t> regionSample;            ///< per region
    std::array<std::vector<std::uint32_t>, 128> onKey;  ///< regions struck by a key going down
    std::array<std::vector<std::uint32_t>, 128> offKey; ///< regions struck by a key coming up
    std::array<bool, 128> switches{};                   ///< keys in a keyswitch range
    int defaultSwitch = -1;
    SfzTechniques techniques;
    std::vector<std::string> missing;
    std::size_t bytes = 0;
};

namespace {

constexpr std::size_t kVoices = 64;
constexpr std::size_t kGeneratorFrames = 2048;
constexpr double kSilent = 1e-4;   // -80 dB: a release ends here
constexpr double kFastOff = 0.006; // seconds a voice silenced by its group takes
constexpr double kMinRelease = 0.001;

double keyFrequency(double key) noexcept {
    return 440.0 * std::exp2((key - 69.0) / 12.0);
}

double decibels(double db) noexcept {
    return std::pow(10.0, db / 20.0);
}

// The gain of a velocity on a region's curve: its amp_velcurve_N points with
// straight lines between them, or the square of the velocity.
double velocityCurve(const SfzRegion& region, int velocity) noexcept {
    const double v = std::clamp(velocity, 0, 127);
    if (region.ampVelcurve.empty()) {
        return (v * v) / (127.0 * 127.0);
    }
    double fromVelocity = 0.0;
    double fromGain = 0.0;
    for (const auto& [atVelocity, gain] : region.ampVelcurve) {
        if (v <= atVelocity) {
            const double span = atVelocity - fromVelocity;
            return span <= 0.0 ? gain : fromGain + (gain - fromGain) * (v - fromVelocity) / span;
        }
        fromVelocity = atVelocity;
        fromGain = gain;
    }
    const double span = 127.0 - fromVelocity;
    return span <= 0.0 ? fromGain : fromGain + (1.0 - fromGain) * (v - fromVelocity) / span;
}

double velocityGain(const SfzRegion& region, int velocity) noexcept {
    const double track = std::clamp(region.ampVeltrack / 100.0, -1.0, 1.0);
    const double curve =
        track >= 0.0 ? velocityCurve(region, velocity) : velocityCurve(region, 128 - velocity);
    const double amount = std::abs(track);
    return (1.0 - amount) + amount * curve;
}

// One coefficient per frame that takes a level down by `ratio` over `frames`.
double coefficient(double ratio, double frames) noexcept {
    return frames <= 1.0 ? 0.0 : std::exp(std::log(ratio) / frames);
}

enum class Stage : std::uint8_t { Off, Delay, Attack, Hold, Decay, Sustain, Release };

struct Voice {
    Stage stage = Stage::Off;
    int key = -1;
    bool held = false;      ///< its key is down and it follows the key
    bool loops = false;     ///< it wraps from loopEnd to loopStart...
    bool loopsHeld = false; ///< ...only while its key is held (loop_sustain)
    bool oneShot = false;   ///< it ignores its key coming up
    std::uint32_t region = 0;
    std::int64_t offBy = 0;
    bool offFast = true;
    std::uint64_t order = 0; ///< when it started, for taking the oldest
    const float* frames = nullptr;
    double end = 0.0; ///< one past the last frame it plays
    double loopStart = 0.0;
    double loopEnd = 0.0; ///< one past the last frame of the loop
    double position = 0.0;
    double step = 0.0;     ///< frames per output frame at its pitch now
    double baseStep = 0.0; ///< ...with no glide
    bool periodic = false; ///< a generator: its frames repeat both ways
    double centsTarget = 0.0;
    double stepFactor = 1.0; ///< a glide multiplies the step by this each frame
    std::int64_t glide = 0;  ///< frames of glide left
    float gain = 0.0f;
    // The envelope.
    double level = 0.0;
    std::int64_t stageLeft = 0;
    double attackStep = 0.0;
    std::int64_t holdFrames = 0;
    std::int64_t decayFrames = 0;
    double decayCoefficient = 0.0;
    double sustain = 1.0;
    double releaseCoefficient = 0.0;
    // A touch: a damping that shortens the note and a low-pass that darkens it.
    double damp = 1.0;
    double dampCoefficient = 1.0;
    float lowPass = 1.0f; ///< 1: open
    float filtered = 0.0f;
};

class SfzSound final : public TrackSound {
public:
    SfzSound(std::shared_ptr<const SfzData> data, double sampleRate)
        : data_(std::move(data)), rate_(sampleRate), sequence_(data_->program.regions.size(), 0),
          switch_(data_->defaultSwitch) {
        for (std::size_t cc = 0; cc < cc_.size(); ++cc) {
            cc_[cc] = data_->program.ccDefaults[cc];
        }
    }

    void noteOn(int key, int velocity, Touch touch) noexcept override {
        if (key < 0 || key > 127) {
            return;
        }
        const auto k = static_cast<std::size_t>(key);
        if (data_->switches[k]) {
            switch_ = key;
        }
        const bool othersHeld = heldCount_ > 0;
        Played played = playing(touch);
        played.chance = random();
        if (!strike(key, velocity, played, othersHeld) && played.touch != touch) {
            // The library's technique has nothing for this key: imitate it.
            played = {.touch = touch, .keyswitch = switch_, .cc = -1, .chance = played.chance};
            strike(key, velocity, played, othersHeld);
        }
        if (!keyDown_[k]) {
            keyDown_[k] = true;
            ++heldCount_;
        }
        keyVelocity_[k] = velocity;
        keyPlayed_[k] = played;
        keyStruck_[k] = frame_;
    }

    void noteOff(int key) noexcept override {
        if (key < 0 || key > 127) {
            return;
        }
        const auto k = static_cast<std::size_t>(key);
        for (Voice& voice : voices_) {
            if (voice.stage != Stage::Off && voice.key == key && voice.held) {
                voice.held = false;
                if (!voice.oneShot) {
                    release(voice);
                }
            }
        }
        if (!keyDown_[k]) {
            return;
        }
        keyDown_[k] = false;
        --heldCount_;
        const double held = static_cast<double>(frame_ - keyStruck_[k]) / rate_;
        const Played& played = keyPlayed_[k];
        for (const std::uint32_t r : data_->offKey[k]) {
            const SfzRegion& region = data_->program.regions[r];
            if (matches(r, region, keyVelocity_[k], played)) {
                start(r, key, keyVelocity_[k], played, decibels(-region.rtDecay * held), false);
            }
        }
    }

    void glide(int key, double cents, double seconds) noexcept override {
        const double frames = std::round(std::clamp(seconds, 0.0, 60.0) * rate_);
        for (Voice& voice : voices_) {
            if (voice.stage == Stage::Off || voice.key != key || !voice.held) {
                continue;
            }
            voice.centsTarget = cents;
            if (frames < 1.0) {
                voice.glide = 0;
                voice.step = voice.baseStep * std::exp2(cents / 1200.0);
            } else {
                // From where the pitch is now, which may be halfway through
                // another glide: a straight line in cents is a constant factor
                // on the step each frame.
                const double now = std::log2(voice.step / voice.baseStep) * 1200.0;
                voice.stepFactor = std::exp2((cents - now) / 1200.0 / frames);
                voice.glide = static_cast<std::int64_t>(frames);
            }
        }
    }

    void releaseAll() noexcept override {
        for (Voice& voice : voices_) {
            if (voice.stage != Stage::Off && voice.stage != Stage::Release) {
                voice.held = false;
                release(voice);
            }
        }
    }

    void reset() noexcept override {
        for (Voice& voice : voices_) {
            voice.stage = Stage::Off;
        }
        keyDown_.fill(false);
        heldCount_ = 0;
    }

    void render(std::span<float> out) noexcept override {
        for (Voice& voice : voices_) {
            if (voice.stage != Stage::Off) {
                play(voice, out);
            }
        }
        frame_ += static_cast<std::int64_t>(out.size());
    }

    [[nodiscard]] std::size_t activeVoices() const noexcept override {
        return static_cast<std::size_t>(std::ranges::count_if(
            voices_, [](const Voice& voice) { return voice.stage != Stage::Off; }));
    }

private:
    // How a note is played: the touch the sampler imitates (Normal when the
    // library plays it), the keyswitch its regions answer to, and a
    // controller the library's technique turns up for it.
    struct Played {
        Touch touch = Touch::Normal;
        int keyswitch = -1;
        int cc = -1;
        double chance = 0.0; ///< the note's random number in [0, 1) (lorand, hirand)

        // A controller's value for this note.
        [[nodiscard]] int value(const std::array<int, 128>& values, int controller) const noexcept {
            return controller == cc ? 127 : values[static_cast<std::size_t>(controller)];
        }
    };

    [[nodiscard]] Played playing(Touch touch) const noexcept {
        const SfzTechniques& techniques = data_->techniques;
        const SfzTechnique* technique = touch == Touch::PalmMute   ? &techniques.palmMute
                                        : touch == Touch::Dead     ? &techniques.dead
                                        : touch == Touch::Harmonic ? &techniques.harmonic
                                                                   : nullptr;
        if (technique == nullptr || !technique->found()) {
            return {.touch = touch, .keyswitch = switch_, .cc = -1, .chance = 0.0};
        }
        return {.touch = Touch::Normal,
                .keyswitch = technique->keyswitch >= 0 ? technique->keyswitch : switch_,
                .cc = technique->cc,
                .chance = 0.0};
    }

    // Starts the regions a key going down plays, played this way; false when
    // no region answers.
    bool strike(int key, int velocity, const Played& played, bool othersHeld) noexcept {
        bool struck = false;
        for (const std::uint32_t r : data_->onKey[static_cast<std::size_t>(key)]) {
            const SfzRegion& region = data_->program.regions[r];
            if (!matches(r, region, velocity, played)) {
                continue;
            }
            if ((region.trigger == SfzTrigger::First && othersHeld) ||
                (region.trigger == SfzTrigger::Legato && !othersHeld)) {
                continue;
            }
            struck = true;
            start(r, key, velocity, played, 1.0, true);
        }
        return struck;
    }

    // Whether a region plays a note of this velocity now: its velocity range,
    // controllers, keyswitch and turn in its round robin.
    bool matches(std::uint32_t r, const SfzRegion& region, int velocity,
                 const Played& played) noexcept {
        if (velocity < region.loVel || velocity > region.hiVel) {
            return false;
        }
        for (const SfzCcRange& range : region.ccRanges) {
            if (range.cc >= 0 && range.cc < 128) {
                const int value = played.value(cc_, range.cc);
                if (value < range.lo || value > range.hi) {
                    return false;
                }
            }
        }
        if (region.swLast >= 0 && region.swLast != played.keyswitch) {
            return false;
        }
        if (played.chance < region.loRand || played.chance >= region.hiRand) {
            return false;
        }
        if (region.seqLength > 1) {
            const std::uint32_t turn = sequence_[r]++;
            return static_cast<int>(turn % static_cast<std::uint32_t>(region.seqLength)) ==
                   region.seqPosition - 1;
        }
        return true;
    }

    void start(std::uint32_t r, int key, int velocity, const Played& played, double extraGain,
               bool follows) noexcept {
        const SfzRegion& region = data_->program.regions[r];
        const Touch touch = played.touch;
        if (region.group != 0) {
            for (Voice& other : voices_) {
                if (other.stage != Stage::Off && other.offBy == region.group) {
                    other.held = false;
                    if (other.offFast) {
                        other.releaseCoefficient = coefficient(kSilent, kFastOff * rate_);
                        other.stage = Stage::Release;
                    } else {
                        release(other);
                    }
                }
            }
        }
        // A region its controllers turn down to nothing (a unison layer while
        // its switch is off) takes no voice.
        double audible = region.amplitude;
        for (const SfzCcModulation& modulation : region.ccModulations) {
            if (modulation.target == SfzTarget::Amplitude) {
                audible *= modulation.depth * played.value(cc_, modulation.cc);
            }
        }
        if (audible <= 0.0 || extraGain <= 0.0) {
            return;
        }
        const SfzSample& sample = data_->samples[data_->regionSample[r]];
        const auto frames = static_cast<double>(sample.frames.size());
        Voice& voice = freeVoice();
        voice = Voice{};
        voice.key = key;
        voice.held = follows;
        voice.region = r;
        voice.offBy = region.offBy;
        voice.offFast = region.offFast;
        voice.order = ++order_;
        voice.frames = sample.frames.data();

        double sampleRate = sample.rate;
        if (sample.generator) {
            sampleRate = static_cast<double>(kGeneratorFrames) * keyFrequency(region.keyCenter);
            voice.end = frames;
            voice.loops = true;
            voice.periodic = true;
            voice.loopStart = 0.0;
            voice.loopEnd = frames;
        } else {
            voice.end =
                region.end >= 0 ? std::min(frames, static_cast<double>(region.end) + 1.0) : frames;
            voice.position = std::clamp(static_cast<double>(region.offset), 0.0, voice.end);
            const bool looping = region.loopMode == SfzLoopMode::Continuous ||
                                 region.loopMode == SfzLoopMode::Sustain;
            voice.loopStart =
                region.loopStart >= 0
                    ? std::clamp(static_cast<double>(region.loopStart), 0.0, voice.end)
                    : 0.0;
            voice.loopEnd =
                region.loopEnd >= 0
                    ? std::clamp(static_cast<double>(region.loopEnd) + 1.0, 0.0, voice.end)
                    : voice.end;
            voice.loops = looping && voice.loopEnd - voice.loopStart >= 2.0;
            voice.loopsHeld = region.loopMode == SfzLoopMode::Sustain;
            voice.oneShot = region.loopMode == SfzLoopMode::OneShot;
        }
        // What the controllers move, at their values now.
        double amplitude = region.amplitude / 100.0;
        double volume = region.volume;
        double tune = region.tune;
        double delay = region.ampegDelay;
        double attackTime = region.ampegAttack;
        double hold = region.ampegHold;
        double decay = region.ampegDecay;
        double sustain = region.ampegSustain;
        double releaseTime = region.ampegRelease;
        for (const SfzCcModulation& modulation : region.ccModulations) {
            const double amount = modulation.depth * played.value(cc_, modulation.cc) / 127.0;
            switch (modulation.target) {
            case SfzTarget::Amplitude:
                amplitude *= amount / 100.0;
                break;
            case SfzTarget::Volume:
                volume += amount;
                break;
            case SfzTarget::Tune:
                tune += amount;
                break;
            case SfzTarget::AmpegDelay:
                delay += amount;
                break;
            case SfzTarget::AmpegAttack:
                attackTime += amount;
                break;
            case SfzTarget::AmpegHold:
                hold += amount;
                break;
            case SfzTarget::AmpegDecay:
                decay += amount;
                break;
            case SfzTarget::AmpegSustain:
                sustain += amount;
                break;
            case SfzTarget::AmpegRelease:
                releaseTime += amount;
                break;
            }
        }

        const double semitones = (key - region.keyCenter) * region.pitchKeytrack / 100.0 +
                                 region.transpose + tune / 100.0;
        voice.baseStep = sampleRate / rate_ * std::exp2(semitones / 12.0);
        voice.step = voice.baseStep;

        double gain = decibels(volume) * amplitude * velocityGain(region, velocity) *
                      decibels(region.ampKeytrack * (key - region.ampKeycenter)) * extraGain;
        double t60 = 0.0;
        double cutoff = 0.0;
        switch (touch) {
        case Touch::Normal:
            break;
        case Touch::PalmMute: // the picking hand damps the string: darker and short
            t60 = 0.3;
            cutoff = 900.0;
            gain *= 0.85;
            break;
        case Touch::Dead: // a muted string: a click that dies at once
            t60 = 0.04;
            cutoff = 2500.0;
            gain *= 0.6;
            break;
        case Touch::Harmonic: // a node touched: only the partial, softer
            gain *= 0.8;
            break;
        }
        voice.gain = static_cast<float>(gain);
        if (t60 > 0.0) {
            voice.dampCoefficient = coefficient(1e-3, t60 * rate_);
        }
        if (cutoff > 0.0) {
            voice.lowPass =
                static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * cutoff / rate_));
        }

        // The envelope, in frames.
        const auto framesOf = [this](double seconds) {
            return static_cast<std::int64_t>(std::llround(std::max(seconds, 0.0) * rate_));
        };
        voice.sustain = std::clamp(sustain / 100.0, 0.0, 1.0);
        const std::int64_t attack = framesOf(attackTime);
        voice.attackStep = attack > 0 ? 1.0 / static_cast<double>(attack) : 1.0;
        voice.holdFrames = framesOf(hold);
        voice.decayFrames = framesOf(decay);
        voice.decayCoefficient = coefficient(1e-3, static_cast<double>(voice.decayFrames));
        voice.releaseCoefficient = coefficient(kSilent, std::max(releaseTime, kMinRelease) * rate_);
        voice.stageLeft = framesOf(delay);
        voice.stage = voice.stageLeft > 0 ? Stage::Delay : Stage::Attack;
        // A release region has no key to wait for: one that would loop for
        // ever (a generator, a loop) is let go as soon as it starts.
        if (!follows && voice.loops && !voice.loopsHeld) {
            release(voice);
        }
    }

    // The voice a new note takes: a silent one, or else the quietest released
    // one, or else the oldest.
    Voice& freeVoice() noexcept {
        Voice* best = nullptr;
        for (Voice& voice : voices_) {
            if (voice.stage == Stage::Off) {
                return voice;
            }
            if (voice.stage == Stage::Release &&
                (best == nullptr || best->stage != Stage::Release || voice.level < best->level)) {
                best = &voice;
            } else if (best == nullptr ||
                       (best->stage != Stage::Release && voice.order < best->order)) {
                best = &voice;
            }
        }
        return *best;
    }

    // The next number of the sound's own generator, in [0, 1): the same
    // sequence for every sound, so that the output depends only on the calls
    // (xorshift64*).
    double random() noexcept {
        random_ ^= random_ >> 12U;
        random_ ^= random_ << 25U;
        random_ ^= random_ >> 27U;
        const std::uint64_t bits = random_ * 0x2545F4914F6CDD1DULL;
        return static_cast<double>(bits >> 11U) * 0x1.0p-53;
    }

    static void release(Voice& voice) noexcept {
        if (voice.stage != Stage::Off) {
            voice.stage = Stage::Release;
        }
    }

    // The envelope's level for the next frame.
    static double envelope(Voice& voice) noexcept {
        switch (voice.stage) {
        case Stage::Off:
            return 0.0;
        case Stage::Delay:
            if (--voice.stageLeft <= 0) {
                voice.stage = Stage::Attack;
            }
            return 0.0;
        case Stage::Attack:
            voice.level += voice.attackStep;
            if (voice.level >= 1.0) {
                voice.level = 1.0;
                voice.stage = Stage::Hold;
                voice.stageLeft = voice.holdFrames;
            }
            return voice.level;
        case Stage::Hold:
            if (--voice.stageLeft < 0) {
                voice.stage = Stage::Decay;
                voice.stageLeft = voice.decayFrames;
            }
            return voice.level;
        case Stage::Decay:
            voice.level = voice.sustain + (voice.level - voice.sustain) * voice.decayCoefficient;
            if (--voice.stageLeft <= 0) {
                voice.level = voice.sustain;
                voice.stage = Stage::Sustain;
            }
            return voice.level;
        case Stage::Sustain:
            if (voice.sustain <= 0.0) {
                voice.stage = Stage::Off;
            }
            return voice.level;
        case Stage::Release:
            voice.level *= voice.releaseCoefficient;
            if (voice.level < kSilent) {
                voice.stage = Stage::Off;
                return 0.0;
            }
            return voice.level;
        }
        return 0.0;
    }

    // The sample at a fractional position: four-point Hermite between the
    // frames around it, which wrap at the loop while it loops.
    float at(const Voice& voice, double position, bool looping) const noexcept {
        // A position is never negative, so truncating floors it.
        const auto index = static_cast<std::size_t>(position);
        const auto base = static_cast<double>(index);
        const auto t = static_cast<float>(position - base);
        float y0 = 0.0f;
        float y1 = 0.0f;
        float y2 = 0.0f;
        float y3 = 0.0f;
        if (index >= 1 && base + 2.0 < (looping ? voice.loopEnd : voice.end)) {
            // Inside the sample, and the loop: four frames in a row, as nearly
            // every frame of a note reads them.
            const float* frames = voice.frames + (index - 1);
            y0 = frames[0];
            y1 = frames[1];
            y2 = frames[2];
            y3 = frames[3];
        } else {
            const auto frame = [&](double spot) -> float {
                if (looping && spot >= voice.loopEnd) {
                    spot -= voice.loopEnd - voice.loopStart;
                } else if (voice.periodic && spot < 0.0) {
                    spot += voice.loopEnd; // a generator's period has no start
                }
                if (spot < 0.0 || spot >= voice.end) {
                    return 0.0f;
                }
                return voice.frames[static_cast<std::size_t>(spot)];
            };
            y0 = frame(base - 1.0);
            y1 = frame(base);
            y2 = frame(base + 1.0);
            y3 = frame(base + 2.0);
        }
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    void play(Voice& voice, std::span<float> out) const noexcept {
        for (float& sample : out) {
            const double level = envelope(voice);
            if (voice.stage == Stage::Off) {
                return;
            }
            const bool looping = voice.loops && (!voice.loopsHeld || voice.held);
            float value = at(voice, voice.position, looping) * voice.gain *
                          static_cast<float>(level * voice.damp);
            if (voice.lowPass < 1.0f) {
                voice.filtered += voice.lowPass * (value - voice.filtered);
                value = voice.filtered;
            }
            sample += value;
            voice.damp *= voice.dampCoefficient;
            if (voice.glide > 0) {
                voice.step *= voice.stepFactor;
                if (--voice.glide == 0) {
                    voice.step = voice.baseStep * std::exp2(voice.centsTarget / 1200.0);
                }
            }
            voice.position += voice.step;
            if (looping && voice.position >= voice.loopEnd) {
                while (voice.position >= voice.loopEnd) {
                    voice.position -= voice.loopEnd - voice.loopStart;
                }
            } else if (voice.position >= voice.end) {
                voice.stage = Stage::Off;
                return;
            }
            if (voice.damp < kSilent * kSilent) {
                voice.stage = Stage::Off;
                return;
            }
        }
    }

    std::shared_ptr<const SfzData> data_;
    double rate_;
    std::array<Voice, kVoices> voices_{};
    std::vector<std::uint32_t> sequence_; ///< per region: notes it has matched
    std::array<int, 128> cc_{};
    int switch_ = -1;
    std::array<bool, 128> keyDown_{};
    std::array<int, 128> keyVelocity_{};
    std::array<Played, 128> keyPlayed_{};
    std::array<std::int64_t, 128> keyStruck_{};
    int heldCount_ = 0;
    std::int64_t frame_ = 0;
    std::uint64_t order_ = 0;
    std::uint64_t random_ = 0x9E3779B97F4A7C15ULL;
};

// The samples a program names, read on every core (the program's worker
// pool). The result holds them in the order of `paths`.
std::vector<std::expected<WavAudio, std::string>>
readSamples(const std::vector<std::filesystem::path>& paths) {
    std::vector<std::expected<WavAudio, std::string>> results(
        paths.size(), std::unexpected(std::string("not read")));
    core::forEachIndex(0, paths.size(), core::threadsFor(0, paths.size(), 2, paths.size()),
                       [&](std::size_t i) { results[i] = readAudioFile(paths[i]); });
    return results;
}

// Whether a library's name for a control holds one of `words`, in any case.
bool names(std::string_view label, std::initializer_list<std::string_view> words) {
    std::string lower(label);
    std::ranges::transform(lower, lower.begin(), [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    });
    return std::ranges::any_of(
        words, [&lower](std::string_view word) { return lower.find(word) != std::string::npos; });
}

// Whether a region's choice or its sound depends on controller `cc`.
bool listensTo(const SfzProgram& program, int cc) {
    return std::ranges::any_of(program.regions, [cc](const SfzRegion& region) {
        return std::ranges::any_of(region.ccRanges,
                                   [cc](const SfzCcRange& range) { return range.cc == cc; }) ||
               std::ranges::any_of(region.ccModulations, [cc](const SfzCcModulation& modulation) {
                   return modulation.cc == cc;
               });
    });
}

// The library's keyswitches and controllers for the touches, by their names:
// the first keyswitch named for each, and for a palm mute without one, a
// controller named for muting that some region listens to.
SfzTechniques findTechniques(const SfzProgram& program) {
    SfzTechniques found;
    for (const SfzRegion& region : program.regions) {
        if (region.swLast < 0 || region.swLabel.empty()) {
            continue;
        }
        SfzTechnique* technique = nullptr;
        if (names(region.swLabel, {"harm"})) {
            technique = &found.harmonic;
        } else if (names(region.swLabel, {"dead", "ghost"})) {
            technique = &found.dead;
        } else if (names(region.swLabel, {"palm", "mute"})) {
            technique = &found.palmMute;
        }
        if (technique != nullptr && !technique->found()) {
            *technique = {.keyswitch = region.swLast, .cc = -1, .label = region.swLabel};
        }
    }
    for (int cc = 0; cc < 128 && !found.palmMute.found(); ++cc) {
        const std::string& label = program.ccLabels[static_cast<std::size_t>(cc)];
        if (names(label, {"palm", "mut"}) && listensTo(program, cc)) {
            found.palmMute = {.keyswitch = -1, .cc = cc, .label = label};
        }
    }
    return found;
}

std::vector<float> generator(std::string_view name) {
    if (name == "*sine") {
        std::vector<float> frames(kGeneratorFrames);
        for (std::size_t i = 0; i < frames.size(); ++i) {
            frames[i] =
                static_cast<float>(std::sin(2.0 * std::numbers::pi * static_cast<double>(i) /
                                            static_cast<double>(kGeneratorFrames)));
        }
        return frames;
    }
    if (name == "*silence") {
        return std::vector<float>(kGeneratorFrames, 0.0f);
    }
    return {};
}

} // namespace

std::expected<std::shared_ptr<const SfzBank>, std::string>
SfzBank::open(const std::filesystem::path& path, std::shared_ptr<const SoundBank> others,
              SfzTracks tracks) {
    auto program = readSfz(path);
    if (!program) {
        return std::unexpected(program.error());
    }
    auto bank = load(*std::move(program), path.parent_path(), path.stem().string(),
                     std::move(others), tracks);
    if (!bank) {
        return std::unexpected(std::format("{}: {}", path.generic_string(), bank.error()));
    }
    return bank;
}

std::expected<std::shared_ptr<const SfzBank>, std::string>
SfzBank::load(SfzProgram program, const std::filesystem::path& folder, std::string name,
              std::shared_ptr<const SoundBank> others, SfzTracks tracks) {
    auto data = std::make_shared<SfzData>();
    data->name = std::move(name);

    // Each sample once, by the name the file gives it.
    std::map<std::string, std::uint32_t> indexOf;
    std::vector<std::string> names;
    for (const SfzRegion& region : program.regions) {
        if (indexOf.try_emplace(region.sample, static_cast<std::uint32_t>(names.size())).second) {
            names.push_back(region.sample);
        }
    }
    std::vector<std::filesystem::path> files;
    std::vector<std::size_t> fileOf(names.size(), names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (!names[i].starts_with('*')) {
            fileOf[i] = files.size();
            files.push_back(folder / std::filesystem::path(names[i]));
        }
    }
    auto decoded = readSamples(files);
    data->samples.resize(names.size());
    std::set<std::string> missing;
    std::vector<bool> usable(names.size(), true);
    for (std::size_t i = 0; i < names.size(); ++i) {
        SfzSample& sample = data->samples[i];
        if (names[i].starts_with('*')) {
            sample.frames = generator(names[i]);
            sample.generator = true;
        } else if (auto& wav = decoded[fileOf[i]]; wav && wav->frames() > 0) {
            sample.frames = wav->format.channels == 1 ? std::move(wav->samples) : wav->mono();
            sample.rate = wav->format.sampleRate;
            data->bytes += sample.frames.size() * sizeof(float);
        }
        if (sample.frames.empty()) {
            usable[i] = false;
            missing.insert(names[i]);
        }
    }

    // The regions whose sample was read, and the keys that strike them.
    SfzProgram kept = program;
    kept.regions.clear();
    for (const SfzRegion& region : program.regions) {
        const std::uint32_t sample = indexOf.at(region.sample);
        if (!usable[sample] || region.end == 0) {
            continue;
        }
        const auto r = static_cast<std::uint32_t>(kept.regions.size());
        kept.regions.push_back(region);
        data->regionSample.push_back(sample);
        const int lo = std::clamp(region.loKey, 0, 127);
        const int hi = std::clamp(region.hiKey, 0, 127);
        for (int key = lo; key <= hi; ++key) {
            auto& list = region.trigger == SfzTrigger::Release
                             ? data->offKey[static_cast<std::size_t>(key)]
                             : data->onKey[static_cast<std::size_t>(key)];
            list.push_back(r);
        }
        if (region.swLoKey >= 0 && region.swHiKey >= region.swLoKey) {
            for (int key = region.swLoKey; key <= std::min(region.swHiKey, 127); ++key) {
                data->switches[static_cast<std::size_t>(key)] = true;
            }
        }
        if (region.swLast >= 0 && region.swLast < 128) {
            data->switches[static_cast<std::size_t>(region.swLast)] = true;
        }
        if (data->defaultSwitch < 0 && region.swDefault >= 0) {
            data->defaultSwitch = region.swDefault;
        }
    }
    data->missing.assign(missing.begin(), missing.end());
    if (kept.regions.empty()) {
        return std::unexpected(data->missing.empty() ? std::string("no region with a sample")
                                                     : std::format("no sample could be read: {}",
                                                                   data->missing.front()));
    }
    data->techniques = findTechniques(kept);
    data->program = std::move(kept);
    // Samples no kept region plays are dropped from memory.
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (!usable[i]) {
            data->samples[i] = SfzSample{};
        }
    }
    return std::make_shared<const SfzBank>(std::move(data), std::move(others), tracks);
}

SfzBank::SfzBank(std::shared_ptr<const SfzData> data, std::shared_ptr<const SoundBank> others,
                 SfzTracks tracks)
    : data_(std::move(data)), others_(others ? std::move(others) : builtinBank()), tracks_(tracks) {
}

std::shared_ptr<const SfzBank> SfzBank::with(std::shared_ptr<const SoundBank> others,
                                             SfzTracks tracks) const {
    return std::make_shared<const SfzBank>(data_, std::move(others), tracks);
}

bool SfzBank::plays(const core::Instrument& instrument) const noexcept {
    if (instrument.kind == core::InstrumentKind::Percussion) {
        return false;
    }
    switch (tracks_) {
    case SfzTracks::Pitched:
        return true;
    case SfzTracks::Guitars:
        return instrument.midiProgram >= 24 && instrument.midiProgram <= 31;
    case SfzTracks::Basses:
        return instrument.midiProgram >= 32 && instrument.midiProgram <= 39;
    }
    return false;
}

std::string_view sfzTracksName(SfzTracks tracks) noexcept {
    switch (tracks) {
    case SfzTracks::Pitched:
        return "pitched";
    case SfzTracks::Guitars:
        return "guitars";
    case SfzTracks::Basses:
        return "basses";
    }
    return "pitched";
}

std::optional<SfzTracks> sfzTracksFromName(std::string_view name) noexcept {
    for (const SfzTracks tracks : {SfzTracks::Pitched, SfzTracks::Guitars, SfzTracks::Basses}) {
        if (name == sfzTracksName(tracks)) {
            return tracks;
        }
    }
    return std::nullopt;
}

const std::string& SfzBank::name() const noexcept {
    return data_->name;
}

const SfzProgram& SfzBank::program() const noexcept {
    return data_->program;
}

const std::vector<std::string>& SfzBank::missingSamples() const noexcept {
    return data_->missing;
}

std::size_t SfzBank::sampleCount() const noexcept {
    return static_cast<std::size_t>(std::ranges::count_if(
        data_->samples, [](const SfzSample& s) { return !s.generator && !s.frames.empty(); }));
}

std::size_t SfzBank::sampleBytes() const noexcept {
    return data_->bytes;
}

const SfzTechniques& SfzBank::techniques() const noexcept {
    return data_->techniques;
}

std::unique_ptr<TrackSound> SfzBank::sound(const core::Instrument& instrument,
                                           double sampleRate) const {
    if (!plays(instrument)) {
        return others_->sound(instrument, sampleRate);
    }
    return std::make_unique<SfzSound>(data_, sampleRate);
}

std::string SfzBank::soundName(const core::Instrument& instrument) const {
    if (!plays(instrument)) {
        return others_->soundName(instrument);
    }
    return data_->name;
}

} // namespace elara::audio
