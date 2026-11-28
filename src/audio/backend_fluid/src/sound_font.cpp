// SPDX-License-Identifier: AGPL-3.0-or-later
// SoundFonts as sound banks, played through FluidSynth (ADR-0043).
#include <elara/audio/sound_font.hpp>
#include <elara/core/instrument.hpp>

#include <fluidsynth.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

namespace elara::audio {

namespace {

constexpr int kChannels = 16;       // MIDI channels of a track's synth: one per note sounding
constexpr int kTouchChannels = 3;   // of them for each of the guitar's touch presets
constexpr std::int64_t kBlock = 64; // FluidSynth renders in blocks of this many frames
constexpr int kBendRange = 24;      // the pitch wheel's sensitivity, in semitones
// Cents a full turn of the wheel bends: FluidSynth's default pitch wheel
// modulator is 12700 cents times the sensitivity over 128.
constexpr double kBendReach = 12700.0 * kBendRange / 128.0;
constexpr int kBendCentre = 8192;
constexpr int kBendMax = 16383;
constexpr int kPolyphony = 128;
constexpr double kGain = 0.5; // about as loud as the built-in synthesizer on General MIDI fonts
constexpr int kDrumBank = 128;
constexpr int kMutedGuitar = 28; // General MIDI: electric guitar (muted)
constexpr int kGuitarHarmonics = 31;
constexpr int kFirstGuitar = 24;
constexpr int kLastGuitar = 31;
constexpr int kFamilySize = 8;        // General MIDI programs come in families of eight
constexpr double kHostRate = 44100.0; // the synth that loads a font never plays

// The first error FluidSynth logged on this thread since it was cleared, which
// names the cause; nothing is printed. A fixed buffer, since FluidSynth may
// log on the audio thread.
thread_local std::array<char, 256> firstError{};

void keepError(int level, const char* message, void* /*data*/) {
    if (level > FLUID_ERR || message == nullptr || firstError[0] != '\0') {
        return;
    }
    const std::string_view text(message);
    const std::size_t length = std::min(text.size(), firstError.size() - 1);
    std::ranges::copy(text.substr(0, length), firstError.begin());
    firstError[length] = '\0';
}

void quietLogs() {
    static std::once_flag once;
    std::call_once(once, [] {
        for (int level = FLUID_PANIC; level < LAST_LOG_LEVEL; ++level) {
            fluid_set_log_function(level, keepError, nullptr);
        }
    });
}

struct SettingsDeleter {
    void operator()(fluid_settings_t* settings) const noexcept { delete_fluid_settings(settings); }
};
struct SynthDeleter {
    void operator()(fluid_synth_t* synth) const noexcept { delete_fluid_synth(synth); }
};
using SettingsPtr = std::unique_ptr<fluid_settings_t, SettingsDeleter>;
using SynthPtr = std::unique_ptr<fluid_synth_t, SynthDeleter>;

std::string utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

/// A synth and its settings, which outlive it.
struct OwnedSynth {
    SettingsPtr settings;
    SynthPtr synth;
};

/// A synth at `sampleRate` with `polyphony` voices and no SoundFont; none
/// when FluidSynth cannot make it.
std::optional<OwnedSynth> makeSynth(double sampleRate, int polyphony) {
    quietLogs();
    OwnedSynth made{.settings = SettingsPtr(new_fluid_settings()), .synth = nullptr};
    fluid_settings_t* settings = made.settings.get();
    if (settings == nullptr ||
        fluid_settings_setnum(settings, "synth.sample-rate", sampleRate) != FLUID_OK) {
        return std::nullopt;
    }
    fluid_settings_setint(settings, "synth.midi-channels", kChannels);
    fluid_settings_setint(settings, "synth.polyphony", polyphony);
    fluid_settings_setint(settings, "synth.reverb.active", 0);
    fluid_settings_setint(settings, "synth.chorus.active", 0);
    fluid_settings_setint(settings, "synth.dynamic-sample-loading", 1); // only what plays
    fluid_settings_setint(settings, "synth.threadsafe-api", 0);         // no locks
    fluid_settings_setint(settings, "synth.cpu-cores", 1);
    fluid_settings_setint(settings, "synth.lock-memory", 0);
    fluid_settings_setnum(settings, "synth.gain", kGain);
    made.synth = SynthPtr(new_fluid_synth(settings));
    if (!made.synth) {
        return std::nullopt;
    }
    return made;
}

} // namespace

namespace detail {

/// The file's presets and instruments, loaded once into a synth that never
/// plays (the host, which deletes them with itself) and added to the synth of
/// every track's sound (ADR-0146). FluidSynth counts in the font the channels
/// that select each preset and the selected presets that need each sample,
/// and loads and unloads samples by these counts, so `mutex` lets one synth
/// at a time select presets or let them go. Voices use samples without it.
struct LoadedSoundFont {
    OwnedSynth host;
    fluid_sfont_t* font = nullptr;
    std::mutex mutex;
};

} // namespace detail

namespace {

/// Which presets a note plays on: the track's own, or a guitar's muted or
/// harmonics preset.
enum Group : std::uint8_t { kNormal, kMuted, kHarmonics, kGroups };

struct ChannelRange {
    int first = 0;
    int count = 0;
};

/// A track's sound: one FluidSynth synth with sixteen channels on the
/// track's presets, from the SoundFont's shared copy. Each note takes a
/// channel of its own, the one held least recently, so a glide bends only the
/// notes of its key.
class FluidSound final : public TrackSound {
public:
    FluidSound(std::shared_ptr<detail::LoadedSoundFont> font, OwnedSynth synth, double sampleRate,
               bool drums, std::array<ChannelRange, kGroups> groups) noexcept
        : font_(std::move(font)), settings_(std::move(synth.settings)),
          synth_(std::move(synth.synth)), sampleRate_(sampleRate), drums_(drums), groups_(groups) {}

