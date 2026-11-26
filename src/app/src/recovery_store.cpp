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
#include <QUuid>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <set>
#include <system_error>
#include <utility>

namespace elara::app {

namespace {

constexpr char kSongExtension[] = "elara";  // the song
constexpr char kSourceExtension[] = "json"; // where it came from
constexpr char kLockExtension[] = "lock";   // held by the run that writes it
// Added to the files a write prepares before renaming them into place.
constexpr char kPreparing[] = ".part";

QJsonObject readSource(const QString& file) {
    QFile source(file);
    if (!source.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(source.readAll()).object();
}

std::filesystem::path pathFrom(const QString& file) {
    return {file.toStdU16String()};
}

bool renameOver(const QString& from, const QString& to) {
    std::error_code error;
    std::filesystem::rename(pathFrom(from), pathFrom(to), error);
    return !error;
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
    const std::scoped_lock writing(writing_);
    {
        const std::scoped_lock guard(mutex_);
        if (removals != removals_) {
            return false;
        }
        // The lock comes first: a copy on the disk without it is a left one.
        if (!lock_) {
            if (!QDir().mkpath(folder_)) {
                return false;
            }
            auto lock = std::make_unique<QLockFile>(pathOf(id_, kLockExtension));
            lock->setStaleLockTime(0); // stale only once its run is gone, however long it runs
            if (!lock->tryLock(0)) {
                return false;
            }
            lock_ = std::move(lock);
        }
    }
    // Prepared beside the copy, without the mutex, so that a removal on the
    // interface thread never waits for the disk; then renamed into place.
    const QString songFile = pathOf(id_, kSongExtension);
    const QString sourceFile = pathOf(id_, kSourceExtension);
    const QString songPrepared = songFile + QLatin1StringView(kPreparing);
    const QString sourcePrepared = sourceFile + QLatin1StringView(kPreparing);
    const QJsonObject about{{QStringLiteral("source"), source.toString(QUrl::FullyEncoded)},
                            {QStringLiteral("title"), title}};
    bool prepared = false;
    try {
        io::saveNative(song, pathFrom(songPrepared));
        QFile file(sourcePrepared);
        prepared = file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                   file.write(QJsonDocument(about).toJson(QJsonDocument::Compact)) >= 0 &&
                   file.flush();
    } catch (const std::exception&) {
        prepared = false;
    }
    if (prepared) {
        const std::scoped_lock guard(mutex_);
        // Unless the copy was removed meanwhile: the song was saved.
        if (removals == removals_ && renameOver(sourcePrepared, sourceFile) &&
            renameOver(songPrepared, songFile)) {
            return true;
        }
    }
    QFile::remove(songPrepared);
    QFile::remove(sourcePrepared);
    return false;
}

void RecoveryStore::remove() {
    const std::scoped_lock guard(mutex_);
    ++removals_;
    QFile::remove(pathOf(id_, kSongExtension));
    QFile::remove(pathOf(id_, kSourceExtension));
}

bool RecoveryStore::hasCopy() const {
    return QFileInfo::exists(copyFile());
}

QString RecoveryStore::copyFile() const {
    return pathOf(id_, kSongExtension);
}

void RecoveryStore::removeFilesOf(const QString& id) const {
    const QDir folder(folder_);
    const QString lock = id + QLatin1Char('.') + QLatin1StringView(kLockExtension);
    for (const QString& name : folder.entryList({id + QStringLiteral(".*")}, QDir::Files)) {
        if (name != lock) {
            QFile::remove(folder.filePath(name));
        }
    }
}

std::vector<RecoveryStore::Copy> RecoveryStore::left() {
    const std::scoped_lock guard(mutex_);
    std::set<QString> ids;
    for (const QFileInfo& file : QDir(folder_).entryInfoList(QDir::Files)) {
        ids.insert(file.baseName());
    }
    std::vector<Copy> copies;
    for (const QString& id : ids) {
        if (id == id_) {
            continue;
        }
        if (!taken_.contains(id)) {
            auto lock = std::make_unique<QLockFile>(pathOf(id, kLockExtension));
            lock->setStaleLockTime(0);
            if (!lock->tryLock(0)) {
                continue; // its run still goes on
            }
            if (!QFileInfo::exists(pathOf(id, kSongExtension))) {
                // What a run left besides a copy: files half written when
                // it ended, or a lock it held after removing its copy.
                removeFilesOf(id);
                continue; // and the lock goes with `lock`
            }
            taken_.emplace(id, std::move(lock));
        }
        const QFileInfo file(pathOf(id, kSongExtension));
        const QJsonObject about = readSource(pathOf(id, kSourceExtension));
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
        info.absoluteFilePath() != QFileInfo(pathOf(id, kSongExtension)).absoluteFilePath()) {
        return false;
    }
    removeFilesOf(id);
    taken_.erase(taken); // and with it the lock file
    return true;
}

} // namespace elara::app
