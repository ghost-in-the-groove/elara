// SPDX-License-Identifier: AGPL-3.0-or-later
#include "recent_scores.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QVariantMap>

#include <memory>

namespace elara::app {

namespace {

const QString kKey = QStringLiteral("recentScores");

// Paths name the same file whatever their separators; on Windows, whatever
// their case.
constexpr Qt::CaseSensitivity kPathCase =
#ifdef Q_OS_WIN
    Qt::CaseInsensitive;
#else
    Qt::CaseSensitive;
#endif

QString pathOf(const QUrl& file) {
    return file.isLocalFile() ? QDir::cleanPath(file.toLocalFile()) : QString();
}

} // namespace

RecentScores::RecentScores(QObject* parent) : QObject(parent) {}

void RecentScores::setSettingsFile(const QString& file) {
    settingsFile_ = file;
}

void RecentScores::setRemember(bool remember) {
    if (remember == remember_) {
        return;
    }
    remember_ = remember;
    if (remember_) {
        const auto settings =
            settingsFile_.isEmpty()
                ? std::make_unique<QSettings>()
                : std::make_unique<QSettings>(settingsFile_, QSettings::IniFormat);
        files_ = settings->value(kKey).toStringList().mid(0, kLimit);
        emit scoresChanged();
    }
    emit rememberChanged();
}

QVariantList RecentScores::scores() const {
    QVariantList result;
    for (const QString& path : files_) {
        const QFileInfo info(path);
        if (!info.isFile()) {
            continue;
        }
        result.append(QVariantMap{
            {QStringLiteral("url"), QUrl::fromLocalFile(path)},
            {QStringLiteral("name"), info.completeBaseName()},
            {QStringLiteral("format"), info.suffix().toUpper()},
            {QStringLiteral("folder"), QDir::toNativeSeparators(info.absolutePath())},
        });
    }
    return result;
}

void RecentScores::add(const QUrl& file) {
    const QString path = pathOf(file);
    if (path.isEmpty()) {
        return;
    }
    files_.removeIf([&path](const QString& other) { return other.compare(path, kPathCase) == 0; });
    files_.prepend(path);
    if (files_.size() > kLimit) {
        files_.resize(kLimit);
    }
    changed();
}

void RecentScores::remove(const QUrl& file) {
    const QString path = pathOf(file);
    if (files_.removeIf(
            [&path](const QString& other) { return other.compare(path, kPathCase) == 0; }) > 0) {
        changed();
    }
}

void RecentScores::clear() {
    if (!files_.isEmpty()) {
        files_.clear();
        changed();
    }
}

void RecentScores::changed() {
    if (remember_) {
        const auto settings =
            settingsFile_.isEmpty()
                ? std::make_unique<QSettings>()
                : std::make_unique<QSettings>(settingsFile_, QSettings::IniFormat);
        settings->setValue(kKey, files_);
    }
    emit scoresChanged();
}

} // namespace elara::app
