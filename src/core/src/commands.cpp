// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/commands.hpp>

#include <stdexcept>
#include <utility>

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
    if (time && !key && !tempo) {
        name_ = "Change time signature";
    } else if (key && !time && !tempo) {
        name_ = "Change key signature";
    } else if (tempo && !time && !key) {
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

} // namespace elara::core
