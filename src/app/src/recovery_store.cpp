// SPDX-License-Identifier: AGPL-3.0-or-later
#include "recovery_store.hpp"

#include <elara/core/song.hpp>
#include <elara/io/native_format.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <utility>

namespace elara::app {

namespace {

constexpr char kCopy[] = "elara"; // the song
constexpr char kAbout[] = "json"; // where it came from
constexpr char kLock[] = "lock";  // held by the run that writes it

QJsonObject readAbout(const QString& file) {
    QFile about(file);
    if (!about.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(about.readAll()).object();
}

} // namespace

RecoveryStore::RecoveryStore(QString folder)
    : folder_(std::move(folder)), id_(QUuid::createUuid().toString(QUuid::WithoutBraces)) {}

RecoveryStore::~RecoveryStore() = default;

QString RecoveryStore::pathOf(const QString& id, const char* extension) const {
    return QDir(folder_).filePath(id + QLatin1Char('.') + QLatin1StringView(extension));
}

std::uint64_t RecoveryStore::removals() const {
    const std::scoped_lock guard(mutex_);
    return removals_;
}

bool RecoveryStore::write(const core::Song& song, const QUrl& source, const QString& title,
                          std::uint64_t removals) {
    const std::scoped_lock guard(mutex_);
    if (removals != removals_) {
        return false;
    }
    // The lock comes first: a copy on the disk without it is a left one.
    if (!lock_) {
        if (!QDir().mkpath(folder_)) {
            return false;
        }
        auto lock = std::make_unique<QLockFile>(pathOf(id_, kLock));
        lock->setStaleLockTime(0); // stale only once its run is gone, however long it runs
        if (!lock->tryLock(0)) {
            return false;
        }
        lock_ = std::move(lock);
    }
    try {
        io::saveNative(song, std::filesystem::path(pathOf(id_, kCopy).toStdU16String()));
    } catch (const std::exception&) {
        return false;
    }
    const QJsonObject about{{QStringLiteral("source"), source.toString(QUrl::FullyEncoded)},
                            {QStringLiteral("title"), title}};
    QSaveFile file(pathOf(id_, kAbout));
    return file.open(QIODevice::WriteOnly) &&
           file.write(QJsonDocument(about).toJson(QJsonDocument::Compact)) >= 0 && file.commit();
}

void RecoveryStore::remove() {
    const std::scoped_lock guard(mutex_);
    ++removals_;
    QFile::remove(pathOf(id_, kCopy));
    QFile::remove(pathOf(id_, kAbout));
}

bool RecoveryStore::hasCopy() const {
    return QFileInfo::exists(copyFile());
}

QString RecoveryStore::copyFile() const {
    return pathOf(id_, kCopy);
}

std::vector<RecoveryStore::Copy> RecoveryStore::left() {
    const std::scoped_lock guard(mutex_);
    std::vector<Copy> copies;
    const QFileInfoList files =
        QDir(folder_).entryInfoList({QStringLiteral("*.") + QLatin1StringView(kCopy)}, QDir::Files);
    for (const QFileInfo& file : files) {
        const QString id = file.completeBaseName();
        if (id == id_) {
            continue;
        }
        if (!taken_.contains(id)) {
            auto lock = std::make_unique<QLockFile>(pathOf(id, kLock));
            lock->setStaleLockTime(0);
            if (!lock->tryLock(0)) {
                continue; // its run still goes on
            }
            taken_.emplace(id, std::move(lock));
        }
        const QJsonObject about = readAbout(pathOf(id, kAbout));
        copies.push_back({.file = file.absoluteFilePath(),
                          .source = QUrl(about.value(QStringLiteral("source")).toString()),
                          .title = about.value(QStringLiteral("title")).toString(),
                          .written = file.lastModified()});
    }
    std::ranges::sort(copies, [](const Copy& a, const Copy& b) {
        return a.written != b.written ? a.written > b.written : a.file < b.file;
    });
    return copies;
}

bool RecoveryStore::discard(const QString& file) {
    const std::scoped_lock guard(mutex_);
    const QFileInfo info(file);
    const QString id = info.completeBaseName();
    const auto taken = taken_.find(id);
    if (taken == taken_.end() ||
        info.absoluteFilePath() != QFileInfo(pathOf(id, kCopy)).absoluteFilePath()) {
        return false;
    }
    QFile::remove(pathOf(id, kCopy));
    QFile::remove(pathOf(id, kAbout));
    taken_.erase(taken); // and with it the lock file
    return true;
}

} // namespace elara::app
