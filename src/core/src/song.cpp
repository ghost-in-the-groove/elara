// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/song.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace elara::core {

namespace {

template <typename Container>
decltype(auto) checkedAt(Container& items, std::size_t index, const char* what) {
    if (index >= items.size()) {
        throw std::out_of_range(what);
    }
    return items[index];
}

} // namespace

// ---- Staff ----------------------------------------------------------------

Staff::Staff(Clef clef, std::optional<Tuning> tuning, int capo)
    : clef_(clef), tuning_(std::move(tuning)), capo_(capo) {
    if (capo < 0 || capo > Tuning::kMaxFret) {
        throw std::invalid_argument("Staff: capo outside the neck");
    }
}

void Staff::setTuning(std::optional<Tuning> tuning, int capo) {
    if (capo < 0 || capo > Tuning::kMaxFret) {
        throw std::invalid_argument("Staff: capo outside the neck");
    }
    tuning_ = std::move(tuning);
    capo_ = capo;
}

Measure& Staff::measure(std::size_t index) {
    return checkedAt(measures_, index, "Staff: no such measure").edit();
}

const Measure& Staff::measure(std::size_t index) const {
    return checkedAt(measures_, index, "Staff: no such measure").get();
}

Note Staff::frettedNote(int string, int fret, SpellingPreference preference) const {
    if (!tuning_) {
        throw std::logic_error("Staff: not a fretted staff");
    }
    const int midi = tuning_->midiAt(string, fret, capo_);
    return Note{.pitch = Pitch::fromMidi(midi, preference),
                .position = FretPosition{.string = static_cast<std::int8_t>(string),
                                         .fret = static_cast<std::int8_t>(fret)}};
}

// ---- Track ----------------------------------------------------------------

Track::Track(std::string name, Instrument instrument, std::vector<Staff> staves)
    : name_(std::move(name)), instrument_(std::move(instrument)), staves_(std::move(staves)) {
    if (staves_.empty()) {
        throw std::invalid_argument("Track: needs at least one staff");
    }
}

Track Track::fromPreset(const InstrumentPreset& preset) {
    std::optional<Tuning> tuning;
    if (!preset.tuningPreset.empty()) {
        tuning = Tuning::preset(preset.tuningPreset);
        if (!tuning) {
            throw std::logic_error("Track: instrument preset names an unknown tuning");
        }
    }
    std::vector<Staff> staves;
    for (int i = 0; i < preset.staffCount; ++i) {
        staves.emplace_back(preset.clefs.at(static_cast<std::size_t>(i)), tuning);
    }
    return Track(std::string(preset.name),
                 Instrument{.name = std::string(preset.name),
                            .kind = preset.kind,
                            .midiProgram = preset.midiProgram,
                            .writtenOffset = preset.writtenOffset},
                 std::move(staves));
}

Staff& Track::staff(std::size_t index) {
    return checkedAt(staves_, index, "Track: no such staff");
}

const Staff& Track::staff(std::size_t index) const {
    return checkedAt(staves_, index, "Track: no such staff");
}

// ---- Song -----------------------------------------------------------------

std::size_t Song::insertTrack(std::size_t at, Track track) {
    if (at > tracks_.size()) {
        throw std::out_of_range("Song: track index past the end");
    }
    for (auto& staff : track.staves_) {
        if (staff.measures_.empty()) {
            // One empty measure for all: each gets its own once it changes.
            staff.measures_.assign(headers_.size(), SharedValue<Measure>{});
        } else if (staff.measures_.size() != headers_.size()) {
            throw std::invalid_argument("Song: track has a different number of measures");
        }
    }
    tracks_.insert(tracks_.begin() + static_cast<std::ptrdiff_t>(at), std::move(track));
    return at;
}

Track Song::takeTrack(std::size_t index) {
    Track taken = std::move(checkedAt(tracks_, index, "Song: no such track"));
    tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(index));
    return taken;
}

void Track::setMix(const TrackMix& mix) {
    // Written so that NaN fails too.
    if (!(mix.volume >= 0.0 && mix.volume <= 1.0)) {
        throw std::invalid_argument("Track: volume must be from 0 to 1");
    }
    if (!(mix.pan >= -1.0 && mix.pan <= 1.0)) {
        throw std::invalid_argument("Track: pan must be from -1 to 1");
    }
    mix_ = mix;
}

