// SPDX-License-Identifier: AGPL-3.0-or-later
// Line breaking, justification and pages (ADR-0014).
#include <elara/core/parallel.hpp>
#include <elara/notation/page_layout.hpp>

#include "layout_common.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
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

std::vector<DisplayList> layoutPages(const core::Song& song, std::span<const StaffSelection> staves,
                                     const EngravingFont& font, const PageLayoutOptions& options) {
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
    const std::size_t last = first + std::min(options.measureCount, song.measureCount() - first);
    const unsigned threads =
        core::threadsFor(options.threads, last - first, kParallelMeasures, last - first);

    const std::vector<MeasureWidth> widths =
        detail::measureWidths(song, staves, font, first, last, threads);
    const LineBreaker breaker(widths, width, detail::nameIndent(song, staves),
                              options.lastSystemFill);
    const std::vector<Line> lines =
        options.measuresPerSystem > 0 ? breaker.every(options.measuresPerSystem) : breaker.best();

    // Every system on its own: they share nothing but the song.
    std::vector<DisplayList> systems(lines.size());
    core::forEachIndex(0, lines.size(),
                       std::min<unsigned>(threads, static_cast<unsigned>(lines.size())),
                       [&](std::size_t s) {
                           systems[s] = detail::layoutLine(
                               song, staves, font, first + lines[s].first, first + lines[s].last,
                               {.stretch = lines[s].stretch, .numbered = s > 0, .named = s == 0});
                       });

    // Systems stacked from the top margin down; a full page spreads them to its
    // bottom margin, each gap growing at most kMaxSpread.
    std::vector<DisplayList> pages;
    DisplayList page = blankPage(options);
    double top = options.credits ? addCredits(page, song.info(), options, first) : options.margin;
    const double bottom = options.height > 0.0 ? options.height - options.margin
                                               : std::numeric_limits<double>::infinity();
    struct Placed {
        std::size_t system = 0;
        double y = 0.0;
    };
    std::vector<Placed> placed;
    const auto flush = [&](bool full) {
        double extra = 0.0;
        if (full && placed.size() > 1) {
            const double room =
                bottom - placed.back().y - systems[placed.back().system].systems.front().bottom;
            extra = std::clamp(room / static_cast<double>(placed.size() - 1), 0.0, kMaxSpread);
        }
        for (std::size_t k = 0; k < placed.size(); ++k) {
            DisplayList& system = systems[placed[k].system];
            detail::shift(system, options.margin, placed[k].y + extra * static_cast<double>(k));
            detail::append(page, std::move(system));
        }
        placed.clear();
        pages.push_back(std::exchange(page, blankPage(options)));
    };
    double previousY = 0.0;
    double previousHeight = 0.0;
    double previousBottom = 0.0;
    for (std::size_t s = 0; s < systems.size(); ++s) {
        const SystemBox& box = systems[s].systems.front();
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

} // namespace elara::notation
