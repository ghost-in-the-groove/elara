// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/notation/layout_issues.hpp>

#include "layout_common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace elara::notation {

namespace {

// Roles whose ink is looked at, and the pairs among them that must never touch.
struct Pairs {
    std::array<bool, kRoleCount> looked{};
    std::array<std::array<bool, kRoleCount>, kRoleCount> apart{};

    constexpr void add(Role a, Role b) {
        const auto i = static_cast<std::size_t>(a);
        const auto j = static_cast<std::size_t>(b);
        looked[i] = true;
        looked[j] = true;
        apart[i][j] = true;
        apart[j][i] = true;
    }
};

constexpr Pairs kPairs = [] {
    Pairs pairs;
    pairs.add(Role::Notehead, Role::Notehead);
    pairs.add(Role::Notehead, Role::Accidental);
    pairs.add(Role::Accidental, Role::Accidental);
    pairs.add(Role::FretNumber, Role::FretNumber);
    pairs.add(Role::Lyric, Role::Lyric);
    pairs.add(Role::Chord, Role::Chord);
    pairs.add(Role::Dynamic, Role::Dynamic);
    return pairs;
}();

bool looked(Role role) noexcept {
    return kPairs.looked[static_cast<std::size_t>(role)];
}

// The sweep runs in horizontal bands this high: an item is in every band its
// ink reaches, and an overlap is reported in the band that holds its top, so
// that each item meets only the few others at its height (the staves of a
// system would otherwise all be open at once).
constexpr double kBand = 2.0;

constexpr std::uint32_t kNoGlyph = std::numeric_limits<std::uint32_t>::max();

// The ink of one item in one band, and what tells two drawings of one glyph
// apart.
struct Ink {
    Rect box;
    Point origin;
    std::size_t measure = 0;
    std::uint32_t system = 0;
    std::int32_t band = 0;
    std::uint32_t order = 0;        // in the page's lists, so that sorting is stable
    std::uint32_t glyph = kNoGlyph; // none for a text
    Role role{};
};

std::int32_t bandOf(double y) noexcept {
    return static_cast<std::int32_t>(std::floor(y / kBand));
}

// Digits and the letters of a fret number stand on their baseline and reach
// their cap height; other texts have ascenders and descenders.
bool capsOnly(std::string_view text) noexcept {
    return std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || c == 'X'; });
}

Rect textBox(const TextItem& text) {
    const double width = textWidth(text);
    const double left = text.align == TextAlign::Left     ? text.origin.x
                        : text.align == TextAlign::Centre ? text.origin.x - (width / 2.0)
                                                          : text.origin.x - width;
    if (capsOnly(text.text)) {
        const double height = capHeight(text.size);
        return {.x = left, .y = text.origin.y - height, .width = width, .height = height};
    }
    return {.x = left,
            .y = text.origin.y - (detail::kTextAscent * text.size),
            .width = width,
            .height = (detail::kTextAscent + detail::kTextDescent) * text.size};
}

Rect glyphBox(const GlyphItem& glyph, const EngravingFont& font) {
    // The font's boxes have y upwards from the glyph's origin.
    const BoundingBox& ink = font.metrics(glyph.glyph).box;
    return {.x = glyph.origin.x + (ink.southWest.x * glyph.scale),
            .y = glyph.origin.y - (ink.northEast.y * glyph.scale),
            .width = ink.width() * glyph.scale,
            .height = ink.height() * glyph.scale};
}

// The system of `page` whose ink holds the middle of `box`, or none.
std::size_t systemOf(const DisplayList& page, const Rect& box) {
    const double middle = box.y + (box.height / 2.0);
    const double centre = box.x + (box.width / 2.0);
    for (std::size_t s = 0; s < page.systems.size(); ++s) {
        const SystemBox& system = page.systems[s];
        if (middle >= system.top && middle <= system.bottom && centre >= system.x - 1.0 &&
            centre <= system.x + system.width + 1.0) {
            return s;
        }
    }
    return page.systems.empty() ? 0 : std::numeric_limits<std::size_t>::max();
}

// What two boxes share, which may be empty.
Rect common(const Rect& a, const Rect& b) {
    const double left = std::max(a.x, b.x);
    const double top = std::max(a.y, b.y);
    const double right = std::min(a.x + a.width, b.x + b.width);
    const double bottom = std::min(a.y + a.height, b.y + b.height);
    return {.x = left, .y = top, .width = right - left, .height = bottom - top};
}

// Finds the overlaps of one page at a time, keeping its buffers.
class OverlapFinder {
public:
    explicit OverlapFinder(const EngravingFont& font) : font_(font) {}

