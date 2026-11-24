// SPDX-License-Identifier: AGPL-3.0-or-later
// Line breaking, justification and pages (ADR-0014).
#include <elara/core/parallel.hpp>
#include <elara/core/song.hpp>
#include <elara/core/tempo.hpp>
#include <elara/notation/page_layout.hpp>

#include "layout_common.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace elara::notation {

namespace {

using detail::kMaxShrink;
using detail::MeasureWidth;

constexpr double kLinePenalty = 0.02; // per system, so that fewer win when spacing is alike
constexpr double kShrinkCost = 6.25; // tightening by kMaxShrink costs as much as stretching by half
constexpr double kOverfull = 1e6;    // a measure wider than the page
constexpr int kBisections = 60;
constexpr double kSystemDistance = 6.0; // at least between the lines of neighbouring systems
constexpr double kSystemInkGap = 2.0;   // at least between their ink
constexpr double kMaxSpread = 8.0;      // added at most between systems to fill a page
constexpr std::size_t kParallelMeasures = 48;
// Items copied into pages before filling them on several threads pays: one
// track's 21 pages (40k items) copy faster than threads start.
constexpr std::size_t kParallelItems = 200'000;

constexpr double kTitleSize = 4.0;
constexpr double kSubtitleSize = 2.4;
constexpr double kArtistSize = 2.0;
constexpr double kCreditSize = 1.5;   // words, music, copyright and page numbers
constexpr double kCreditGap = 0.6;    // between lines of credits
constexpr double kCreditsBelow = 3.0; // between the credits and the first system

// A system: measures [first, last) of the laid out range, at `stretch`.
struct Line {
    std::size_t first = 0;
    std::size_t last = 0;
    double stretch = 0.0;
};

// The width of the system of measures [first, last) at `stretch`.
double lineWidth(std::span<const MeasureWidth> widths, std::size_t first, std::size_t last,
                 double stretch) {
    double width = widths[first].before(true) + detail::kFirstNoteGap;
    for (std::size_t m = first; m < last; ++m) {
        if (m > first) {
            width += widths[m].before(false) + detail::kBarlineGap;
        }
        width += widths[m].width(stretch) + widths[m].barline;
    }
    return width;
}

// Sums for one candidate system, grown a measure at a time.
struct Candidate {
    double room = 0.0; // the width it has to fill
    double natural = 0.0;
    double tightest = 0.0; // at -kMaxShrink
    double ideal = 0.0;
};

class LineBreaker {
public:
    // `indent` is taken from the width of the first system.
    LineBreaker(std::span<const MeasureWidth> widths, double width, double indent, double lastFill)
        : widths_(widths), width_(width), indent_(indent), lastFill_(lastFill) {
        natural_.reserve(widths.size());
        tightest_.reserve(widths.size());
        ideal_.reserve(widths.size());
        for (const MeasureWidth& measure : widths) {
            natural_.push_back(measure.width(0.0));
            tightest_.push_back(measure.width(-kMaxShrink));
            ideal_.push_back(measure.ideal());
        }
    }

    // The breaks that make the systems' spacing most even: the least sum of
    // squared stretches, tightening costing more than stretching.
    [[nodiscard]] std::vector<Line> best() const {
        const std::size_t count = widths_.size();
        std::vector<double> cost(count + 1, std::numeric_limits<double>::infinity());
        std::vector<std::size_t> from(count + 1, 0);
        cost[0] = 0.0;
        for (std::size_t first = 0; first < count; ++first) {
            Candidate line = start(first);
            for (std::size_t last = first + 1; last <= count; ++last) {
                add(line, first, last - 1);
                if (last > first + 1 && line.tightest > line.room) {
                    break;
                }
                const double total = cost[first] + lineCost(line, last == count);
                if (total < cost[last]) {
                    cost[last] = total;
                    from[last] = first;
                }
                if (line.tightest > line.room) {
                    break;
                }
            }
        }
        std::vector<Line> lines;
        for (std::size_t last = count; last > 0; last = from[last]) {
            lines.push_back(Line{.first = from[last], .last = last});
        }
        std::ranges::reverse(lines);
        for (Line& line : lines) {
            line.stretch = stretchFor(line);
        }
        return lines;
    }

