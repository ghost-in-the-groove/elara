// SPDX-License-Identifier: AGPL-3.0-or-later
// The edits a Document understands (ADR-0005). Note-level edits replace the
// measure that holds them; structure has its own commands.
#pragma once

#include <elara/core/document.hpp>
#include <elara/core/shared_value.hpp>
#include <elara/core/tuning.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
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
    [[nodiscard]] std::size_t heldBytes() const noexcept override;

private:
    std::size_t at_;
    std::size_t count_;
    std::vector<MeasureHeader> headers_;
    // Track by track, staff by staff, shared as the song held them (ADR-0051).
    std::vector<SharedValue<Measure>> measures_;
    // The song's measure layouts, then each track's, as they were (ADR-0152).
    std::vector<MeasureLayouts> layouts_;
};

/// Inserts a track before track `at` (see Song::insertTrack).
class InsertTrack final : public Command {
public:
    InsertTrack(std::size_t at, Track track) : at_(at), track_(std::move(track)) {}
    void apply(Song& song) override;
    void revert(Song& song) override;
    [[nodiscard]] std::string_view name() const noexcept override { return "Add track"; }
    [[nodiscard]] std::size_t heldBytes() const noexcept override;

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
    [[nodiscard]] std::size_t heldBytes() const noexcept override;

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
    /// Puts in `content` as it is shared (ADR-0051), copying none of it: a
    /// pasted measure (ADR-0160).
    ReplaceMeasure(std::size_t track, std::size_t staff, std::size_t measure,
                   SharedValue<Measure> content, std::string name = "Edit measure")
        : track_(track), staff_(staff), measure_(measure), content_(std::move(content)),
          name_(std::move(name)) {}
    void apply(Song& song) override;
    void revert(Song& song) override { swapWith(song); }
    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    [[nodiscard]] std::size_t heldBytes() const noexcept override;
    /// Sound unchanged when the measures differ only in what does not sound:
    /// chord symbols, beat texts and lyrics.
    [[nodiscard]] Changes changes() const noexcept override { return changes_; }

private:
    void swapWith(Song& song);

    std::size_t track_;
    std::size_t staff_;
    std::size_t measure_;
    // The new content before apply(), the old one after, shared as the song
    // held it (ADR-0051): the swap copies neither.
    SharedValue<Measure> content_;
    std::string name_;
    Changes changes_;
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
    /// Sound unchanged when only the section name or the double barline did.
    [[nodiscard]] Changes changes() const noexcept override { return changes_; }

private:
    std::size_t measure_;
    MeasureHeader header_;
    std::string_view name_ = "Change measure header";
    Changes changes_;
};

/// Replaces the title, artist and other song information.
class SetSongInfo final : public Command {
public:
    explicit SetSongInfo(SongInfo info) : info_(std::move(info)) {}
    void apply(Song& song) override { std::swap(song.info(), info_); }
    void revert(Song& song) override { std::swap(song.info(), info_); }
    [[nodiscard]] std::string_view name() const noexcept override { return "Edit song info"; }
    /// The credits are drawn; none of it sounds.
    [[nodiscard]] Changes changes() const noexcept override {
        return {.notation = true, .sound = false};
    }

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
    /// The mix is neither drawn nor part of the sequence: the player applies it.
    [[nodiscard]] Changes changes() const noexcept override {
        return {.notation = false, .sound = false};
    }

private:
    void swapWith(Song& song);

    std::size_t track_;
    TrackMix mix_;
};

/// Gives a track another name, as it is written left of its staves and in
/// the lists of tracks. An empty name leaves the track without one.
class SetTrackName final : public Command {
public:
    SetTrackName(std::size_t track, std::string name) : track_(track), name_(std::move(name)) {}
    void apply(Song& song) override { swapWith(song); }
    void revert(Song& song) override { swapWith(song); }
    [[nodiscard]] std::string_view name() const noexcept override { return "Rename track"; }
    /// The name is drawn; it is not part of the sequence.
    [[nodiscard]] Changes changes() const noexcept override {
        return {.notation = true, .sound = false};
    }

private:
    void swapWith(Song& song);

    std::size_t track_;
    std::string name_;
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

/// Replaces the measure layouts of one scope as a whole (ADR-0152). Changes
/// what is drawn, never what sounds.
class SetMeasureLayouts final : public Command {
public:
    SetMeasureLayouts(LayoutScope scope, MeasureLayouts layouts,
                      std::string_view name = "Change breaks")
        : scope_(scope), layouts_(std::move(layouts)), name_(name) {}
    void apply(Song& song) override { swapWith(song); }
    void revert(Song& song) override { swapWith(song); }
    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    [[nodiscard]] std::size_t heldBytes() const noexcept override;
    [[nodiscard]] Changes changes() const noexcept override {
        return {.notation = true, .sound = false};
    }

private:
    void swapWith(Song& song);

