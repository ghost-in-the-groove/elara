// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QVariant>
#include <QVariantList>

namespace elara::app {

/// The tracks the song panel lists, one row each, as a model whose rows the
/// view keeps: a new list of tracks changes, inserts or removes only the rows
/// that differ, so a mute or an added track does not make every row again.
class TrackList final : public QAbstractListModel {
    Q_OBJECT

public:
    /// The role that holds a row's map, which QML delegates read as modelData.
    static constexpr int kRowRole = Qt::UserRole + 1;

    explicit TrackList(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Takes these tracks: the rows that stay at the start and at the end are
    /// left alone, those between them change in place, and the rest are
    /// removed or inserted.
    void setTracks(const QVariantList& tracks);

private:
    QVariantList rows_;
};

} // namespace elara::app
