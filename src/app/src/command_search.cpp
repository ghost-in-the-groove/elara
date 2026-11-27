// SPDX-License-Identifier: AGPL-3.0-or-later
#include "command_search.hpp"

#include <QChar>
#include <QSettings>
#include <QStringView>
#include <QVariantMap>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace elara::app {

namespace {

const QString kKey = QStringLiteral("recentCommands");

// What each way of matching a word typed is worth, best first.
constexpr int kWholeWord = 100;
constexpr int kWordStart = 80;
constexpr int kInitials = 70;
constexpr int kInsideWord = 50;
constexpr int kLettersInOrder = 40;

// How much a match counts by where it is, in hundredths: the name most.
constexpr int kInName = 100;
constexpr int kInWords = 75;
constexpr int kInGroup = 60;

// At most this much for a name that what was typed covers whole.
constexpr int kCoverage = 20;
// For each place a recent command is ahead of the last one kept.
constexpr int kRecency = 3;

// A text without capitals or accents, each character for one of the text's,
// so that a position in one is the same in the other; what is neither a
// letter nor a digit becomes a space.
QString fold(const QString& text) {
    QString folded;
    folded.reserve(text.size());
    for (const QChar c : text) {
        QChar base = c;
        if (c.decompositionTag() == QChar::Canonical) {
            base = c.decomposition().front();
        }
        base = base.toCaseFolded();
        folded.append(base.isLetterOrNumber() ? base : QChar(u' '));
    }
    return folded;
}

// Where the words of a folded text start, and how long each is.
struct Word {
    qsizetype start = 0;
    qsizetype length = 0;
};

QList<Word> wordsOf(QStringView folded) {
    QList<Word> words;
    qsizetype at = 0;
    while (at < folded.size()) {
        if (folded[at] == u' ') {
            ++at;
            continue;
        }
        const qsizetype start = at;
        while (at < folded.size() && folded[at] != u' ') {
            ++at;
        }
        words.append({.start = start, .length = at - start});
    }
    return words;
}

// A field of a command, folded, with its words.
struct Field {
    QString folded;
    QList<Word> words;