    ~FluidSound() override {
        // Lets the presets go and ends the voices while no other synth
        // selects presets; the font stays loaded for them.
        const std::scoped_lock hold(font_->mutex);
        fluid_synth_remove_sfont(synth_.get(), font_->font);
        synth_.reset();
    }

    FluidSound(const FluidSound&) = delete;
    FluidSound& operator=(const FluidSound&) = delete;
    FluidSound(FluidSound&&) = delete;
    FluidSound& operator=(FluidSound&&) = delete;

    /// Gives each channel the preset of its group, then adds the font, which
    /// selects them all at once: no channel loads the font's first preset on
    /// the way. False when a preset is missing or a sample cannot be read.
    [[nodiscard]] bool select(const std::array<const SoundFontPreset*, kGroups>& presets) noexcept {
        fluid_synth_t* synth = synth_.get();
        for (std::size_t g = 0; g < kGroups; ++g) {
            for (int c = groups_[g].first; c < groups_[g].first + groups_[g].count; ++c) {
                if (drums_) {
                    fluid_synth_set_channel_type(synth, c, CHANNEL_TYPE_DRUM);
                }
                fluid_synth_pitch_wheel_sens(synth, c, kBendRange);
                fluid_synth_bank_select(synth, c, presets[g]->bank);
                fluid_synth_program_change(synth, c, presets[g]->program);
            }
        }
        const std::scoped_lock hold(font_->mutex);
        firstError[0] = '\0';
        const int id = fluid_synth_add_sfont(synth, font_->font);
        if (id == FLUID_FAILED) {
            return false;
        }
        for (std::size_t g = 0; g < kGroups; ++g) {
            for (int c = groups_[g].first; c < groups_[g].first + groups_[g].count; ++c) {
                if (fluid_synth_program_select(synth, c, id, presets[g]->bank,
                                               presets[g]->program) != FLUID_OK) {
                    return false;
                }
            }
        }
        return firstError[0] == '\0'; // FluidSynth says when a sample cannot be read
    }

