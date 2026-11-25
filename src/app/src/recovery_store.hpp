// SPDX-License-Identifier: AGPL-3.0-or-later
// Copies of unsaved songs, kept while the app runs so that the next start can
// offer them back after a crash (ADR-0106).
#pragma once

#include <QDateTime>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

class QLockFile;

namespace elara::core {
class Song;
}

namespace elara::app {

/// The recovery copies in one folder: this run's own copy of the song being
/// edited, and the copies earlier runs left behind.
///
/// Each run writes one copy, an `.elara` file beside a small JSON file that
/// says where the song came from, and holds a lock file for as long as it
/// runs. A run that ends without removing its copy, as a crash does, leaves
/// it there with nobody holding its lock: left() finds such copies, and holds
/// their locks in turn so that two runs started together do not both offer
/// them.
///
/// write() may run on a worker thread while the others run on the interface
/// thread. Destroying the store releases its locks and keeps the copies.
class RecoveryStore {
public:
    /// A copy an earlier run left behind.
    struct Copy {
        QString file;      ///< the copy: an .elara file
        QUrl source;       ///< the song's own file; empty for a song never saved
        QString title;     ///< the song's title as the window showed it
        QDateTime written; ///< when the copy was written
    };

    /// The copies in `folder`, which is made on the first write.
    explicit RecoveryStore(QString folder);
    ~RecoveryStore();
    RecoveryStore(const RecoveryStore&) = delete;
    RecoveryStore& operator=(const RecoveryStore&) = delete;
    RecoveryStore(RecoveryStore&&) = delete;
    RecoveryStore& operator=(RecoveryStore&&) = delete;

    [[nodiscard]] const QString& folder() const noexcept { return folder_; }

    /// How many times remove() has run. A write prepared before a removal,
    /// on a worker, passes the count it read and is dropped.
    [[nodiscard]] std::uint64_t removals() const;

    /// Writes this run's copy of `song`, replacing the one before. False when
    /// remove() ran since `removals` was read, or when the copy could not be
    /// written.
    bool write(const core::Song& song, const QUrl& source, const QString& title,
               std::uint64_t removals);
    /// Removes this run's copy: the song was saved, or its changes discarded.
    void remove();
    /// Whether this run's copy is on the disk.
    [[nodiscard]] bool hasCopy() const;
    /// This run's copy, whether or not it is written yet.
    [[nodiscard]] QString copyFile() const;

    /// The copies earlier runs left behind, the newest first.
    [[nodiscard]] std::vector<Copy> left();
    /// Removes a copy left() offered, once it is recovered or discarded.
    /// False for any other file.
    bool discard(const QString& file);

private:
    [[nodiscard]] QString pathOf(const QString& id, const char* extension) const;

    QString folder_;
    QString id_; ///< names this run's files
    mutable std::mutex mutex_;
    std::unique_ptr<QLockFile> lock_;                     ///< this run's, from its first write
    std::map<QString, std::unique_ptr<QLockFile>> taken_; ///< left copies' locks, by id
    std::uint64_t removals_ = 0;
};

} // namespace elara::app