    // Systems of `perSystem` measures.
    [[nodiscard]] std::vector<Line> every(std::size_t perSystem) const {
        std::vector<Line> lines;
        for (std::size_t first = 0; first < widths_.size(); first += perSystem) {
            Line line{.first = first, .last = std::min(widths_.size(), first + perSystem)};
            line.stretch = stretchFor(line);
            lines.push_back(line);
        }
        return lines;
    }

private:
    [[nodiscard]] Candidate start(std::size_t first) const {
        const double fixed =
            widths_[first].before(true) + detail::kFirstNoteGap - detail::kBarlineGap;
        return Candidate{.room = first == 0 ? width_ - indent_ : width_,
                         .natural = fixed,
                         .tightest = fixed,
                         .ideal = 0.0};
    }

    void add(Candidate& line, std::size_t first, std::size_t m) const {
        const double around =
            (m > first ? widths_[m].before(false) : 0.0) + detail::kBarlineGap + widths_[m].barline;
        line.natural += natural_[m] + around;
        line.tightest += tightest_[m] + around;
        line.ideal += ideal_[m];
    }

    [[nodiscard]] bool ragged(const Candidate& line, bool last) const {
        return last && line.natural <= line.room && line.natural < lastFill_ * line.room;
    }

    [[nodiscard]] double lineCost(const Candidate& line, bool last) const {
        if (line.tightest > line.room) {
            return kOverfull + line.tightest - line.room;
        }
        if (ragged(line, last)) {
            return kLinePenalty;
        }
        if (line.natural <= line.room) {
            const double stretch = (line.room - line.natural) / line.ideal;
            return stretch * stretch + kLinePenalty;
        }
        const double shrink =
            kMaxShrink * (line.natural - line.room) / (line.natural - line.tightest);
        return kShrinkCost * shrink * shrink + kLinePenalty;
    }

    // The stretch that makes the system exactly as wide as the page, 0 for a
    // short last system and -kMaxShrink for one that cannot fit.
    [[nodiscard]] double stretchFor(const Line& line) const {
        Candidate sums = start(line.first);
        for (std::size_t m = line.first; m < line.last; ++m) {
            add(sums, line.first, m);
        }
        if (sums.tightest >= sums.room) {
            return sums.natural <= sums.room ? 0.0 : -kMaxShrink;
        }
        if (sums.natural <= sums.room) {
            return ragged(sums, line.last == widths_.size())
                       ? 0.0
                       : (sums.room - sums.natural) / sums.ideal;
        }
        // Tightening is not linear (springs stop at their ink): bisect.
        double low = -kMaxShrink;
        double high = 0.0;
        for (int i = 0; i < kBisections; ++i) {
            const double middle = (low + high) / 2;
            if (lineWidth(widths_, line.first, line.last, middle) > sums.room) {
                high = middle;
            } else {
                low = middle;
            }
        }
        return low;
    }

    std::span<const MeasureWidth> widths_;
    double width_;
    double indent_;
    double lastFill_;
    std::vector<double> natural_;
    std::vector<double> tightest_;
    std::vector<double> ideal_;
};

TextItem creditText(std::string text, Point origin, double size, TextAlign align,
                    std::size_t measure) {
    TextItem item;
    item.text = std::move(text);
    item.origin = origin;
    item.size = size;
    item.align = align;
    item.role = Role::Credit;
    item.measure = measure;
    return item;
}

// Title, subtitle and artist centred at the top of the first page, words by on
// the left and music by on the right under them. Returns where they end.
double addCredits(DisplayList& page, const core::SongInfo& info, const PageLayoutOptions& options,
                  std::size_t measure) {
    double y = options.margin;
    double below = 0.0; // descent of the line above
    bool any = false;
    const auto line = [&](const std::string& text, double size) {
        if (text.empty()) {
            return;
        }
        y += below + (any ? kCreditGap : 0.0) + detail::kTextAscent * size;
        page.texts.push_back(
            creditText(text, {options.width / 2, y}, size, TextAlign::Centre, measure));
        below = detail::kTextDescent * size;
        any = true;
    };
    line(info.title, kTitleSize);
    line(info.subtitle, kSubtitleSize);
    line(info.artist, kArtistSize);
    if (!info.words.empty() || !info.music.empty()) {
        y += below + (any ? kCreditGap : 0.0) + detail::kTextAscent * kCreditSize;
        if (!info.words.empty()) {
            page.texts.push_back(creditText("Words by " + info.words, {options.margin, y},
                                            kCreditSize, TextAlign::Left, measure));
        }
        if (!info.music.empty()) {
            page.texts.push_back(creditText("Music by " + info.music,
                                            {options.width - options.margin, y}, kCreditSize,
                                            TextAlign::Right, measure));
        }
        below = detail::kTextDescent * kCreditSize;
        any = true;
    }
    return any ? y + below + kCreditsBelow : options.margin;
}

DisplayList blankPage(const PageLayoutOptions& options) {
    DisplayList page;
    page.width = options.width;
    page.top = 0.0;
    page.bottom = options.height;
    return page;
}

} // namespace

