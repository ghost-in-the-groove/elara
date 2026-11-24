// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/commands.hpp>

#include <stdexcept>
#include <utility>
#include <vector>

namespace elara::core {

// ---- Measures ----------------------------------------------------------------

void InsertMeasures::apply(Song& song) {
    song.insertMeasures(at_, count_);
}

void InsertMeasures::revert(Song& song) {
    song.removeMeasures(at_, count_);
}

void RemoveMeasures::apply(Song& song) {
    const std::size_t total = song.measureCount();
    if (at_ > total || count_ > total - at_) {
        throw std::out_of_range("RemoveMeasures: range outside the song");
    }
    std::size_t staves = 0;
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        staves += song.track(t).staffCount();
    }
    // Allocate before moving anything out, so nothing can fail half-way.
    headers_.reserve(count_);
    measures_.reserve(staves * count_);

    for (std::size_t m = at_; m < at_ + count_; ++m) {
        headers_.push_back(std::move(song.measureHeader(m)));
    }
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        Track& track = song.track(t);
        for (std::size_t s = 0; s < track.staffCount(); ++s) {
            for (std::size_t m = at_; m < at_ + count_; ++m) {
                measures_.push_back(std::move(track.staff(s).measure(m)));
            }
        }
    }
    song.removeMeasures(at_, count_);
}

void RemoveMeasures::revert(Song& song) {
    song.insertMeasures(at_, count_);
    for (std::size_t i = 0; i < count_; ++i) {
        song.measureHeader(at_ + i) = std::move(headers_[i]);
    }
    auto next = measures_.begin();
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        Track& track = song.track(t);
        for (std::size_t s = 0; s < track.staffCount(); ++s) {
            for (std::size_t m = at_; m < at_ + count_; ++m) {
                track.staff(s).measure(m) = std::move(*next++);
            }
        }
    }
    headers_.clear();
    measures_.clear();
}

// ---- Tracks ------------------------------------------------------------------

void InsertTrack::apply(Song& song) {
    if (at_ > song.trackCount()) {
        throw std::out_of_range("InsertTrack: index past the end");
    }
    song.insertTrack(at_, std::move(*track_));
    track_.reset();
}

void InsertTrack::revert(Song& song) {
    track_ = song.takeTrack(at_);
}

void RemoveTrack::apply(Song& song) {
    track_ = song.takeTrack(index_);
}

void RemoveTrack::revert(Song& song) {
    song.insertTrack(index_, std::move(*track_));
    track_.reset();
}

// ---- Content -----------------------------------------------------------------

void ReplaceMeasure::swapWith(Song& song) {
    Measure& target = song.track(track_).staff(staff_).measure(measure_); // throws first
    std::swap(target, content_);
}

void SetMeasureHeader::apply(Song& song) {
    MeasureHeader& target = song.measureHeader(measure_);
    const bool time = target.timeSignature != header_.timeSignature;
    const bool key = target.keySignature != header_.keySignature;
    const bool tempo = target.tempoChanges != header_.tempoChanges;
    // The target with the new signs and jumps: equal to the new header when
    // nothing else changes.
    MeasureHeader marks = target;
    marks.segno = header_.segno;
    marks.segnoSegno = header_.segnoSegno;
    marks.coda = header_.coda;
    marks.doubleCoda = header_.doubleCoda;
    marks.fine = header_.fine;
    marks.toCoda = header_.toCoda;
    marks.toDoubleCoda = header_.toDoubleCoda;
    marks.jump = header_.jump;
    marks.jumpThen = header_.jumpThen;
    const bool navigation = marks != target;
    // The same with the new repeats and alternate endings (ADR-0016).
    MeasureHeader repeats = target;
    repeats.repeatStart = header_.repeatStart;
    repeats.repeatTimes = header_.repeatTimes;
    repeats.alternateEndings = header_.alternateEndings;
    const bool repeated = repeats != target;
    const bool marked = navigation || repeated;
    if (navigation && marks == header_) {
        name_ = "Change navigation marks";
    } else if (repeated && repeats == header_) {
        name_ = "Change repeats";
    } else if (!marked && time && !key && !tempo) {
        name_ = "Change time signature";
    } else if (!marked && key && !time && !tempo) {
        name_ = "Change key signature";
    } else if (!marked && tempo && !time && !key) {
        name_ = "Change tempo";
    }
    std::swap(target, header_);
}

void SetMeasureHeader::revert(Song& song) {
    std::swap(song.measureHeader(measure_), header_);
}

