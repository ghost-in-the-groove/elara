// SPDX-License-Identifier: AGPL-3.0-or-later
// Internal: the warnings readers and writers give for what a format cannot hold.
#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace elara::io::detail {

/// Dropped or changed features, one line per (part, feature) with where it first
/// happened and how often.
class Warnings {
public:
    /// `who` is a part or track name, empty for the whole score; `measure` is 0-based.
    void add(std::string_view who, std::string_view what, std::size_t measure) {
        for (Entry& entry : entries_) {
            if (entry.who == who && entry.what == what) {
                ++entry.count;
                return;
            }
        }
        entries_.push_back(Entry{std::string(who), std::string(what), measure, 1});
    }

    /// Adds `other`'s entries after these (parts are merged in score order).
    void append(const Warnings& other) {
        for (const Entry& entry : other.entries_) {
            entries_.push_back(entry);
        }
    }

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

    /// "Part: feature (measure N)" or "Part: feature (K times, first in measure N)".
    [[nodiscard]] std::vector<std::string> lines() const {
        std::vector<std::string> result;
        result.reserve(entries_.size());
        for (const Entry& entry : entries_) {
            const std::string where =
                entry.count == 1
                    ? std::format("measure {}", entry.measure + 1)
                    : std::format("{} times, first in measure {}", entry.count, entry.measure + 1);
            result.push_back(entry.who.empty()
                                 ? std::format("{} ({})", entry.what, where)
                                 : std::format("{}: {} ({})", entry.who, entry.what, where));
        }
        return result;
    }

private:
    struct Entry {
        std::string who;
        std::string what;
        std::size_t measure = 0;
        std::size_t count = 0;
    };
    std::vector<Entry> entries_;
};

} // namespace elara::io::detail