PageLayoutOptions pageOptions(const Paper& paper, double staffSpace, double margin) {
    PageLayoutOptions options;
    options.width = paper.width / staffSpace;
    options.height = paper.height / staffSpace;
    options.margin = margin / staffSpace;
    return options;
}

namespace {

// The measures a page layout lays out: [first, last), on lines `width` wide.
struct Span {
    std::size_t first = 0;
    std::size_t last = 0;
    double width = 0.0;
};

Span checkedSpan(const core::Song& song, std::span<const StaffSelection> staves,
                 const PageLayoutOptions& options) {
    if (staves.empty()) {
        throw std::invalid_argument("layoutPages: no staves");
    }
    if (options.firstMeasure >= song.measureCount()) {
        throw std::out_of_range("layoutPages: no measure " + std::to_string(options.firstMeasure));
    }
    const double width = options.width - 2 * options.margin;
    if (width <= 0.0 || (options.height > 0.0 && options.height <= 2 * options.margin)) {
        throw std::invalid_argument("layoutPages: the margins leave no room");
    }
    const std::size_t first = options.firstMeasure;
    return Span{.first = first,
                .last = first + std::min(options.measureCount, song.measureCount() - first),
                .width = width};
}

unsigned threadsFor(const PageLayoutOptions& options, const Span& span) {
    return core::threadsFor(options.threads, span.last - span.first, kParallelMeasures,
                            span.last - span.first);
}

std::vector<Line> breakLines(const core::Song& song, std::span<const StaffSelection> staves,
                             std::span<const MeasureWidth> widths, const Span& span,
                             const PageLayoutOptions& options) {
    const LineBreaker breaker(widths, span.width, detail::nameIndent(song, staves),
                              options.lastSystemFill);
    return options.measuresPerSystem > 0 ? breaker.every(options.measuresPerSystem)
                                         : breaker.best();
}

// One system: the first carries the track names, the others the
// number of their first measure.
DisplayList layOutLine(const core::Song& song, std::span<const StaffSelection> staves,
                       const EngravingFont& font, const Span& span, const Line& line) {
    return detail::layoutLine(
        song, staves, font, span.first + line.first, span.first + line.last,
        {.stretch = line.stretch, .numbered = line.first > 0, .named = line.first == 0});
}

// The systems stacked into pages, with the credits and page numbers. The
// systems are copied, so they can be kept; the pages are filled on `threads`.
std::vector<DisplayList> stackPages(const core::Song& song,
                                    std::span<const DisplayList* const> systems, std::size_t first,
                                    const PageLayoutOptions& options, unsigned threads) {
    // Systems stacked from the top margin down; a full page spreads them to its
    // bottom margin, each gap growing at most kMaxSpread.
    struct Placed {
        std::size_t system = 0;
        double y = 0.0;
    };
    struct Page {
        std::vector<Placed> placed;
        double extra = 0.0; // added to each gap
    };
    std::vector<Page> layout;
    DisplayList firstPage = blankPage(options);
    double top =
        options.credits ? addCredits(firstPage, song.info(), options, first) : options.margin;
    const double bottom = options.height > 0.0 ? options.height - options.margin
                                               : std::numeric_limits<double>::infinity();
    std::vector<Placed> placed;
    const auto flush = [&](bool full) {
        double extra = 0.0;
        if (full && placed.size() > 1) {
            const double room =
                bottom - placed.back().y - systems[placed.back().system]->systems.front().bottom;
            extra = std::clamp(room / static_cast<double>(placed.size() - 1), 0.0, kMaxSpread);
        }
        layout.push_back(Page{.placed = std::exchange(placed, {}), .extra = extra});
    };
    double previousY = 0.0;
    double previousHeight = 0.0;
    double previousBottom = 0.0;
    for (std::size_t s = 0; s < systems.size(); ++s) {
        const SystemBox& box = systems[s]->systems.front();
        double y = top - box.top;
        if (!placed.empty()) {
            y = std::max(previousY + previousHeight + kSystemDistance,
                         previousBottom + kSystemInkGap - box.top);
            if (y + box.bottom > bottom) {
                flush(true);
                top = options.margin;
                y = top - box.top;
            }
        }
        placed.push_back(Placed{.system = s, .y = y});
        previousY = y;
        previousHeight = box.height;
        previousBottom = y + box.bottom;
    }
    flush(false);

    // Every page on its own: each copies its systems where they go.
    std::size_t items = 0;
    for (const DisplayList* system : systems) {
        items += system->glyphs.size() + system->lines.size() + system->texts.size() +
                 system->beams.size() + system->curves.size();
    }
    std::vector<DisplayList> pages(layout.size());
    pages.front() = std::move(firstPage);
    core::forEachIndex(
        0, pages.size(),
        items < kParallelItems ? 1U
                               : std::min<unsigned>(threads, static_cast<unsigned>(pages.size())),
        [&](std::size_t p) {
            DisplayList& page = pages[p];
            if (p > 0) {
                page = blankPage(options);
            }
            std::vector<const DisplayList*> lists;
            lists.reserve(layout[p].placed.size());
            for (const Placed& each : layout[p].placed) {
                lists.push_back(systems[each.system]);
            }
            detail::reserveFor(page, lists);
            for (std::size_t k = 0; k < layout[p].placed.size(); ++k) {
                const Placed& each = layout[p].placed[k];
                detail::appendShifted(page, *systems[each.system], options.margin,
                                      each.y + layout[p].extra * static_cast<double>(k));
            }
        });
    if (options.height <= 0.0) {
        pages.front().bottom = previousBottom + options.margin;
    }
    if (options.credits) {
        for (std::size_t p = 0; p < pages.size(); ++p) {
            DisplayList& each = pages[p];
            const double baseline = each.bottom - options.margin / 2;
            if (p > 0) {
                each.texts.push_back(
                    creditText(std::to_string(p + 1), {options.width / 2, baseline}, kCreditSize,
                               TextAlign::Centre, each.systems.front().firstMeasure));
            } else if (!song.info().copyright.empty()) {
                each.texts.push_back(creditText(song.info().copyright,
                                                {options.width / 2, baseline}, kCreditSize,
                                                TextAlign::Centre, first));
            }
        }
    }
    return pages;
}

} // namespace

