// SPDX-License-Identifier: AGPL-3.0-or-later
// The songs open in the window, one of them shown (ADR-0160).
#pragma once

#include <elara/core/clipboard.hpp>

#include "score_document.hpp"
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

namespace elara::app {

/// The songs open in the window, each a ScoreDocument of its own, and the one
/// shown (ADR-0160). It always holds one song at least: an empty one while no
/// song is open, which the next song opened or made goes into.
///
/// Files opened together each start loading at once, so they load in
/// parallel on the worker threads; the first is shown while it loads. A file
/// already open is shown instead of opened again, and one that does not open
/// closes again, saying why through failed(). Every song's editing session
/// copies into and pastes from clipboard(), so measures copied in one song
/// are pasted in any other.
///
/// As a list model it has a row per song, in the order of the tabs, with the
/// roles `document`, `title`, `modified` (unsaved changes) and `busy`.
class DocumentSet : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    /// The song shown.
    Q_PROPERTY(elara::app::ScoreDocument* current READ current NOTIFY currentChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// Whether a song has changes no file holds, or a save being written.
    Q_PROPERTY(bool unsaved READ unsaved NOTIFY unsavedChanged)
    /// Where the songs are copied while they have unsaved changes (ADR-0106).
    Q_PROPERTY(QString recoveryFolder READ recoveryFolder WRITE setRecoveryFolder NOTIFY
                   recoveryFolderChanged)

public:
    enum Role : int {
        DocumentRole = Qt::UserRole + 1,
        TitleRole,
        ModifiedRole,
        BusyRole,
    };

    explicit DocumentSet(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] ScoreDocument* current() const { return entries_[current_].document; }
    [[nodiscard]] int currentIndex() const { return static_cast<int>(current_); }
    /// Shows song `index`; one out of range changes nothing.
    void setCurrentIndex(int index);
    [[nodiscard]] int count() const { return static_cast<int>(entries_.size()); }
    [[nodiscard]] bool unsaved() const;

    /// Song `index`, in the order of the tabs; null out of range.
    Q_INVOKABLE [[nodiscard]] elara::app::ScoreDocument* at(int index) const;
    /// Where `document` is among the songs; -1 when it is not one of them.
    Q_INVOKABLE [[nodiscard]] int indexOf(const QObject* document) const;
    /// The song read from `file`, or being opened from it; -1 when none is.
    Q_INVOKABLE [[nodiscard]] int indexOfFile(const QUrl& file) const;
    /// The first song with unsaved changes or a save being written, from the
    /// one shown on round to the one before it; -1 when none has.
    Q_INVOKABLE [[nodiscard]] int nextUnsaved() const;

    /// Opens each of `files` (file: URLs) in a song of its own, all at once,
    /// and shows the first: a file already open is shown again, not opened
    /// twice, and the first one new goes into the song shown when that one is
    /// empty. Each song is shown loading until its pages are laid out.
    Q_INVOKABLE void open(const QList<QUrl>& files);
    /// Starts a new song of the instrument preset `presetId`
    /// (ScoreDocument::newSong()) and shows it: in the song shown when that
    /// one is empty, else in one of its own after the others.
    Q_INVOKABLE void newSong(const QString& presetId);
    /// The copies an earlier run left in recoveryFolder()
    /// (ScoreDocument::recoverable()).
    Q_INVOKABLE QVariantList recoverable();
    /// Opens a copy recoverable() offered in a song of its own, shown, as the
    /// song it was (ScoreDocument::recover()).
    Q_INVOKABLE void recover(const QString& file);
    /// Removes a copy recoverable() offered.
    Q_INVOKABLE void discardRecovery(const QString& file);
    /// Closes song `index` as it is, unsaved changes and all: the window asks
    /// first. Its recovery copy goes with it. The song after it is shown when
    /// it was (the one before, for the last), and closing the only song leaves
    /// an empty one in its place.
    Q_INVOKABLE void close(int index);
    /// Shows the next song, after the last the first; and the previous one.
    Q_INVOKABLE void showNext();
    Q_INVOKABLE void showPrevious();

    [[nodiscard]] QString recoveryFolder() const { return recoveryFolder_; }
    void setRecoveryFolder(const QString& folder);

    /// What every song's editing session copies into and pastes from.
    [[nodiscard]] const std::shared_ptr<core::Clipboard>& clipboard() const noexcept {
        return clipboard_;
    }

signals:
    void currentChanged();
    void countChanged();
    void unsavedChanged();
    void recoveryFolderChanged();
    /// Something a song could not do, with the reason to show: a file that
    /// did not open ("missing.gp: The file could not be read."), its song
    /// closed again, or a score that could not be laid out.
    void failed(const QString& message);

private:
    /// A song and the file it is being opened from, if any.
    struct Entry {
        ScoreDocument* document = nullptr; // a child of this set
        QUrl opening;
    };

    /// A new empty song, its signals followed, at the end of the tabs.
    ScoreDocument* add();
    /// The song shown when it is empty, else a new one after the others.
    ScoreDocument* emptyOrNew();
    /// Whether `document` holds no song and none is on its way.
    [[nodiscard]] static bool isEmpty(const ScoreDocument* document);
    /// Tells the views that `document`'s row changed in `roles`.
    void rowChanged(const ScoreDocument* document, const QList<int>& roles);
    /// A song's status changed: a file opened, or failed to.
    void statusChanged(ScoreDocument* document);
    /// Tells unsavedChanged() when unsaved() is no longer what it was.
    void updateUnsaved();

    std::vector<Entry> entries_; ///< in the order of the tabs, never empty
    std::size_t current_ = 0;
    QString recoveryFolder_;
    std::shared_ptr<core::Clipboard> clipboard_ = std::make_shared<core::Clipboard>();
    bool unsaved_ = false;  ///< unsaved() as last told
    bool relaying_ = false; ///< telling the songs that the clipboard changed
};

} // namespace elara::app
