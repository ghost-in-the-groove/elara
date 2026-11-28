// SPDX-License-Identifier: AGPL-3.0-or-later
#include "document_set.hpp"

#include <QFileInfo>
#include <QQmlEngine>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace elara::app {

namespace {

/// Whether `a` and `b` name the same file.
bool sameFile(const QUrl& a, const QUrl& b) {
    if (a.isEmpty() || b.isEmpty()) {
        return false;
    }
    if (a.isLocalFile() && b.isLocalFile()) {
        return QFileInfo(a.toLocalFile()) == QFileInfo(b.toLocalFile());
    }
    return a == b;
}

} // namespace

DocumentSet::DocumentSet(QObject* parent) : QAbstractListModel(parent) {
    add();
}

int DocumentSet::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant DocumentSet::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const Entry& entry = entries_[static_cast<std::size_t>(index.row())];
    ScoreDocument* document = entry.document;
    switch (role) {
    case DocumentRole:
        return QVariant::fromValue(document);
    case Qt::DisplayRole:
    case TitleRole: {
        // A song on its way has no title yet: its file's name stands for it.
        if (!document->song() && !entry.opening.isEmpty()) {
            return QFileInfo(entry.opening.toLocalFile()).completeBaseName();
        }
        const QString title = document->title();
        return title.isEmpty() ? tr("Untitled") : title;
    }
    case ModifiedRole:
        return document->modified();
    case BusyRole:
        return document->busy() || document->status() == ScoreDocument::Status::Loading;
    default:
        return {};
    }
}

QHash<int, QByteArray> DocumentSet::roleNames() const {
    return {{DocumentRole, "document"},
            {TitleRole, "title"},
            {ModifiedRole, "modified"},
            {BusyRole, "busy"}};
}

void DocumentSet::setCurrentIndex(int index) {
    if (index < 0 || index >= count() || static_cast<std::size_t>(index) == current_) {
        return;
    }
    current_ = static_cast<std::size_t>(index);
    emit currentChanged();
}

bool DocumentSet::unsaved() const {
    return std::ranges::any_of(entries_, [](const Entry& entry) {
        return entry.document->modified() || entry.document->saving();
    });
}

ScoreDocument* DocumentSet::at(int index) const {
    return index >= 0 && index < count() ? entries_[static_cast<std::size_t>(index)].document
                                         : nullptr;
}

int DocumentSet::indexOf(const QObject* document) const {
    const auto found = std::ranges::find_if(
        entries_, [document](const Entry& entry) { return entry.document == document; });
    return found == entries_.end() ? -1 : static_cast<int>(found - entries_.begin());
}

int DocumentSet::indexOfFile(const QUrl& file) const {
    const auto found = std::ranges::find_if(entries_, [&file](const Entry& entry) {
        return sameFile(entry.opening, file) || sameFile(entry.document->source(), file);
    });
    return found == entries_.end() ? -1 : static_cast<int>(found - entries_.begin());
}