std::vector<DisplayList> layoutPages(const core::Song& song, std::span<const StaffSelection> staves,
                                     const EngravingFont& font, const PageLayoutOptions& options) {
    const Span span = checkedSpan(song, staves, options);
    const unsigned threads = threadsFor(options, span);
    const std::vector<MeasureWidth> widths =
        detail::measureWidths(song, staves, font, span.first, span.last, threads);
    const std::vector<Line> lines = breakLines(song, staves, widths, span, options);

    // Every system on its own: they share nothing but the song.
    std::vector<DisplayList> systems(lines.size());
    core::forEachIndex(
        0, lines.size(), std::min<unsigned>(threads, static_cast<unsigned>(lines.size())),
        [&](std::size_t s) { systems[s] = layOutLine(song, staves, font, span, lines[s]); });
    std::vector<const DisplayList*> lists;
    lists.reserve(systems.size());
    for (const DisplayList& system : systems) {
        lists.push_back(&system);
    }
    return stackPages(song, lists, span.first, options, threads);
}

// ---- PageLayoutCache (ADR-0052) -------------------------------------------------------

namespace {

// The tempo in force before each measure, as a system starting there reads it:
// the start tempo, then the last change of the measures before.
std::vector<core::Tempo> temposBefore(const core::Song& song) {
    std::vector<core::Tempo> tempos;
    tempos.reserve(song.measureCount());
    core::Tempo current = song.startTempo();
    for (std::size_t m = 0; m < song.measureCount(); ++m) {
        tempos.push_back(current);
        for (const core::TempoChange& change : song.measureHeader(m).tempoChanges) {
            current = change.tempo;
        }
    }
    return tempos;
}

// Whether what the staves' writers read of their tracks besides the measures
// is the same in both songs: names (the first system's, and its indent),
// instruments, clefs, tunings and capos.
bool sameTracks(const core::Song& before, const core::Song& now,
                std::span<const StaffSelection> staves) {
    for (const StaffSelection& selection : staves) {
        if (selection.track >= before.trackCount() || selection.track >= now.trackCount()) {
            return false;
        }
        const core::Track& was = before.track(selection.track);
        const core::Track& is = now.track(selection.track);
        if (was.name() != is.name() || was.instrument() != is.instrument() ||
            was.staffCount() != is.staffCount()) {
            return false;
        }
        for (std::size_t s = 0; s < is.staffCount(); ++s) {
            const core::Staff& a = was.staff(s);
            const core::Staff& b = is.staff(s);
            if (a.clef() != b.clef() || a.tuning() != b.tuning() || a.capo() != b.capo()) {
                return false;
            }
        }
    }
    return true;
}

constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

// What differs between two versions of a song with as many measures.
struct Changes {
    std::vector<bool> measures;       // a header or a staff's content
    std::size_t firstHeader = kNone;  // the first measure whose header differs
    std::vector<std::size_t> firstOn; // per staff, the first measure whose content differs