void SetStartTempo::swapWith(Song& song) noexcept {
    const Tempo previous = song.startTempo();
    song.setStartTempo(tempo_);
    tempo_ = previous;
}

void SetTrackMix::swapWith(Song& song) {
    Track& track = song.track(track_);
    const TrackMix previous = track.mix();
    track.setMix(mix_);
    mix_ = previous;
}

void SetStaffTuning::swapWith(Song& song) {
    Staff& staff = song.track(track_).staff(staff_);
    std::optional<Tuning> previous = staff.tuning();
    const int previousCapo = staff.capo();
    staff.setTuning(std::move(tuning_), capo_);
    tuning_ = std::move(previous);
    capo_ = previousCapo;
}

// ---- Groups ------------------------------------------------------------------

CommandGroup::CommandGroup(std::string name, std::vector<std::unique_ptr<Command>> commands)
    : name_(std::move(name)), commands_(std::move(commands)) {
    for (const auto& command : commands_) {
        if (!command) {
            throw std::invalid_argument("CommandGroup: null command");
        }
    }
}

void CommandGroup::apply(Song& song) {
    std::size_t applied = 0;
    try {
        for (; applied < commands_.size(); ++applied) {
            commands_[applied]->apply(song);
        }
    } catch (...) {
        while (applied > 0) {
            commands_[--applied]->revert(song);
        }
        throw;
    }
}

void CommandGroup::revert(Song& song) {
    for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
        (*it)->revert(song);
    }
}

// ---- Signatures --------------------------------------------------------------

namespace {

// One SetMeasureHeader per measure from `from` while `field` of its header
// equals the value it had at `from`, each setting `field` to `value`.
template <typename Value, typename Field>
std::unique_ptr<Command> changeRun(const Song& song, std::size_t from, const Value& value,
                                   Field field, std::string name) {
    if (from >= song.measureCount()) {
        throw std::out_of_range(name + ": no such measure");
    }
    const Value old = song.measureHeader(from).*field;
    if (old == value) {
        return nullptr;
    }
    std::vector<std::unique_ptr<Command>> steps;
    for (std::size_t m = from; m < song.measureCount() && song.measureHeader(m).*field == old;
         ++m) {
        MeasureHeader header = song.measureHeader(m);
        header.*field = value;
        steps.push_back(std::make_unique<SetMeasureHeader>(m, std::move(header)));
    }
    return std::make_unique<CommandGroup>(std::move(name), std::move(steps));
}

} // namespace

std::unique_ptr<Command> changeTimeSignature(const Song& song, std::size_t from,
                                             TimeSignature time) {
    return changeRun(song, from, time, &MeasureHeader::timeSignature, "Change time signature");
}

std::unique_ptr<Command> changeKeySignature(const Song& song, std::size_t from, KeySignature key) {
    return changeRun(song, from, key, &MeasureHeader::keySignature, "Change key signature");
}

// ---- Tempo -------------------------------------------------------------------

std::unique_ptr<Command> changeTempo(const Song& song, std::size_t measure,
                                     std::optional<double> bpm) {
    if (measure >= song.measureCount()) {
        throw std::out_of_range("Change tempo: no such measure");
    }
    if (measure == 0) {
        throw std::invalid_argument("Change tempo: the first measure plays at the song's tempo");
    }
    MeasureHeader header = song.measureHeader(measure);
    std::erase_if(header.tempoChanges,
                  [](const TempoChange& change) { return change.offset == Fraction{}; });
    if (bpm) {
        // Tempo checks the range.
        const Tempo tempo(*bpm, song.tempoAt(measure).beatUnit());
        header.tempoChanges.insert(header.tempoChanges.begin(),
                                   TempoChange{.offset = {}, .tempo = tempo});
    }
    if (header == song.measureHeader(measure)) {
        return nullptr;
    }
    std::vector<std::unique_ptr<Command>> steps;
    steps.push_back(std::make_unique<SetMeasureHeader>(measure, std::move(header)));
    return std::make_unique<CommandGroup>("Change tempo", std::move(steps));
}

std::unique_ptr<Command> changeCredit(const Song& song, Credit credit, std::string text) {
    if (creditOf(song.info(), credit) == text) {
        return nullptr;
    }
    SongInfo info = song.info();
    creditOf(info, credit) = std::move(text);
    return std::make_unique<SetSongInfo>(std::move(info));
}

} // namespace elara::core