    explicit Field(const QString& text) : folded(fold(text)), words(wordsOf(folded)) {}
};

// A word typed, found in a field.
struct Hit {
    int score = 0;
    QList<qsizetype> marks;
};

QList<qsizetype> run(qsizetype start, qsizetype length) {
    QList<qsizetype> marks;
    marks.reserve(length);
    for (qsizetype i = 0; i < length; ++i) {
        marks.append(start + i);
    }
    return marks;
}

bool startsWord(const Field& field, qsizetype at) {
    return field.folded[at] != u' ' && (at == 0 || field.folded[at - 1] == u' ');
}

// The letters of `token` in order in the field, the first at a word's start:
// the best such run, or none.
std::optional<Hit> lettersInOrder(QStringView token, const Field& field) {
    std::optional<Hit> best;
    for (const Word& word : field.words) {
        if (field.folded[word.start] != token.front()) {
            continue;
        }
        Hit hit{.score = 0, .marks = {word.start}};
        int atWordStart = 1;
        int gaps = 0;
        qsizetype at = word.start + 1;
        for (qsizetype i = 1; i < token.size(); ++i) {
            const qsizetype found = field.folded.indexOf(token[i], at);
            if (found < 0) {
                hit.marks.clear();
                break;
            }
            gaps += found > at ? 1 : 0;
            atWordStart += startsWord(field, found) ? 1 : 0;
            hit.marks.append(found);
            at = found + 1;
        }
        if (hit.marks.isEmpty()) {
            continue;
        }
        const auto spread =
            static_cast<int>(hit.marks.back() - hit.marks.front() + 1 - token.size());
        hit.score = std::max(1, kLettersInOrder + 3 * atWordStart - 2 * gaps - spread / 4);
        if (!best || hit.score > best->score) {
            best = std::move(hit);
        }
    }
    return best;
}

// The best way `token` is found in the field, or none.
std::optional<Hit> findToken(QStringView token, const Field& field) {
    std::optional<Hit> best;
    const auto offer = [&best](int score, QList<qsizetype> marks) {
        if (!best || score > best->score) {
            best = Hit{.score = score, .marks = std::move(marks)};
        }
    };
    const qsizetype length = token.size();
    // A word, or the start of one: the earlier the word, the better.
    for (qsizetype w = 0; w < field.words.size(); ++w) {
        const Word& word = field.words[w];
        if (!QStringView(field.folded).sliced(word.start, word.length).startsWith(token)) {
            continue;
        }
        const int place = w == 0 ? 5 : -static_cast<int>(std::min<qsizetype>(w, 10));
        offer((word.length == length ? kWholeWord : kWordStart) + place, run(word.start, length));
    }
    // The initials of words in order, as few skipped as can be.
    if (length >= 2) {
        QList<qsizetype> marks;
        int skipped = 0;
        for (const Word& word : field.words) {
            if (marks.size() == length) {
                break;
            }
            if (field.folded[word.start] == token[marks.size()]) {
                marks.append(word.start);
            } else if (!marks.isEmpty()) {
                ++skipped;
            }
        }
        if (marks.size() == length) {
            offer(kInitials - 5 * skipped, std::move(marks));
        }
    }
    if (const qsizetype at = field.folded.indexOf(token); at >= 0) {
        offer(kInsideWord - static_cast<int>(std::min<qsizetype>(at / 4, 10)), run(at, length));
    }
    if (length >= 2) {
        if (auto hit = lettersInOrder(token, field)) {
            offer(hit->score, std::move(hit->marks));
        }
    }
    return best;
}

// How much a command used `index` places ahead of the oldest kept counts.
int recency(const QStringList& recent, const QString& id) {
    const qsizetype index = recent.indexOf(id);
    return index < 0 || index >= kRecentCommands
               ? 0
               : static_cast<int>(kRecentCommands - index) * kRecency;
}

// Every command, with nothing typed.
QList<PaletteMatch> everyCommand(std::span<const PaletteCommand> commands,
                                 const QStringList& recent) {
    QList<PaletteMatch> matches;
    const auto count = static_cast<qsizetype>(commands.size());
    std::vector<bool> listed(commands.size(), false);
    for (const QString& id : recent.mid(0, kRecentCommands)) {
        for (qsizetype i = 0; i < count; ++i) {
            const PaletteCommand& command = commands[static_cast<std::size_t>(i)];
            if (command.id == id && command.enabled && !listed[static_cast<std::size_t>(i)]) {
                matches.append({.command = i, .score = 0, .marks = {}, .recent = true});
                listed[static_cast<std::size_t>(i)] = true;
            }
        }
    }
    for (const bool enabled : {true, false}) {
        for (qsizetype i = 0; i < count; ++i) {
            if (commands[static_cast<std::size_t>(i)].enabled == enabled &&
                !listed[static_cast<std::size_t>(i)]) {
                matches.append({.command = i, .score = 0, .marks = {}, .recent = false});
            }
        }
    }
    return matches;
}

} // namespace

