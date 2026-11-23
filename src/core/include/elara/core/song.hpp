// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/core/instrument.hpp>
#include <elara/core/measure.hpp>
#include <elara/core/pitch.hpp>
#include <elara/core/signature.hpp>
#include <elara/core/tempo.hpp>
#include <elara/core/tuning.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace elara::core {

/// One staff of a track: its measures and, for fretted instruments, the tuning
/// and capo. Tablature is a view of a fretted staff (ADR-0004). The number of
/// measures is controlled by the Song.
class Staff {
public:
    /// Throws std::invalid_argument for a capo outside 0..Tuning::kMaxFret.
    explicit Staff(Clef clef = Clef::Treble, std::optional<Tuning> tuning = std::nullopt,
                   int capo = 0);

    [[nodiscard]] Clef clef() const noexcept { return clef_; }
    void setClef(Clef clef) noexcept { clef_ = clef; }

    [[nodiscard]] const std::optional<Tuning>& tuning() const noexcept { return tuning_; }
    [[nodiscard]] bool isFretted() const noexcept { return tuning_.has_value(); }
    [[nodiscard]] int capo() const noexcept { return capo_; }

    [[nodiscard]] std::size_t measureCount() const noexcept { return measures_.size(); }

    /// Throws std::out_of_range for a missing measure.
    [[nodiscard]] Measure& measure(std::size_t index);
    [[nodiscard]] const Measure& measure(std::size_t index) const;

    /// The note played at `fret` on `string` (frets relative to the capo), spelled
    /// with `preference`. Throws std::logic_error on a staff without a tuning and
    /// std::out_of_range for a position off the neck.
    [[nodiscard]] Note
    frettedNote(int string, int fret,
                SpellingPreference preference = SpellingPreference::Sharps) const;

    friend bool operator==(const Staff&, const Staff&) = default;

private:
    friend class Song;

    Clef clef_;
    std::optional<Tuning> tuning_;
    int capo_ = 0;
    std::vector<Measure> measures_;
};

/// One instrument with one or more staves.
/// How a track sits in the mix (ADR-0020): what the mixer shows and playback
/// follows. A fader position, not a gain: playback turns it into one.
struct TrackMix {
    /// General MIDI's default channel volume (100 of 127), rounded.
    static constexpr double kDefaultVolume = 0.8;

    double volume = kDefaultVolume; ///< from 0 (silent) to 1
    double pan = 0.0;               ///< from -1 (left) through 0 (centre) to 1 (right)
    bool mute = false;
    bool solo = false;

    friend bool operator==(const TrackMix&, const TrackMix&) = default;
};

class Track {
public:
    /// Throws std::invalid_argument when `staves` is empty.
    Track(std::string name, Instrument instrument, std::vector<Staff> staves);

    /// A new track with the preset's instrument, clefs and tuning.
    [[nodiscard]] static Track fromPreset(const InstrumentPreset& preset);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void setName(std::string name) { name_ = std::move(name); }

    [[nodiscard]] const Instrument& instrument() const noexcept { return instrument_; }

    [[nodiscard]] const TrackMix& mix() const noexcept { return mix_; }
    /// Throws std::invalid_argument when the volume is outside [0, 1] or the pan
    /// outside [-1, 1] (or either is not a number).
    void setMix(const TrackMix& mix);

    [[nodiscard]] std::size_t staffCount() const noexcept { return staves_.size(); }

    /// Throws std::out_of_range for a missing staff.
    [[nodiscard]] Staff& staff(std::size_t index);
    [[nodiscard]] const Staff& staff(std::size_t index) const;

    friend bool operator==(const Track&, const Track&) = default;

private:
    friend class Song;

    std::string name_;
    Instrument instrument_;
    std::vector<Staff> staves_;
    TrackMix mix_;
};

/// Song-wide information about one measure, shared by every staff. Tempo changes
/// and repeats live here so they move with their measure (ADR-0016). Markers
/// and jumps (segno, coda, da capo) come later.
struct MeasureHeader {
    /// Alternate endings name times through a repeat from 1 to this.
    static constexpr int kMaxEnding = 8;

    TimeSignature timeSignature{};
    KeySignature keySignature{};             // concert key
    std::vector<TempoChange> tempoChanges{}; // ascending offsets inside the measure
    /// A start repeat barline opens the measure.
    bool repeatStart = false;
    /// 2 or more when an end repeat barline closes the measure: how many times
    /// its section is played. 0 (or 1) for none.
    std::uint8_t repeatTimes = 0;
    /// The measure belongs to the alternate endings of these times through the
    /// repeat: bit n - 1 for the n-th time. 0 for a measure played every time.
    std::uint8_t alternateEndings = 0;

    /// True when an end repeat barline closes the measure.
    [[nodiscard]] bool repeatEnd() const noexcept { return repeatTimes >= 2; }
    /// True when the measure is played the `time`-th time through its repeat.
    [[nodiscard]] bool playedOnTime(int time) const noexcept {
        return alternateEndings == 0 ||
               (time >= 1 && time <= kMaxEnding &&
                ((static_cast<unsigned>(alternateEndings) >> static_cast<unsigned>(time - 1)) &
                 1U) != 0);
    }

