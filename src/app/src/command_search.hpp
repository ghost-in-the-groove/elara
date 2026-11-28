// SPDX-License-Identifier: AGPL-3.0-or-later
// Finding a command of the window by what is typed: the command palette's
// ranking (ADR-0135), apart from QML so that it is tested.
#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <span>
#include <vector>

namespace elara::app {

/// How many commands used last the palette keeps, the latest first.
inline constexpr qsizetype kRecentCommands = 8;

/// A command as the palette searches it.
struct PaletteCommand {
    /// What names it from one start to the next, whatever the language.
    QString id{};
    /// What it is called, as the window shows it.
    QString name{};
    /// Where it belongs: a menu, a group of the note effects.
    QString group{};
    /// More words that find it: the signs the score writes for it (P.M., 8va),
    /// other names it goes by.
    QString words{};
    /// Whether it can run now.
    bool enabled = true;
};

/// A command found.
struct PaletteMatch {
    /// Its index in the commands searched.
    qsizetype command = -1;
    /// How well it matched: higher is better.
    int score = 0;
    /// The positions in its name of the letters that what was typed matched,
    /// in order.
    QList<qsizetype> marks{};
    /// Listed first for having been used last, with nothing typed.
    bool recent = false;
};

/// The commands that `text` finds, best first. Each word typed must be found
/// in a command's name, its words or its group: as a whole word, the start of
/// one, the initials of its words ("pm", Palm mute), inside a word, or as
/// letters in order of which the first starts a word. Capitals and accents do
/// not count. The name counts most; a name that what was typed covers more of
/// comes first. Commands that cannot run come after those that can; among
/// close matches, those in `recent` (ids, the latest first) come first.
/// With nothing typed, every command: the recent ones, then the others in
/// their order, then those that cannot run.
[[nodiscard]] QList<PaletteMatch> searchCommands(const QString& text,
                                                 std::span<const PaletteCommand> commands,
                                                 const QStringList& recent);

/// `name` as rich text, the letters at `marks` in bold, the rest escaped.
[[nodiscard]] QString markedName(const QString& name, const QList<qsizetype>& marks);

/// The command palette's search: the commands the window offers now, what a
/// text finds among them, and the commands used last. Those are kept in the
/// settings for the next start only while `remember` is on (off for checks and
/// screenshots); the list is read when it turns on.
class CommandSearch : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool remember READ remember WRITE setRemember NOTIFY rememberChanged)
    /// The ids of the commands used last, the latest first.
    Q_PROPERTY(QStringList recent READ recent NOTIFY recentChanged)

public:
    static constexpr qsizetype kRecentLimit = kRecentCommands;

    explicit CommandSearch(QObject* parent = nullptr);

    [[nodiscard]] bool remember() const noexcept { return remember_; }
    void setRemember(bool remember);

    /// Keeps the list in this INI file instead of the app's settings (tests).
    void setSettingsFile(const QString& file);

    [[nodiscard]] QStringList recent() const { return recent_; }

    /// The commands to search: maps of `id`, `name`, `group`, `words` and
    /// `enabled` (true when left out).
    Q_INVOKABLE void setCommands(const QVariantList& commands);
    /// What `text` finds, best first: maps of `index` (in the commands set),
    /// `marked` (the name as rich text, what matched in bold) and `recent`.
    Q_INVOKABLE [[nodiscard]] QVariantList search(const QString& text) const;
    /// Puts a command at the top of the recent ones.
    Q_INVOKABLE void used(const QString& id);

signals:
    void rememberChanged();
    void recentChanged();

private:
    std::vector<PaletteCommand> commands_;
    QStringList recent_;
    bool remember_ = false;
    QString settingsFile_;
};

} // namespace elara::app