    void noteOn(int key, int velocity, Touch touch) noexcept override {
        const int c = pick(rangeFor(touch));
        Channel& channel = channels_[static_cast<std::size_t>(c)];
        if (channel.cents != 0.0) {
            bend(c, 0.0); // a new note starts at its key
        }
        channel.length = 0;
        // A dead note is the muted preset struck lightly.
        const int struck = touch == Touch::Dead ? std::max(1, velocity * 2 / 3) : velocity;
        fluid_synth_noteon(synth_.get(), c, std::clamp(key, 0, 127), std::clamp(struck, 1, 127));
        channel.key = key;
        channel.struck = ++struck_;
    }

    void noteOff(int key) noexcept override {
        if (drums_) {
            return; // drums ring out, as on the built-in synthesizer
        }
        for (int c = 0; c < kChannels; ++c) {
            Channel& channel = channels_[static_cast<std::size_t>(c)];
            if (channel.key == key) {
                fluid_synth_noteoff(synth_.get(), c, std::clamp(key, 0, 127));
                channel.key = -1;
                channel.length = 0;
            }
        }
    }

    void glide(int key, double cents, double seconds) noexcept override {
        if (drums_) {
            return;
        }
        const double target = std::clamp(cents, -kBendReach, kBendReach);
        const std::int64_t length = std::llround(std::max(seconds, 0.0) * sampleRate_);
        for (int c = 0; c < kChannels; ++c) {
            Channel& channel = channels_[static_cast<std::size_t>(c)];
            if (channel.key != key) {
                continue;
            }
            if (length <= 0) {
                bend(c, target);
                channel.length = 0;
            } else {
                channel.from = channel.cents;
                channel.to = target;
                channel.length = length;
                channel.done = 0;
            }
        }
    }

    void releaseAll() noexcept override {
        for (int c = 0; c < kChannels; ++c) {
            fluid_synth_all_notes_off(synth_.get(), c);
            channels_[static_cast<std::size_t>(c)].key = -1;
            channels_[static_cast<std::size_t>(c)].length = 0;
        }
    }

    void reset() noexcept override {
        for (int c = 0; c < kChannels; ++c) {
            fluid_synth_all_sounds_off(synth_.get(), c);
            if (channels_[static_cast<std::size_t>(c)].cents != 0.0) {
                bend(c, 0.0);
            }
            channels_[static_cast<std::size_t>(c)] = Channel{};
        }
    }

    void render(std::span<float> out) noexcept override {
        // In pieces that end on FluidSynth's blocks, moving the glides at
        // the start of each block, so the output does not depend on the calls.
        for (std::size_t done = 0; done < out.size();) {
            const std::int64_t offset = frame_ % kBlock;
            if (offset == 0) {
                advanceGlides();
            }
            const std::size_t count =
                std::min(out.size() - done, static_cast<std::size_t>(kBlock - offset));
            float* mono = out.subspan(done).data();
            std::array<float*, 2> buffers{mono, mono}; // left and right mixed into one
            fluid_synth_process(synth_.get(), static_cast<int>(count), 0, nullptr, 2,
                                buffers.data());
            frame_ += static_cast<std::int64_t>(count);
            done += count;
        }
    }

    [[nodiscard]] std::size_t activeVoices() const noexcept override {
        return static_cast<std::size_t>(
            std::max(fluid_synth_get_active_voice_count(synth_.get()), 0));
    }

private:
    struct Channel {
        int key = -1;             ///< the key held, -1 for none
        std::uint64_t struck = 0; ///< when its last note was struck
        double cents = 0.0;       ///< its pitch bend
        double from = 0.0;        ///< a glide: from where...
        double to = 0.0;          ///< ...to where...
        std::int64_t length = 0;  ///< ...over how many frames (0: no glide)
        std::int64_t done = 0;    ///< ...and how many of them are done
    };