    // Whether any of measures [from, to) differs, clipped to the song.
    [[nodiscard]] bool any(std::size_t from, std::size_t to) const {
        to = std::min(to, measures.size());
        for (std::size_t m = from; m < to; ++m) {
            if (measures[m]) {
                return true;
            }
        }
        return false;
    }
};

// Measures the two songs share compare by address.
Changes changesBetween(const core::Song& before, const core::Song& now,
                       std::span<const StaffSelection> staves) {
    Changes changes;
    changes.measures.assign(now.measureCount(), false);
    changes.firstOn.assign(staves.size(), kNone);
    if (&before == &now) {
        return changes;
    }
    // Measure 0 draws the start tempo, and every system after it reads it.
    if (before.startTempo() != now.startTempo()) {
        changes.measures.front() = true;
        changes.firstHeader = 0;
    }
    for (std::size_t m = 0; m < now.measureCount(); ++m) {
        if (before.measureHeader(m) != now.measureHeader(m)) {
            changes.measures[m] = true;
            changes.firstHeader = std::min(changes.firstHeader, m);
        }
        for (std::size_t i = 0; i < staves.size(); ++i) {
            const StaffSelection& selection = staves[i];
            const core::Measure& a =
                before.track(selection.track).staff(selection.staff).measure(m);
            const core::Measure& b = now.track(selection.track).staff(selection.staff).measure(m);
            if (&a != &b && a != b) {
                changes.measures[m] = true;
                changes.firstOn[i] = std::min(changes.firstOn[i], m);
            }
        }
    }
    return changes;
}

} // namespace

struct PageLayoutCache::State {
    struct System {
        Line line;
        DisplayList list; // where layoutLine() put it, before stacking
    };

    std::shared_ptr<const core::Song> song;
    std::vector<StaffSelection> staves;
    const EngravingFont* font = nullptr;
    PageLayoutOptions options;
    std::vector<MeasureWidth> widths; // of the measures laid out
    std::vector<System> systems;
};

PageLayoutCache::PageLayoutCache() = default;
PageLayoutCache::~PageLayoutCache() = default;

PageLayoutCache::Reuse PageLayoutCache::lastReuse() const {
    const std::scoped_lock lock(mutex_);
    return reuse_;
}