QList<PaletteMatch> searchCommands(const QString& text, std::span<const PaletteCommand> commands,
                                   const QStringList& recent) {
    const QString query = fold(text);
    QList<QStringView> tokens;
    for (const Word& word : wordsOf(query)) {
        tokens.append(QStringView(query).sliced(word.start, word.length));
    }
    if (tokens.isEmpty()) {
        return everyCommand(commands, recent);
    }
    QList<PaletteMatch> matches;
    for (std::size_t i = 0; i < commands.size(); ++i) {
        const PaletteCommand& command = commands[i];
        const Field name(command.name);
        const Field words(command.words);
        const Field group(command.group);
        PaletteMatch match{
            .command = static_cast<qsizetype>(i), .score = 0, .marks = {}, .recent = false};
        bool all = true;
        for (const QStringView token : tokens) {
            int best = 0;
            QList<qsizetype> marks;
            for (const auto& [field, weight] :
                 {std::pair{&name, kInName}, std::pair{&words, kInWords},
                  std::pair{&group, kInGroup}}) {
                if (const auto hit = findToken(token, *field)) {
                    const int score = hit->score * weight / 100;
                    if (score > best) {
                        best = score;
                        marks = field == &name ? hit->marks : QList<qsizetype>{};
                    }
                }
            }
            if (best == 0) {
                all = false;
                break;
            }
            match.score += best;
            match.marks.append(marks);
        }
        if (!all) {
            continue;
        }
        std::ranges::sort(match.marks);
        const auto [last, end] = std::ranges::unique(match.marks);
        match.marks.erase(last, end);
        if (!command.name.isEmpty()) {
            match.score += static_cast<int>(kCoverage * match.marks.size() / command.name.size());
        }
        match.score += recency(recent, command.id);
        matches.append(std::move(match));
    }
    std::ranges::stable_sort(matches, [commands](const PaletteMatch& a, const PaletteMatch& b) {
        const bool aRuns = commands[static_cast<std::size_t>(a.command)].enabled;
        const bool bRuns = commands[static_cast<std::size_t>(b.command)].enabled;
        if (aRuns != bRuns) {
            return aRuns;
        }
        return a.score > b.score;
    });
    return matches;
}

QString markedName(const QString& name, const QList<qsizetype>& marks) {
    QString marked;
    qsizetype at = 0;
    qsizetype mark = 0;
    while (at < name.size()) {
        const bool bold = mark < marks.size() && marks[mark] == at;
        qsizetype end = at;
        while (end < name.size() && (mark < marks.size() && marks[mark] == end) == bold) {
            mark += bold ? 1 : 0;
            ++end;
        }
        const QString part = name.sliced(at, end - at).toHtmlEscaped();
        marked += bold ? QStringLiteral("<b>%1</b>").arg(part) : part;
        at = end;
    }
    return marked;
}

CommandSearch::CommandSearch(QObject* parent) : QObject(parent) {}

void CommandSearch::setSettingsFile(const QString& file) {
    settingsFile_ = file;
}

void CommandSearch::setRemember(bool remember) {
    if (remember == remember_) {
        return;
    }
    remember_ = remember;
    if (remember_) {
        const auto settings =
            settingsFile_.isEmpty()
                ? std::make_unique<QSettings>()
                : std::make_unique<QSettings>(settingsFile_, QSettings::IniFormat);
        recent_ = settings->value(kKey).toStringList().mid(0, kRecentLimit);
        emit recentChanged();
    }
    emit rememberChanged();
}

void CommandSearch::setCommands(const QVariantList& commands) {
    commands_.clear();
    commands_.reserve(static_cast<std::size_t>(commands.size()));
    for (const QVariant& entry : commands) {
        const QVariantMap map = entry.toMap();
        commands_.push_back({
            .id = map.value(QStringLiteral("id")).toString(),
            .name = map.value(QStringLiteral("name")).toString(),
            .group = map.value(QStringLiteral("group")).toString(),
            .words = map.value(QStringLiteral("words")).toString(),
            .enabled = map.value(QStringLiteral("enabled"), true).toBool(),
        });
    }
}

QVariantList CommandSearch::search(const QString& text) const {
    QVariantList results;
    for (const PaletteMatch& match : searchCommands(text, commands_, recent_)) {
        const QString& name = commands_[static_cast<std::size_t>(match.command)].name;
        results.append(QVariantMap{
            {QStringLiteral("index"), static_cast<int>(match.command)},
            {QStringLiteral("marked"), markedName(name, match.marks)},
            {QStringLiteral("recent"), match.recent},
        });
    }
    return results;
}

void CommandSearch::used(const QString& id) {
    if (id.isEmpty()) {
        return;
    }
    recent_.removeAll(id);
    recent_.prepend(id);
    recent_ = recent_.mid(0, kRecentLimit);
    if (remember_) {
        const auto settings =
            settingsFile_.isEmpty()
                ? std::make_unique<QSettings>()
                : std::make_unique<QSettings>(settingsFile_, QSettings::IniFormat);
        settings->setValue(kKey, recent_);
    }
    emit recentChanged();
}

} // namespace elara::app