    [[nodiscard]] ChannelRange rangeFor(Touch touch) const noexcept {
        const ChannelRange muted = groups_[kMuted];
        const ChannelRange harmonics = groups_[kHarmonics];
        if ((touch == Touch::PalmMute || touch == Touch::Dead) && muted.count > 0) {
            return muted;
        }
        if (touch == Touch::Harmonic && harmonics.count > 0) {
            return harmonics;
        }
        return groups_[kNormal];
    }

    // The channel of `range` free the longest, or else held the longest.
    [[nodiscard]] int pick(ChannelRange range) const noexcept {
        int best = range.first;
        bool bestFree = false;
        for (int c = range.first; c < range.first + range.count; ++c) {
            const Channel& channel = channels_[static_cast<std::size_t>(c)];
            const bool free = channel.key < 0;
            if ((free && !bestFree) ||
                (free == bestFree &&
                 channel.struck < channels_[static_cast<std::size_t>(best)].struck)) {
                best = c;
                bestFree = free;
            }
        }
        return best;
    }

    void bend(int c, double cents) noexcept {
        const auto value = std::clamp(
            static_cast<int>(std::lround(kBendCentre + (cents * kBendCentre / kBendReach))), 0,
            kBendMax);
        fluid_synth_pitch_bend(synth_.get(), c, value);
        channels_[static_cast<std::size_t>(c)].cents = cents;
    }

    void advanceGlides() noexcept {
        for (int c = 0; c < kChannels; ++c) {
            Channel& channel = channels_[static_cast<std::size_t>(c)];
            if (channel.length <= 0) {
                continue;
            }
            channel.done = std::min<std::int64_t>(channel.done + kBlock, channel.length);
            const double reached =
                static_cast<double>(channel.done) / static_cast<double>(channel.length);
            bend(c, channel.from + ((channel.to - channel.from) * reached));
            if (channel.done >= channel.length) {
                channel.length = 0;
            }
        }
    }

    std::shared_ptr<detail::LoadedSoundFont> font_; // outlives the synth
    SettingsPtr settings_;                          // outlives the synth
    SynthPtr synth_;
    double sampleRate_;
    bool drums_;
    std::array<ChannelRange, kGroups> groups_;
    std::array<Channel, kChannels> channels_{};
    std::uint64_t struck_ = 0;
    std::int64_t frame_ = 0;
};

} // namespace

SoundFont::SoundFont(std::filesystem::path path, std::vector<SoundFontPreset> presets,
                     std::shared_ptr<detail::LoadedSoundFont> loaded)
    : path_(std::move(path)), presets_(std::move(presets)), loaded_(std::move(loaded)) {}

std::expected<std::shared_ptr<const SoundFont>, std::string>
SoundFont::open(const std::filesystem::path& path) {
    quietLogs();
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return std::unexpected(std::format("{}: no such file", utf8(path)));
    }
    if (fluid_is_soundfont(utf8(path).c_str()) == 0) {
        return std::unexpected(std::format("{}: not a SoundFont (SF2 or SF3)", utf8(path)));
    }
    auto host = makeSynth(kHostRate, 1);
    if (!host) {
        return std::unexpected(std::string("no SoundFont synthesizer"));
    }
    // No channel of the host selects a preset, so loading reads no samples.
    for (int c = 0; c < kChannels; ++c) {
        fluid_synth_unset_program(host->synth.get(), c);
    }
    firstError[0] = '\0';
    const int id = fluid_synth_sfload(host->synth.get(), utf8(path).c_str(), 0);
    if (id == FLUID_FAILED) {
        const std::string_view reason(firstError.data());
        return std::unexpected(std::format("{}: could not load the SoundFont{}{}", utf8(path),
                                           reason.empty() ? "" : ": ", reason));
    }
    auto loaded = std::make_shared<detail::LoadedSoundFont>();
    loaded->font = fluid_synth_get_sfont_by_id(host->synth.get(), id);
    loaded->host = *std::move(host);
    std::vector<SoundFontPreset> presets;
    fluid_sfont_iteration_start(loaded->font);
    while (fluid_preset_t* preset = fluid_sfont_iteration_next(loaded->font)) {
        const char* name = fluid_preset_get_name(preset);
        presets.push_back({.bank = fluid_preset_get_banknum(preset),
                           .program = fluid_preset_get_num(preset),
                           .name = name != nullptr ? name : ""});
    }
    if (presets.empty()) {
        return std::unexpected(std::format("{}: a SoundFont without presets", utf8(path)));
    }
    std::ranges::sort(presets, {}, [](const SoundFontPreset& preset) {
        return std::pair(preset.bank, preset.program);
    });
    return std::shared_ptr<const SoundFont>(
        new SoundFont(path, std::move(presets), std::move(loaded)));
}