    /// The header a new measure after this one starts with: same meter and key,
    /// no events.
    [[nodiscard]] MeasureHeader continuation() const {
        return MeasureHeader{.timeSignature = timeSignature, .keySignature = keySignature};
    }

    friend bool operator==(const MeasureHeader&, const MeasureHeader&) = default;
};

struct SongInfo {
    std::string title{};
    std::string subtitle{};
    std::string artist{};
    std::string album{};
    std::string words{};
    std::string music{};
    std::string copyright{};
    std::string transcriber{};
    std::string notes{};

    friend bool operator==(const SongInfo&, const SongInfo&) = default;
};

/// The whole document (ADR-0004). Keeps every staff of every track at exactly
/// measureCount() measures.
class Song {
public:
    [[nodiscard]] SongInfo& info() noexcept { return info_; }
    [[nodiscard]] const SongInfo& info() const noexcept { return info_; }

    [[nodiscard]] std::size_t trackCount() const noexcept { return tracks_.size(); }
    [[nodiscard]] std::size_t measureCount() const noexcept { return headers_.size(); }

    /// Appends a track and returns its index. Its staves get measureCount() empty
    /// measures. Throws std::invalid_argument if a staff already holds a different
    /// number of measures (e.g. a track copied from another song).
    std::size_t addTrack(Track track) { return insertTrack(trackCount(), std::move(track)); }

    /// Inserts a track before track `at` (at == trackCount() appends), with the
    /// same rules as addTrack(). Throws std::out_of_range when at > trackCount().
    std::size_t insertTrack(std::size_t at, Track track);

    /// Removes a track and returns it. Throws std::out_of_range for a missing track.
    Track takeTrack(std::size_t index);

    /// Throws std::out_of_range for a missing track.
    void removeTrack(std::size_t index) { (void)takeTrack(index); }
    [[nodiscard]] Track& track(std::size_t index);
    [[nodiscard]] const Track& track(std::size_t index) const;

    /// True when track `index` sounds: it is not muted and, when any track is
    /// soloed, it is one of them. Throws std::out_of_range for a missing track.
    [[nodiscard]] bool audible(std::size_t index) const;

    /// Inserts `count` empty measures before measure `at` (at == measureCount()
    /// appends) in every staff. Their headers continue the measure before them
    /// (or the first measure when inserting at 0). Throws std::out_of_range when
    /// at > measureCount().
    void insertMeasures(std::size_t at, std::size_t count = 1);
    void appendMeasures(std::size_t count) { insertMeasures(measureCount(), count); }

    /// Removes measures [at, at + count) from every staff. Throws
    /// std::out_of_range when the range is not inside the song.
    void removeMeasures(std::size_t at, std::size_t count = 1);

    /// Throws std::out_of_range for a missing measure.
    [[nodiscard]] MeasureHeader& measureHeader(std::size_t index);
    [[nodiscard]] const MeasureHeader& measureHeader(std::size_t index) const;

    // ---- Timeline ----------------------------------------------------------
    // Positions are (measure, offset in whole notes); measure == measureCount()
    // with offset 0 is the end of the song. Positions and times count measures
    // as written; playOrder() unrolls the repeats.

    /// The measures in the order they are played (ADR-0016): a repeat plays its
    /// section as many times as it says, and each time through only the
    /// alternate endings of that time. An end repeat without a start repeat
    /// goes back to the start of the song, or to the start of the section after
    /// the previous repeat. Never longer than 256 times the measures.
    [[nodiscard]] std::vector<std::size_t> playOrder() const;

    /// Tempo in force at the start of the song.
    [[nodiscard]] const Tempo& startTempo() const noexcept { return startTempo_; }
    void setStartTempo(const Tempo& tempo) noexcept { startTempo_ = tempo; }

    /// Start of `measure` in whole notes from the start of the song. Throws
    /// std::out_of_range when measure > measureCount().
    [[nodiscard]] Fraction measureStart(std::size_t measure) const;
    /// Length of the whole song in whole notes.
    [[nodiscard]] Fraction length() const { return measureStart(measureCount()); }

    /// Seconds from the start of the song to a position. Throws
    /// std::out_of_range for a position outside the song.
    [[nodiscard]] double secondsAt(std::size_t measure, const Fraction& offset = {}) const;
    [[nodiscard]] double durationSeconds() const { return secondsAt(measureCount()); }

    /// Tempo in force at a position (a change exactly there counts).
    [[nodiscard]] Tempo tempoAt(std::size_t measure, const Fraction& offset = {}) const;

    friend bool operator==(const Song&, const Song&) = default;

private:
    void checkPosition(std::size_t measure, const Fraction& offset) const;

    SongInfo info_;
    Tempo startTempo_;
    std::vector<MeasureHeader> headers_;
    std::vector<Track> tracks_;
};

} // namespace elara::core