bool Song::audible(std::size_t index) const {
    return track(index).mix().audible(
        std::ranges::any_of(tracks_, [](const Track& other) { return other.mix().solo; }));
}

Track& Song::track(std::size_t index) {
    return checkedAt(tracks_, index, "Song: no such track");
}

const Track& Song::track(std::size_t index) const {
    return checkedAt(tracks_, index, "Song: no such track");
}

void Song::insertMeasures(std::size_t at, std::size_t count) {
    if (at > headers_.size()) {
        throw std::out_of_range("Song: insert position past the end");
    }
    const auto offset = static_cast<std::ptrdiff_t>(at);
    const MeasureHeader header = headers_.empty() ? MeasureHeader{}
                                 : at > 0         ? headers_[at - 1].continuation()
                                                  : headers_.front().continuation();
    headers_.insert(headers_.begin() + offset, count, header);
    const SharedValue<Measure> empty;
    for (auto& track : tracks_) {
        for (auto& staff : track.staves_) {
            staff.measures_.insert(staff.measures_.begin() + offset, count, empty);
        }
    }
}

void Song::removeMeasures(std::size_t at, std::size_t count) {
    if (at > headers_.size() || count > headers_.size() - at) {
        throw std::out_of_range("Song: measure range outside the song");
    }
    const auto first = static_cast<std::ptrdiff_t>(at);
    const auto last = static_cast<std::ptrdiff_t>(at + count);
    headers_.erase(headers_.begin() + first, headers_.begin() + last);
    for (auto& track : tracks_) {
        for (auto& staff : track.staves_) {
            staff.measures_.erase(staff.measures_.begin() + first, staff.measures_.begin() + last);
        }
    }
}

MeasureHeader& Song::measureHeader(std::size_t index) {
    return checkedAt(headers_, index, "Song: no such measure");
}

const MeasureHeader& Song::measureHeader(std::size_t index) const {
    return checkedAt(headers_, index, "Song: no such measure");
}

// ---- Timeline ---------------------------------------------------------------

void Song::checkPosition(std::size_t measure, const Fraction& offset) const {
    const bool inside = measure < headers_.size() && offset >= Fraction(0) &&
                        offset <= headers_[measure].timeSignature.measureLength();
    const bool atEnd = measure == headers_.size() && offset.isZero();
    if (!inside && !atEnd) {
        throw std::out_of_range("Song: position outside the song");
    }
}

Fraction Song::measureStart(std::size_t measure) const {
    if (measure > headers_.size()) {
        throw std::out_of_range("Song: no such measure");
    }
    Fraction start;
    for (std::size_t m = 0; m < measure; ++m) {
        start += headers_[m].timeSignature.measureLength();
    }
    return start;
}

namespace {

// The highest time through that each measure's run of alternate endings plays:
// the one a run plays after a jump. 0 for a measure played every time.
std::vector<int> lastEndings(const std::vector<MeasureHeader>& headers) {
    std::vector<int> last(headers.size(), 0);
    std::size_t m = 0;
    while (m < headers.size()) {
        if (headers[m].alternateEndings == 0) {
            ++m;
            continue;
        }
        const std::size_t first = m;
        unsigned endings = 0;
        for (; m < headers.size() && headers[m].alternateEndings != 0; ++m) {
            endings |= headers[m].alternateEndings;
        }
        const int highest = static_cast<int>(std::bit_width(endings));
        std::fill(last.begin() + static_cast<std::ptrdiff_t>(first),
                  last.begin() + static_cast<std::ptrdiff_t>(m), highest);
    }
    return last;
}

// The measure a jump from `from` goes to, or nullopt when its sign is missing.
std::optional<std::size_t> jumpTarget(const std::vector<MeasureHeader>& headers, std::size_t from,
                                      JumpTo to) {
    if (to == JumpTo::Start) {
        return 0;
    }
    for (std::size_t m = from + 1; m-- > 0;) {
        if (to == JumpTo::Segno ? headers[m].segno : headers[m].segnoSegno) {
            return m;
        }
    }
    return std::nullopt;
}

// The first coda (or double coda) sign after `from`, or nullopt.
std::optional<std::size_t> codaAfter(const std::vector<MeasureHeader>& headers, std::size_t from,
                                     bool doubleCoda) {
    for (std::size_t m = from + 1; m < headers.size(); ++m) {
        if (doubleCoda ? headers[m].doubleCoda : headers[m].coda) {
            return m;
        }
    }
    return std::nullopt;
}

} // namespace

