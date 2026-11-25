// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/core/lyrics.hpp>
#include <elara/core/song.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace elara::core {

namespace {

/// `lyrics` with verse `verse` set to `syllable` (empty text: taken away),
/// never ending with an empty verse.
SharedOptional<std::vector<Syllable>> withVerse(const SharedOptional<std::vector<Syllable>>& lyrics,
                                                std::size_t verse, Syllable syllable) {
    std::vector<Syllable> verses = lyrics ? *lyrics : std::vector<Syllable>{};
    if (syllable.text.empty()) {
        syllable = Syllable{};
        if (verse >= verses.size()) {
            return lyrics;
        }
    } else if (verse >= verses.size()) {
        verses.resize(verse + 1);
    }
    verses[verse] = std::move(syllable);
    while (!verses.empty() && verses.back().text.empty()) {
        verses.pop_back();
    }
    return verses.empty() ? SharedOptional<std::vector<Syllable>>{}
                          : SharedOptional(std::move(verses));
}

/// A syllable read from text: underscores after it hold it.
Syllable syllableFrom(std::string word) {
    Syllable syllable;
    while (word.size() > 1 && word.back() == '_') {
        word.pop_back();
        syllable.extend = true;
    }
    syllable.text = std::move(word);
    return syllable;
}

/// Whether a syllable goes on the beat: it has notes, not all of them tied.
bool sings(const Beat& beat) {
    return std::ranges::any_of(beat.notes,
                               [](const Note& note) { return !note.effects.tiedFromPrevious; });
}

} // namespace

std::vector<std::optional<Syllable>> syllablesOf(std::string_view text) {
    std::vector<std::optional<Syllable>> out;
    std::string word;
    // The last syllable, kept until the next one starts: the separators after
    // it decide its hyphen and the blanks between them.
    std::optional<Syllable> pending;
    int run = 0; // spaces and hyphens since the last syllable (or the start)
    bool comment = false;
    bool afterComment = false; // spaces after a comment do not count
    for (const char c : text) {
        if (comment) {
            comment = c != ']';
            continue;
        }
        if (c == '[') {
            // With the spaces around it, a comment separates like a line break.
            comment = true;
            if (!word.empty()) {
                pending = syllableFrom(std::move(word));
                word.clear();
            }
            run = 0;
            afterComment = true;
            continue;
        }
        if (c == ' ' || c == '-' || c == '\t' || c == '\n' || c == '\r') {
            const bool counted = c == '-' || ((c == ' ' || c == '\t') && !afterComment);
            if (!word.empty()) {
                pending = syllableFrom(std::move(word));
                word.clear();
                run = 0;
            }
            if (counted) {
                if (pending && run == 0 && c == '-') {
                    pending->hyphen = true;
                }
                ++run;
            }
            continue;
        }
        afterComment = false;
        if (word.empty()) { // a syllable starts
            int blanks = run;
            if (pending) {
                out.emplace_back(std::move(pending));
                pending.reset();
                blanks = std::max(run - 1, 0);
            }
            out.insert(out.end(), static_cast<std::size_t>(blanks), std::nullopt);
            run = 0;
        }
        word += c == '+' ? ' ' : c;
    }
    if (!word.empty()) {
        out.emplace_back(syllableFrom(std::move(word)));
    } else if (pending) {
        out.emplace_back(std::move(pending));
    }
    return out;
}

std::size_t placeVerse(Staff& staff, std::size_t verse, std::size_t first, std::string_view text) {
    const std::vector<std::optional<Syllable>> syllables = syllablesOf(text);
    std::size_t next = 0;
    std::vector<SharedOptional<std::vector<Syllable>>> placed;
    for (std::size_t m = first; m < staff.measureCount(); ++m) {
        // Measures are shared between copies of the song (ADR-0051): only those
        // that change are written.
        const std::vector<Beat>& beats = std::as_const(staff).measure(m).voices[0].beats;
        placed.clear();
        bool changed = false;
        for (const Beat& beat : beats) {
            Syllable syllable;
            if (sings(beat) && next < syllables.size()) {
                syllable = syllables[next].value_or(Syllable{});
                ++next;
            }
            placed.push_back(withVerse(beat.lyrics, verse, std::move(syllable)));
            changed = changed || placed.back() != beat.lyrics;
        }
        if (changed) {
            std::vector<Beat>& written = staff.measure(m).voices[0].beats;
            for (std::size_t b = 0; b < written.size(); ++b) {
                written[b].lyrics = std::move(placed[b]);
            }
        }
    }
    return static_cast<std::size_t>(std::count_if(
        syllables.begin() + static_cast<std::ptrdiff_t>(next), syllables.end(),
        [](const std::optional<Syllable>& syllable) { return syllable.has_value(); }));
}

std::size_t verseCount(const Staff& staff) {
    std::size_t count = 0;
    for (std::size_t m = 0; m < staff.measureCount(); ++m) {
        for (const Voice& voice : staff.measure(m).voices) {
            for (const Beat& beat : voice.beats) {
                if (!beat.lyrics) {
                    continue;
                }
                const std::vector<Syllable>& verses = *beat.lyrics;
                for (std::size_t verse = verses.size(); verse > count; --verse) {
                    if (!verses[verse - 1].text.empty()) {
                        count = verse;
                        break;
                    }
                }
            }
        }
    }
    return count;
}

VerseText verseText(const Staff& staff, std::size_t verse) {
    const auto syllableOf = [verse](const Beat& beat) -> const Syllable* {
        if (!sings(beat) || !beat.lyrics || verse >= beat.lyrics->size() ||
            (*beat.lyrics)[verse].text.empty()) {
            return nullptr;
        }
        return &(*beat.lyrics)[verse];
    };
    std::size_t first = 0;
    while (first < staff.measureCount() &&
           std::ranges::none_of(
               staff.measure(first).voices[0].beats,
               [&](const Beat& beat) { return syllableOf(beat) != nullptr; })) {
        ++first;
    }
    if (first == staff.measureCount()) {
        return {};
    }
    VerseText typed{.first = first};
    std::string& text = typed.text;
    const Syllable* last = nullptr;
    std::size_t blanks = 0; // beats with notes since the last syllable
    for (std::size_t m = first; m < staff.measureCount(); ++m) {
        for (const Beat& beat : staff.measure(m).voices[0].beats) {
            if (!sings(beat)) {
                continue;
            }
            const Syllable* syllable = syllableOf(beat);
            if (syllable == nullptr) {
                ++blanks;
                continue;
            }
            if (last != nullptr) {
                text += last->hyphen ? '-' : ' ';
            }
            text.append(blanks, ' ');
            blanks = 0;
            for (const char c : syllable->text) {
                text += c == ' ' ? '+' : c;
            }
            if (syllable->extend) {
                text += '_';
            }
            last = syllable;
        }
    }
    if (last != nullptr && last->hyphen) {
        text += '-';
    }
    return typed;
}

} // namespace elara::core
