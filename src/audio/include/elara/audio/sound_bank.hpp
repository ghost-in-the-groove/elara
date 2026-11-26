// SPDX-License-Identifier: AGPL-3.0-or-later
// Sound banks: where the tracks of a song get their sound (ADR-0043).
#pragma once

#include <elara/audio/synth.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <string>

namespace elara::core {
struct Instrument;
}

namespace elara::audio {

/// The sound of one track as it plays: what the renderer and the player strike
/// the track's notes on, with the calls of Synth.
///
/// Real-time safe: every call but the destructor never allocates, locks, logs,
/// throws or does I/O. The output depends only on the calls and the frames
/// between them, not on how render() splits them.
class TrackSound {
public:
    TrackSound() = default;
    virtual ~TrackSound() = default;
    TrackSound(const TrackSound&) = delete;
    TrackSound& operator=(const TrackSound&) = delete;
    TrackSound(TrackSound&&) = delete;
    TrackSound& operator=(TrackSound&&) = delete;

    /// Strikes a MIDI key (0-127) with a velocity (1-127) and a touch.
    virtual void noteOn(int key, int velocity, Touch touch) noexcept = 0;

    /// Releases every note playing `key`.
    virtual void noteOff(int key) noexcept = 0;

    /// Moves the pitch of every note still holding `key` to `cents` from the
    /// key, in a straight line over `seconds` (at once for 0), as Synth::glide.
    virtual void glide(int key, double cents, double seconds) noexcept = 0;

    /// Sounds the note `cents` above `key` on the string of the notes still
    /// holding `key`, without picking it: a hammer-on, a pull-off or the end
    /// of a legato slide, at `velocity` (ADR-0070). The note keeps answering
    /// to `key`: later glides are from the key, and noteOff(key) releases it.
    /// A sound with no samples of its own for it moves the notes there at
    /// once, as glide(key, cents, 0) does.
    virtual void legato(int key, double cents, [[maybe_unused]] int velocity) noexcept {
        glide(key, cents, 0.0);
    }

    /// Releases every note.
    virtual void releaseAll() noexcept = 0;

    /// Silences every note at once.
    virtual void reset() noexcept = 0;

    /// Adds the next `out.size()` mono frames to `out`.
    virtual void render(std::span<float> out) noexcept = 0;

    /// Voices still sounding, released ones included.
    [[nodiscard]] virtual std::size_t activeVoices() const noexcept = 0;
};

/// Where tracks get their sound: the built-in synthesizer (builtinBank()) or
/// a SoundFont (elara_audio_fluid). Shared between renderers and players and
/// used from several threads at once, so it never changes after it is made.
class SoundBank {
public:
    SoundBank() = default;
    virtual ~SoundBank() = default;
    SoundBank(const SoundBank&) = delete;
    SoundBank& operator=(const SoundBank&) = delete;
    SoundBank(SoundBank&&) = delete;
    SoundBank& operator=(SoundBank&&) = delete;

    /// A new sound for a track of `instrument` at `sampleRate`. Not real-time
    /// safe: it may allocate and read files.
    [[nodiscard]] virtual std::unique_ptr<TrackSound> sound(const core::Instrument& instrument,
                                                            double sampleRate) const = 0;

    /// What the sound for `instrument` is called, as `elara-cli` prints it:
    /// the timbre's name for the built-in synthesizer ("guitar"), the
    /// preset's for a SoundFont.
    [[nodiscard]] virtual std::string soundName(const core::Instrument& instrument) const = 0;

    /// What makes the sound for `instrument`: two banks that give the same
    /// source for an instrument make the same sound for it, so that a player
    /// keeps a track's sound when its bank changes only for other tracks. The
    /// bank itself, unless it hands the instrument to another bank. Compared
    /// only while the bank that gave it is alive.
    [[nodiscard]] virtual const void* source(const core::Instrument& instrument) const noexcept;
};

/// The built-in synthesizer as a sound bank: a Synth in the instrument's
/// timbre (timbreFor()). The bank every renderer and player starts with.
[[nodiscard]] std::shared_ptr<const SoundBank> builtinBank();

} // namespace elara::audio
