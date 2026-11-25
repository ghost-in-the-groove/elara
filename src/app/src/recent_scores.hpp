// SPDX-License-Identifier: AGPL-3.0-or-later
// The scores opened or saved last, for the welcome pane and the Open recent
// menu (ADR-0071).
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

namespace elara::app {

/// The files of the scores opened or saved last, the latest first, each once.
/// Kept in the settings for the next start only while `remember` is on (off
/// for checks and screenshots, which leave the person's list alone); the list
/// is read when it turns on.
class RecentScores : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool remember READ remember WRITE setRemember NOTIFY rememberChanged)
    /// The recent files that still exist, the latest first: maps of `url`,
    /// `name` (the file name without its extension), `format` (the extension
    /// in capitals, MusicXML's as XML) and `folder` (the folder's path as the
    /// system writes it).
    Q_PROPERTY(QVariantList scores READ scores NOTIFY scoresChanged)

public:
    /// How many files the list keeps.
    static constexpr qsizetype kLimit = 8;

    explicit RecentScores(QObject* parent = nullptr);

    [[nodiscard]] bool remember() const noexcept { return remember_; }
    void setRemember(bool remember);

    /// Keeps the list in this INI file instead of the app's settings (tests).
    void setSettingsFile(const QString& file);

    [[nodiscard]] QVariantList scores() const;

    /// Puts a local file at the top of the list. Other URLs are ignored.
    Q_INVOKABLE void add(const QUrl& file);
    /// Takes a file off the list.
    Q_INVOKABLE void remove(const QUrl& file);
    /// Empties the list.
    Q_INVOKABLE void clear();

signals:
    void rememberChanged();
    void scoresChanged();

private:
    void changed();

    QStringList files_;
    bool remember_ = false;
    QString settingsFile_;
};

} // namespace elara::app
