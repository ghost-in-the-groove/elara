// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal: the warnings readers and writers give for what a format cannot hold.
#pragma once

#include <elara/core/measure_layout.hpp>
#include <elara/core/song.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elara::io::detail {

/// Dropped or changed features, one line per (part, feature) with where it first
/// happened and how often.
class Warnings {
public:
    /// `who` is a part or track name, empty for the whole score; `measure` is 0-based,
    /// nullopt for problems of the file rather than of a measure.
    void add(std::string_view who, std::string_view what,
             std::optional<std::size_t> measure = std::nullopt) {
        for (Entry& entry : entries_) {
            if (entry.who == who && entry.what == what) {
                ++entry.count;
                return;
            }
        }
        entries_.push_back(Entry{std::string(who), std::string(what), measure, 1});
    }

    /// Adds `other`'s entries after these (parts are merged in score order), as if
    /// they had been added here: a line already present counts them instead.
    void append(const Warnings& other) {
        for (const Entry& entry : other.entries_) {
            appendEntry(entry);
        }
    }

    /// Adds the entries of `parts` (each read on its own, measure by measure, as
    /// staves read on several threads are) in the order one reading measure by
    /// measure, part after part, would have added them: by the measure they
    /// first happened in, then by part. Entries without a measure come last.
    void appendByMeasure(std::span<const Warnings> parts) {
        struct Ref {
            std::size_t measure;
            std::size_t part;
            const Entry* entry;
        };
        std::vector<Ref> refs;
        for (std::size_t p = 0; p < parts.size(); ++p) {
            for (const Entry& entry : parts[p].entries_) {
                refs.push_back(Ref{entry.measure.value_or(SIZE_MAX), p, &entry});
            }
        }
        std::ranges::stable_sort(refs, {},
                                 [](const Ref& ref) { return std::pair(ref.measure, ref.part); });
        for (const Ref& ref : refs) {
            appendEntry(*ref.entry);
        }
    }

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

    /// "Part: feature (measure N)" or "Part: feature (K times, first in measure N)";
    /// without a measure, "Part: feature" or "Part: feature (K times)".
    [[nodiscard]] std::vector<std::string> lines() const {
        std::vector<std::string> result;
        result.reserve(entries_.size());
        for (const Entry& entry : entries_) {
            std::string where;
            if (entry.measure) {
                where = entry.count == 1 ? std::format(" (measure {})", *entry.measure + 1)
                                         : std::format(" ({} times, first in measure {})",
                                                       entry.count, *entry.measure + 1);
            } else if (entry.count > 1) {
                where = std::format(" ({} times)", entry.count);
            }
            result.push_back(entry.who.empty()
                                 ? std::format("{}{}", entry.what, where)
                                 : std::format("{}: {}{}", entry.who, entry.what, where));
        }
        return result;
    }

private:
    struct Entry {
        std::string who;
        std::string what;
        std::optional<std::size_t> measure{};
        std::size_t count = 0;
    };

    void appendEntry(const Entry& entry) {
        const auto same = std::ranges::find_if(entries_, [&entry](const Entry& mine) {
            return mine.who == entry.who && mine.what == entry.what;
        });
        if (same == entries_.end()) {
            entries_.push_back(entry);
        } else {
            same->count += entry.count;
        }
    }

    std::vector<Entry> entries_;
};

/// Adds what a format without layout adjustments (ADR-0152) leaves out of
/// `song`: its stored system and page breaks and measure spacings, the song's
/// under no name and each track's under the track's, once per measure.
inline void reportLayoutDropped(const core::Song& song, Warnings& warnings) {
    const auto report = [&warnings](std::string_view who, const core::MeasureLayouts& layouts) {
        if (!layouts) {
            return;
        }
        for (std::size_t m = 0; m < layouts->size(); ++m) {
            const core::MeasureLayout& layout = (*layouts)[m];
            // A break before the first measure means nothing.
            if (m > 0 && layout.breakBefore != core::Break::Auto) {
                warnings.add(who, "system and page breaks dropped", m);
            }
            if (layout.spacing != core::MeasureLayout::kDefaultSpacing) {
                warnings.add(who, "measure spacing dropped", m);
            }
        }
    };
    report({}, song.measureLayouts(core::LayoutScope::song()));
    for (std::size_t t = 0; t < song.trackCount(); ++t) {
        report(song.track(t).name(), song.track(t).measureLayouts());
    }
}

} // namespace elara::io::detail