    void page(const DisplayList& list, std::size_t index, std::vector<LayoutIssue>& issues) {
        const std::size_t found = issues.size();
        sweep(list, index, issues);
        // Bands found them from the top: put them system by system, from the left.
        std::stable_sort(issues.begin() + static_cast<std::ptrdiff_t>(found), issues.end(),
                         [&list](const LayoutIssue& a, const LayoutIssue& b) {
                             const std::size_t first = systemOf(list, a.box);
                             const std::size_t second = systemOf(list, b.box);
                             return first != second ? first < second : a.box.x < b.box.x;
                         });
    }

private:
    void sweep(const DisplayList& list, std::size_t index, std::vector<LayoutIssue>& issues) {
        inks_.clear();
        collect(list);
        // By system and band, then from the left: a sweep compares each box
        // only with the boxes of its band that still reach over its left edge.
        std::ranges::sort(inks_, [](const Ink& a, const Ink& b) {
            if (a.system != b.system) {
                return a.system < b.system;
            }
            if (a.band != b.band) {
                return a.band < b.band;
            }
            if (a.box.x != b.box.x) {
                return a.box.x < b.box.x;
            }
            return a.order < b.order;
        });
        open_.clear();
        for (std::size_t i = 0; i < inks_.size(); ++i) {
            const Ink& ink = inks_[i];
            if (i > 0 && (ink.system != inks_[i - 1].system || ink.band != inks_[i - 1].band)) {
                open_.clear();
            }
            std::erase_if(open_, [this, &ink](std::size_t j) {
                const Rect& box = inks_[j].box;
                return box.x + box.width <= ink.box.x + kOverlapTolerance;
            });
            for (const std::size_t j : open_) {
                check(inks_[j], ink, index, issues);
            }
            open_.push_back(i);
        }
    }

    void collect(const DisplayList& list) {
        std::uint32_t order = 0;
        for (const GlyphItem& glyph : list.glyphs) {
            ++order;
            if (!looked(glyph.role)) {
                continue;
            }
            const Rect box = glyphBox(glyph, font_);
            if (box.width <= 0.0 || box.height <= 0.0) {
                continue;
            }
            add(list, Ink{.box = box,
                          .origin = glyph.origin,
                          .measure = glyph.measure,
                          .order = order,
                          .glyph = static_cast<std::uint32_t>(glyph.glyph),
                          .role = glyph.role});
        }
        for (const TextItem& text : list.texts) {
            ++order;
            if (!looked(text.role) || text.text.empty()) {
                continue;
            }
            add(list, Ink{.box = textBox(text),
                          .origin = text.origin,
                          .measure = text.measure,
                          .order = order,
                          .role = text.role});
        }
    }

    // `ink` once for every band it reaches, in the system that holds it.
    void add(const DisplayList& list, Ink ink) {
        const std::size_t system = systemOf(list, ink.box);
        if (system == std::numeric_limits<std::size_t>::max()) {
            return;
        }
        ink.system = static_cast<std::uint32_t>(system);
        const std::int32_t last = bandOf(ink.box.y + ink.box.height);
        for (ink.band = bandOf(ink.box.y); ink.band <= last; ++ink.band) {
            inks_.push_back(ink);
        }
    }

    static void check(const Ink& left, const Ink& right, std::size_t page,
                      std::vector<LayoutIssue>& issues) {
        if (!kPairs.apart[static_cast<std::size_t>(left.role)]
                         [static_cast<std::size_t>(right.role)]) {
            return;
        }
        // One glyph drawn twice in one place, as a notehead two voices share.
        if (left.glyph == right.glyph && left.glyph != kNoGlyph && left.origin == right.origin) {
            return;
        }
        const Rect shared = common(left.box, right.box);
        // Both reach every band the overlap does: it is reported in its top's.
        if (shared.width <= kOverlapTolerance || shared.height <= kOverlapTolerance ||
            bandOf(shared.y) != left.band) {
            return;
        }
        issues.push_back(LayoutIssue{.kind = LayoutIssueKind::Overlap,
                                     .page = page,
                                     .measure = std::min(left.measure, right.measure),
                                     .first = left.role,
                                     .second = right.role,
                                     .box = shared});
    }

    const EngravingFont& font_;
    std::vector<Ink> inks_;
    std::vector<std::size_t> open_;
};

constexpr double kEpsilon = 1e-6;

} // namespace

std::string_view issueKindName(LayoutIssueKind kind) noexcept {
    switch (kind) {
    case LayoutIssueKind::Overlap:
        return "overlap";
    case LayoutIssueKind::MeasureTooWide:
        return "measureTooWide";
    }
    return "overlap";
}

bool mustNotTouch(Role a, Role b) noexcept {
    return kPairs.apart[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)];
}

std::vector<LayoutIssue> overlaps(std::span<const DisplayList> pages, const EngravingFont& font) {
    std::vector<LayoutIssue> issues;
    OverlapFinder finder(font);
    for (std::size_t p = 0; p < pages.size(); ++p) {
        finder.page(pages[p], p, issues);
    }
    return issues;
}

std::vector<LayoutIssue> layoutIssues(std::span<const DisplayList> pages, const EngravingFont& font,
                                      const PageLayoutOptions& options) {
    std::vector<LayoutIssue> issues;
    OverlapFinder finder(font);
    const double limit = options.width - options.margin;
    for (std::size_t p = 0; p < pages.size(); ++p) {
        const DisplayList& page = pages[p];
        for (const SystemBox& system : page.systems) {
            const double right = system.x + system.width;
            if (right <= limit + kEpsilon) {
                continue;
            }
            // The first of its measures that reaches past the margin.
            std::size_t measure = system.firstMeasure;
            for (const MeasureBox& box : page.measures) {
                if (box.index >= system.firstMeasure &&
                    box.index < system.firstMeasure + system.measureCount &&
                    box.x + box.width > limit + kEpsilon) {
                    measure = box.index;
                    break;
                }
            }
            issues.push_back(LayoutIssue{
                .kind = LayoutIssueKind::MeasureTooWide,
                .page = p,
                .measure = measure,
                .box = {.x = limit, .y = system.y, .width = right - limit, .height = system.height}});
        }
        finder.page(page, p, issues);
    }
    return issues;
}

} // namespace elara::notation