    LayoutScope scope_;
    MeasureLayouts layouts_; // the new ones before apply(), the old ones after
    std::string_view name_;
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
    [[nodiscard]] std::size_t heldBytes() const noexcept override;
    /// What any of its commands changes.
    [[nodiscard]] Changes changes() const noexcept override;

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
/// Changes the triplet feel (ADR-0104) from measure `from` until the next
/// change of it, as one undo step ("Change triplet feel"); nullptr when it is
/// already that. Throws std::out_of_range for a missing measure.
[[nodiscard]] std::unique_ptr<Command> changeTripletFeel(const Song& song, std::size_t from,
                                                         TripletFeel feel);
[[nodiscard]] std::unique_ptr<Command> changeKeySignature(const Song& song, std::size_t from,
                                                          KeySignature key);

/// Sets the tempo that starts measure `measure`, in beats per minute of the
/// beat the song counts in there (a dotted quarter in 6/8 stays one), or with
/// no `bpm` takes it away; it holds until the next tempo change. Changes later
/// in the measure stay. One undo step; null when nothing would change. The
/// first measure plays at the song's own tempo unless a change starts it:
/// there the edit sets the song's tempo, in the beat it counts in, and none
/// changes nothing (ADR-0042). Throws std::out_of_range when the song has no
/// measure `measure`, and std::invalid_argument for a bpm outside the range of
/// Tempo.
[[nodiscard]] std::unique_ptr<Command> changeTempo(const Song& song, std::size_t measure,
                                                   std::optional<double> bpm);

/// Makes the tempo change that starts `measure` gradual, moving to the next
/// change's tempo, or sudden again (ADR-0082), as one undo step ("Change
/// tempo"); a measure without one starts one at the tempo in force. Null when
/// nothing changes. Throws std::out_of_range when the song has no measure
/// `measure`, and std::invalid_argument for the first measure, which starts at
/// the song's own tempo.
[[nodiscard]] std::unique_ptr<Command> changeTempoGradual(const Song& song, std::size_t measure,
                                                          bool gradual);

/// Sets one line of the song's credits to `text` (empty clears it), as one
/// undo step, "Edit song info"; null when it reads `text` already.
[[nodiscard]] std::unique_ptr<Command> changeCredit(const Song& song, Credit credit,
                                                    std::string text);

/// Names the section that starts at measure `measure` (ADR-0062); an empty
/// name takes it off. One undo step, "Change section"; null when the measure
/// has that name already. Throws std::out_of_range when the song has no
/// measure `measure`.
[[nodiscard]] std::unique_ptr<Command> changeSection(const Song& song, std::size_t measure,
                                                     std::string name);

// The marks of a measure header, a group at a time (ADR-0123): each takes the
// whole group, checks it and is one undo step named as SetMeasureHeader names
// a change of that group alone. Each is null when the measure has those marks
// already, and throws std::out_of_range ("Song: no such measure") when the song
// has no measure `measure`.

/// Gives measure `measure` exactly the signs and the jump of `marks`
/// (ADR-0049), "Change navigation marks". Without a jump nothing ends the
/// music early: `jumpThen` is taken as JumpThen::End. Throws
/// std::invalid_argument for a jump or a jumpThen that is no enumerator.
[[nodiscard]] std::unique_ptr<Command> changeNavigation(const Song& song, std::size_t measure,
                                                        const NavigationMarks& marks);

/// Gives measure `measure` exactly the repeat barlines and alternate endings
/// of `marks` (ADR-0016), "Change repeats". Throws std::invalid_argument, as
/// checkRepeatTimes() does, for a number of times other than 0 (no end
/// repeat) or 2 to 255.
[[nodiscard]] std::unique_ptr<Command> changeRepeats(const Song& song, std::size_t measure,
                                                     const RepeatMarks& marks);

/// Closes measure `measure` with a double barline or a single one (ADR-0103),
/// "Change barline"; it does not change the sound.
[[nodiscard]] std::unique_ptr<Command> changeBarline(const Song& song, std::size_t measure,
                                                     bool doubleBar);

/// Sets the break before each measure of `breaks`, {measure, break}, in the
/// measure layouts of `scope` (ADR-0152), as one undo step, "Change breaks";
/// a later entry for the same measure wins. Null when every measure has its
/// break already. Throws std::out_of_range for a missing track or measure and
/// std::invalid_argument for a break that is no enumerator.
[[nodiscard]] std::unique_ptr<Command>
changeBreaks(const Song& song, LayoutScope scope,
             std::span<const std::pair<std::size_t, Break>> breaks);

} // namespace elara::core