std::vector<DisplayList> PageLayoutCache::layout(std::shared_ptr<const core::Song> song,
                                                 std::span<const StaffSelection> staves,
                                                 const EngravingFont& font,
                                                 const PageLayoutOptions& options) {
    if (!song) {
        throw std::invalid_argument("PageLayoutCache: no song");
    }
    const std::scoped_lock lock(mutex_);
    // Taken out while it lays out: an exception leaves the cache empty.
    std::unique_ptr<State> before = std::move(state_);
    const core::Song& now = *song;
    const Span span = checkedSpan(now, staves, options);
    const unsigned threads = threadsFor(options, span);

    const bool related = before && before->font == &font && before->options == options &&
                         std::ranges::equal(before->staves, staves) &&
                         before->song->measureCount() == now.measureCount() &&
                         sameTracks(*before->song, now, staves);
    Changes changes;
    if (related) {
        changes = changesBetween(*before->song, now, staves);
    }

    // Measure widths: a measure's own, and how it follows the one before.
    auto state = std::make_unique<State>();
    Reuse reuse{.measures = span.last - span.first};
    if (!related) {
        state->widths = detail::measureWidths(now, staves, font, span.first, span.last, threads);
        reuse.measured = reuse.measures;
    } else {
        state->widths = std::move(before->widths);
        const auto stale = [&changes](std::size_t m) {
            return changes.measures[m] || (m > 0 && changes.measures[m - 1]);
        };
        for (std::size_t m = span.first; m < span.last;) {
            if (!stale(m)) {
                ++m;
                continue;
            }
            std::size_t end = m + 1;
            while (end < span.last && stale(end)) {
                ++end;
            }
            const std::vector<MeasureWidth> fresh =
                detail::measureWidths(now, staves, font, m, end, threads);
            std::ranges::copy(fresh,
                              state->widths.begin() + static_cast<std::ptrdiff_t>(m - span.first));
            reuse.measured += end - m;
            m = end;
        }
    }
    const std::vector<Line> lines = breakLines(now, staves, state->widths, span, options);

    // What a system reads from before its first measure: the tempo, if a
    // header before it changed, and on each staff whose notes before it
    // changed, the dynamic in force.
    std::optional<std::vector<core::Tempo>> temposWere;
    std::optional<std::vector<core::Tempo>> temposAre;
    const auto sameContext = [&](std::size_t first) {
        if (changes.firstHeader < first) {
            if (!temposWere) {
                temposWere = temposBefore(*before->song);
                temposAre = temposBefore(now);
            }
            if ((*temposWere)[first] != (*temposAre)[first]) {
                return false;
            }
        }
        for (std::size_t i = 0; i < staves.size(); ++i) {
            if (changes.firstOn[i] < first) {
                const StaffSelection& selection = staves[i];
                if (detail::dynamicBefore(
                        before->song->track(selection.track).staff(selection.staff), first) !=
                    detail::dynamicBefore(now.track(selection.track).staff(selection.staff),
                                          first)) {
                    return false;
                }
            }
        }
        return true;
    };

    // Systems: one reused when it starts and ends where it did, at the same
    // stretch, and neither its measures nor their neighbours nor what it reads
    // from before it changed.
    state->systems.resize(lines.size());
    std::vector<std::size_t> fresh;
    std::size_t next = 0; // the first cached system that may still match
    for (std::size_t s = 0; s < lines.size(); ++s) {
        State::System& system = state->systems[s];
        system.line = lines[s];
        const std::size_t first = span.first + lines[s].first;
        const std::size_t last = span.first + lines[s].last;
        bool reused = false;
        if (related) {
            while (next < before->systems.size() &&
                   before->systems[next].line.first < lines[s].first) {
                ++next;
            }
            if (next < before->systems.size()) {
                State::System& old = before->systems[next];
                if (old.line.first == lines[s].first && old.line.last == lines[s].last &&
                    old.line.stretch == lines[s].stretch &&
                    !changes.any(first > 0 ? first - 1 : 0, last + 1) && sameContext(first)) {
                    system.list = std::move(old.list);
                    reused = true;
                }
            }
        }
        if (!reused) {
            fresh.push_back(s);
        }
    }
    core::forEachIndex(0, fresh.size(),
                       std::min<unsigned>(threads, static_cast<unsigned>(fresh.size())),
                       [&](std::size_t k) {
                           State::System& system = state->systems[fresh[k]];
                           system.list = layOutLine(now, staves, font, span, system.line);
                       });
    reuse.systems = lines.size();
    reuse.laidOut = fresh.size();

    std::vector<const DisplayList*> lists;
    lists.reserve(state->systems.size());
    for (const State::System& system : state->systems) {
        lists.push_back(&system.list);
    }
    std::vector<DisplayList> pages = stackPages(now, lists, span.first, options, threads);

    state->song = std::move(song);
    state->staves.assign(staves.begin(), staves.end());
    state->font = &font;
    state->options = options;
    state_ = std::move(state);
    reuse_ = reuse;
    return pages;
}