const SoundFontPreset* SoundFont::find(int bank, int program) const noexcept {
    const auto found = std::ranges::find_if(presets_, [&](const SoundFontPreset& preset) {
        return preset.bank == bank && preset.program == program;
    });
    return found != presets_.end() ? &*found : nullptr;
}

const SoundFontPreset& SoundFont::presetFor(const core::Instrument& instrument) const {
    const int program = std::clamp(instrument.midiProgram, 0, 127);
    if (instrument.kind == core::InstrumentKind::Percussion) {
        for (const int kit : {program, 0}) {
            if (const SoundFontPreset* preset = find(kDrumBank, kit)) {
                return *preset;
            }
        }
        const auto drums = std::ranges::find_if(
            presets_, [](const SoundFontPreset& preset) { return preset.bank >= kDrumBank; });
        return drums != presets_.end() ? *drums : presets_.front();
    }
    if (const SoundFontPreset* preset = find(0, program)) {
        return *preset;
    }
    const auto melodic = [](const SoundFontPreset& preset) {
        return preset.bank != kDrumBank;
    };
    for (const SoundFontPreset& preset : presets_) {
        if (melodic(preset) && preset.program == program) {
            return preset;
        }
    }
    const int family = program / kFamilySize * kFamilySize;
    for (int member = family; member < family + kFamilySize; ++member) {
        if (const SoundFontPreset* preset = find(0, member)) {
            return *preset;
        }
    }
    const auto first = std::ranges::find_if(presets_, melodic);
    return first != presets_.end() ? *first : presets_.front();
}

std::string SoundFont::soundName(const core::Instrument& instrument) const {
    return presetFor(instrument).name;
}

std::unique_ptr<TrackSound> SoundFont::sound(const core::Instrument& instrument,
                                             double sampleRate) const {
    const bool drums = instrument.kind == core::InstrumentKind::Percussion;
    const bool guitar =
        !drums && instrument.midiProgram >= kFirstGuitar && instrument.midiProgram <= kLastGuitar;
    const auto touchPreset = [&](int program) -> const SoundFontPreset* {
        if (!guitar) {
            return nullptr;
        }
        const auto found = std::ranges::find_if(presets_, [&](const SoundFontPreset& preset) {
            return preset.bank != kDrumBank && preset.program == program;
        });
        return found != presets_.end() ? &*found : nullptr;
    };
    const std::array<const SoundFontPreset*, kGroups> presets{
        &presetFor(instrument), touchPreset(kMutedGuitar), touchPreset(kGuitarHarmonics)};
    std::array<ChannelRange, kGroups> groups{};
    groups[kMuted].count = presets[kMuted] != nullptr ? kTouchChannels : 0;
    groups[kHarmonics].count = presets[kHarmonics] != nullptr ? kTouchChannels : 0;
    groups[kNormal].count = kChannels - groups[kMuted].count - groups[kHarmonics].count;
    groups[kMuted].first = groups[kNormal].count;
    groups[kHarmonics].first = groups[kMuted].first + groups[kMuted].count;

    if (auto synth = makeSynth(sampleRate, kPolyphony)) {
        auto made =
            std::make_unique<FluidSound>(loaded_, *std::move(synth), sampleRate, drums, groups);
        if (made->select(presets)) {
            return made;
        }
    }
    return builtinBank()->sound(instrument, sampleRate);
}

} // namespace elara::audio
