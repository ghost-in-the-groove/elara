// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/commands.hpp>
#include <elara/core/names.hpp>

#include "held_bytes.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
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
    layouts_.reserve(song.trackCount() + 1);
    layouts_.push_back(song.measureLayouts(LayoutScope::song()));
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        layouts_.push_back(song.measureLayouts(LayoutScope::ofTrack(t)));
    }

    for (std::size_t m = at_; m < at_ + count_; ++m) {
        headers_.push_back(std::move(song.measureHeader(m)));
    }
    // The measures are kept as the song shares them: no copy, even while a
    // copy of the song holds them too.
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        const Track& track = std::as_const(song).track(t);
        for (std::size_t s = 0; s < track.staffCount(); ++s) {
            for (std::size_t m = at_; m < at_ + count_; ++m) {
                measures_.push_back(track.staff(s).sharedMeasure(m));
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
                track.staff(s).swapMeasure(m, *next++);
            }
        }
    }
    // As they were before: the insertion gave them default entries.
    song.setMeasureLayouts(LayoutScope::song(), std::move(layouts_[0]));
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        song.setMeasureLayouts(LayoutScope::ofTrack(t), std::move(layouts_[t + 1]));
    }
    headers_.clear();
    measures_.clear();
    layouts_.clear();
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

namespace {

// Whether beats `a` and `b` play the same: they differ at most in what does
// not sound, chord symbols, beat texts and lyrics (measure.hpp).
bool soundTheSame(const Beat& a, const Beat& b) {
    if (a == b) {
        return true;
    }
    Beat x = a;
    Beat y = b;
    for (Beat* beat : {&x, &y}) {
        beat->chord.reset();
        beat->text.reset();
        beat->lyrics.reset();
    }
    return x == y;
}

// Whether measures `a` and `b` play the same. Only beats that differ are
// copied, to be compared without what does not sound.
bool soundTheSame(const Measure& a, const Measure& b) {
    if (a.clef != b.clef || a.repeat != b.repeat) {
        return false;
    }
    for (std::size_t v = 0; v < Measure::kMaxVoices; ++v) {
        const std::vector<Beat>& x = a.voices[v].beats;
        const std::vector<Beat>& y = b.voices[v].beats;
        if (x.size() != y.size()) {
            return false;
        }
        for (std::size_t i = 0; i < x.size(); ++i) {
            if (!soundTheSame(x[i], y[i])) {
                return false;
            }
        }
    }
    return true;
}

// `header` with the signs and the jump of `marks`.
MeasureHeader withNavigation(MeasureHeader header, const NavigationMarks& marks) {
    for (const NavigationSign sign : enumValues<NavigationSign>()) {
        header.*signFlag(sign) = marks.has(sign);
    }
    header.jump = marks.jump;
    header.jumpThen = marks.jumpThen;
    return header;
}

} // namespace

void ReplaceMeasure::apply(Song& song) {
    const Measure& target =
        std::as_const(song).track(track_).staff(staff_).measure(measure_); // throws first
    changes_ = {.notation = true, .sound = !soundTheSame(target, content_.get())};
    swapWith(song);
}

void ReplaceMeasure::swapWith(Song& song) {
    song.track(track_).staff(staff_).swapMeasure(measure_, content_); // throws first
}