int DocumentSet::nextUnsaved() const {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const std::size_t index = (current_ + i) % entries_.size();
        const ScoreDocument* document = entries_[index].document;
        if (document->modified() || document->saving()) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void DocumentSet::open(const QList<QUrl>& files) {
    int shown = -1;
    for (const QUrl& file : files) {
        if (file.isEmpty()) {
            continue;
        }
        int index = indexOfFile(file);
        if (index < 0) {
            ScoreDocument* document = emptyOrNew();
            index = indexOf(document);
            entries_[static_cast<std::size_t>(index)].opening = file;
            // Its load starts now, on a worker: the next file's starts beside it.
            document->open(file);
            rowChanged(document, {TitleRole, BusyRole});
        }
        if (shown < 0) {
            shown = index;
        }
    }
    setCurrentIndex(shown);
}

void DocumentSet::newSong(const QString& presetId) {
    ScoreDocument* document = emptyOrNew();
    document->newSong(presetId);
    setCurrentIndex(indexOf(document));
}

QVariantList DocumentSet::recoverable() {
    return current()->recoverable();
}

void DocumentSet::recover(const QString& file) {
    // The song shown listed the copy, and holds it until another song takes it.
    ScoreDocument* listing = current();
    ScoreDocument* document = emptyOrNew();
    if (document != listing) {
        listing->releaseRecovery(file);
    }
    document->recover(file);
    setCurrentIndex(indexOf(document));
}

void DocumentSet::discardRecovery(const QString& file) {
    current()->discardRecovery(file);
}

void DocumentSet::close(int index) {
    if (index < 0 || index >= count()) {
        return;
    }
    const auto at = static_cast<std::size_t>(index);
    ScoreDocument* closing = entries_[at].document;
    if (entries_.size() == 1) {
        add(); // the empty song left in its place
    }
    ScoreDocument* const shown = current();
    const std::size_t shownAt = current_;
    beginRemoveRows({}, index, index);
    entries_.erase(entries_.begin() + index);
    // The song shown stays; the one after it takes the place of a song
    // closed while shown, or the one before it at the end of the tabs.
    if (shown == closing) {
        current_ = std::min(at, entries_.size() - 1);
    } else if (current_ > at) {
        --current_;
    }
    endRemoveRows();
    if (current() != shown || current_ != shownAt) {
        emit currentChanged();
    }
    emit countChanged();
    updateUnsaved();
    // It goes once the views have let go of it; its jobs end first.
    closing->disconnect(this);
    closing->deleteLater();
}

void DocumentSet::showNext() {
    setCurrentIndex(static_cast<int>((current_ + 1) % entries_.size()));
}

void DocumentSet::showPrevious() {
    setCurrentIndex(static_cast<int>((current_ + entries_.size() - 1) % entries_.size()));
}

void DocumentSet::setRecoveryFolder(const QString& folder) {
    if (folder == recoveryFolder_) {
        return;
    }
    recoveryFolder_ = folder;
    for (const Entry& entry : entries_) {
        entry.document->setRecoveryFolder(folder);
    }
    emit recoveryFolderChanged();
}

ScoreDocument* DocumentSet::add() {
    auto* document = new ScoreDocument(this);
    // Handed to QML by at() and current, and deleted by close() only.
    QQmlEngine::setObjectOwnership(document, QQmlEngine::CppOwnership);
    document->shareClipboard(clipboard_);
    document->setRecoveryFolder(recoveryFolder_);
    connect(document, &ScoreDocument::songChanged, this,
            [this, document] { rowChanged(document, {TitleRole}); });
    connect(document, &ScoreDocument::sourceChanged, this,
            [this, document] { rowChanged(document, {TitleRole}); });
    connect(document, &ScoreDocument::busyChanged, this,
            [this, document] { rowChanged(document, {BusyRole}); });
    connect(document, &ScoreDocument::statusChanged, this,
            [this, document] { statusChanged(document); });
    const auto savedChanged = [this, document] {
        rowChanged(document, {ModifiedRole});
        updateUnsaved();
    };
    connect(document, &ScoreDocument::historyChanged, this, savedChanged);
    connect(document, &ScoreDocument::savingChanged, this, savedChanged);
    // What one song copies, the others can paste.
    connect(document, &ScoreDocument::clipboardChanged, this, [this, document] {
        if (relaying_) {
            return;
        }
        relaying_ = true;
        for (const Entry& entry : entries_) {
            if (entry.document != document) {
                emit entry.document->clipboardChanged();
            }
        }
        relaying_ = false;
    });
    const int row = count();
    beginInsertRows({}, row, row);
    entries_.push_back(Entry{.document = document, .opening = {}});
    endInsertRows();
    emit countChanged();
    return document;
}

ScoreDocument* DocumentSet::emptyOrNew() {
    return isEmpty(current()) ? current() : add();
}

bool DocumentSet::isEmpty(const ScoreDocument* document) {
    return !document->song() && document->status() != ScoreDocument::Status::Loading;
}

void DocumentSet::rowChanged(const ScoreDocument* document, const QList<int>& roles) {
    const int row = indexOf(document);
    if (row >= 0) {
        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed, roles);
    }
}

void DocumentSet::statusChanged(ScoreDocument* document) {
    const int row = indexOf(document);
    if (row < 0) {
        return;
    }
    Entry& entry = entries_[static_cast<std::size_t>(row)];
    const ScoreDocument::Status status = document->status();
    if (status == ScoreDocument::Status::Loading) {
        return;
    }
    const QUrl opening = std::exchange(entry.opening, QUrl());
    if (status != ScoreDocument::Status::Error) {
        rowChanged(document, {TitleRole, BusyRole});
        return;
    }
    if (opening.isEmpty() || document->song()) {
        emit failed(document->errorMessage());
        return;
    }
    // A file that did not open: its song goes again.
    emit failed(
        tr("%1: %2").arg(QFileInfo(opening.toLocalFile()).fileName(), document->errorMessage()));
    close(row);
}

void DocumentSet::updateUnsaved() {
    const bool now = unsaved();
    if (now != unsaved_) {
        unsaved_ = now;
        emit unsavedChanged();
    }
}

} // namespace elara::app
