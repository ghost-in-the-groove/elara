// SPDX-License-Identifier: AGPL-3.0-or-later
// SoundFonts as sound banks, played through FluidSynth (ADR-0043). FluidSynth
// stays behind this header: nothing of it is visible here.
#pragma once

#include <elara/audio/sound_bank.hpp>

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace elara::audio {

namespace detail {
struct LoadedSoundFont; // the file as FluidSynth loaded it
} // namespace detail

/// One sound of a SoundFont, by its MIDI bank and program.
struct SoundFontPreset {
    int bank = 0;    ///< 128 for drum kits
    int program = 0; ///< 0-127
    std::string name;

    friend bool operator==(const SoundFontPreset&, const SoundFontPreset&) = default;
};

/// A SoundFont file (SF2, or SF3 with compressed samples) as a sound bank.
///
/// Opening loads the presets and instruments once, and every track's sound
/// plays that copy (ADR-0146); a sound reads the samples of its own presets
/// when it is made, so a song loads what it plays. Sounds may outlive the
/// SoundFont and render on different threads.
class SoundFont final : public SoundBank {
public:
    /// Fails with a message naming the file when it is missing or holds no
    /// SoundFont with presets.
    [[nodiscard]] static std::expected<std::shared_ptr<const SoundFont>, std::string>
    open(const std::filesystem::path& path);

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    /// Every preset, by bank, then program. Never empty.
    [[nodiscard]] const std::vector<SoundFontPreset>& presets() const noexcept { return presets_; }

    /// The preset a track of `instrument` plays: its General MIDI program in
    /// bank 0, or in bank 128 for percussion. When the font lacks it: the
    /// program in another bank, then the first program of its General MIDI
    /// family (24-31 for guitars), then the first program of the bank.
    [[nodiscard]] const SoundFontPreset& presetFor(const core::Instrument& instrument) const;

    /// A sound with the instrument's preset, and for guitars the muted
    /// (General MIDI 28) and harmonics (31) presets for those touches. Falls
    /// back to the built-in synthesizer when the file can no longer be read.
    [[nodiscard]] std::unique_ptr<TrackSound> sound(const core::Instrument& instrument,
                                                    double sampleRate) const override;

    /// The name of presetFor(instrument).
    [[nodiscard]] std::string soundName(const core::Instrument& instrument) const override;

private:
    SoundFont(std::filesystem::path path, std::vector<SoundFontPreset> presets,
              std::shared_ptr<detail::LoadedSoundFont> loaded);

    [[nodiscard]] const SoundFontPreset* find(int bank, int program) const noexcept;

    std::filesystem::path path_;
    std::vector<SoundFontPreset> presets_;
    std::shared_ptr<detail::LoadedSoundFont> loaded_;
};

} // namespace elara::audio