std::vector<std::size_t> Song::playOrder() const {
    const std::size_t count = headers_.size();
    const std::size_t limit = count * 256; // a guard: valid repeats stay far below it
    std::vector<std::size_t> order;
    order.reserve(count);
    std::size_t start = 0; // first measure of the section being repeated
    int time = 1;          // time through that section
    bool jumped = false;   // the current measure was reached by repeating
    // Once a jump was taken: what ends the music, and the endings played.
    std::optional<JumpThen> after;
    std::vector<int> lastEnding;
    std::vector<bool> taken(count, false);
    std::size_t measure = 0;
    while (measure < count && order.size() < limit) {
        const MeasureHeader& header = headers_[measure];
        if (header.repeatStart && !jumped) {
            start = measure;
            time = 1;
        }
        jumped = false;
        const bool played = header.playedOnTime(after ? lastEnding[measure] : time);
        if (played) {
            order.push_back(measure);
            if (!after && header.repeatEnd() && time < header.repeatTimes) {
                ++time;
                measure = start;
                jumped = true;
                continue;
            }
            if (after == JumpThen::Fine && header.fine) {
                break;
            }
            if ((after == JumpThen::Coda && header.toCoda) ||
                (after == JumpThen::DoubleCoda && header.toDoubleCoda)) {
                if (const auto coda = codaAfter(headers_, measure, after == JumpThen::DoubleCoda)) {
                    after = JumpThen::End;
                    measure = *coda;
                    start = *coda;
                    continue;
                }
            }
            if (header.jump != JumpTo::None && !taken[measure]) {
                if (const auto target = jumpTarget(headers_, measure, header.jump)) {
                    taken[measure] = true;
                    if (!after) {
                        lastEnding = lastEndings(headers_);
                    }
                    after = header.jumpThen;
                    measure = *target;
                    start = *target;
                    continue;
                }
            }
        }
        // A new section starts after the last alternate ending, or after a
        // finished repeat that no alternate ending follows.
        const std::size_t next = measure + 1;
        const bool nextIsEnding = next < count && headers_[next].alternateEndings != 0;
        if (!nextIsEnding && (header.alternateEndings != 0 || (played && header.repeatEnd()))) {
            start = next;
            time = 1;
        }
        measure = next;
    }
    return order;
}

double Song::secondsAt(std::size_t measure, const Fraction& offset) const {
    checkPosition(measure, offset);
    double seconds = 0.0;
    Tempo tempo = startTempo_;
    for (std::size_t m = 0; m <= measure && m < headers_.size(); ++m) {
        const MeasureHeader& header = headers_[m];
        const Fraction end = m == measure ? offset : header.timeSignature.measureLength();
        Fraction cursor;
        for (const TempoChange& change : header.tempoChanges) {
            if (m == measure && change.offset > end) {
                break;
            }
            const Fraction at = change.offset < end ? change.offset : end;
            seconds += tempo.secondsFor(at - cursor);
            cursor = at;
            tempo = change.tempo;
        }
        seconds += tempo.secondsFor(end - cursor);
    }
    return seconds;
}

Tempo Song::tempoAt(std::size_t measure, const Fraction& offset) const {
    checkPosition(measure, offset);
    Tempo tempo = startTempo_;
    for (std::size_t m = 0; m <= measure && m < headers_.size(); ++m) {
        for (const TempoChange& change : headers_[m].tempoChanges) {
            if (m == measure && change.offset > offset) {
                break;
            }
            tempo = change.tempo;
        }
    }
    return tempo;
}

} // namespace elara::core
