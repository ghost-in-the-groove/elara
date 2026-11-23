// SPDX-License-Identifier: AGPL-3.0-or-later
// The edits a Document understands (ADR-0005). Note-level edits replace the
// measure that holds them; structure has its own commands.
#pragma once

#include <elara/core/document.hpp>
#include <elara/core/tuning.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elara::core {

/// Inserts empty measures in every staff (see Song::insertMeasures).
class InsertMeasures final : public Command {
public:
    explicit InsertMeasures(std::size_t at, std::size_t count = 1) : at_(at), count_(count) {}
    void apply(Song& song) override;
    void revert(Song& song) override;
    [[nodiscard]] std::string_view name() const noexcept override { return "Insert measures"; }

private:
    std::size_t at_;
    std::size_t count_;
};

/// Removes measures [at, at + count) from every staff, keeping them for undo.
class RemoveMeasures final : public Command {
public:
    explicit RemoveMeasures(std::size_t at, std::size_t count = 1) : at_(at), count_(count) {}
    void apply(Song& song) override;
    void revert(Song& song) override;
    [[nodiscard]] std::string_view name() const noexcept override { return "Remove measures"; }

private:
    std::size_t at_;
    std::size_t count_;
    std::vector<MeasureHeader> headers_;
    std::vector<Measure> measures_; // track by track, staff by staff
};

/// Inserts a track before track `at` (see Song::insertTrack).
class InsertTrack final : public Command {
public:
    InsertTrack(std::size_t at, Track track) : at_(at), track_(std::move(track)) {}
    void apply(Song& song) override;
    void revert(Song& song) override;
    [[nodiscard]] std::string_view name() const noexcept override { return "Add track"; }

private:
    std::size_t at_;
    std::optional<Track> track_; // empty while the song holds it
};

/// Removes a track, keeping it for undo.
class RemoveTrack final : public Command {
public:
    explicit RemoveTrack(std::size_t index) : index_(index) {}
    void apply(Song& song) override;
    void revert(Song& song) override;
    [[nodiscard]] std::string_view name() const noexcept override { return "Remove track"; }

private:
    std::size_t index_;
    std::optional<Track> track_; // set while removed
};

/// Replaces one measure of one staff: the way notes, rests and effects change.
/// `name` says what the edit was, e.g. "Enter note".
class ReplaceMeasure final : public Command {
public:
    ReplaceMeasure(std::size_t track, std::size_t staff, std::size_t measure, Measure content,
                   std::string name = "Edit measure")
        : track_(track), staff_(staff), measure_(measure), content_(std::move(content)),
          name_(std::move(name)) {}
    void apply(Song& song) override { swapWith(song); }
    void revert(Song& song) override { swapWith(song); }
    [[nodiscard]] std::string_view name() const noexcept override { return name_; }

private:
    void swapWith(Song& song);

    std::size_t track_;
    std::size_t staff_;
    std::size_t measure_;
    Measure content_; // the new content before apply(), the old one after
    std::string name_;
};

/// Replaces a measure header: time signature, key signature, tempo changes.
class SetMeasureHeader final : public Command {
public:
    SetMeasureHeader(std::size_t measure, MeasureHeader header)
        : measure_(measure), header_(std::move(header)) {}
    void apply(Song& song) override;
    void revert(Song& song) override;
    /// Names what changed, e.g. "Change time signature", once applied.
    [[nodiscard]] std::string_view name() const noexcept override { return name_; }

private:
    std::size_t measure_;
    MeasureHeader header_;
    std::string_view name_ = "Change measure header";
};

/// Replaces the title, artist and other song information.
class SetSongInfo final : public Command {
public:
    explicit SetSongInfo(SongInfo info) : info_(std::move(info)) {}
    void apply(Song& song) override { std::swap(song.info(), info_); }
    void revert(Song& song) override { std::swap(song.info(), info_); }
    [[nodiscard]] std::string_view name() const noexcept override { return "Edit song info"; }

private:
    SongInfo info_;
};

/// Changes the tempo at the start of the song.
class SetStartTempo final : public Command {
public:
    explicit SetStartTempo(Tempo tempo) : tempo_(tempo) {}
    void apply(Song& song) override { swapWith(song); }
    void revert(Song& song) override { swapWith(song); }
    [[nodiscard]] std::string_view name() const noexcept override { return "Change tempo"; }

private:
    void swapWith(Song& song) noexcept;

    Tempo tempo_;
};

/// Changes how a track sits in the mix: volume, pan, mute and solo.
class SetTrackMix final : public Command {
public:
    SetTrackMix(std::size_t track, TrackMix mix) : track_(track), mix_(mix) {}
    void apply(Song& song) override { swapWith(song); }
    void revert(Song& song) override { swapWith(song); }
    [[nodiscard]] std::string_view name() const noexcept override { return "Change mix"; }

private:
    void swapWith(Song& song);

    std::size_t track_;
    TrackMix mix_;
};

/// Gives a staff another tuning and capo, its notes staying where they are
/// (makeRetune moves them with it, ADR-0046).
class SetStaffTuning final : public Command {
public:
    SetStaffTuning(std::size_t track, std::size_t staff, std::optional<Tuning> tuning, int capo)
        : track_(track), staff_(staff), tuning_(std::move(tuning)), capo_(capo) {}
    void apply(Song& song) override { swapWith(song); }
    void revert(Song& song) override { swapWith(song); }
    [[nodiscard]] std::string_view name() const noexcept override { return "Change tuning"; }

private:
    void swapWith(Song& song);

    std::size_t track_;
    std::size_t staff_;
    std::optional<Tuning> tuning_;
    int capo_;
};

/// Several commands as one undo step, applied in order and reverted in reverse.
/// If one fails, the ones already applied are reverted and the error propagates.
class CommandGroup final : public Command {
public:
    /// Throws std::invalid_argument if a command is null.
    CommandGroup(std::string name, std::vector<std::unique_ptr<Command>> commands);
    void apply(Song& song) override;
    void revert(Song& song) override;
    [[nodiscard]] std::string_view name() const noexcept override { return name_; }

private:
    std::string name_;
    std::vector<std::unique_ptr<Command>> commands_;
};

/// Changes the time signature from measure `from` until the next measure whose
/// time signature differs from the one `from` had: what changing it "here"
/// means in an editor. One undo step; null when nothing would change. Throws
/// std::out_of_range when the song has no measure `from`.
[[nodiscard]] std::unique_ptr<Command> changeTimeSignature(const Song& song, std::size_t from,
                                                           TimeSignature time);

/// The same for the key signature.
[[nodiscard]] std::unique_ptr<Command> changeKeySignature(const Song& song, std::size_t from,
                                                          KeySignature key);

} // namespace elara::core
