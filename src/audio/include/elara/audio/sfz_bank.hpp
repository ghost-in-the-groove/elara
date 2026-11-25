// SPDX-License-Identifier: AGPL-3.0-or-later
// SFZ instruments as sound banks (ADR-0066): Elara's own sampler plays the
// regions of an .sfz file from samples held in memory.
#pragma once

#include <elara/audio/sfz.hpp>
#include <elara/audio/sound_bank.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace elara::audio {

struct SfzData;

/// The tracks an SFZ instrument plays; another bank plays the rest.
enum class SfzTracks : std::uint8_t {
    Pitched, ///< every track but the percussion ones
    Guitars, ///< tracks of a General MIDI guitar (programs 24-31, from 0)
    Basses,  ///< tracks of a General MIDI bass (programs 32-39)
};

/// "pitched", "guitars" or "basses", as `elara-cli` writes them.
[[nodiscard]] std::string_view sfzTracksName(SfzTracks tracks) noexcept;

/// The tracks a name says; empty for another name.
[[nodiscard]] std::optional<SfzTracks> sfzTracksFromName(std::string_view name) noexcept;

/// A control of an SFZ library that plays a guitar technique: a keyswitch, or
/// a controller turned up to 127 (ADR-0068).
struct SfzTechnique {
    int keyswitch = -1; ///< the key that switches to its regions, or -1
    int cc = -1;        ///< the controller that plays it at 127, or -1
    std::string label;  ///< the library's name for it

    [[nodiscard]] bool found() const noexcept { return keyswitch >= 0 || cc >= 0; }
};

/// The library's own ways of playing the touches the sampler would otherwise
/// imitate, found by the names the library gives its keyswitches
/// (`sw_label`) and controllers (`label_ccN`).
struct SfzTechniques {
    SfzTechnique palmMute; ///< a keyswitch named "palm" or "mute", or a controller "mut"
    SfzTechnique dead;     ///< a keyswitch named "dead" or "ghost"
    SfzTechnique harmonic; ///< a keyswitch named "harm"
};

/// An SFZ instrument as a sound bank. Its sampler plays the tracks it is for
/// (every pitched track, the guitars or the basses), and another bank plays
/// the rest: the built-in synthesizer, a SoundFont or another SFZ instrument.
///
/// Each note plays every region its key, velocity, controllers, keyswitch
/// and round robin pick, through the region's envelope, at the pitch of the
/// key, moved by glides of its own: bends, slides and vibrato touch only the
/// notes of their key. A palm mute, a dead note or a harmonic plays on the
/// library's own keyswitch or controller for it, for that note alone, when
/// the library names one (techniques()); otherwise the sampler darkens,
/// shortens or softens the note as the built-in synthesizer does. Samples are
/// WAV or FLAC files, read into memory as mono when the bank is made, on every
/// core.
class SfzBank final : public SoundBank {
public:
    /// Reads an .sfz file and the samples it names. Samples that cannot be
    /// read are listed in missingSamples() and their regions left out. Fails
    /// with a message when the file cannot be read or no region is left.
    [[nodiscard]] static std::expected<std::shared_ptr<const SfzBank>, std::string>
    open(const std::filesystem::path& path, std::shared_ptr<const SoundBank> others = builtinBank(),
         SfzTracks tracks = SfzTracks::Pitched);

    /// The same from a program already read, its samples found from `folder`.
    [[nodiscard]] static std::expected<std::shared_ptr<const SfzBank>, std::string>
    load(SfzProgram program, const std::filesystem::path& folder, std::string name,
         std::shared_ptr<const SoundBank> others = builtinBank(),
         SfzTracks tracks = SfzTracks::Pitched);

    /// The same instrument, its samples shared, for `tracks`, with the other
    /// tracks on `others`. Nothing is read again.
    [[nodiscard]] std::shared_ptr<const SfzBank> with(std::shared_ptr<const SoundBank> others,
                                                      SfzTracks tracks) const;

    /// The tracks it plays.
    [[nodiscard]] SfzTracks tracks() const noexcept { return tracks_; }

    /// Whether it plays a track of `instrument`, or leaves it to the other bank.
    [[nodiscard]] bool plays(const core::Instrument& instrument) const noexcept;

    /// The instrument's name: the file's, without its extension.
    [[nodiscard]] const std::string& name() const noexcept;

    /// The regions that play, the ones whose sample was read.
    [[nodiscard]] const SfzProgram& program() const noexcept;

    /// The samples that could not be read, as the file names them, sorted.
    [[nodiscard]] const std::vector<std::string>& missingSamples() const noexcept;

    /// How many samples were read, and the bytes they take in memory.
    [[nodiscard]] std::size_t sampleCount() const noexcept;
    [[nodiscard]] std::size_t sampleBytes() const noexcept;

    /// The library's keyswitches and controllers for the touches.
    [[nodiscard]] const SfzTechniques& techniques() const noexcept;

    [[nodiscard]] std::unique_ptr<TrackSound> sound(const core::Instrument& instrument,
                                                    double sampleRate) const override;

    /// The instrument's name, or the other bank's for the tracks it leaves.
    [[nodiscard]] std::string soundName(const core::Instrument& instrument) const override;

    SfzBank(std::shared_ptr<const SfzData> data, std::shared_ptr<const SoundBank> others,
            SfzTracks tracks);

private:
    std::shared_ptr<const SfzData> data_;
    std::shared_ptr<const SoundBank> others_;
    SfzTracks tracks_;
};

} // namespace elara::audio