void SetMeasureHeader::apply(Song& song) {
    MeasureHeader& target = song.measureHeader(measure_);
    const bool time = target.timeSignature != header_.timeSignature;
    const bool key = target.keySignature != header_.keySignature;
    const bool tempo = target.tempoChanges != header_.tempoChanges;
    // The target with the new signs and jumps: equal to the new header when
    // nothing else changes.
    const MeasureHeader marks = withNavigation(target, navigationOf(header_));
    const bool navigation = marks != target;
    // The same with the new repeats and alternate endings (ADR-0016).
    MeasureHeader repeats = target;
    repeats.repeatStart = header_.repeatStart;
    repeats.repeatTimes = header_.repeatTimes;
    repeats.alternateEndings = header_.alternateEndings;
    const bool repeated = repeats != target;
    // And with the new double barline (ADR-0103).
    MeasureHeader barline = target;
    barline.doubleBar = header_.doubleBar;
    const bool barred = barline != target;
    const bool marked = navigation || repeated || barred;
    if (navigation && marks == header_) {
        name_ = "Change navigation marks";
    } else if (repeated && repeats == header_) {
        name_ = "Change repeats";
    } else if (barred && barline == header_) {
        name_ = "Change barline";
    } else if (!marked && time && !key && !tempo) {
        name_ = "Change time signature";
    } else if (!marked && key && !time && !tempo) {
        name_ = "Change key signature";
    } else if (!marked && tempo && !time && !key) {
        name_ = "Change tempo";
    }
    // The same with the new section name and double barline, which do not
    // sound.
    MeasureHeader silent = target;
    silent.section = header_.section;
    silent.doubleBar = header_.doubleBar;
    changes_ = {.notation = true, .sound = silent != header_};
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

void SetTrackName::swapWith(Song& song) {
    Track& track = song.track(track_);
    std::string previous = track.name();
    track.setName(std::move(name_));
    name_ = std::move(previous);
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

// ---- What the history holds (ADR-0121) -------------------------------------------

std::size_t RemoveMeasures::heldBytes() const noexcept {
    std::size_t bytes = headers_.size() * sizeof(MeasureHeader);
    for (const SharedValue<Measure>& measure : measures_) {
        bytes += approximateBytes(measure.get());
    }
    for (const MeasureLayouts& layouts : layouts_) {
        bytes += detail::sharedBytes(layouts);
    }
    return bytes;
}

std::size_t SetMeasureLayouts::heldBytes() const noexcept {
    return detail::sharedBytes(layouts_);
}

std::size_t InsertTrack::heldBytes() const noexcept {
    return track_ ? approximateBytes(*track_) : 0;
}

std::size_t RemoveTrack::heldBytes() const noexcept {
    return track_ ? approximateBytes(*track_) : 0;
}

std::size_t ReplaceMeasure::heldBytes() const noexcept {
    return approximateBytes(content_.get());
}

std::size_t CommandGroup::heldBytes() const noexcept {
    std::size_t bytes = 0;
    for (const auto& command : commands_) {
        bytes += command->heldBytes();
    }
    return bytes;
}

Changes CommandGroup::changes() const noexcept {
    Changes changes{.notation = false, .sound = false};
    for (const auto& command : commands_) {
        changes = changes | command->changes();
    }
    return changes;
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

std::unique_ptr<Command> changeTripletFeel(const Song& song, std::size_t from, TripletFeel feel) {
    return changeRun(song, from, feel, &MeasureHeader::tripletFeel, "Change triplet feel");
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
    // A new tempo stays gradual (ADR-0082).
    const bool gradual = std::ranges::any_of(header.tempoChanges, [](const TempoChange& change) {
        return change.offset == Fraction{} && change.gradual;
    });
    std::erase_if(header.tempoChanges,
                  [](const TempoChange& change) { return change.offset == Fraction{}; });
    if (bpm) {
        // Tempo checks the range.
        const Tempo tempo(*bpm, song.tempoAt(measure).beatUnit());
        header.tempoChanges.insert(header.tempoChanges.begin(),
                                   TempoChange{.offset = {}, .tempo = tempo, .gradual = gradual});
    }
    if (header == song.measureHeader(measure)) {
        return nullptr;
    }
    std::vector<std::unique_ptr<Command>> steps;
    steps.push_back(std::make_unique<SetMeasureHeader>(measure, std::move(header)));
    return std::make_unique<CommandGroup>("Change tempo", std::move(steps));
}

std::unique_ptr<Command> changeTempoGradual(const Song& song, std::size_t measure, bool gradual) {
    if (measure >= song.measureCount()) {
        throw std::out_of_range("Change tempo: no such measure");
    }
    if (measure == 0) {
        throw std::invalid_argument("Change tempo: the first measure plays at the song's tempo");
    }
    MeasureHeader header = song.measureHeader(measure);
    auto& changes = header.tempoChanges;
    if (changes.empty() || changes.front().offset != Fraction{}) {
        if (!gradual) {
            return nullptr;
        }
        changes.insert(changes.begin(),
                       TempoChange{.offset = {}, .tempo = song.tempoAt(measure), .gradual = true});
    } else if (changes.front().gradual == gradual) {
        return nullptr;
    } else {
        changes.front().gradual = gradual;
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

std::unique_ptr<Command> changeSection(const Song& song, std::size_t measure, std::string name) {
    if (measure >= song.measureCount()) {
        throw std::out_of_range("Change section: no such measure");
    }
    if (song.measureHeader(measure).section == name) {
        return nullptr;
    }
    MeasureHeader header = song.measureHeader(measure);
    header.section = std::move(name);
    std::vector<std::unique_ptr<Command>> steps;
    steps.push_back(std::make_unique<SetMeasureHeader>(measure, std::move(header)));
    return std::make_unique<CommandGroup>("Change section", std::move(steps));
}

// ---- Measure marks (ADR-0123) ----------------------------------------------------

namespace {

// `header` as one undo step named `name` for measure `measure`, null when the
// song has that header there already.
std::unique_ptr<Command> changeHeader(const Song& song, std::size_t measure, MeasureHeader header,
                                      std::string name) {
    if (header == song.measureHeader(measure)) {
        return nullptr;
    }
    std::vector<std::unique_ptr<Command>> steps;
    steps.push_back(std::make_unique<SetMeasureHeader>(measure, std::move(header)));
    return std::make_unique<CommandGroup>(std::move(name), std::move(steps));
}

} // namespace

std::unique_ptr<Command> changeNavigation(const Song& song, std::size_t measure,
                                          const NavigationMarks& marks) {
    const MeasureHeader& header = song.measureHeader(measure); // throws first
    if (!enumFromIndex<JumpTo>(std::to_underlying(marks.jump)) ||
        !enumFromIndex<JumpThen>(std::to_underlying(marks.jumpThen))) {
        throw std::invalid_argument("not a jump");
    }
    NavigationMarks wanted = marks;
    if (wanted.jump == JumpTo::None) {
        wanted.jumpThen = JumpThen::End; // nothing to end the music after
    }
    return changeHeader(song, measure, withNavigation(header, wanted), "Change navigation marks");
}

std::unique_ptr<Command> changeRepeats(const Song& song, std::size_t measure,
                                       const RepeatMarks& marks) {
    MeasureHeader header = song.measureHeader(measure); // throws first
    if (marks.times != 0) {
        checkRepeatTimes(marks.times);
    }
    header.repeatStart = marks.start;
    header.repeatTimes = static_cast<std::uint8_t>(marks.times);
    header.alternateEndings = marks.endings;
    return changeHeader(song, measure, std::move(header), "Change repeats");
}

std::unique_ptr<Command> changeBarline(const Song& song, std::size_t measure, bool doubleBar) {
    MeasureHeader header = song.measureHeader(measure); // throws first
    header.doubleBar = doubleBar;
    return changeHeader(song, measure, std::move(header), "Change barline");
}

// ---- Layout adjustments ------------------------------------------------------

void SetMeasureLayouts::swapWith(Song& song) {
    MeasureLayouts current = song.measureLayouts(scope_);
    song.setMeasureLayouts(scope_, layouts_); // a shared copy: nothing lost if it throws
    layouts_ = std::move(current);
}

std::unique_ptr<Command> changeBreaks(const Song& song, LayoutScope scope,
                                      std::span<const std::pair<std::size_t, Break>> breaks) {
    const MeasureLayouts& held = song.measureLayouts(scope); // throws first
    std::vector<MeasureLayout> next =
        held ? *held : std::vector<MeasureLayout>(song.measureCount());
    bool changed = false;
    for (const auto& [measure, kind] : breaks) {
        if (measure >= next.size()) {
            throw std::out_of_range("Song: no such measure");
        }
        if (nameOf(kind).empty()) {
            throw std::invalid_argument("changeBreaks: no such break");
        }
        changed = changed || next[measure].breakBefore != kind;
        next[measure].breakBefore = kind;
    }
    if (!changed) {
        return nullptr;
    }
    MeasureLayouts layouts;
    if (!std::ranges::all_of(next, &MeasureLayout::isDefault)) {
        layouts = MeasureLayouts(std::move(next));
    }
    return std::make_unique<SetMeasureLayouts>(scope, std::move(layouts));
}

} // namespace elara::core