std::vector<DisplayList> cutIntoStrips(const DisplayList& page, double height) {
    if (!(height > 0.0)) {
        throw std::invalid_argument("strips need a positive height");
    }
    const std::vector<SystemBox>& systems = page.systems;
    if (systems.size() < 2) {
        return {page};
    }
    // Halfway between the ink of system s and the next.
    const auto cutBelow = [&systems](std::size_t s) {
        return (systems[s].bottom + systems[s + 1].top) / 2;
    };
    // Strip k runs from bounds[k] to bounds[k + 1] on the page.
    std::vector<double> bounds{0.0};
    std::size_t opening = 0; // the first system of the strip being filled
    for (std::size_t s = 0; s < systems.size(); ++s) {
        const double end = s + 1 < systems.size() ? cutBelow(s) : page.bottom;
        if (s > opening && end - bounds.back() > height) {
            bounds.push_back(cutBelow(s - 1));
            opening = s;
        }
    }
    bounds.push_back(page.bottom);
    const std::size_t count = bounds.size() - 1;
    const auto inner = std::span(bounds).subspan(1, count - 1);
    const auto stripAt = [inner](double y) {
        return static_cast<std::size_t>(std::ranges::upper_bound(inner, y) - inner.begin());
    };

    std::vector<DisplayList> strips(count);
    const auto deal = [&]<typename Item>(std::vector<Item> DisplayList::* member, auto anchor) {
        for (const Item& item : page.*member) {
            (strips[stripAt(anchor(item))].*member).push_back(item);
        }
    };
    const auto middle = [](const SystemBox& system) {
        return (system.top + system.bottom) / 2;
    };
    deal(&DisplayList::systems, middle);
    deal(&DisplayList::staves, [](const StaffBox& staff) { return staff.y + staff.height / 2; });
    deal(&DisplayList::measures,
         [](const MeasureBox& measure) { return measure.y + measure.height / 2; });
    deal(&DisplayList::glyphs, [](const GlyphItem& glyph) { return glyph.origin.y; });
    deal(&DisplayList::lines, [](const LineItem& line) { return (line.from.y + line.to.y) / 2; });
    deal(&DisplayList::beams,
         [](const BeamItem& beam) { return (beam.left.y + beam.right.y) / 2; });
    deal(&DisplayList::texts, [](const TextItem& text) { return text.origin.y; });
    deal(&DisplayList::curves,
         [](const CurveItem& curve) { return (curve.from.y + curve.to.y) / 2; });
    deal(&DisplayList::polygons, [](const PolygonItem& polygon) {
        return polygon.points.empty() ? 0.0 : polygon.points.front().y;
    });
    // A column has no height: it goes with the system of its measure.
    for (const ColumnBox& column : page.columns) {
        const auto after =
            std::ranges::upper_bound(systems, column.measure, {}, &SystemBox::firstMeasure);
        const double y = after == systems.begin() ? 0.0 : middle(*std::prev(after));
        strips[stripAt(y)].columns.push_back(column);
    }

    for (std::size_t k = 0; k < count; ++k) {
        DisplayList& strip = strips[k];
        detail::shift(strip, 0.0, -bounds[k]);
        strip.width = page.width;
        strip.top = 0.0;
        strip.bottom = bounds[k + 1] - bounds[k];
    }
    return strips;
}

} // namespace elara::notation
